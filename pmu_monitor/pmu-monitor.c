#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include<string.h>
#include<stdbool.h>
#include <stdlib.h>
#include <inttypes.h>
#include <linux/perf_event.h> /* Definition of PERF_* constants */
#include <sys/syscall.h> /* Definition of SYS_* constants */
#include <sys/ioctl.h>
#include <sys/stat.h>
# include <sys/types.h>
#include <pthread.h>

#define MAX_EVENT_NUM 6
#define MAX_CPU_NUM 4       // Freely modifiable, but operation with multiple CPUs is not guaranteed.

#define EXP_PER_CORE

// 計測対象のコアとプロセス(PID)を指定
static int target_pid = -1;   // 計測対象とするタスクのPID (0の場合は全てのプロセスが計測対象)

// イベント管理情報
struct perf_event_attr pe[MAX_CPU_NUM][MAX_EVENT_NUM];
long fds[MAX_CPU_NUM][MAX_EVENT_NUM];

// PMUカウンタの計測対象CPU
int target_cpu = 3;

bool buf_full_flag = false;
bool start_flag = false;
bool exp_all_flag = false;
bool overall_flag = false;
bool use_file_export = false;
bool use_threadset = false;

uint64_t* buf_logs[MAX_EVENT_NUM];
uint32_t buf_log_size = 1024*1024*3;
char log_file_path[256];
FILE *log_fp;
uint32_t result_idx = 0;

int target_cpu_num = 0;
int* int_target_cpus = NULL;
char* cnt_names[MAX_EVENT_NUM];
int counter_num = 0;

void signal_handler(int signum);
void timer_handler(int signum);

void error_print(void) {
    printf("arg[1]: PMU events ID\n");
    printf("  > Enter in hexadecimal.\n");
    printf("  > Separate items with commas.\n");
    printf("arg[2]: PMU events mnemonic\n");
    printf("  > Separate items with commas.\n");
    printf("  > For outputting results only (therefore freely configurable).\n");
    printf("arg[3]: Target CPU(s) ID\n");
    printf("  > Separate items with commas.\n");
    printf("  > The maximum number of CPUs is defined by \"MAX_CPU_NUM\" macro.\n");
    printf("  > Using multiple CPUs is not recommended.\n");
    printf("arg[4 or more]: Reference below.\n");
    printf("  > Use \"-i <EXPORT INTERVAL>\" to adjust the export interval. (If negative, only the total shows.)\n");
    printf("  > Use \"-p <PID>\" to specify the target PID. (By default, all processes are included with PID=-1)\n");
    printf("  > Use \"-f <FILE NAME>\" when using file export.\n");
    printf("  > Use \"-e\" to output the score for each CPU.\n");
    printf("  > Use \"-t <CPU NUM>\" when pinning threads to a specific CPU.\n");
}



static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags){
    long fd;

    fd = syscall(SYS_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    if (fd == -1) {
        fprintf(stderr, "Error creating event.\n");
        fprintf(stderr, "You may need sudo.\n");
        exit(EXIT_FAILURE);
    }

    return fd;
}

void create_events_per_core(int* event_ids, int* cpu_ids) {   // target_cpu: 計測対象とするCPU (-1の場合は全コア上のプロセスが計測対象)
    long group_id;

    for (int i = 0; i < target_cpu_num; i++) {
        group_id = -1;
        for (int j = 0; j < counter_num; j++) {
            memset(&pe[i], 0, sizeof(struct perf_event_attr));
            pe[i][j].type = PERF_TYPE_RAW;
            pe[i][j].size = sizeof(struct perf_event_attr);
            pe[i][j].config = event_ids[j];      // Event number
            pe[i][j].disabled = 1;         // Counter disabled start
            pe[i][j].exclude_kernel = 1;   // Do not measure instructions executed in the kernel
            pe[i][j].exclude_hv = 1;       // Do not measure instructions executed in a hypervisor
            // Create the event
            fds[i][j] = perf_event_open(&pe[i][j], target_pid, cpu_ids[i], group_id, 0);   // Event leader

            if (group_id == -1) {
                group_id = fds[i][j];
            }
        }
    }
}

void reset_and_start_counter(void) {
    for (int i = 0; i < target_cpu_num; i++) {
        for (int j = 0; j < counter_num; j++) {
            ioctl(fds[i][j], PERF_EVENT_IOC_RESET, 0);
        }
    }

    for (int i = 0; i < target_cpu_num; i++) {
        for (int j = 0; j < counter_num; j++) {
            ioctl(fds[i][j], PERF_EVENT_IOC_ENABLE, 0);
        }
    }
}

void stop_counter(void) {
    for (int i = 0; i < target_cpu_num; i++) {
        for (int j = 0; j < counter_num; j++) {
            ioctl(fds[i][j], PERF_EVENT_IOC_DISABLE, 0);
        }
    }
}

void export_counter(void) {
    uint64_t val[MAX_CPU_NUM][MAX_EVENT_NUM];
    ssize_t ret[MAX_CPU_NUM][MAX_EVENT_NUM];
    uint64_t total_val[MAX_EVENT_NUM];

    for (int i = 0; i < MAX_EVENT_NUM; i++) {
        total_val[i] = 0;
    }

    for (int i = 0; i < target_cpu_num; i++) {
        for (int j = 0; j < counter_num; j++) {
            ret[i][j] = read(fds[i][j], &val[i][j], sizeof(val[i][j]));
        }
    }

    for (int i = 0; i < target_cpu_num; i++) {
        for (int j = 0; j < counter_num; j++) {
            if (ret[i][j] < 0) {
                printf("WARN: Failed to read fd (%d-%d).\n", i, j);
            }
        }
    }

    if (!exp_all_flag || use_file_export) {
        for (int i = 0; i < target_cpu_num; i++) {
            for (int j = 0; j < counter_num; j++) {
                total_val[j] += val[i][j];
            }
        }
    }

    if (use_file_export) {
        for (int j = 0; j < counter_num; j++) {
            buf_logs[j][result_idx] = total_val[j];
        }
        result_idx++;
        if (result_idx >= buf_log_size) {
            buf_full_flag = true;
            signal_handler(15);
        }
    }
    else {
        if (exp_all_flag) {
            for (int j = 0; j < counter_num; j++) {
                for (int i = 0; i < target_cpu_num; i++) {
                    printf("%s(CPU%d),%"PRIu64"", cnt_names[j], int_target_cpus[i], val[i][j]);
                    if (i != target_cpu_num - 1) {
                        printf("\t\t");
                    }
                }
                printf("\n");
            }
            printf("\n");
        }
        else {
            for (int j = 0; j < counter_num; j++) {
                printf("%s,%"PRIu64"\n", cnt_names[j], total_val[j]);
            }
            printf("\n");
        }
    }
}

/*
 * タイマハンドラ
 */
void timer_handler(int signum)
{
    if (start_flag) {
        stop_counter();
        export_counter();
        //printf("-----\n");
    }
    else {
        start_flag = true;
    }
    reset_and_start_counter();
}

/*
 * シグナルハンドラ
 */
void signal_handler(int signum) {
    int report_idx = 0;

    //printf("signal_handler (%d)\n", signum);

    if (overall_flag) {
        stop_counter();
        printf("\n");
        export_counter();
    }
    else {
        printf("\n");
    }

    if (use_file_export) {
        log_fp = fopen(log_file_path, "w");
        if (!log_fp) {
            printf("ERR: Could not open the log file. (%s)\n", log_file_path);
            exit(EXIT_FAILURE);
        }
        else {
            printf("Logfile created.\n");
        }
        if (buf_full_flag) {
            fprintf(log_fp, "WARN: BUFFER FULL.\n\n");
        }

        if (overall_flag) {
            fprintf(log_fp, "OverallResultsOnly\n\n");
        }

        for (int i = 0; i < counter_num; i++) {
            fprintf(log_fp, "%s", cnt_names[i]);
            if (i != counter_num - 1) {
                fprintf(log_fp, ",");
            }
        }
        fprintf(log_fp, "\n");

        while(report_idx < result_idx){
            for (int i = 0; i < counter_num; i++) {
                fprintf(log_fp, "%lu", buf_logs[i][report_idx]);
                if (i != counter_num - 1) {
                    fprintf(log_fp, ",");
                }
            }
            fprintf(log_fp, "\n");
            report_idx++;
        }
        fclose(log_fp);
    }
    exit(0);
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

char *dirname_dup(const char *path) {
    if (!path || *path == '\0') {
        return strdup(".");
    }

    char *buf = strdup(path);
    if (!buf) return NULL;

    size_t len = strlen(buf);

    /* 末尾のスラッシュを取り除く（ただしルート "/" は残す） */
    while (len > 1 && buf[len - 1] == '/') {
        buf[len - 1] = '\0';
        --len;
    }

    /* 最後の '/' を探す */
    char *p = strrchr(buf, '/');
    if (!p) {
        /* スラッシュがない -> カレントディレクトリ */
        free(buf);
        return strdup(".");
    }

    if (p == buf) {
        /* 例: "/file" または "/" -> ディレクトリは "/" */
        free(buf);
        return strdup("/");
    }

    /* p の位置で区切る */
    *p = '\0';
    char *res = strdup(buf);
    free(buf);
    return res;
}

int compare_desc(const void *a, const void *b) {
    int va = *(const int*)a;
    int vb = *(const int*)b;
    return va - vb;
}

int ch_extract_comma(char* target_str, int element_max, char** buf){
    char* token = strtok(target_str, ",");
    int extracted_cnt = 0;

    extracted_cnt = 0;
    while (token != NULL) {
        if (extracted_cnt >= element_max) {
            printf("ERR: To many extracted elements\n");
            exit(EXIT_FAILURE);
        }
        buf[extracted_cnt] = token;
        token = strtok(NULL, ",");
        extracted_cnt++;
    }

    return extracted_cnt;
}

int int_extract_comma(char* target_str, int element_max, int* buf){
    char* token = strtok(target_str, ",");
    int extracted_cnt = 0;

    extracted_cnt = 0;
    while (token != NULL) {
        if (extracted_cnt >= element_max) {
            printf("ERR: To many extracted elements\n");
            exit(EXIT_FAILURE);
        }
        buf[extracted_cnt] = atoi(token);
        token = strtok(NULL, ",");
        extracted_cnt++;
    }
    
    qsort(buf, extracted_cnt, sizeof(int), compare_desc);

    return extracted_cnt;
}

int main(int argc, char** argv)
{
    struct sigaction act, oldact;
    timer_t tid;
    struct itimerspec itval;
    char *endptr;
    char *file_name;
    double interval = 1.0;
    int interval_sec = 1;
    uint64_t interval_nsec = 0;
    int ns = 1000*1000*1000;
    char *dir_path = NULL;
    uint64_t* buf_log;
    int mandatory_argc = 4;
    int cpu_to_self_set = 0;
    cpu_set_t cpu_set;
    int ts_result = 0;
    pid_t my_pid = syscall(SYS_gettid);

    char* ch_event_ids = NULL;
    char* ch_hex_event_ids[MAX_EVENT_NUM];
    int int_event_ids[MAX_EVENT_NUM];
    char* arg_event_name = NULL;
    int event_names_num = 0;
    
    char* ch_target_cpus = NULL;

    int_target_cpus = (int*)malloc(sizeof(int) * MAX_CPU_NUM);

    if (argc < mandatory_argc) {
        error_print();
        exit(EXIT_FAILURE);
    }

    ch_event_ids = argv[1];
    arg_event_name = argv[2];
    ch_target_cpus = argv[3];

    if (argc >= mandatory_argc) {
        for (int awc = mandatory_argc; awc < argc; awc+=2) {
            if (argv[awc][2] != '\0') {
                error_print();
                exit(EXIT_FAILURE);
            }
            switch (argv[awc][1]) {
                case 'p':
                    target_pid = strtoull(argv[awc+1], &endptr, 10);
                    printf("%d\n", target_pid);
                    break;
                case 'f':
                    use_file_export = true;
                    file_name = argv[awc+1];
                    sprintf(log_file_path, "%s/%s", "./cyc_pmu", file_name);
                    break;
                case 'i':
                    interval = atof(argv[awc+1]);
                    if (interval < 0) {
                        overall_flag = true;
                    }
                    break;
                case 'e':
                    exp_all_flag = true;
                    awc-=1;
                    break;
                case 't':
                    use_threadset = true;
                    cpu_to_self_set = atoi(argv[awc+1]);
                    break;
                default:
                    error_print();
                    exit(EXIT_FAILURE);
            }
        }
    }

    if (use_threadset) {
        CPU_ZERO(&cpu_set);
        CPU_SET(cpu_to_self_set, &cpu_set);
        ts_result = sched_setaffinity(my_pid, sizeof(cpu_set_t), &cpu_set);
        if (ts_result != 0) {
            printf("WARN: Failed to set cpu affinity.\n");
        }
    }

    counter_num = ch_extract_comma(ch_event_ids, MAX_EVENT_NUM, ch_hex_event_ids);
    event_names_num = ch_extract_comma(arg_event_name, MAX_EVENT_NUM, cnt_names);
    if (counter_num != event_names_num) {
        printf("ERR: The number of event names does not match the number of event IDs.\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < counter_num; i++) {
        int_event_ids[i] = (int)strtoul(ch_hex_event_ids[i], &endptr, 16);
    }

    target_cpu_num = int_extract_comma(ch_target_cpus, MAX_CPU_NUM, int_target_cpus);

    if (target_cpu == 1) {
        exp_all_flag = false;
    }

    if (target_cpu_num != 1 && use_file_export) {
        printf("ERR: File output is available only when measuring with single CPU.\n");
        exit(EXIT_FAILURE);
    }

    memset(&act, 0, sizeof(struct sigaction));
    memset(&oldact, 0, sizeof(struct sigaction));
 
    // シグナルハンドラの登録
    act.sa_handler = timer_handler;
    act.sa_flags = SA_RESTART;
    if(sigaction(SIGALRM, &act, &oldact) < 0) {
        perror("sigaction()");
        return -1;
    }
 
    // タイマ割り込みを発生させる
    if (!overall_flag) {
        interval_sec = (int)interval / 1;
        interval = interval - (double)interval_sec;
        interval_nsec = (uint64_t)(interval * ns);
        itval.it_value.tv_sec = interval_sec;     // 初回起動までの時間
        itval.it_value.tv_nsec = interval_nsec;
        itval.it_interval.tv_sec = interval_sec;  // 2回目以降の間隔
        itval.it_interval.tv_nsec = interval_nsec;

        // タイマの作成
        if(timer_create(CLOCK_REALTIME, NULL, &tid) < 0) {
            perror("timer_create");
            return -1;
        }
    }

    // PMUカウンタグループの生成
    create_events_per_core(int_event_ids, int_target_cpus);

    // file export
    if (use_file_export) {
        for (int i = 0; i < counter_num; i++) {
            buf_log = (uint64_t *)malloc(sizeof(uint64_t) * buf_log_size);
            if (!buf_log) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            buf_logs[i] = buf_log;
        }
        
        dir_path = dirname_dup(log_file_path);
        create_directory(dir_path);
        // Check if a file with the same name exists. If not, create it.
        log_fp = fopen(log_file_path, "r");
        if (!log_fp) {
            log_fp = fopen(log_file_path, "w");
            if (!log_fp) {
                printf("Failed to create %s.\n", log_file_path);
                exit(EXIT_FAILURE);
            }
            else {
                printf("Logfile created.\n\n");
            }
        }
        else {
            fclose(log_fp);
            // Delete the existing log file
            if (remove(log_file_path) != 0) {
                printf("ERR: Failed to remove %s.\n", log_file_path);
                exit(EXIT_FAILURE);
            }
            else {
                printf("Logfile deleted.\n");
            }
        }
    }

    // Register signal handler (Ctrl+C, killall)
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (overall_flag){
        printf("Overall results only.\n");
        fflush(stdout);
        fflush(stderr);
        reset_and_start_counter();
        while(1) {
            pause();
        }
    } else {
        //  タイマのセット
        fflush(stdout);
        fflush(stderr);
        if(timer_settime(tid, 0, &itval, NULL) < 0) {
            perror("timer_settime");
            return -1;
        }
    
        // シグナル(タイマ割り込み)が発生するまでサスペンドする
        while(1) {
            pause();
        }

        // タイマの解除
        timer_delete(tid);

        printf("stop and sleep\n");
        
        // シグナルハンドラの解除
        sigaction(SIGALRM, &oldact, NULL);
    }

    return 0;
}
