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

#define TYPE    int
#define CHUNK_SIZE  32

double	wr(uint64_t iterations, void *cookie);
void	init_overhead(uint64_t iterations, void *cookie);
void	init_loop(uint64_t iterations, void *cookie);
void	cleanup(uint64_t iterations, void *cookie);

bool ex_latency = false;

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
	TYPE	*lastchunk;
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

int
main(int ac, char **av)
{
    uint64_t iterations = 50;
    uint64_t access_cnt = 0;
	double tim_mid;
	//int	parallel = 1;
	//int	warmup = 0;
	//int	repetitions = -1;
	size_t	nbytes;
	state_t	state;
	//int	c;

	if (ac != 3 && ac != 4) {
        printf("arg[1]: Max buffer size\n");
		printf("  > 1k(small k) -> 1024.\n");
        printf("arg[2]: Stride\n");
        printf("  > 1k(small k) -> 1024.\n");
		printf("arg[3]: -l\n");
        printf("  > Export latency(optional).\n");
        return 1;
    }

	if (ac == 4) {
		if (strcmp(av[3], "-l") == 0) {
			ex_latency = true;
		}
		else {
			printf("ERR: Unkown argument (%s).\n", av[3]);
			return 1;
		}
	}
	//printf("int: %lu\n", sizeof(int));

	state.overhead = 0;

	state.aligned = state.need_buf2 = 0;

	nbytes = state.nbytes = bytes(av[optind]);
	state.stride_b = bytes(av[optind+1]);

	if ((state.stride_b*32) > nbytes) {
		printf("ERR: Stride is too large for the work size. (maximum: %luByte)\n", nbytes / 32);
		return 1;
	}

	if ((state.stride_b / sizeof(TYPE)) < 1) {
		printf("ERR: Stride is too small. (minimum: %luByte)\n", sizeof(TYPE));
		return 1;
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
    state->lastchunk = (TYPE*)(buff_end - (state->stride_b * CHUNK_SIZE) + 1);
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
	register TYPE *lastchunk = state->lastchunk;
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
    
    #define	ONE(i)	p[i] = 0x7FFFFFFF;
    start_t = get_time_ns();
    while (outer_cnt-- > 0) {
        p = state->buf;
	    while (p <= lastchunk) {
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
