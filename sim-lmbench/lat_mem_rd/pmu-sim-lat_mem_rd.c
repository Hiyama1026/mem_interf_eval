/*
 * -O2でコンパイルすること
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sched.h>
#include <errno.h>

#define MAX_CPU_NUM 1
#define MAX_MEM_PARALLELISM 16

#define CORETEX_A76
//#define CORETEX_A55

// PMUカウンタ
extern struct perf_event_attr pe0, pe1, pe2, pe3, pe4, pe5;
extern long fd0, fd1, fd2, fd3, fd4, fd5;
void reset_and_start_counter();
void stop_counter();
void create_six_event_group(int target_cpu, int event0, int event1, int event2, int event3, int event4, int event5);
void export_and_clean_counter(char* cnt_name0, char* cnt_name1, char* cnt_name2, char* cnt_name3, char* cnt_name4, char* cnt_name5);
// PMUカウンタの計測対象CPU
int target_cpu = 3;

bool use_cg_taskset = false;
bool use_threadset = false;

int* cpus_to_set;
int thread_cpu_num;

// アクセス回数
uint64_t num_iters = 1200 * 1000;
// wsをstrideで割り切れるときに立てるフラグ
bool just_size_flug = false;

//#define _DEBUG

// 差分計算用
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
	char*	p[MAX_MEM_PARALLELISM];
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

void error_print(void) {
    printf("arg[1]: Max buffer size\n  > Use 5GB if it is 0.\n");
    printf("arg[2]: Stride\n");
    printf("  > Use 2048KB if it is 0.\n");
    printf("arg[3 or more]: Reference below.\n");
    printf("  > Use \"-g <CPU NUM>\" when using cgroup-taskset(cpuset).\n");
    printf("  > Use \"-t <CPU NUM>\" when pinning threads to specific CPUs.\n");
}

// 10周アクセスして，キャッシュに乗せる
void warm_up(void *cookie, size_t list_len) {
    struct mem_state* state = (struct mem_state*)cookie;
    char **p = (char**)state->p[0];
    
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

	if ((unsigned long)p % state->pagesize)
		p += state->pagesize - (unsigned long)p % state->pagesize;

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
	state->p[0] = addr;

    if (!(range % stride))
        just_size_flug = true;
    else
    just_size_flug = false;
}

static volatile uint64_t	use_result_dummy;

void
use_pointer(void *result) { use_result_dummy += (long)result; }

void measure_stride_acesses(void *cookie) {
    struct mem_state* state = (struct mem_state*)cookie;
    register char **p = (char**)state->p[0];
    register uint64_t num_access;
	register size_t i;
    
    #if 1   // 計測用
    num_access = num_iters / 100;       // マクロ展開量で割る
    
    for (i = 0; i < num_access; ++i) {
        // THOUSAND;
        HUNDRED;
    }
    
    use_pointer((void *)p);
    state->p[0] = (char*)p;

    #else   // アクセス場所を表示するのみ(debug)
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
}

void
loads(size_t max_work_size, size_t size, size_t stride)
{
	struct mem_state state;
    size_t list_len;

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

    list_len = (state.len / state.line) + 1;

    if (just_size_flug)
        list_len--;     // WSがstrideで割り切れるとき，list_lenは1長くなるためデクリメント

    // PMU計測前に予めデータをキャッシュに乗せたい場合はコメント解除
    //warm_up(&state, list_len);

    reset_and_start_counter();      // PMUカウンタ起動
    measure_stride_acesses(&state);
    stop_counter();                 // PMUカウンタ停止
    
    free(state.addr);
    free(state.pages);

	/* We want to get to nanoseconds / load. */
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
    create_directory("/sys/fs/cgroup/sim_lmb/pmu-sim-lat_mem_rd");

    // Set CPU num 1 to cgroup config
    cg_fp = fopen("/sys/fs/cgroup/sim_lmb/pmu-sim-lat_mem_rd/cpuset.cpus", "w");
    if (!cg_fp) {
        printf("ERR: Could not open the croup-v2 cpuset file. (%s)\n", "/sys/fs/cgroup/sim_lmb/pmu-sim-lat_mem_rd/cpuset.cpus");
        exit(1);
    }
    fprintf(cg_fp, "%s", cpu_to_set);
    fclose(cg_fp);

    // Write PID to the  cpuset file
    cg_fp = fopen("/sys/fs/cgroup/sim_lmb/pmu-sim-lat_mem_rd/cgroup.procs", "w");
    if (!cg_fp) {
        printf("ERR: Could not open the croup-v2 cpuset file. (\"pmu-sim-lat_mem_rd\" group)\n");
        exit(1);
    }
    fprintf(cg_fp, "%d\n", pid);
    fclose(cg_fp);

    // Read an integer from the file
    do {
        cg_fp = fopen("/sys/fs/cgroup/sim_lmb/pmu-sim-lat_mem_rd/cgroup.procs", "r");
        if (!cg_fp) {
            printf("ERR: Could not open the croup-v2 cpuset file. (\"pmu-sim-lat_mem_rd\" group)\n");
            exit(1);
        }

        int fscan_res = fscanf(cg_fp, "%d", &cpuset_value);
        (void)fscan_res;
        fclose(cg_fp);
        usleep(100*1000);   // sleep 0.1s
        
        // 2s check
        if (pid_check_cnt == 20) {
            printf("ERR: Could not set PID to the cpuset setting. (\"pmu-sim-lat_mem_rd\" group)\n");
            exit(1);
        }
        pid_check_cnt++;
    } while (cpuset_value != pid);
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
    uint64_t stride;
	char* ch_cpu_to_set = NULL;
    pid_t pid = getpid();
    cpu_set_t cpu_set;
    int ts_result;

    if (argc != 3 && argc != 5) {
        error_print();
        return 1;
    }

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
                cpus_to_set = extract_cpunum(ch_cpu_to_set);
                target_cpu = cpus_to_set[0];
                break;
            case 't':
                use_threadset = true;
                ch_cpu_to_set = argv[(awc+3)+1];
                cpus_to_set = extract_cpunum(ch_cpu_to_set);
                target_cpu = cpus_to_set[0];
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

    if (max_work_size == 0) {
        max_work_size = 5 * 1024 * 1024;    // デフォルト5MB
    }
    else if (max_work_size < 5 * 1024) {
        printf("err: Maximum work size (%lu Byte) is too small (5K or more).\n", max_work_size);
        return 1;
    }
    else {
        //pass
    }
    if (stride == 0){
        stride = 2 * 1024;  // デフォルト2K
    }
    else if (stride < sizeof(char *)) {
        printf("err: Stride (%lu Byte) is too small (%lu Byte or more).\n", stride, sizeof(char *));
        return 1;
    }
    else {
        //pass
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

    fflush(stdout);
    fflush(stderr);

#ifdef CORETEX_A76
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("SW_INCR", "L1I_CACHE_REFILL", "L1I_TLB_REFILL", 
                            "L1D_CACHE_REFILL", "L1D_CACHE", "L1D_TLB_REFILL");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x8, 0x9, 0xa, 0xb, 0x10, 0x11);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("INST_RETIRED", "EXC_TAKEN", "EXC_RETURN", 
                            "CID_WRITE_RETIRED", "BR_MIS_PRED", "CPU_CYCLES");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("BR_PRED", "MEM_ACCESS", "L1I_CACHE", 
                            "L1D_CACHE_WB", "L2D_CACHE", "L2D_CACHE_REFILL");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L2D_CACHE_WB", "BUS_ACCESS", "MEMORY_ERROR", 
                            "INST_SPEC", "TTBR_WRITE_RETIRED", "BUS_CYCLES");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L2D_CACHE_ALLOCATE", "BR_RETIRED", "BR_MIS_PRED_RETIRED",
                            "STALL_FRONTEND", "STALL_BACKEND", "L1D_TLB");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x26, 0x29, 0x2a, 0x2b, 0x2d, 0x2f);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L1I_TLB", "L3D_CACHE_ALLOCATE", "L3D_CACHE_REFILL", 
                            "L3D_CACHE_RD", "L2D_TLB_REFILL", "L2D_TLB");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x31, 0x34, 0x35, 0x36, 0x37, 0x40);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("REMOTE_ACCESS", "DTLB_WALK", "ITLB_WALK", 
                            "LL_CACHE_RD", "LL_CACHE_MISS_RD", "L1D_CACHE_RD");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L1D_CACHE_WR", "L1D_CACHE_REFILL_RD", "L1D_CACHE_REFILL_WR", 
                            "L1D_CACHE_REFILL_INNER", "L1D_CACHE_REFILL_OUTER", "L1D_CACHE_WB_VICTIM");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x47, 0x48, 0x4C, 0x4D, 0x4E, 0x4F);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L1D_CACHE_WB_CLEAN", "L1D_CACHE_INVAL", "L1D_TLB_REFILL_RD", 
                            "L1D_TLB_REFILL_WR", "L1D_TLB_RD", "L1D_TLB_WR");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x50, 0x51, 0x52, 0x53, 0x56, 0x57);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L2D_CACHE_RD", "L2D_CACHE_WR", "L2D_CACHE_REFILL_RD", 
                            "L2D_CACHE_REFILL_WR", "L2D_CACHE_WB_VICTIM", "L2D_CACHE_WB_CLEAN");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x58, 0x5C, 0x5D, 0x5E, 0x5F, 0x60);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L2D_CACHE_INVAL", "L2D_TLB_REFILL_RD", "L2D_TLB_REFILL_WR", 
                            "L2D_TLB_RD", "L2D_TLB_WR", "BUS_ACCESS_RD");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x61, 0x66, 0x67, 0x68, 0x69, 0x6C);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("BUS_ACCESS_WR", "MEM_ACCESS_RD", "MEM_ACCESS_WR", 
                            "UNALIGNED_LD_SPEC", "UNALIGNED_ST_SPEC", "LDREX_SPEC");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x73);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("STREX_PASS_SPEC", "STREX_FAIL_SPEC", "STREX_SPEC", 
                            "LD_SPEC", "ST_SPEC", "DP_SPEC");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("ASE_SPEC", "VFP_SPEC", "PC_WRITE_SPEC", 
                            "CRYPTO_SPEC", "BR_IMMED_SPEC", "BR_RETURN_SPEC");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x7A, 0x7C, 0x7D, 0x7E, 0x81, 0x82);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("BR_INDIRECT_SPEC", "ISB_SPEC", "DSB_SPEC", 
                            "DMB_SPEC", "EXC_UNDEF", "EXC_SVC");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x83, 0x84, 0x86, 0x87, 0x88, 0x8A);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("EXC_PABORT", "EXC_DABORT", "EXC_IRQ", 
                            "EXC_FIQ", "EXC_SMC", "EXC_HVC");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("EXC_TRAP_PABORT", "EXC_TRAP_DABORT", "EXC_TRAP_OTHER", 
                            "EXC_TRAP_IRQ", "EXC_TRAP_FIQ", "RC_LD_SPEC");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x91, 0xA0, 0x1, 0x2, 0x3, 0x4);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("RC_ST_SPEC", "L3D_CACHE_RD", "--", 
                            "--", "--", "--");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x500, 0x501, 0x502, 0x503, 0x504, 0x4);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("SCU_PFTCH_CPU_ACCESS", "SCU_PFTCH_CPU_MISS", "SCU_PFTCH_CPU_HIT", 
                            "SCU_PFTCH_CPU_MATCH", "SCU_PFTCH_CPU_KILL", "--");
    /*--------------------------------------------------------------------------*/
#endif

#ifdef CORETEX_A55
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("SW_INCR", "L1I_CACHE_REFILL", "L1I_TLB_REFILL", 
                            "L1D_CACHE_REFILL", "L1D_CACHE", "L1D_TLB_REFILL");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("LD_RETIRED", "ST_RETIRED", "INST_RETIRED", 
                            "EXC_TAKEN", "EXC_RETURN", "CID_WRITE_RETIRED");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0xC, 0xD, 0xE, 0xF, 0x10, 0x11);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("PC_WRITE_RETIRED", "BR_IMMED_RETIRED", "BR_RETURN_RETIRED", 
                            "UNALIGNED_LDST_RETIRED", "BR_MIS_PRED", "CPU_CYCLES");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("BR_PRED", "MEM_ACCESS", "L1I_CACHE", 
                            "L1D_CACHE_WB", "L2D_CACHE", "L2D_CACHE_REFILL");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L2D_CACHE_WB", "BUS_ACCESS", "MEMORY_ERROR", 
                            "INT_SPEC", "TTBR_WRITE_RETIRED", "BUS_CYCLES");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x00, 0x20, 0x21, 0x22, 0x23, 0x24);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("-- (Could not read 0x1E/CHAIN)", "L2D_CACHE_ALLOCATE", "BR_RETIRED", 
                            "BR_MIS_PRED_RETIRED", "STALL_FRONTEND", "STALL_BACKEND");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x25, 0x26, 0x29, 0x2A, 0x2B, 0x2D);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L1D_TLB", "L1I_TLB", "L3D_CACHE_ALLOCATE", 
                            "L3D_CACHE_REFILL", "L3D_CACHE", "L2D_TLB_REFILL");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x2F, 0x34, 0x35, 0x36, 0x37, 0x38);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L2D_TLB", "DTLB_WALK", "ITLB_WALK", 
                            "LL_CACHE_RD", "LL_CACHE_MISS_RD", "REMOTE_ACCESS_RD");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L1D_CACHE_RD", "L1D_CACHE_WR", "L1D_CACHE_REFILL_RD", 
                            "L1D_CACHE_REFILL_WR", "L1D_CACHE_REFILL_INNER", "L1D_CACHE_REFILL_OUTER");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x50, 0x51, 0x52, 0x53, 0x60, 0x61);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L2D_CACHE_RD", "L2D_CACHE_WR", "L2D_CACHE_REFILL_RD", 
                            "L2D_CACHE_REFILL_WR", "BUS_ACCESS_RD", "BUS_ACCESS_WR");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x66, 0x67, 0x70, 0x71, 0x72, 0x73);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("MEM_ACCESS_RD", "MEM_ACCESS_WR", "LD_SPEC", 
                            "ST_SPEC", "LDST_SPEC", "DP_SPEC");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("ASE_SPEC", "VFP_SPEC", "PC_WRITE_SPEC", 
                            "CRYPTO_SPEC", "BR_IMMED_SPEC", "BR_RETURN_SPEC");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0x7A, 0x86, 0x87, 0xA0, 0xA2, 0xC0);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("BR_INDIRECT_SPEC", "EXC_IRQ", "EXC_FIQ", 
                            "L3D_CACHE_RD", "L3D_CACHE_REFILL_RD", "L3D_CACHE_REFILL_PREFETCH");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L2D_CACHE_REFILL_PREFETCH", "L1D_CACHE_REFILL_PREFETCH", "L2D_WS_MODE", 
                            "L1D_WS_MODE_ENTRY", "L1D_WS_MODE", "PREDECODE_ERROR");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0xC7, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L3D_WS_MODE", "BR_COND_PRED", "BR_INDIRECT_MIS_PRED", 
                            "BR_INDIRECT_ADDR_MIS_PRED", "BR_COND_MIS_PRED", "BR_INDIRECT_ADDR_PRED");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("BR_RETURN_ADDR_PRED", "BR_RETURN_ADDR_MIS_PRED", "L2D_LLWALK_TLB", 
                            "L2D_LLWALK_TLB_REFILL", "L2D_L2WALK_TLB", "L2D_L2WALK_TLB_REFILL");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0xD4, 0xD5, 0xD6, 0xE1, 0xE2, 0xE3);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("L2D_S2_TLB", "L2D_S2_TLB_REFILL", "L2D_CACHE_STASH_DROPPED", 
                            "STALL_FRONTEND_CACHE", "STALL_FRONTEND_TLB", "STALL_FRONTEND_PDERR");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("STALL_BACKEND_ILOCK", "STALL_BACKEND_ILOCK_AGU", "STALL_BACKEND_ILOCK_FPU", 
                            "STALL_BACKEND_LD", "STALL_BACKEND_ST", "STALL_BACKEND_LD_CACHE");
    /*--------------------------------------------------------------------------*/
    // PMUカウンタグループの生成
    create_six_event_group(target_cpu, 0xEA, 0xEB, 0xEC, 0x00, 0x00, 0x00);

    // テスト実行
    loads(max_work_size, max_work_size, stride);

    // Read and print result
    export_and_clean_counter("STALL_BACKEND_LD_TLB", "STALL_BACKEND_ST_STB", "STALL_BACKEND_ST_TLB", 
                            "---", "---", "---");
    /*--------------------------------------------------------------------------*/
#endif

    return 0;
}
