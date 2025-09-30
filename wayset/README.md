# wayset

## 概要
- WAYパーティショニングを行うための各種ツール

## wpuser-control.patch
- Pi5-CacheWayPartitionのドライバ``wpuser-control.c``に当てるパッチ
    - レジスタ読み込み時に値が0だと表示しない仕様となっているのを，レジスタ読み込み時にはどの値でも表示するように変更する
    - https://github.com/ColeStrickler/Pi5-CacheWayPartition

## calculate_clpartcr.c
- WAYパーティショニング時にCLUSTER_PART_CRに書き込む値を計算するプログラム
- DSU(CLUSTER_PART_CR)を使う場合はプラットフォームやドライバに関係なく共通に使える
- コンパイル：``$ gcc -o calculate_clpartcr calculate_clpartcr.c ``
- 実行後は各WAYに割り付けるCPUクラスタIDを入力
    - 例) WAY0と1にDSUのCPUクラスタID0を，WAY2と3にCPUクラスタID1を割り付ける
    ```sh
    $ ./calculate_clpartcr
    Input ClusterID
    WAY0: 0
    WAY1: 0
    WAY2: 1
    WAY3: 1

    --------------
    WAY0 Cluster-ID: 0
    WAY1 Cluster-ID: 0
    WAY2 Cluster-ID: 1
    WAY3 Cluster-ID: 1
    --------------
    Continue? [y/n]: y
    WRITE CLUSTERPARTCR: (0x)c3    ← レジスタに入力する値
    ```
- 本プログラムからレジスタへの書き込みは行わない

## wayset_ctrl.c
- Pi5-CacheWayPartitionのドライバ操作プログラム``wpuser-control.c``の改良版
    - CLUSTER_PART_CRレジスタとCLUSTER_THREAD_SIDの設定のみ可能
    - 各CPUにCPUクラスタを付与(CLUSTER_THREAD_SID)して，クラスタIDとWAYを紐づける(CLUSTER_PART_CR)
- コンパイル：``$ gcc -o wayset_ctrl wayset_ctrl.c``
- Pi5-CacheWayPartitionの``way-part-control.ko``をinsmodしてから実行すること
- ソースコード内のマクロ``#define NUM_CPU 4``でCPU数を設定すること
- 引数に各CPUに割り当てるクラスタIDを入力して実行
- 実行後は各WAYに割り付けるCPUクラスタIDを入力
- 実行例
    - CPU0～2をクラスタ0，CPU3をクラスタ1に設定 → WAY0～1とクラスタ0を割り付け，WAY2～3をクラスタ1に割り付け
    ```sh
    $ sudo ./wayset_ctrl 0 0 0 1
    --------------
    CPU0 ClusterID: 0
    CPU1 ClusterID: 0
    CPU2 ClusterID: 0
    CPU3 ClusterID: 1
    --------------

    Input ClusterID
    WAY0: 0 
    WAY1: 0
    WAY2: 1
    WAY3: 1

    --------------
    WAY0 Cluster-ID: 0
    WAY1 Cluster-ID: 0
    WAY2 Cluster-ID: 1
    WAY3 Cluster-ID: 1
    --------------
    Continue? [y/n]: y
    WRITE CLUSTERPARTCR: 0xc3
    ```
- 本プログラムはレジスタの書き込みまで行う

## insmod_wp.sh
- Pi5-CacheWayPartitionのドライバ``way-part-control.ko``をinsmodするスクリプト
    - https://github.com/ColeStrickler/Pi5-CacheWayPartition
- スクリプト内の``PATH_TO_KO="$HOME/Pi5-CacheWayPartition/way-part-control.ko"``のパスを適宜設定すること