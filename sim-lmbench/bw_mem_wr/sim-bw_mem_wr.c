/*
 * -O2でコンパイルすること
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>

#define TYPE    char
#define CHUNK_SIZE  32

double	wr(uint64_t iterations, void *cookie);
void	init_overhead(uint64_t iterations, void *cookie);
void	init_loop(uint64_t iterations, void *cookie);
void	cleanup(uint64_t iterations, void *cookie);

bool ex_latency = false;
bool use_threadset = false;
bool use_cg_taskset = false;

#define NUM_SAMPLE 20
double times[NUM_SAMPLE];

typedef struct _state {
	double	overhead;
	size_t	nbytes;
	int	need_buf2;
	int	aligned;
	TYPE	*buf;
	TYPE	*buf2;
	TYPE	*buf2_orig;
	TYPE	*remainder_boundary;
	TYPE	*lastone;
	size_t stride_b;		// stride
	size_t	N;
} state_t;

void	adjusted_bandwidth(uint64_t t, uint64_t b, uint64_t iter, double ovrhd);

uint64_t get_time_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return 1000000000ULL * t.tv_sec + t.tv_nsec;
}

void error_print(void) {
    printf("arg[1]: Max buffer size\n");
    printf("  > 1k -> 1024.\n");
    printf("arg[2]: Stride\n");
    printf("  > 1k -> 1024.\n");
    printf("arg[3 or more]: Reference below.\n");
    printf("  > Use \"-r <REPORT INTERVAL>\" when using report-function.\n");
    printf("  > Use \"-g <CPU NUM>\" when using cgroup-taskset(cpuset).\n");
    printf("  > Use \"-f <FILE NAME>\" when using file export.\n");
    printf("  > Use \"-e <NUM OF ELEMENTS>\" to set the number of logs can be saved.\n");
    printf("  > Use \"-t <CPU NUM>\" when pinning threads to a specific CPU.\n");

}

int create_directory(const char *path) {
    struct stat st = {0};
    int lg_mkdir;
    
    // ディレクトリが既に存在するかチェック
    if (stat(path, &st) != 0) {
        printf("Create %s\n", path);
        lg_mkdir = mkdir(path, 0755);
        if (lg_mkdir == -1) {
            printf("ERR: Failed to create %s\n", path);
            return -1;
        }
    }
    return 0;
}

char
last(char *s)
{
    while (*s++)
        ;
    return (s[-2]);
}

uint64_t bytes(const char* str) {
    size_t len = strlen(str);
    
    if (len == 0) {
        fprintf(stderr, "Empty string\n");
        exit(EXIT_FAILURE);
    }

    char unit = str[len - 1];
    uint64_t multiplier = 1;

    if (unit == 'M' || unit == 'm') {
        multiplier = 1024 * 1024;
    } else if (unit == 'K' || unit == 'k') {
        multiplier = 1024;
    } else if (unit == 'G' || unit == 'g') {
        multiplier = 1024 * 1024 * 1024;
    } else if (unit >= '0' && unit <= '9') {
        return strtoull(str, NULL, 10);
    } else {
        fprintf(stderr, "Invalid unit\n");
        exit(EXIT_FAILURE);
    }

    char number_part[len];
    strncpy(number_part, str, len - 1);
    number_part[len - 1] = '\0';

    double temp_number = atof(number_part);
    uint64_t number = (uint64_t)(temp_number * (double)multiplier);
    return number;
}

// 比較関数（qsort用）
int compare(const void* a, const void* b) {
    double diff = *(double*)a - *(double*)b;
    return (diff > 0) - (diff < 0); // 正負を返す
}

// 中央値を求める
double find_median(double* arr, size_t size) {
    if (size == 0) {
        fprintf(stderr, "Array size must be greater than 0.\n");
        exit(EXIT_FAILURE);
    }

    // 配列をソート
    qsort(arr, size, sizeof(double), (int (*)(const void*, const void*))compare);

    // 中央値を計算
    if (size % 2 == 0) {
        return (arr[size / 2 - 1] + arr[size / 2]) / 2.0;
    } else {
        return arr[size / 2];
    }
}

void cg_taskset(pid_t pid, char* cpu_to_set){
    int cpuset_value = 0;
    int pid_check_cnt = 0;
    FILE *cg_fp;

    // Enable cpuset module at master
    cg_fp = fopen("/sys/fs/cgroup/cgroup.subtree_control", "w");
    if (!cg_fp) {
        printf("ERR: Could not open the croup-v2 cpuset file. (%s)\n", "/sys/fs/cgroup/cgroup.subtree_control");
        printf("> You may need sudo.\n");
        exit(1);
    }
    fprintf(cg_fp, "%s", "+cpuset");
    fclose(cg_fp);

    // Create parent group
    create_directory("/sys/fs/cgroup/sim_lmb");

    // Enable cpuset module at parent group
    cg_fp = fopen("/sys/fs/cgroup/sim_lmb/cgroup.subtree_control", "w");
    if (!cg_fp) {
        printf("ERR: Could not open the croup-v2 cpuset file. (%s)\n", "/sys/fs/cgroup/cgroup.subtree_control");
        exit(1);
    }
    fprintf(cg_fp, "%s", "+cpuset");
    fclose(cg_fp);

    // Create sub group
    create_directory("/sys/fs/cgroup/sim_lmb/sim-lat_mem_rd");

    // Set CPU num 1 to cgroup config
    cg_fp = fopen("/sys/fs/cgroup/sim_lmb/sim-lat_mem_rd/cpuset.cpus", "w");
    if (!cg_fp) {
        printf("ERR: Could not open the croup-v2 cpuset file. (%s)\n", "/sys/fs/cgroup/sim_lmb/sim-lat_mem_rd/cpuset.cpus");
        exit(1);
    }
    fprintf(cg_fp, "%s", cpu_to_set);
    fclose(cg_fp);

    // Write PID to the  cpuset file
    cg_fp = fopen("/sys/fs/cgroup/sim_lmb/sim-lat_mem_rd/cgroup.procs", "w");
    if (!cg_fp) {
        printf("ERR: Could not open the croup-v2 cpuset file. (\"sim-lat_mem_rd\" group)\n");
        exit(1);
    }
    fprintf(cg_fp, "%d\n", pid);
    fclose(cg_fp);

    // Read an integer from the file
    do {
        cg_fp = fopen("/sys/fs/cgroup/sim_lmb/sim-lat_mem_rd/cgroup.procs", "r");
        if (!cg_fp) {
            printf("ERR: Could not open the croup-v2 cpuset file. (\"sim-lat_mem_rd\" group)\n");
            exit(1);
        }

        int fscan_res = fscanf(cg_fp, "%d", &cpuset_value);
        (void)fscan_res;
        fclose(cg_fp);
        usleep(100*1000);   // sleep 0.1s
        
        // 2s check
        if (pid_check_cnt == 20) {
            printf("ERR: Could not set PID to the cpuset setting. (\"sim-lat_mem_rd\" group)\n");
            exit(1);
        }
        pid_check_cnt++;
    } while (cpuset_value != pid);
}

int
main(int argc, char **argv)
{
    uint64_t iterations = 50;
    uint64_t access_cnt = 0;
	double tim_mid;
	int mandatory_argc = 3;
	size_t	nbytes;
	state_t	state;
	char* ch_cpu_to_set = NULL;
	int cpu_to_self_set = 0;
	int ts_result = 0;
	pid_t pid = getpid();
	cpu_set_t cpu_set;

	if (argc < mandatory_argc) {
		error_print();
        return 1;
    }

	for (int awc = mandatory_argc; awc < argc; awc+=2) {      
        if (argv[awc][2] != '\0') {
                error_print();
                exit(EXIT_FAILURE);
            }  
        switch (argv[awc][1]) {
            case 't':
                    use_threadset = true;
                    cpu_to_self_set = atoi(argv[awc+1]);
                    break;
            case 'g':
                use_cg_taskset = true;
                ch_cpu_to_set = argv[awc+1];
                break;
            case 'l':
                ex_latency = true;
				awc--;
                break;
            default:
                printf("ERR: Unknown argument. (%s)\n\n", argv[(awc*2)+3]);
                error_print();
                return 1;
        }
    }
	//printf("int: %lu\n", sizeof(int));

	state.overhead = 0;

	state.aligned = state.need_buf2 = 0;

	nbytes = state.nbytes = bytes(argv[optind]);
	state.stride_b = bytes(argv[optind+1]);

	if ((state.stride_b*32) > nbytes) {
		printf("ERR: Stride is too large for the work size. (maximum: %luByte)\n", nbytes / 32);
		return 1;
	}

	if ((state.stride_b / sizeof(TYPE)) < 1) {
		printf("ERR: Stride is too small. (minimum: %luByte)\n", sizeof(TYPE));
		return 1;
	}

	if (use_threadset) {
        CPU_ZERO(&cpu_set);
        CPU_SET(cpu_to_self_set, &cpu_set);
        ts_result = sched_setaffinity(pid, sizeof(cpu_set_t), &cpu_set);
        if (ts_result != 0) {
            printf("WARN: Failed to set cpu affinity.\n");
        }
    }

	// cgroup taskset (cpuset)
    if (use_cg_taskset) {
        cg_taskset(pid, ch_cpu_to_set);
    }
	
	init_loop(0, &state);
	for (int i = 0; i < NUM_SAMPLE; i++) {
		times[i] = wr(iterations, &state);
	}
	cleanup(0, &state);
	tim_mid = find_median(times, NUM_SAMPLE);

    access_cnt = iterations;
    
	if (ex_latency) {
		printf("%.5f,%.6f\n", (double)nbytes/(1024. * 1024.), tim_mid);
	}
	else {
		adjusted_bandwidth(tim_mid, nbytes, access_cnt, state.overhead);
	}
    
	return(0);
}

void
init_overhead(uint64_t iterations, void *cookie)
{
}

void
init_loop(uint64_t iterations, void *cookie)
{
	state_t *state = (state_t *) cookie;
	TYPE* buff_end;

	if (iterations) return;

    state->buf = (TYPE *)valloc(state->nbytes);
	state->buf2_orig = NULL;
	buff_end = (TYPE*)((char *)state->buf + state->nbytes - 1);
    state->remainder_boundary = (TYPE*)(buff_end - (state->stride_b * CHUNK_SIZE) + 1);
	state->lastone = (TYPE*)(buff_end - state->stride_b + 1);
	state->N = state->nbytes;

	if (!state->buf) {
		perror("malloc");
		exit(1);
	}
	bzero((void*)state->buf, state->nbytes);

	if (state->need_buf2 == 1) {
		state->buf2_orig = state->buf2 = (TYPE *)valloc(state->nbytes + 2048);
		if (!state->buf2) {
			perror("malloc");
			exit(1);
		}

		/* default is to have stuff unaligned wrt each other */
		/* XXX - this is not well tested or thought out */
		if (state->aligned) {
			char	*tmp = (char *)state->buf2;

			tmp += 2048 - 128;
			state->buf2 = (TYPE *)tmp;
		}
	}
}

void
cleanup(uint64_t iterations, void *cookie)
{
	state_t *state = (state_t *) cookie;

	if (iterations) return;

	free(state->buf);
	if (state->buf2_orig) free(state->buf2_orig);
}

double
wr(uint64_t iterations, void *cookie)
{	
	state_t *state = (state_t *) cookie;
	uint64_t start_t, end_t = 0;
	register TYPE *rem_boundary = state->remainder_boundary;
	register TYPE *lastone = state->lastone;
	register size_t stride = (size_t)(state->stride_b / sizeof(TYPE));
	register int head = 0;
	uint64_t chunk_count = 0;
    uint64_t stride_cnt = 0;
	uint64_t rest_cnt = 0;
    uint64_t outer_cnt = iterations;
    register TYPE *p;
	double res;
    
    // チャンク単位のアクセスを何回実行するか計算
    stride_cnt = state->nbytes / state->stride_b;
    chunk_count = stride_cnt / CHUNK_SIZE;
    // チャンクアクセスからはみ出る量を計算
    rest_cnt = stride_cnt % CHUNK_SIZE;
    
    #define	ONE(i)	p[i] = 1;
    start_t = get_time_ns();
    while (outer_cnt-- > 0) {
        p = state->buf;
	    while (p <= rem_boundary) {
            ONE(head) 
            ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) 
            ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) 
            ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) 
            ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) 
            ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) 
            ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) ONE(head+=stride) 
            ONE(head+=stride);
			p += (stride * CHUNK_SIZE);
			head = 0;
            // chunk_count はここの回数
	    }
        head = 0;
		while (p <= lastone) {
            ONE(head)
            p+=stride;
            // rest_cnt はここの回数
        }
	}
    end_t = get_time_ns();
	res = (double)(end_t - start_t);

	if (ex_latency) {
		res /= (double)((chunk_count * CHUNK_SIZE) + rest_cnt);
		res /= (double)iterations;
	}

	return res;
}
#undef	DOIT


/*
 * Almost like bandwidth() in lib_timing.c, but we need to adjust
 * bandwidth based upon loop overhead.
 */
void adjusted_bandwidth(uint64_t time, uint64_t bytes, uint64_t iter, double overhd)
{
#define MB	(1024. * 1024.)
	//extern FILE *ftiming;
	double secs = ((double)time / (double)iter - overhd) / 1000000000.0;
	double mb;
	
    //printf("bytes: %lu\n", bytes);
	mb = bytes / MB;

	if (secs <= 0.)
		return;
	//printf("mb: %.6f, secs: %.10f\n", mb, secs);

    //if (!ftiming) {
    //    ftiming = stderr;
    //}
	if (mb < 1.) {
		(void) fprintf(stderr, "%.6f,", mb);
	} else {
		(void) fprintf(stderr, "%.2f,", mb);
	}
	if (mb / secs < 1.) {
		(void) fprintf(stderr, "%.6f\n", mb/secs);
	} else {
		(void) fprintf(stderr, "%.2f\n", mb/secs);
	}
}
