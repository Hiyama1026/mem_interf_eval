#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// IDの最大値
#define ID_NUM  7
// WAYの数
#define WAY_NUM 4


int main(int argc, char* argv[]) {
    uint64_t id_way[WAY_NUM];
    uint64_t temp_id;
    uint64_t set_val = 0;
    char do_next;

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
    
    printf("WRITE CLUSTERPARTCR: (0x)%lx\n", set_val);

    return 0;
}