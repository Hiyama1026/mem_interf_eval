/*
 * メモリアクセス方法をLMbenchと同様にstrideずつアクセスするように変更したmemsys 
 * -O2でコンパイルすること ($ gcc -O2 -Wall -o memsys_lmb memsys_lmb.c -lm)
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
#include <errno.h>

#define MAX_MEM_PARALLELISM 16
#define NUM_SAMPLE 100

// アクセス回数(ToDo：LMbenchでだいたいどれくらいの値が入るか調べて揃える)
uint64_t num_iters = 1200 * 1000;
// wsをstrideで割り切れるときに立てるフラグ
bool just_size_flug = false;
// 計測時間保存用配列
double times[NUM_SAMPLE];

#define kBufferSizePow2Low 13
#define kBufferSizePow2High 29

// ワークサイズをステップ増加するかを示すフラグ
bool use_step = true;

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

uint64_t get_time_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return 1000000000ULL * t.tv_sec + t.tv_nsec;
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

// 10周アクセスして，キャッシュに乗せる
void warm_up(void *cookie, size_t list_len) {
    struct mem_state* state = (struct mem_state*)cookie;
    char **p = (char**)state->p[0];
    
    for (volatile size_t j = 0; j < list_len; ++j) {
        TEN;
    }
}

/*---------------------------------------------------*/
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
/*---------------------------------------------------*/
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
    //if (state->addr == NULL)
	//	return;

	if ((unsigned long)p % state->pagesize) {
		p += state->pagesize - (unsigned long)p % state->pagesize;
	}
	state->base = p;
	state->initialized = 1;
	//mem_reset();
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

	//mem_reset();
}

static volatile uint64_t	use_result_dummy;

void
use_pointer(void *result) { use_result_dummy += (long)result; }

double measure_random_acesses(void *cookie) {
    struct mem_state* state = (struct mem_state*)cookie;
    register char **p = (char**)state->p[0];
    register uint64_t num_access;
	register size_t i;
	size_t list_len = (state->len / state->line) + 1;
	//register size_t count = state->len / (state->line * 100) + 1;
    uint64_t start_t, end_t;
    double res;
    
    
	if (just_size_flug)
        list_len--;     // WSがstrideで割り切れるとき，list_lenは1長くなるためデクリメント
    
    #if 1   // 計測用
    
    warm_up(cookie, list_len);

    num_access = num_iters / 100;       // マクロ展開量で割る
    
    start_t = get_time_ns();
    for (i = 0; i < num_access; ++i) {
//        THOUSAND;
        HUNDRED;
    }
    
    use_pointer((void *)p);
    state->p[0] = (char*)p;
    
    end_t = get_time_ns();
    res = (double)(end_t - start_t) / (double)(num_access * 100);      // メモリアクセス1回分
    

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

#if 0
	(*fpInit)(0, &state);
	fprintf(stderr, "loads: after init\n");
	(*benchmark_loads)(2, &state);
	fprintf(stderr, "loads: after benchmark\n");
	mem_cleanup(0, &state);
	fprintf(stderr, "loads: after cleanup\n");
	settime(1);
	save_n(1);
#else
	/*
	 * Now walk them and time it.
	 */
    stride_initialize(&state);
    //result = measure_random_acesses(&state);

    for (int j = 0; j < NUM_SAMPLE; j++) {
        times[j] = measure_random_acesses(&state);    // 引数：バッファの先頭へのポインタ，バッファサイズ，周回数
    }
    result = find_median(times, NUM_SAMPLE);
    
    free(state.addr);
    free(state.pages);
#endif

	/* We want to get to nanoseconds / load. */
	fprintf(stderr, "%.5f,%.3f\n", size / (1024. * 1024.), result);
    fflush(stdout);
    fflush(stderr);

}
/*---------------------------------------------------*/

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

int main(int argc, char* argv[]) {
    size_t max_work_size;
    size_t size = 0;
    uint64_t stride;
    bool use_taskset = false;
    char* cpu_to_set;
    int cpuset_value = 0;
    int pid_check_cnt = 0;
    pid_t pid = getpid();
    FILE *cg_fp;
    //struct mem_state state;

    if (argc != 3 && argc != 4 && argc != 5 && argc != 6) {
        printf("arg[1]: Max buffer size\n");
        printf("  > Use 5GB if it is 0.\n");
        printf("arg[2]: Stride\n");
        printf("  > Use 2048KB if it is 0.\n");
        printf("arg[3 ~ 5]: Reference below.\n");
        printf("  > Use \"-o\" if you use OneShot mode.\n");
        printf("  > Use \"-t <CPU NUM>\" when using cgroup-taskset(cpuset).\n");
        return 1;
    }

    if (argc == 4) {
        if (strcmp(argv[3], "-o") == 0) {
            use_step = false;
        }
        else {
            printf("ERR: Unkown argument (%s).\n", argv[3]);
            return 1;
        }
    }
    if (argc == 5) {
        if (strcmp(argv[3], "-t") == 0) {
            use_taskset = true;
            cpu_to_set = argv[4];
        }
        else {
            printf("ERR: Unkown argument (%s).\n", argv[3]);
            return 1;
        }
    }
    if (argc == 6) {
        if (strcmp(argv[3], "-o") == 0) {       // $0 $1 $2 -o -t <CPU-NUM>
            if (strcmp(argv[4], "-t") != 0) {
                printf("ERR: Unkown argument (%s).\n", argv[4]);
                return 1;
            }
            use_step = false;
            use_taskset = true;
            cpu_to_set = argv[5];
        }
        else if (strcmp(argv[3], "-t") == 0) {     // $0 $1 $2 -t <CPU-NUM> -o
            if (strcmp(argv[5], "-o") != 0) {
                printf("ERR: Unkown argument (%s).\n", argv[5]);
                return 1;
            }
            use_step = false;
            use_taskset = true;
            cpu_to_set = argv[4];
        }
        else {
            printf("ERR: Unkown argument (%s).\n", argv[3]);
            return 1;
        }
    }

    // cgroup taskset (cpuset)
    if (use_taskset) {
        // Enable cpuset module at master
        cg_fp = fopen("/sys/fs/cgroup/cgroup.subtree_control", "w");
        if (!cg_fp) {
            printf("ERR: Could not open the croup_cpuset file. (%s)\n", "/sys/fs/cgroup/cgroup.subtree_control");
            return 1;
        }
        fprintf(cg_fp, "%s", "+cpuset");
        fclose(cg_fp);

        // Create parent group
        create_directory("/sys/fs/cgroup/Example");

        // Enable cpuset module at parent group
        cg_fp = fopen("/sys/fs/cgroup/Example/cgroup.subtree_control", "w");
        if (!cg_fp) {
            printf("ERR: Could not open the croup_cpuset file. (%s)\n", "/sys/fs/cgroup/cgroup.subtree_control");
            return 1;
        }
        fprintf(cg_fp, "%s", "+cpuset");
        fclose(cg_fp);

        // Create sub group
        create_directory("/sys/fs/cgroup/Example/sim_lat_mem_rd");

        // Set CPU num 1 to cgroup config
        cg_fp = fopen("/sys/fs/cgroup/Example/sim_lat_mem_rd/cpuset.cpus", "w");
        if (!cg_fp) {
            printf("ERR: Could not open the croup_cpuset file. (%s)\n", "/sys/fs/cgroup/Example/sim_lat_mem_rd/cpuset.cpus");
            return 1;
        }
        fprintf(cg_fp, "%s", cpu_to_set);
        fclose(cg_fp);

        // Write PID to the  cpuset file
        cg_fp = fopen("/sys/fs/cgroup/Example/sim_lat_mem_rd/cgroup.procs", "w");
        if (!cg_fp) {
            printf("ERR: Could not open the croup_cpuset file. (\"sim_lat_mem_rd\" group)\n");
            return 1;
        }
        fprintf(cg_fp, "%d\n", pid);
        fclose(cg_fp);

        // Read an integer from the file
        do {
            cg_fp = fopen("/sys/fs/cgroup/Example/sim_lat_mem_rd/cgroup.procs", "r");
            if (!cg_fp) {
                printf("ERR: Could not open the croup_cpuset file. (\"sim_lat_mem_rd\" group)\n");
                fclose(cg_fp);
                return 1;
            }

            int fscan_res = fscanf(cg_fp, "%d", &cpuset_value);
            (void)fscan_res;
            fclose(cg_fp);
            //printf("PID: %d, FILE: %d!!\n", pid, cpuset_value);
            usleep(100*1000);   // sleep 0.1s
            
            // 2s check
            if (pid_check_cnt == 20) {
                printf("ERR: Could not set PID to the cpuset setting. (\"sim_lat_mem_rd\" group)\n");
                return 1;
            }
            pid_check_cnt++;
        } while (cpuset_value != pid);
    }

    // ワークサイズ・strideを設定 (ステップ実行時には設定内容を出力)
    max_work_size = (size_t)parse_size(argv[1]);
    stride = parse_size(argv[2]);

    if (max_work_size == 0) {
        max_work_size = 5 * 1024 * 1024;    // デフォルト5MB
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
        stride = 2 * 1024;  // デフォルト2K
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

    if (use_step) {
        // ステップ実行時のみ表示
        printf("Buffer [MB], Latency [ns]\n");
    }

    fflush(stdout);
    fflush(stderr);

    int i = kBufferSizePow2Low;

    if (use_step) {     // ステップ
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
    else {              // 指定されたサイズ(max_work_size)のみ
        loads(max_work_size, max_work_size, stride);
    }

    if (use_step) {
        // ステップ実行時のみ表示
        printf("\ntest end (ws: %lu)\n", max_work_size);
    }
    return 0;
}
