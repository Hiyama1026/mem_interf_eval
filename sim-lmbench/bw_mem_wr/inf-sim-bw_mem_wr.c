/*
 * -O2でコンパイルすること
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>

#define TYPE        char
#define CHUNK_SIZE  32
//#define DEBUG

#ifdef DEBUG
void db_wr(uint64_t iterations, void *cookie);
#endif

double	wr(uint64_t iterations, void *cookie);
void	report_cnt(uint64_t iterations, void *cookie);
void	init_loop(uint64_t iterations, void *cookie);
void	cleanup(uint64_t iterations, void *cookie);

bool use_cg_taskset = false;
bool use_report = false;
bool use_file_export = false;
bool use_buffsize_set = false;
bool buf_full_flag = false;
bool use_threadset = false;

uint64_t start_t, end_t = 0;
double  *buf_log;
uint32_t buf_log_size = 1024*1024*3;
FILE *log_fp;
char log_file_path[256];
uint32_t result_idx = 0;

// バッファサイズとstride幅 (ログファイル出力用)
size_t work_size;
uint64_t stride;

// コメント解除で，同名のログファイルが既存の場合に，削除して良いかをユーザに確認してから削除するようになる．
//#define CHECK_LOGFILE_DEL

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
    printf("  > Use 5GB if it is 0.\n");
    printf("arg[2]: Stride\n");
    printf("  > Use 2048KB if it is 0.\n");
    printf("arg[3 or more]: Reference below.\n");
    printf("  > Use \"-r <REPORT INTERVAL>\" when using report-function.\n");
    printf("  > Use \"-g <CPU NUM>\" when using cgroup-taskset(cpuset).\n");
    printf("  > Use \"-f <FILE NAME>\" when using file export.\n");
    printf("  > Use \"-e <NUM OF ELEMENTS>\" to set the number of logs can be saved.\n");
    printf("  > Use \"-t <CPU NUM>\" when pinning threads to a specific CPU.\n");

}

char
last(char *s)
{
    while (*s++);
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

void signal_handler(int signum) {
    int report_idx = 0;

    //printf("signal_handler (%d)\n", signum);
    if (use_file_export) {
        log_fp = fopen(log_file_path, "w");
        if (!log_fp) {
            printf("ERR: Could not open the log file. (%s)\n", log_file_path);
            exit(1);
        }
        if (buf_full_flag) {
            fprintf(log_fp, "WARN: BUFFER FULL.\n\n");
        }
        fprintf(log_fp, "BufSize,%lu, ,Stride,%lu\n\n", work_size, stride);
        fprintf(log_fp, "WriteLatency[ns]\n");
        while(report_idx < result_idx){
            fprintf(log_fp, "%.5f\n", buf_log[report_idx]);
            report_idx++;
        }
        fclose(log_fp);
        exit(0);
    }
    exit(0);
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
    uint64_t iterations = 10000;
	double res;
	size_t	nbytes;
	state_t	state;
	int mandatory_argc = 3;
	char *endptr;
    char *file_name;
	char* ch_cpu_to_set = NULL;
    pid_t pid = getpid();
    int cpu_to_self_set = 0;
    int ts_result = 0;
    cpu_set_t cpu_set;

	//printf("int: %lu\n", sizeof(TYPE));

	if (argc < mandatory_argc) {
		error_print();
        return 1;
    }

    //for (int awc = 0; awc < (argc/2); awc++) {        
    for (int awc = mandatory_argc; awc < argc; awc+=2) {      
        if (argv[awc][2] != '\0') {
                error_print();
                exit(EXIT_FAILURE);
            }  
        switch (argv[awc][1]) {
            case 'r':
                use_report = true;
                iterations = strtoull(argv[awc+1], &endptr, 10);
                break;
            case 't':
                    use_threadset = true;
                    cpu_to_self_set = atoi(argv[awc+1]);
                    break;
            case 'g':
                use_cg_taskset = true;
                ch_cpu_to_set = argv[awc+1];
                break;
            case 'f':
                use_file_export = true;
                file_name = argv[awc+1];
                sprintf(log_file_path, "%s", file_name);
                break;
            case 'e':
                    buf_log_size = strtoull(argv[awc+1], &endptr, 10);
                    use_buffsize_set = true;
                break;
            default:
                printf("ERR: Unknown argument. (%s)\n\n", argv[(awc*2)+3]);
                error_print();
                return 1;
        }
    }

    if (!use_report && use_file_export) {
        printf("ERR: You must specify the report interval using -r option when using -f option.\n\n");
        error_print();
        exit(1);
    }

    if (!use_file_export && use_buffsize_set) {
        printf("ERR: Use -f with -f.\n\n");
        error_print();
        exit(1);
    }

    state.overhead = 0;
	
	state.aligned = state.need_buf2 = 0;

	nbytes = state.nbytes = bytes(argv[1]);
	state.stride_b = bytes(argv[2]);
    // ログファイル出力用に保持
    work_size = nbytes;
    stride = state.stride_b;
	
	if ((state.stride_b*CHUNK_SIZE) > nbytes) {
		printf("ERR: Stride is too large for the work size. (maximum: %luByte)\n", nbytes / CHUNK_SIZE);
		return 1;
	}

	if ((state.stride_b / sizeof(TYPE)) < 1) {
		printf("ERR: Stride is too small. (minimum: %luByte)\n", sizeof(TYPE));
		return 1;
	}
    
	init_loop(0, &state);

    if (use_file_export) {
        buf_log = (double *)valloc(sizeof(double) * buf_log_size);
        if (!buf_log) {
            perror("malloc");
            exit(1);
        }
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

    // File export
    if (use_file_export) {
        create_directory("./wr-infresults/");
        // 同名のファイルが存在する確認．存在しなければ生成
        log_fp = fopen(log_file_path, "r");
        if (!log_fp) {
            printf("Create log file\n");
            log_fp = fopen(log_file_path, "w");
            if (!log_fp) {
                printf("Failed to create %s.\n", log_file_path);
                exit(1);
            }
        }
        else {
            fclose(log_fp);
#ifdef CHECK_LOGFILE_DEL
            // 既存のファイルを削除してよいか確認してから削除
            char is_remove[16];
            bool is_check_fdel = false;
            while(!is_check_fdel) {
                printf("%s is already exist.\n", log_file_path);
                printf("Remove it? (Y/n)\n");
                fflush(stdin);

                if (fgets(is_remove, sizeof(is_remove), stdin) == NULL) {
                    printf("ERR: Text input error.\n");
                    exit(1);
                }
                is_remove[strcspn(is_remove, "\n")] = 0;    // 改行を除去
                if (strlen(is_remove) == 0 || strcmp(is_remove, "Y") == 0 || strcmp(is_remove, "y") == 0) {
                    if (remove(log_file_path) != 0) {
                        printf("ERR: Failed to remove %s.\n", log_file_path);
                        exit(1);
                    }
                    else {
                        printf("Logfile deleted.\n");
                    }
                    is_check_fdel = true;
                }
                else if (strcmp(is_remove, "N") == 0 || strcmp(is_remove, "n") == 0) {
                    printf("Prosess stopped.\n");
                    is_check_fdel = true;
                    exit(2);
                }
            }
            // 再度ファイル作成
            log_fp = fopen(log_file_path, "w");
            if (!log_fp) {
                printf("ERR: Failed to create %s.\n", log_file_path);
                exit(1);
            }
            fclose(log_fp);
#else
            // 既存のログファイルを削除
            if (remove(log_file_path) != 0) {
                printf("ERR: Failed to remove %s.\n", log_file_path);
                exit(1);
            }
            else {
                printf("Logfile deleted.\n");
            }
#endif // CHECK_LOGFILE_DEL
        }
    }

    // シグナルハンドラの登録 (Ctrl+C, killall)
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Write start.\n");
    fflush(stdout);

    #ifdef DEBUG
    db_wr(iterations, &state);
    #endif

	while(1) {
		res = wr(iterations, &state);
        if (use_report && !use_file_export) {
		    fprintf(stderr, "%.5f\n", res);
            fflush(stderr);
        }
		
		//res = wr(iterations, &state);
		//access_cnt = iterations;
		//adjusted_bandwidth(res, nbytes, iterations, state.overhead);
	}
	cleanup(0, &state);
    
	return(0);
}

void
report_cnt(uint64_t iterations, void *cookie)
{
}

void
init_loop(uint64_t iterations, void *cookie)
{
	state_t *state = (state_t *) cookie;
    uint access_num = 0;
    TYPE* buff_end;

	if (iterations) return;

    state->buf = (TYPE *)valloc(state->nbytes);
	state->buf2_orig = NULL;
	buff_end = (TYPE*)((char *)state->buf + state->nbytes - 1);
    state->remainder_boundary = (TYPE*)(buff_end - (state->stride_b * CHUNK_SIZE) + 1);
    access_num = state->nbytes / state->stride_b;
    if ((state->nbytes % state->stride_b) != 0) {
        access_num++;
    }
    state->lastone = (TYPE*)(state->buf + (state->stride_b * (access_num - 1)));
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
    //printf("start: %p, lastone: %p\n", state->buf, state->lastone);
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
	register TYPE *rem_boundary = state->remainder_boundary;
	register TYPE *lastone = state->lastone;
	register size_t stride = (size_t)(state->stride_b / sizeof(TYPE));
	register int head = 0;
	double res = 0;
	uint64_t chunk_count = 0;
    uint64_t stride_cnt = 0;
	uint64_t rest_cnt = 0;
    uint64_t outer_cnt = iterations;
    register TYPE *p;
    
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

    if (use_report || use_file_export) {
        res = (double)(end_t - start_t);
        res /= (double)((chunk_count * CHUNK_SIZE) + rest_cnt);
        res /= (double)iterations;
    }

    if (use_file_export) {
        buf_log[result_idx] = res;
        result_idx++;
        if (result_idx >= buf_log_size) {
            buf_full_flag = true;
            signal_handler(15);
        }
    }

	return res;
}
#undef	ONE

// ToDo 帯域幅出力するか否か
void adjusted_bandwidth(uint64_t time, uint64_t bytes, uint64_t iter, double overhd)
{
	#define MB	(1024. * 1024.)

	double secs = ((double)time / (double)iter - overhd) / 1000000000.0;
	double mb;
	
	mb = bytes / MB;

	if (secs <= 0.)
		return;

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

	return;
}

#ifdef DEBUG
void
db_wr(uint64_t iterations, void *cookie)
{	
	state_t *state = (state_t *) cookie;
	register TYPE *rem_boundary = state->remainder_boundary;
	register TYPE *lastone = state->lastone;
	register size_t stride = (size_t)(state->stride_b / sizeof(TYPE));
	register int head = 0;
    uint64_t outer_cnt = iterations;
    register TYPE *p;
    TYPE *start_p, *end_p;
    TYPE* accessed_tail = NULL;
    int t = 0;

    
    #define	ONE(i)	p[i] = 1;
    while (outer_cnt-- > 0) {
        p = state->buf;
        start_p = p;
	    while (p <= rem_boundary) {
            ONE(head)   // 1
            ONE(head+=stride)   // 2
            ONE(head+=stride)   // 3
            ONE(head+=stride)   // 4
            ONE(head+=stride)   // 5
            ONE(head+=stride)   // 6
            ONE(head+=stride)   // 7
            ONE(head+=stride)   // 8
            ONE(head+=stride)   // 9
            ONE(head+=stride)   // 10
            ONE(head+=stride)   // 11
            ONE(head+=stride)   // 12
            ONE(head+=stride)   // 13
            ONE(head+=stride)   // 14
            ONE(head+=stride)   // 15
            ONE(head+=stride)   // 16
            ONE(head+=stride)   // 17
            ONE(head+=stride)   // 18
            ONE(head+=stride)   // 19
            ONE(head+=stride)   // 20
            ONE(head+=stride)   // 21
            ONE(head+=stride)   // 22
            ONE(head+=stride)   // 23
            ONE(head+=stride)   // 24
            ONE(head+=stride)   // 25
            ONE(head+=stride)   // 26
            ONE(head+=stride)   // 27
            ONE(head+=stride)   // 28
            ONE(head+=stride)   // 29
            ONE(head+=stride)   // 30
            ONE(head+=stride)   // 31
            ONE(head+=stride);  // 32 (chunk_size)

            accessed_tail = p+(stride*31);  // 31回strideを足した為
            printf("CHUNK-LAST: %p\n", accessed_tail);

			p += (stride * CHUNK_SIZE);
			head = 0;
	    }
        head = 0;
        
		while (p <= lastone) {
            ONE(head)

            printf("REMAIN-p: %p (%d)\n", p, t);
            accessed_tail = p;
            t++;

            p+=stride;
        }
        end_p = accessed_tail;
        
        ptrdiff_t diff = (char*)end_p - (char*)start_p;
        printf("\n");
        printf("start: %p, end: %p, diff: %ld (%dKB, rem: %d)\n", start_p, end_p, diff, (int)diff/1024, (int)diff%1024);
        exit(0);
	}
}
#endif