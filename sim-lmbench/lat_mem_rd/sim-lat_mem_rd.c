/*
 * Compile with -O2
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
# include <sched.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_CPU_NUM 32
#define NUM_SAMPLE 100

// Number of accesses
uint64_t num_iters = 1200 * 1000;
// Flag set when buffer-size is divisible by stride
bool just_size_flug = false;
// Array to store measurement times
double times[NUM_SAMPLE];

#define kBufferSizePow2Low 13
#define kBufferSizePow2High 29

bool use_threadset = false;
bool use_cg_taskset = false;
bool use_step = true;

int* cpus_to_set;
int thread_cpu_num;

//#define _DEBUG

// For difference calculation
#include <stddef.h>
ptrdiff_t diff;
ptrdiff_t ca_diff;
char* prev_p;
char** pre_caa_p;


#define	ONE	p = (char **)*p;
#define	FIVE	ONE ONE ONE ONE ONE
#define	TEN	FIVE FIVE
#define	FIFTY	TEN TEN TEN TEN TEN
#define	HUNDRED	FIFTY FIFTY
#define F_HUNDRED HUNDRED HUNDRED HUNDRED HUNDRED HUNDRED
#define THOUSAND F_HUNDRED F_HUNDRED

struct mem_state {
	char*	addr;	/* raw pointer returned by malloc */
	char*	base;	/* page-aligned pointer */
	char*	p;
	int	initialized;
	int	width;
	size_t	len;
	size_t	maxlen;
	size_t	line;
	size_t	pagesize;
	size_t	nlines;
	size_t	npages;
	size_t	nwords;
	size_t*	pages;
	size_t*	lines;
	size_t*	words;
};

uint64_t get_time_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return 1000000000ULL * t.tv_sec + t.tv_nsec;
}

void error_print(void) {
    printf("arg[1]: Max buffer size\n  > Use 5GB if it is 0.\n");
    printf("arg[2]: Stride\n");
    printf("  > Use 2048KB if it is 0.\n");
    printf("arg[3 or more]: Reference below.\n");
    printf("  > Use \"-g <CPU NUM>\" when using cgroup-taskset(cpuset).\n");
    printf("  > Use \"-t <CPU NUM>\" when pinning threads to specific CPUs.\n");
    printf("  > Use \"-o <CPU NUM>\" when using one-shot mode.\n");
}

// Comparison function (for qsort)
int compare(const void* a, const void* b) {
    double diff = *(double*)a - *(double*)b;
    return (diff > 0) - (diff < 0);     // Return positive or negative
}

// Find the median
double find_median(double* arr, size_t size) {
    if (size == 0) {
        fprintf(stderr, "Array size must be greater than 0.\n");
        exit(EXIT_FAILURE);
    }

    // sort
    qsort(arr, size, sizeof(double), (int (*)(const void*, const void*))compare);

    // Calculate median
    if (size % 2 == 0) {
        return (arr[size / 2 - 1] + arr[size / 2]) / 2.0;
    } else {
        return arr[size / 2];
    }
}

// Access 10 times to warm up cache
void warm_up(void *cookie, size_t list_len) {
    struct mem_state* state = (struct mem_state*)cookie;
    char **p = (char**)state->p;
    
    for (volatile size_t j = 0; j < list_len; ++j) {
        TEN;
    }
}

size_t*
permutation(size_t max, size_t scale)
{
	size_t	i, v, o;
	static size_t r = 0;
	size_t*	result = (size_t*)malloc(max * sizeof(size_t));

	if (result == NULL) return NULL;

	for (i = 0; i < max; ++i) {
		result[i] = i * scale;
	}
#if 1
	if (r == 0)
		r = (getpid()<<6) ^ getppid() ^ rand() ^ (rand()<<10);

	/* randomize the sequence */
	for (i = 0; i < max; ++i) {
		r = (r << 1) ^ rand();
		o = r % max;
		v = result[o];
		result[o] = result[i];
		result[i] = v;
	}

#ifdef _DEBUG
	fprintf(stderr, "permutation(%d): {", max);
	for (i = 0; i < max; ++i) {
	  fprintf(stderr, "%d", result[i]);
	  if (i < max - 1) 
	    fprintf(stderr, ",");
	}
	fprintf(stderr, "}\n");
	fflush(stderr);
#endif /* _DEBUG */
#endif

	return (result);
}

void
base_initialize(void* cookie)
{
	size_t	nwords, nlines, nbytes, npages, nmpages;
	size_t *pages;
	size_t *lines;
	size_t *words;
	struct mem_state* state = (struct mem_state*)cookie;
	char *p = 0 /* lint */;

	state->initialized = 0;

	nbytes = state->len;
	nwords = state->line / sizeof(char*);
	nlines = state->pagesize / state->line;
	npages = (nbytes + state->pagesize - 1) / state->pagesize;
	nmpages= (state->maxlen + state->pagesize - 1) / state->pagesize;

	srand(getpid());

	words = NULL;
	lines = NULL;
	pages = permutation(nmpages, state->pagesize);
	p = state->addr = (char*)malloc(state->maxlen + 2 * state->pagesize);
	if (!p) {
		perror("base_initialize: malloc");
		exit(1);
	}

	state->nwords = nwords;
	state->nlines = nlines;
	state->npages = npages;
	state->lines = lines;
	state->pages = pages;
	state->words = words;

	if (state->addr == NULL || pages == NULL)
		return;

	if ((unsigned long)p % state->pagesize) {
		p += state->pagesize - (unsigned long)p % state->pagesize;
	}
	state->base = p;
	state->initialized = 1;
}

void
stride_initialize(void* cookie)
{
	struct mem_state* state = (struct mem_state*)cookie;
	size_t	i;
	size_t	range = state->len;
	size_t	stride = state->line;
	char*	addr;

	base_initialize(cookie);
	if (!state->initialized) {
        printf("WARN: !state->initialized\n");
        return;
    }
	addr = state->base;

	for (i = stride; i < range; i += stride) {
        *(char **)&addr[i - stride] = (char*)&addr[i];
	}
	*(char **)&addr[i - stride] = (char*)&addr[0]; 
	state->p = addr;

    if (!(range % stride))
        just_size_flug = true;
    else
    just_size_flug = false;
}

static volatile uint64_t	use_result_dummy;

void
use_pointer(void *result) { use_result_dummy += (long)result; }

double measure_stride_acesses(void *cookie) {
    struct mem_state* state = (struct mem_state*)cookie;
    register char **p = (char**)state->p;
    register uint64_t num_access;
	register size_t i;
	size_t list_len = (state->len / state->line) + 1;
    uint64_t start_t, end_t;
    double res;
    
    
	if (just_size_flug)
        list_len--;     // When the buffer size is divisible by stride, list_len increases by 1 and is decremented.
    
    #if 1   // Measurement
    
    warm_up(cookie, list_len);

    num_access = num_iters / 100;       // Divide by the macro defined size.
    
    start_t = get_time_ns();
    for (i = 0; i < num_access; ++i) {
        // THOUSAND;
        HUNDRED;
    }
    
    use_pointer((void *)p);
    state->p = (char*)p;
    
    end_t = get_time_ns();
    res = (double)(end_t - start_t) / (double)(num_access * 100);
    
    #else   // Address print only(debug)
    ca_diff = 0;
    diff = 0;
    prev_p = (char*)p;
    pre_caa_p = p;
    while (1) {
        fprintf(stderr, "p: %p,        diff: %td\n", p, ca_diff);
        fprintf(stderr, "p: %p, (char*)diff: %td\n", p, diff);
        if (ca_diff < 0) {
            printf("%p, %ld\n", (char *)p, ca_diff);
            exit(1);
            break;
        }
        ONE;
        ca_diff = (char*)p - prev_p;
        diff = p - pre_caa_p; // アドレスの差分を計算

        prev_p = (char*)p;
        pre_caa_p = p;
        //HUNDRED;
    }
    #endif

    return res;
}

void
loads(size_t max_work_size, size_t size, size_t stride)
{
	double result;
	struct mem_state state;

	if (size < stride) 
        return;
    
	state.width = 1;
	state.len = size;
	state.maxlen = max_work_size;
	state.line = stride;
	state.pagesize = getpagesize();

	/*
	 * Now walk them and time it.
	 */
    stride_initialize(&state);

    for (int j = 0; j < NUM_SAMPLE; j++) {
        times[j] = measure_stride_acesses(&state);
    }
    result = find_median(times, NUM_SAMPLE);
    
    free(state.addr);
    free(state.pages);

	/* We want to get to nanoseconds / load. */
	fprintf(stderr, "%.5f,%.3f\n", size / (1024. * 1024.), result);
    fflush(stdout);
    fflush(stderr);

}

uint64_t parse_size(const char* str) {
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

int create_directory(const char *path) {
    struct stat st = {0};
    int lg_mkdir;
    
    // Check if the directory already exists
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

int compare_desc(const void *a, const void *b) {
    int va = *(const int*)a;
    int vb = *(const int*)b;
    return vb - va;
}

int* extract_cpunum(char* cpu_src){
    int* extracted = malloc(sizeof(int) * MAX_CPU_NUM);
    char *token = strtok(cpu_src, ",");

    thread_cpu_num = 0;
    while (token != NULL) {
        if (thread_cpu_num > MAX_CPU_NUM - 1) {
            printf("ERR: Too many cpus to set. (MAX_CPU_NUM: %d)\n", MAX_CPU_NUM);
            exit(1);
        }
        extracted[thread_cpu_num] = atoi(token);
        token = strtok(NULL, ",");
        thread_cpu_num++;
    }

    qsort(extracted, thread_cpu_num, sizeof(int), compare_desc);

    return extracted;
}

void cpu_set_all(cpu_set_t* cpu_set, int* cpus_num) {
    int cnt = 0;

    while(cnt < thread_cpu_num) {
        CPU_SET(cpus_num[cnt], cpu_set);
        cnt++;
    }
}

int main(int argc, char* argv[]) {
    size_t max_work_size;
    size_t size = 0;
    uint64_t stride;
    char* ch_cpu_to_set = NULL;
    pid_t pid = getpid();
    cpu_set_t cpu_set;
    int ts_result;

    if (argc < 3) {
		error_print();
        return 1;
    }
    
    max_work_size = (size_t)parse_size(argv[1]);
    stride = parse_size(argv[2]);

    argc -= 3;
    for (int awc = 0; awc < argc; awc+=2) {        
        switch (argv[awc+3][1]) {
            case 'g':
                use_cg_taskset = true;
                ch_cpu_to_set = argv[(awc+3)+1];
                break;
            case 't':
                use_threadset = true;
                ch_cpu_to_set = argv[(awc+3)+1];
                cpus_to_set = extract_cpunum(ch_cpu_to_set);
                break;
            case 'o':
                use_step = false;
                awc--;
                break;
            default:
                printf("ERR: Unknown argument. (%s)\n\n", argv[awc+3]);
                error_print();
                return 1;
        }
    }

    if (use_threadset && use_cg_taskset) {
        printf("ERR: The -g option and the -t option cannot be used together.\n\n");
        error_print();
        exit(1);
    }

    // Set work size and stride (output settings in step mode)
    max_work_size = (size_t)parse_size(argv[1]);
    stride = parse_size(argv[2]);
    
    if (max_work_size == 0) {
        max_work_size = 5 * 1024 * 1024;    // Default 5MB
        if (use_step)
        printf("max_ws (default), %lu\n", max_work_size);
    }
    else if (max_work_size < 5 * 1024) {
        printf("err: Maximum work size (%lu Byte) is too small (5K or more).\n", max_work_size);
        return 1;
    }
    else {
        if (use_step)
            printf("max_ws, %lu\n", max_work_size);
    }
    if (stride == 0){
        stride = 2 * 1024;  // Default 2K
        if (use_step)
            printf("stride (default), %lu\n\n", stride);
    }
    else if (stride < sizeof(char *)) {
        printf("err: Stride (%lu Byte) is too small (%lu Byte or more).\n", stride, sizeof(char *));
        return 1;
    }
    else {
        if (use_step)
        printf("stride, %lu\n\n", stride);
    }

    if (use_threadset) {
        CPU_ZERO(&cpu_set);
        cpu_set_all(&cpu_set, cpus_to_set);
        ts_result = sched_setaffinity(pid, sizeof(cpu_set_t), &cpu_set);
        if (ts_result != 0) {
            printf("WARN: Failed to set cpu affinity.\n");
        }
    }

    // cgroup taskset (cpuset)
    if (use_cg_taskset) {
        cg_taskset(pid, ch_cpu_to_set);
    }

    if (use_step) {
        // Display only in step mode
        printf("Buffer [MB], Latency [ns]\n");
    }

    fflush(stdout);
    fflush(stderr);

    int i = kBufferSizePow2Low;

    if (use_step) {     // Step mode
        while (i < kBufferSizePow2High && size <= max_work_size) {
            const int64_t num_bt = 2;
            for (int64_t b = (1 << num_bt) - 1; b >= 0; b--) {
                size = (1ULL << i) - (b << (i - num_bt - 1));
                if (size > max_work_size) {
                    break;
                }
                loads(max_work_size, size, stride);
            }
            i++;
        }
    }
    else {              // Only the specified size (max_work_size)
        loads(max_work_size, max_work_size, stride);
    }

    if (use_step) {
        // Display only in step mode
        printf("\ntest end (ws: %lu)\n", max_work_size);
    }
    return 0;
}
