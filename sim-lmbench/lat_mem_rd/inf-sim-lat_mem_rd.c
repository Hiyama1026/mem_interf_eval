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
#include <signal.h>

#define MAX_MEM_PARALLELISM 16
#define NUM_SAMPLE 100

// アクセス回数
uint64_t num_iters = 200000 * 1000;

char *ERR_INVALID_ARGS = "arg[1]: Max buffer size\n  > Use 5GB if it is 0.\narg[2]: Stride\n  > Use 2048KB if it is 0.\narg[3 or more]: Reference below.\n  > Use \"-r <REPORT INTERVAL>\" when using report-function.\n  > Use \"-t <CPU NUM>\" when using cgroup-taskset(cpuset).\n  > Use \"-f <FILE NAME>\" when using file export.\n  > Use \"-e <NUM OF ELEMENTS>\" to set the number of logs can be saved.\n";

bool use_taskset = false;
bool use_report = false;
bool use_file_export = false;
bool use_buffsize_set = false;
bool buf_full_flag = false;

double  *buf_log;
uint32_t buf_log_size = 1024*1024*3;
FILE *log_fp;
char log_file_path[256];
uint32_t result_idx = 0;

void signal_handler(int signum);

#define	ONE	p = (char **)*p;
#define	FIVE	ONE ONE ONE ONE ONE
#define	TEN	FIVE FIVE
#define	FIFTY	TEN TEN TEN TEN TEN
#define	HUNDRED	FIFTY FIFTY
#define F_HUNDRED HUNDRED HUNDRED HUNDRED HUNDRED HUNDRED
#define THOUSAND F_HUNDRED F_HUNDRED

// バッファサイズとstride幅
size_t work_size;
uint64_t stride;

// コメント解除で，同名のログファイルが既存の場合に，削除して良いかをユーザに確認してから削除するようになる．
//#define CHECK_LOGFILE_DEL

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
	state->p[0] = addr;
}

static volatile uint64_t	use_result_dummy;

void
use_pointer(void *result) { use_result_dummy += (long)result; }

void measure_stride_acesses(void *cookie) {
    struct mem_state* state = (struct mem_state*)cookie;
    register char **p = (char**)state->p[0];
    register uint64_t num_access;
	register size_t i;
    uint64_t start_t, end_t;
    double res = 0;

    num_access = num_iters / 100;       // マクロ展開量で割る
    
    start_t = get_time_ns();
    for (i = 0; i < num_access; ++i) {
        HUNDRED;
    }
    end_t = get_time_ns();
    
    use_pointer((void *)p);
    state->p[0] = (char*)p;

    if (use_report || use_file_export) {
        res = (double)(end_t - start_t) / (double)(num_access * 100);      // メモリアクセス1回分
    }
    if (use_report && !use_file_export) {
        fprintf(stderr, "%.5f\n", res);
        fflush(stderr);
    }
    if (use_file_export) {
        buf_log[result_idx] = res;
        result_idx++;
        if (result_idx >= buf_log_size) {
            buf_full_flag = true;
            signal_handler(15);
        }
    }
}

void
loads(size_t max_work_size, size_t size, size_t stride)
{
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

    while(1) {
        measure_stride_acesses(&state);
    }
    //free(state.addr);
    //free(state.pages);
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
        fprintf(log_fp, "ReadLatency[ns]\n");
        while(report_idx < result_idx){
            fprintf(log_fp, "%.5f\n", buf_log[report_idx]);
            report_idx++;
        }
        fclose(log_fp);
        exit(0);
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    char *endptr;
	FILE *cg_fp;
    char *file_name;
	char* cpu_to_set = NULL;
	int cpuset_value = 0;
    int pid_check_cnt = 0;
    pid_t pid = getpid();

    if (argc < 3) {
		printf("%s", ERR_INVALID_ARGS);
        return 1;
    }
    
    work_size = (size_t)parse_size(argv[1]);
    stride = parse_size(argv[2]);

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
                num_iters = strtoull(argv[((awc*2)+3)+1], &endptr, 10);
                break;
            case 't':
                use_taskset = true;
                cpu_to_set = argv[((awc*2)+3)+1];
                break;
            case 'f':
                use_file_export = true;
                file_name = argv[((awc*2)+3)+1];
                sprintf(log_file_path, "%s%s", "./inf-results/", file_name);
                break;
            case 'e':
                    buf_log_size = strtoull(argv[((awc*2)+3)+1], &endptr, 10);
                    use_buffsize_set = true;
                break;
            default:
                printf("ERR: Unknown argument. (%s)\n\n", argv[(awc*2)+3]);
                printf("%s", ERR_INVALID_ARGS);
                return 1;
        }
    }

    if (!use_report && use_file_export) {
        printf("ERR: You must specify the report interval using -r option when using -f option.\n\n");
        printf("%s", ERR_INVALID_ARGS);
        exit(1);
    }

    if (!use_file_export && use_buffsize_set) {
        printf("ERR: Use -f with -f.\n\n");
        printf("%s", ERR_INVALID_ARGS);
        exit(1);
    }

    if (work_size == 0) {
        work_size = 5 * 1024 * 1024;    // デフォルト5MB
        printf("max_ws (default), %lu\n", work_size);
    }
    else if (work_size < 5 * 1024) {
        printf("err: Maximum work size (%lu Byte) is too small (5K or more).\n", work_size);
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
        printf("err: Stride (%lu Byte) is too small (%lu Byte or more).\n", stride, sizeof(char *));
        return 1;
    }
    else {
        // OK
    }

    if (use_file_export) {
        buf_log = (double *)valloc(sizeof(double) * buf_log_size);
        if (!buf_log) {
            perror("malloc");
            exit(1);
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
        create_directory("/sys/fs/cgroup/Example/inf-sim-lat_mem_rd");

        // Set CPU num 1 to cgroup config
        cg_fp = fopen("/sys/fs/cgroup/Example/inf-sim-lat_mem_rd/cpuset.cpus", "w");
        if (!cg_fp) {
            printf("ERR: Could not open the croup_cpuset file. (%s)\n", "/sys/fs/cgroup/Example/inf-sim-lat_mem_rd/cpuset.cpus");
            return 1;
        }
        fprintf(cg_fp, "%s", cpu_to_set);
        fclose(cg_fp);

        // Write PID to the  cpuset file
        cg_fp = fopen("/sys/fs/cgroup/Example/inf-sim-lat_mem_rd/cgroup.procs", "w");
        if (!cg_fp) {
            printf("ERR: Could not open the croup_cpuset file. (\"inf-sim-lat_mem_rd\" group)\n");
            return 1;
        }
        fprintf(cg_fp, "%d\n", pid);
        fclose(cg_fp);

        // Read an integer from the file
        do {
            cg_fp = fopen("/sys/fs/cgroup/Example/inf-sim-lat_mem_rd/cgroup.procs", "r");
            if (!cg_fp) {
                printf("ERR: Could not open the croup_cpuset file. (\"inf-sim-lat_mem_rd\" group)\n");
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
                printf("ERR: Could not set PID to the cpuset setting. (\"inf-sim-lat_mem_rd\" group)\n");
                return 1;
            }
            pid_check_cnt++;
        } while (cpuset_value != pid);
    }

    // File export
    if (use_file_export) {
        create_directory("./inf-results/");
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
            bool is_check_fdel = false;
            char is_remove[16];
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

    printf("max_ws:%lu,stride:%lu\n", work_size, stride);

    fflush(stdout);
    fflush(stderr);

    // アクセス実行
    loads(work_size, work_size, stride);

    return 0;
}
