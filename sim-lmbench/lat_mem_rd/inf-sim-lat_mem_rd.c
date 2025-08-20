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
#include <errno.h>
#include <sys/stat.h>

#define MAX_MEM_PARALLELISM 16
#define NUM_SAMPLE 100

char *ERR_INVALID_ARGS = "arg[1]: Max buffer size\n  > Use 5GB if it is 0.\narg[2]: Stride\n  > Use 2048KB if it is 0.\narg[3 or more]: Reference below.\n  > Use \"-r <REPORT INTERVAL>\" when using report-function.\n  > Use \"-t <CPU NUM>\" when using cgroup-taskset(cpuset).\n  > Use \"-f <FILE NAME>\" when using file export.\n  > Use \"-e <NUM OF ELEMENTS>\" to set the number of logs can be saved.\n";

// アクセス回数
uint64_t num_iters = 2000000 * 100;
uint64_t report_cnt = 2000000;
bool use_report = false;

// wsをstrideで割り切れるときに立てるフラグ
bool just_size_flug = false;
// 計測時間保存用配列
//double times[NUM_SAMPLE];
double total_time = 0;

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
}

static volatile uint64_t	use_result_dummy;

void
use_pointer(void *result) { use_result_dummy += (long)result; }

void measure_stride_acesses(void *cookie) {
    struct mem_state* state = (struct mem_state*)cookie;
    register char **p = (char**)state->p[0];
    register uint64_t num_access;
	register size_t i, j;
	size_t list_len = (state->len / state->line) + 1;
    uint64_t start_t, end_t;
    uint64_t outer_cnt = 0;
    double res;
    
    
	if (just_size_flug)
        list_len--;     // WSがstrideで割り切れるとき，list_lenは1長くなるためデクリメント

    num_access = (uint64_t)(num_iters / 100);       // マクロ展開量で割る
    outer_cnt = (uint64_t)(num_access / report_cnt);

    while(1) {
        for (i = 0; i < outer_cnt; i++){
            start_t = get_time_ns();
            for (j = 0; j < report_cnt; j++) {
                HUNDRED;
            }
            end_t = get_time_ns();
            res = (double)(end_t - start_t);
            if (use_report) {
                fprintf(stderr, "%.3f\n", res / (report_cnt * 100));    // 1Byteアクセス時間 (ToDo確認)
            }
        }
        use_pointer((void *)p);
        state->p[0] = (char*)p;
    }
    
}

void
loads(size_t max_work_size, size_t size, size_t stride)
{
	double result;
	struct mem_state state;
    (void)result;       // 使用しなくても警告を出さない

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

    measure_stride_acesses(&state);
    
    //free(state.addr);
    //free(state.pages);
}
/*---------------------------------------------------*/

uint64_t parse_size(const char* str) {
    size_t len = strlen(str);
    
    if (len == 0) {
        fprintf(stderr, "Empty string\n\n");
        printf("%s", ERR_INVALID_ARGS);
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
        fprintf(stderr, "ERR: Invalid unit\n\n");
        printf("%s", ERR_INVALID_ARGS);
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
    uint64_t stride;
    //struct mem_state state;
    bool use_taskset = false;
    char* cpu_to_set;
    int cpuset_value = 0;
    int pid_check_cnt = 0;
    pid_t pid = getpid();
    char *endptr;
    FILE *cg_fp;

    if (argc < 3) {
        printf("%s", ERR_INVALID_ARGS);
        return 1;
    }

    max_work_size = (size_t)parse_size(argv[1]);
    stride = parse_size(argv[2]);

    if (max_work_size == 0) {
        max_work_size = 5 * 1024 * 1024;    // デフォルト5MB
        printf("max_ws (default), %lu\n", max_work_size);
    }
    else if (max_work_size < 5 * 1024) {
        printf("ERR: Maximum work size (%lu Byte) is too small (5K or more).\n", max_work_size);
        return 1;
    }
    else {
        // OK
    }

    if (stride == 0){
        stride = 2 * 1024;  // デフォルト2K
        printf("stride (default), %lu\n\n", stride);
    }
    else if (stride < sizeof(char *)) {
        printf("ERR: Stride (%lu Byte) is too small (%lu Byte or more).\n", stride, sizeof(char *));
        return 1;
    }
    else {
        // OK
    }

    argc -= 3;
    if ((argc % 2) != 0) {
        printf("ERR: Argument num error.\n\n");
        printf("%s", ERR_INVALID_ARGS);
        return 1;
    }
    for (int awc = 0; awc < (argc/2); awc++) {
        
        switch (argv[(awc*2)+3][1]) {
            case 'r':
                use_report = true;
                report_cnt = strtoull(argv[((awc*2)+3)+1], &endptr, 10);
                break;
            case 't':
                use_taskset = true;
                cpu_to_set = argv[((awc*2)+3)+1];
                break;
            default:
                printf("ERR: Unknown argument. (%s)\n\n", argv[(awc*2)+3]);
                printf("%s", ERR_INVALID_ARGS);
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
        create_directory("/sys/fs/cgroup/Example/inf-sim_lat_mem_rd");

        // Set CPU num 1 to cgroup config
        cg_fp = fopen("/sys/fs/cgroup/Example/inf-sim_lat_mem_rd/cpuset.cpus", "w");
        if (!cg_fp) {
            printf("ERR: Could not open the croup_cpuset file. (%s)\n", "/sys/fs/cgroup/Example/inf-sim_lat_mem_rd/cpuset.cpus");
            return 1;
        }
        fprintf(cg_fp, "%s", cpu_to_set);
        fclose(cg_fp);

        // Write PID to the  cpuset file
        cg_fp = fopen("/sys/fs/cgroup/Example/inf-sim_lat_mem_rd/cgroup.procs", "w");
        if (!cg_fp) {
            printf("ERR: Could not open the croup_cpuset file. (\"inf-sim_lat_mem_rd\" group)\n");
            return 1;
        }
        fprintf(cg_fp, "%d\n", pid);
        fclose(cg_fp);

        // Read an integer from the file
        do {
            cg_fp = fopen("/sys/fs/cgroup/Example/inf-sim_lat_mem_rd/cgroup.procs", "r");
            if (!cg_fp) {
                printf("ERR: Could not open the croup_cpuset file. (\"inf-sim_lat_mem_rd\" group)\n");
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
                printf("ERR: Could not set PID to the cpuset setting. (\"inf-sim_lat_mem_rd\" group)\n");
                return 1;
            }
            pid_check_cnt++;
        } while (cpuset_value != pid);
    }
    

    fflush(stdout);
    fflush(stderr);

    // アクセス実行
    loads(max_work_size, max_work_size, stride);
    
    return 0;
}
