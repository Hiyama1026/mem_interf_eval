#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/perf_event.h> /* Definition of PERF_* constants */
#include <sys/syscall.h> /* Definition of SYS_* constants */
#include <inttypes.h>
#include <sys/ioctl.h>

// 計測対象のコアとプロセス(PID)を指定
static int target_pid = 0;   // 計測対象とするタスクのPID (0の場合は全てのプロセスが計測対象)

// イベント管理情報
struct perf_event_attr pe0, pe1, pe2, pe3, pe4, pe5;
long fd0, fd1, fd2, fd3, fd4, fd5;

/* perf_event_open()
 *   syscall()を呼び出し，イベントを登録する
 */
static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags){
    long fd;
    fd = syscall(SYS_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    if (fd == -1) {
        fprintf(stderr, "Error creating event");
        exit(EXIT_FAILURE);
    }

    return fd;
}

/* create_six_event_group()
 *   イベントグループを生成する
 *   生成時はカウンタを無効とする
 */
void create_six_event_group(int target_cpu, int event0, int event1, int event2, int event3, int event4, int event5) {   // target_cpu: 計測対象とするCPU (-1の場合は全コア上のプロセスが計測対象)
    // event0
    memset(&pe0, 0, sizeof(struct perf_event_attr));
    pe0.type = PERF_TYPE_RAW;
    pe0.size = sizeof(struct perf_event_attr);
    pe0.config = event0;      // Event number
    pe0.disabled = 1;         // Counter disabled start
    pe0.exclude_kernel = 1;   // Do not measure instructions executed in the kernel
    pe0.exclude_hv = 1;       // Do not measure instructions executed in a hypervisor
    // Create the event
    fd0 = perf_event_open(&pe0, target_pid, target_cpu, -1, 0);   // Event leader
    //printf("create 0x%x\n", event0);

    // event1
    memset(&pe1, 0, sizeof(struct perf_event_attr));
    pe1.type = PERF_TYPE_RAW;
    pe1.size = sizeof(struct perf_event_attr);
    pe1.config = event1;
    pe1.disabled = 1;
    pe1.exclude_kernel = 1;
    pe1.exclude_hv = 1;
    // Create the event
    fd1 = perf_event_open(&pe1, target_pid, target_cpu, fd0, 0);    // pe0のグループに
    //printf("create 0x%x\n", event1);

    // event2
    memset(&pe2, 0, sizeof(struct perf_event_attr));
    pe2.type = PERF_TYPE_RAW;
    pe2.size = sizeof(struct perf_event_attr);
    pe2.config = event2;
    pe2.disabled = 1;
    pe2.exclude_kernel = 1;
    pe2.exclude_hv = 1;    
    // Create the event
    fd2 = perf_event_open(&pe2, target_pid, target_cpu, fd0, 0);    // pe0のグループに
    //printf("create 0x%x\n", event2);

    // event3
    memset(&pe3, 0, sizeof(struct perf_event_attr));
    pe3.type = PERF_TYPE_RAW;
    pe3.size = sizeof(struct perf_event_attr);
    pe3.config = event3;
    pe3.disabled = 1;
    pe3.exclude_kernel = 1;
    pe3.exclude_hv = 1;    
    // Create the event
    fd3 = perf_event_open(&pe3, target_pid, target_cpu, fd0, 0);    // pe0のグループに
    //printf("create 0x%x\n", event3);

    // event4
    memset(&pe4, 0, sizeof(struct perf_event_attr));
    pe4.type = PERF_TYPE_RAW;
    pe4.size = sizeof(struct perf_event_attr);
    pe4.config = event4;
    pe4.disabled = 1;
    pe4.exclude_kernel = 1;
    pe4.exclude_hv = 1;    
    // Create the event
    fd4 = perf_event_open(&pe4, target_pid, target_cpu, fd0, 0);    // pe0のグループに
    //printf("create 0x%x\n", event4);

    // event5
    memset(&pe5, 0, sizeof(struct perf_event_attr));
    pe5.type = PERF_TYPE_RAW;
    pe5.size = sizeof(struct perf_event_attr);
    pe5.config = event5;
    pe5.disabled = 1;
    pe5.exclude_kernel = 1;
    pe5.exclude_hv = 1;
    // Create the event
    fd5 = perf_event_open(&pe5, target_pid, target_cpu, fd0, 0);    // pe0のグループに
    //printf("create 0x%x\n", event5);
}

/* reset_and_start_counter()
 *  カウンタをリセットし，計測を開始する
 */
void reset_and_start_counter(void) {
    ioctl(fd0, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd1, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd2, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd3, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd4, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd5, PERF_EVENT_IOC_RESET, 0);

    ioctl(fd0, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(fd1, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(fd2, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(fd3, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(fd4, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(fd5, PERF_EVENT_IOC_ENABLE, 0);
}

/* reset_counter()
 *  カウンタをリセットする
 */
void reset_counter(void) {
    ioctl(fd0, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd1, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd2, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd3, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd4, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd5, PERF_EVENT_IOC_RESET, 0);
}

/* start_counter()
 *  リセットを行わずに計測を開始する
 */
void start_counter(void) {
    ioctl(fd0, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(fd1, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(fd2, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(fd3, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(fd4, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(fd5, PERF_EVENT_IOC_ENABLE, 0);
}

/* stop_counter()
 *  計測を停止する
 */
void stop_counter(void) {
    ioctl(fd0, PERF_EVENT_IOC_DISABLE, 0);
    ioctl(fd1, PERF_EVENT_IOC_DISABLE, 0);
    ioctl(fd2, PERF_EVENT_IOC_DISABLE, 0);
    ioctl(fd3, PERF_EVENT_IOC_DISABLE, 0);
    ioctl(fd4, PERF_EVENT_IOC_DISABLE, 0);
    ioctl(fd5, PERF_EVENT_IOC_DISABLE, 0);
}

/* export_counter()
 *   カウント値の読み取り，出力を行う
 */
void export_counter(char* cnt_name0, char* cnt_name1, char* cnt_name2, 
                              char* cnt_name3, char* cnt_name4, char* cnt_name5) {
    uint64_t val0 = 0;
    uint64_t val1 = 0;
    uint64_t val2 = 0;
    uint64_t val3 = 0;
    uint64_t val4 = 0;
    uint64_t val5 = 0;
    ssize_t ret[6];

    ret[0] = read(fd0, &val0, sizeof(val0));
    ret[1] = read(fd1, &val1, sizeof(val1));
    ret[2] = read(fd2, &val2, sizeof(val2));
    ret[3] = read(fd3, &val3, sizeof(val3));
    ret[4] = read(fd4, &val4, sizeof(val4));
    ret[5] = read(fd5, &val5, sizeof(val5));

    for (int t = 0; t < 6; t++) {
        if (ret[t] < 0) {
            printf("WARN: Failed to read fd%d.\n", t);
        }
    }

    printf("%s, %"PRIu64"\n", cnt_name0, val0);
    printf("%s, %"PRIu64"\n", cnt_name1, val1);
    printf("%s, %"PRIu64"\n", cnt_name2, val2);
    printf("%s, %"PRIu64"\n", cnt_name3, val3);
    printf("%s, %"PRIu64"\n", cnt_name4, val4);
    printf("%s, %"PRIu64"\n", cnt_name5, val5);
}

/* export_and_clean_counter()
 *   カウント値の読み取り，出力，ディスクリプタの削除を行う
 */
void export_and_clean_counter(char* cnt_name0, char* cnt_name1, char* cnt_name2, 
                              char* cnt_name3, char* cnt_name4, char* cnt_name5) {
    uint64_t val0 = 0;
    uint64_t val1 = 0;
    uint64_t val2 = 0;
    uint64_t val3 = 0;
    uint64_t val4 = 0;
    uint64_t val5 = 0;
    ssize_t ret[6];

    ret[0] = read(fd0, &val0, sizeof(val0));
    ret[1] = read(fd1, &val1, sizeof(val1));
    ret[2] = read(fd2, &val2, sizeof(val2));
    ret[3] = read(fd3, &val3, sizeof(val3));
    ret[4] = read(fd4, &val4, sizeof(val4));
    ret[5] = read(fd5, &val5, sizeof(val5));

    for (int t = 0; t < 6; t++) {
        if (ret[t] < 0) {
            printf("WARN: Failed to read fd%d.\n", t);
        }
    }

    printf("%s, %"PRIu64"\n", cnt_name0, val0);
    printf("%s, %"PRIu64"\n", cnt_name1, val1);
    printf("%s, %"PRIu64"\n", cnt_name2, val2);
    printf("%s, %"PRIu64"\n", cnt_name3, val3);
    printf("%s, %"PRIu64"\n", cnt_name4, val4);
    printf("%s, %"PRIu64"\n", cnt_name5, val5);

    // Clean up file descriptor
    close(fd0);
    close(fd1);
    close(fd2);
    close(fd3);
    close(fd4);
    close(fd5);
}