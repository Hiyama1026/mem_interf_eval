#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define DEVICE_FILE "/dev/way-part"
#define IOCTL_WRITE_CLUSTERPARTCR _IO('k', 1)
#define IOCTL_WRITE_CLUSTERSTASHSID _IO('k', 2)
#define IOCTL_WRITE_CLUSTERTHREADSIDOVR _IO('k', 3)
#define IOCTL_WRITE_CLUSTERACPSID _IO('k', 4)
#define IOCTL_WRITE_CLUSTERTHREADSID _IO('k', 5)
#define IOCTL_READ_CLUSTERPARTCR _IO('k', 6)
#define IOCTL_READ_CLUSTERTHREADSID _IO('k', 10)
#define IOCTL_READ_CPUECTLR _IO('k', 11)

typedef struct ioctl_data {
    uint32_t in_value;
    uint32_t* out_value_low;
    uint32_t* out_value_high;
}ioctl_data;

// CPU数
#define NUM_CPU 4

// 各CPUに割り付けるクラスタID
int CPU0_ID = 0;
int CPU1_ID = 0;
int CPU2_ID = 0;
int CPU3_ID = 0;

// IDの最大値
#define ID_NUM  7
// WAYの数
#define WAY_NUM 4


void pin_cpu(long cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset); // Initialize CPU set to all zeros

    // Add CPU cores to the CPU set
    CPU_SET(cpu, &cpuset); // Add CPU core 0
    pid_t pid = getpid(); // Get PID of the current process
    int result = sched_setaffinity(pid, sizeof(cpuset), &cpuset);
    if (result != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
    return;
}


int main(int argc, char* argv[]) {
    uint64_t id_way[WAY_NUM];
    uint32_t cpu_id[NUM_CPU];
    uint64_t temp_id;
    uint64_t set_val = 0;
    char do_next;
    uint32_t read_value_low = 0; // lower 32 bits
    uint32_t read_value_high = 0;
    ioctl_data data;
    uint32_t check_clusterID[NUM_CPU];
    int fd, ret;
    char *endptr;

    // Open the device file
    fd = open(DEVICE_FILE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device file");
        return 1;
    }

    /*
     * 引数チェック・リセット処理
     */
    if (argc == 2) {
        if (strcmp(argv[1], "-c") == 0) {           // wayの設定をリセット(パーティショニング無効化)
            data.in_value = 0;
            data.out_value_low = &read_value_low;
            data.out_value_high = &read_value_high;
            // CLUSTERPARTCRレジスタに0をセット
            ret = ioctl(fd, IOCTL_WRITE_CLUSTERPARTCR, &data);
            
            //設定を確認
            ioctl(fd, IOCTL_READ_CLUSTERPARTCR, &data);
            if (!(read_value_low == 0 && read_value_high == 0)) {
                printf("Failed to reset way-set.\n");
                printf("CLUSTERPARTCR: 0x%ux (read_value_low)\n", read_value_low);
                exit(1);
            }
            printf("Reset way-set succeed.\n");
            exit(0);
        }
        else {
            printf("[Usage]\n$ sudo ./exe-file -c\t# Reset way-set\n$ sudo ./exe-file CPU-ID CPU-ID ... CPU-ID\t# Set CPU-ID\n");
            exit(1);
        }
    }
    else if (argc == NUM_CPU+1) {

        for (int i = 0; i < NUM_CPU; i++) {
            cpu_id[i] = (uint32_t)strtol(argv[i+1], &endptr, 10);
            if (*endptr != '\0') {
                printf("[Usage]\n$ sudo ./exe-file -c\t# Reset way-set\n$ sudo ./exe-file CPU-ID CPU-ID ... CPU-ID\t# Set CPU-ID\n");
                exit(1);
            }
        }
    }
    else {
        printf("The number of arguments is 1 or <Num of CPUs>+1.\n");
        printf("[Usage]\n$ sudo ./exe-file -c\t# Reset way-set\n$ sudo ./exe-file CPU-ID CPU-ID ... CPU-ID\t# Set CPU-ID\n");
        exit(1);
    }
    
    // cluster-IDを設定
    for (int i = 0; i < NUM_CPU; i++) {
        pin_cpu(i);
        data.in_value = cpu_id[i];
        data.out_value_low = &read_value_low;
        data.out_value_high = &read_value_high;

        // レジスタにセット
        ret = ioctl(fd, IOCTL_WRITE_CLUSTERTHREADSID, &data);
        if (ret < 0) {
            perror("IOCTL_INC_COUNT ioctl failed");
            close(fd);
            return -1;
        }

        // CLUSTERTHREADSID読み取り
        data.in_value = 0;
        ioctl(fd, IOCTL_READ_CLUSTERTHREADSID, &data);
        check_clusterID[(int)i] = read_value_low;
        
        if (read_value_low != cpu_id[i]) {
            printf("Failed to set cluster id for CPU%d.\n", i);
            printf("Expected: %d, Actual: %d\n", cpu_id[i], read_value_low);
            exit(1);
        }
    }


    // 設定内容(clusterID)をを表示
    printf("--------------\n");
    for (int i = 0; i < NUM_CPU; i++) {
        printf("CPU%d ClusterID: %d\n", i, check_clusterID[i]);
    }
    printf("--------------\n\n");


    printf("Input ClusterID\n");
    // 各WAYに割り付けるClusterIDを入力
    for (int i = 0; i < WAY_NUM; i++) {
        printf("WAY%d: ", i);
        scanf("%lu", &temp_id);

        // 入力IDが適切か確認
        while (temp_id < 0 || ID_NUM < temp_id) {
            printf("  err: Invalid ID\n");
            printf("WAY%d: ", i);
            scanf("%lu", &temp_id);
        }

        id_way[i] = temp_id;
    }
    printf("\n");

    // 設定するclusterIDを表示
    printf("--------------\n");
    for (int i = 0; i < WAY_NUM; i++) {
        printf("WAY%d Cluster-ID: %lu\n", i, id_way[i]);
    }
    printf("--------------\n");
    
    printf("Continue? [y/n]: ");
    scanf(" %c", &do_next);  // 先頭にスペースを入れて、前の入力の改行を無視
    while (do_next != 'y' && do_next != 'Y' && do_next != 'n' && do_next != 'N') {
        printf("Invalid input. Please enter 'y' or 'n': ");
        scanf(" %c", &do_next);  // 先頭にスペースを入れて、前の入力の改行を無視
    }
    if (do_next == 'n' || do_next == 'N') {
        printf("Prosess stoped.\n");
        return 1;
    }


    // レジスタ記入データを生成
    for (int i = 0; i < WAY_NUM; i++) {
        temp_id = 0b0001 << i;
        temp_id = temp_id << (4 * id_way[i]);   // IDが1の場合は4bit左シフト
        set_val = set_val | temp_id;
    }
    
    printf("WRITE CLUSTERPARTCR: 0x%lx\n", set_val);

    // 転送データ生成
    data.in_value = (long)set_val;
    data.out_value_low = &read_value_low;
    data.out_value_high = &read_value_high;


    ret = ioctl(fd, IOCTL_WRITE_CLUSTERPARTCR, &data);
    //ret = ioctl(fd, IOCTL_READ_CLUSTERTHREADSID, &data);

    close(fd);

    return 0;
}