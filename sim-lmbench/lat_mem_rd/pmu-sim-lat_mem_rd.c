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

#define MAX_MEM_PARALLELISM 16

// PMUカウンタ
extern struct perf_event_attr pe0, pe1, pe2, pe3, pe4, pe5;
extern long fd0, fd1, fd2, fd3, fd4, fd5;
void reset_and_start_counter();
void stop_counter();
void create_six_event_group();
void export_and_clean_counter();
// PMUカウンタの計測対象CPU
static int target_cpu = 3;

// アクセス回数(ToDo：LMbenchでだいたいどれくらいの値が入るか調べて揃える)
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

uint64_t get_time_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return 1000000000ULL * t.tv_sec + t.tv_nsec;
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

double measure_stride_acesses(void *cookie) {
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
    (void)result;

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

    reset_and_start_counter();
    result = measure_stride_acesses(&state);    // 引数：バッファの先頭へのポインタ，バッファサイズ，周回数
    stop_counter();
    
    free(state.addr);
    free(state.pages);

	/* We want to get to nanoseconds / load. */
	//fprintf(stderr, "%.5f,%.3f\n", size / (1024. * 1024.), result);
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

int main(int argc, char* argv[]) {
    size_t max_work_size;
    uint64_t stride;

    if (argc != 3) {
        printf("arg[1]: Max buffer size\n");
        printf("  > Use 5GB if it is 0.\n");
        printf("arg[2]: Stride\n");
        printf("  > Use 2048KB if it is 0.\n");
        return 1;
    }

    // ワークサイズ・strideを設定 (ステップ実行時には設定内容を出力)
    max_work_size = (size_t)parse_size(argv[1]);
    stride = parse_size(argv[2]);

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

    fflush(stdout);
    fflush(stderr);

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
  create_six_event_group(target_cpu, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25);     // 0x1EのCHAINはperf_event_open()で「Error creating event」となるため飛ばす

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
    return 0;
}
