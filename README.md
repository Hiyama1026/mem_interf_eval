# mem_interf_eval
- メモリアクセスによるコア間の実行時間干渉を評価を目的としたプログラム
    - 共有キャッシュ上での干渉が主な対象

## 動作確認済みターゲット
- Raspberry Pi 5
- Sulfur

## プログラム概要
- pmu_counter
    - Linuxでパフォーマンスカウンタを使用するためのプログラム

- sim-lmbench
    - LMbenchの一部ベンチマークをシングルスレッド化するなど簡素化したもの
        - メモリアクセスのレイテンシ等を計測
    - キャッシュパーティショニングが機能しているかの確認にも使用可能
    - RD・WR負荷を生成するためにカスタムしたプログラムもあり

## 手順書Index
- sim-lmbench内のプログラムをYocto Linux上で使用する方法
    - [./sim-lmbench/README.md/#Yocto Linuxで使用する方法](./sim-lmbench/README.md/#yocto-linuxで使用する方法)
- パーティショニングが有効かを確認
    - [./sim-lmbench/lat_mem_rd/README.md/#キャッシュパーティショニングが有効かを確認](./sim-lmbench/lat_mem_rd/README.md/#キャッシュパーティショニングが有効かを確認)

## ARM DSUによるLLCのWAYパーティショニングのかけ方
- 前提
    - DSUが有効化されていること
- WAYパーティショニング用レジスタ操作ツールをインストール
    - Raspberry Pi 5向けのプログラムだが，レジスタ操作プログラムはRaspberry Pi 5以外のターゲットにも使用可能 (動作確認は末)
    ```sh
    cd ~
    git clone https://github.com/ColeStrickler/Pi5-CacheWayPartition.git
    ```
- レジスタ操作ツールにパッチを当てる
    - レジスタ値の読み込み時は無条件で取得値を表示するように変更
        ```sh
        cp mem_interf_eval/wayset/wpuser-control.patch ~/Pi5-CacheWayPartition
        cd ~/Pi5-CacheWayPartition
        patch < wpuser-control.patch 
        ```
- ツールをコンパイル
    - ``$ cd ~/Pi5-CacheWayPartition``
    - ``$ make``
        - ``wpuser-control``ができれば成功
- wpuser-controlを実行してパーティショニング
    - wpuser-controlに関する情報
        - 実行フォーマットは``$ sudo ./wpuser-control <レジスタ操作ID> <設定値> <操作対象のCPU ID>``
        - **設定値は16進数で入力(0xはつけない)**
        - WAYパーティショニングに使用するレジスタとレジスタ操作ID
            - ``CLUSTERTHREADSID``
                - 書き込み用レジスタ操作ID：5 (IOCTL_WRITE_CLUSTERTHREADSID)
                - 読み込み用レジスタ操作ID：10 (IOCTL_READ_CLUSTERTHREADSID)
            - ``CLUSTERPARTCR``
                - 書き込み用レジスタ操作ID：1 (IOCTL_WRITE_CLUSTERPARTCR)
                - 読み込み用レジスタ操作ID：6 (IOCTL_READ_CLUSTERPARTCR)
        - レジスタの仕様の詳細は[Arm® DynamIQ™ Shared Unit TRM](https://developer.arm.com/documentation/100453/0401/?lang=en) ([Control registers章](https://developer.arm.com/documentation/100453/0401/Control-registers?lang=en))を参照
    - 各CPUにCPUクラスタIDを設定
        - ``$ sudo ./wpuser-control 5 <クラスタID> <CPU ID>``
    - CPUクラスタIDを確認
        ```sh
        $ sudo ./wpuser-control 10 0 <CPU ID>
        Read value: <設定した値>
        ```
    - 各CPUクラスタを特定のWAYに割り付ける
        - ``$ sudo ./wpuser-control 1 <設定値> 0``
            - CPU ID(↑は0)は何でも良い
            - 設定値がいくつになるかはDSU TRMの[CLUSTERPARTCR](https://developer.arm.com/documentation/100453/0401/Control-registers/CLUSTERPARTCR--Cluster-Partition-Control-Register?lang=en)を参照
                - 例）クラスタID0をWAY0とWAY1に，クラスタID1をWAY2とWAY3に割り付ける場合
                    - ``1100 0011``をレジスタに記入 → 設定値は``c3`` (``$ sudo ./wpuser-control 1 c3 0``)
    - WAY割り付けの設定値を確認
        ```sh
        $ sudo ./wpuser-control 6 0 <CPU ID(どれでも可)>
        Read value: <設定値>
        ```
- 参考：4コアのターゲットで，CPU0~2をクラスタ0，CPU3をクラスタ1に設定し，各クラスタにWAYを2枚ずつ割り当てた(LLCを2等分した)ときのコマンド
    ```sh
    # クラスタID設定
    $ sudo ./wpuser-control 5 0 0
    $ sudo ./wpuser-control 5 0 1
    $ sudo ./wpuser-control 5 0 2
    $ sudo ./wpuser-control 5 1 3
    # クラスタID確認
    $ sudo ./wpuser-control 10 0 0
    Read value: 0x0
    $ sudo ./wpuser-control 10 0 1
    Read value: 0x0
    $ sudo ./wpuser-control 10 0 2
    Read value: 0x0
    $ sudo ./wpuser-control 10 0 3
    Read value: 0x1
    # WAY割り付け
    $ sudo ./wpuser-control 1 c3 0
    # WAY割り付け確認
    $ sudo ./wpuser-control 6 0 0
    Read value: 0xc3
    ```