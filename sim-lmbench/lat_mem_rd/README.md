# メモリReadベンチマーク sim-lat_mem_rd

## 各プログラムの説明
### sim-lat_mem_rd.c
- 概要
    - メモリRDレイテンシを計測する
    - バッファ内をstrideアクセスして，メモリRead1回に要した時間を報告
- コンパイル方法
    - -O2でコンパイルする (gcc)
        - ``$ make sim-lat_mem_rd``
        - ``$ gcc -O2 -o sim-lat_mem_rd sim-lat_mem_rd.c -pthread -lrt -D_GNU_SOURCE`` でも可
- 使用方法
    - 第一引数に最大バッファサイズ，第二引数にstride幅を入力 (順番は固定)
        - ``$ ./sim-lat_mem_rd 5M 2K``
        - 実行するとバッファサイズが最大バッファサイズまでステップ的に増加していく
        - バッファ内を指定した幅でstrideアクセスする
    - 第三引数以降で以下のオプションを使用可能 (順番は任意)
        - ``-t <CPU ID>``：taskset機能
            - スレッドにCPUアフィニティを設定する
            - 複数コアを指定する場合はコンマ区切りで入力する
        - ``-g <CPU ID>``：cgroup v2を使用したtaskset機能
            - cgroupを使用するため環境によってはsudo実行が必要
            - 複数コアを指定する場合はコンマ区切りで入力する
        - ``-o``：OneShotモード
            - 指定したバッファサイズのみを計測
                ```sh
                $ ./sim-lat_mem_rd 5M 2K -o
                5.00000,24.021
                ```
    - その他
        - -gと-tは併用不可能

### inf-sim-lat_mem_rd.c
- 概要
    - メモリRDトラフィックを生成する
        - 干渉評価時における，負荷をかける側として使用することを想定
    - strideアクセスで無限にメモリRDを行う
- コンパイル方法
    - -O2でコンパイルする (gcc)
        - ``$ make inf-sim-lat_mem_rd``
        - ``gcc -O2 -o inf-sim-lat_mem_rd inf-sim-lat_mem_rd.c -pthread -lrt -D_GNU_SOURCE``でも可
- 使用方法
    - 第一引数にバッファサイズ，第二引数にstride幅を入力 (順番は固定)
        - ``$ ./inf-sim-lat_mem_rd 2M 64``
    - 第三引数以降で以下のオプションを使用可能 (順番は任意)
        - ``-t <CPU ID>``：taskset機能 (スレッド単位)
            - スレッドにCPUアフィニティを設定する (マルチスレッド対応)
            - 複数コアを指定する場合はコンマ区切りで入力する
            - CPU番号の大きいものから順にセットする
                - ``「スレッド数」>「セットするCPU数」``の場合は各CPUに一つずつスレッドをセットしていき，余ったスレッドに全てのCPUを割り当てる
                    ```sh
                    (例) スレッド数：4，セットするCPU数：3 (CPU1~3)
                    $ ./inf-sim-lat_mem_rd 2M 64 -t 1,2,3 -m 4  # 実行コマンド
                    - スレッド1 -> CPU3
                    - スレッド2 -> CPU2
                    - スレッド3 -> CPU1
                    - スレッド4 -> CPU1, CPU2, CPU3
                    ```
                - (注)マルチスレッド時はバッファを分割し，各スレッド分割された領域を占有するため，スレッドが待たされる事により上記の場合はアクセス範囲が小さくなる可能性がある
        - ``-g <CPU ID>``：cgroup v2を使用したtaskset機能
            - cgroupを使用するため環境によってはsudo実行が必要
            - 複数コアを指定する場合はコンマ区切りで入力する
        - ``-r <INTERVAL>``：実行時のメモリRDレイテンシを表示
            - ファイル出力しない場合はターミナルに出力
            - INTERVALは報告を行う間隔 (大きいほど間隔が長い)
        - ``-f <FILE NAME>``：メモリRDレイテンシをファイルに出力
            - FILE NAMEに結果を格納するファイル名を入れる
            - ``-r``でレポート間隔を指定した上で使用
            - ベンチ実行中は配列に結果を格納し，プログラム終了時にファイル出力を行う
                - バッファ(配列)フルでもプログラムを終了し，ファイルに結果を出力する
        - ``-e <NUM OF ELEMENTS>``：バッファに格納可能なログの数を指定
            - NUM OF ELEGANSに保存可能なログの上限数を入力
            - ``-e``を使用しない場合はデフォルト値が使用される (プログラム内の``buf_log_size``変数で定義)
        - ``-m <NUM OF THREADS>``: マルチスレッドで実行
            - バッファをスレッド間で共有する
                - スレッド数でバッファを等分する
                    - 端数が生じる場合は最後のスレッドに含ませる
                - 各スレッドが，各々に割り当てられた領域内でメモリRD
            - スコアの報告機能 (``-r``，``-f``) と併用する場合は，最初に生成されたスレッドのスコアのみを報告する
    - 実行コマンド例
        - ``$ ./inf-sim-lat_mem_rd 2M 64 -r 500000000 -f test.csv``
        - ``$ sudo ./inf-sim-lat_mem_rd 2M 16K -m 3 -t 1,2,3``
- その他
    - -gと-tは併用不可能
    - ファイル出力を行う場合，ログ出力用のバッファフルでプログラムが終了した場合はログファイルの先頭に警告が表示される
        - ``WARN: BUFFER FULL.``
    - プログラムの終了方法はtaskkillと強制終了(Ctrl+C)に対応している
    - stride幅はキャッシュラインサイズと一致させることで効率よくキャッシュを汚すことができる

### pmu-sim-lat_mem_rd.c
- 概要
    - メモリRead中に生じるHW操作をPMUカウンタで観察する
    - 干渉をかけた時とかけない時の結果を比較することで，干渉によりどのHW操作が増加したかを観測することが可能
- コンパイル方法
    - ``pmu-sim-lat_mem_rd.c``を本レポジトリ内にある``pmu_counter.c``と一緒に-O2でコンパイルする(gcc)
        - ``$ gcc -O2 -o pmu-sim-lat_mem_rd pmu-sim-lat_mem_rd.c <PATH TO pmu_counter.c>``
    - ``$ make pmu-sim-lat_mem_rd`` でも可
        - Makefile内の``pmu_counter.c``までのパスは適宜合わせること
- 実行方法
    - 方法1：``-t``または``-g``オプションでベンチの実行コアを指定して実行する
        - ``pmu_counter.c``の仕様に合わせて``create_six_event_group()``や``export_and_clean_counter()``の設定を行う
        - ``$ sudo ./pmu-sim-lat_mem_rd <BUFFER SIZE> <STRIDE> -t <CPU NUM>`` で実行する
            - -t・-gオプションを使用する場合はPMUの計測対象のコアも指定したコアになる
            - -gはcgroup-vのcpusetを使用してコア固定を行うため，sudo実行が必要な場合がある
    - 方法2：``taskset``コマンドなど，外部コマンドによりベンチの実行コアを指定して実行する
        - ``pmu_counter.c``の仕様に合わせて``create_six_event_group()``や``export_and_clean_counter()``の設定を行う
        - ``taskset -c <CPU NUM> ./pmu-sim-lat_mem_rd <BUFFER SIZE> <STRIDE>`` などで実行する
            - 実行するCPUは``pmu-sim-lat_mem_rd.c``内の``target_cpu``変数で**指定されているCPUと一致**している必要がある
- その他
    - 実行引数で指定したバッファサイズでstrideアクセスを行う
        - アクセス回数は``pmu-sim-lat_mem_rd.c``の``num_iters``変数で定義されている
    - ``pmu_counter.c``の仕様は[mem_interf_eval/pmu_counter/README.md](../../pmu_counter/README.md)を参照

## 注意事項など
### strideアクセスとは
- メモリを等間隔でアクセスすること
### Readレイテンシ計測時のstride幅の条件
- **stride幅は2のべき乗(正確にはWAYサイズを割り切れるサイズ)である必要がある**
    - 例: 64や512，2K，4K，8K，16Kなど
- 2のべき乗でなければならない理由
    - キャッシュのコンフリクトミスを適切に起こすため
    - stride幅間隔でアクセスするため，バッファサイズに対して実際のアクセスサイズが小さくなるが，コンフリクトミスが起きる事により，キャッシュミスが起き始める時のバッファサイズがキャッシュサイズに一致する
        - 例)下図「LLCのWAY数毎のメモリRDレイテンシ」の``way×4 (no-partition)``曲線
        - 16KB間隔でアクセスするため，バッファ内の一部しかアクセスしていない
        - しかし，stride幅が2のべき乗であるため，バッファサイズが2MB(LLCサイズ)を超すタイミングでコンフリクトミスが発生し，レイテンシが上昇し始める (LLCをミスし始める)
### stride幅とプリフェッチ
- stride幅を増加させるとプリフェッチが効かなくなることを確認済み

## Yocto Linuxで使用する方法
- [mem_interf_eval/sim-lmbench/README.md](../README.md) を参照

## 使用例
### キャッシュパーティショニングが有効かを確認
#### 概要
- ARM DSUによるLLCのWAY操作でWAYパーティショニングをかける
- WAYパーティショニングが正しく動作しているかを確認する
- 下記の例では4コアのターゲット(RaspberryPi5)を使用している
#### 手順
- WAYパーティショニングをかける
    - トップのREADMEの「[ARM DSUによるLLCのWAYパーティショニングのかけ方](../../README.md/#arm-dsuによるllcのwayパーティショニングのかけ方)」を参照
    - 今回は例としてCPU0~2をクラスタ0に，CPU3をクラスタ1に設定し，CPU3上でベンチマークを実行する
        - クラスタ1(CPU3)に割り当てるWAY数を変化させてベンチマークの挙動を確認する
- sim-lat_mem_rdを実行する
    ```sh
    $ taskset -c 3 ./sim-lat_mem_rd 5M 16K      # WAYパーティショニングをかけるためtaskset(-tでも可)
    max_ws, 5242880
    stride, 16384

    Buffer [MB], Latency [ns]
    0.01562,1.668
    0.01953,1.668
    <省略>
    4.00000,102.378
    5.00000,126.538

    test end (ws: 5242880)
    ```
- 結果をcsvファイルに格納してグラフ化する
    - WAY割り付けによりレイテンシの上昇タイミングが変化している場合は，パーティショニングが機能していると考えられる

- 参考)下記グラフはCPU3で使用可能なLLCのWAY数を変化させた時の結果グラフ (凡例の``way×◯``はベンチマークが利用可能なWAY数を表す)
    - LLCサイズは2MB，1WAYあたり512KB
    - 上記のようにCPU3上でsim-lat_mem_rdを実行
    - ベンチマークを実行するCPU3(クラスタ1)が使用できるWAY数(LLCサイズ)に応じてレイテンシ上昇のタイミングが変化する

![wayset_mem-latency_graph](https://private-user-images.githubusercontent.com/119314361/479981957-cdedc1e5-9ffd-4f23-867e-a621be77c8ab.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3NjAwMDcwODEsIm5iZiI6MTc2MDAwNjc4MSwicGF0aCI6Ii8xMTkzMTQzNjEvNDc5OTgxOTU3LWNkZWRjMWU1LTlmZmQtNGYyMy04NjdlLWE2MjFiZTc3YzhhYi5wbmc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjUxMDA5JTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI1MTAwOVQxMDQ2MjFaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT00MzdjYjEyZWU1ZjVjYzg1MGMxODhlNGRiN2U1ZjYwZTAyNTA5ZmUzNTY5YjljMDM3YzUwM2I5OWIwOGE0NDEyJlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCJ9.RxiOcHbXNxmm2gyhY6evqUf3hsyluzP9gW33V8I-E44)
