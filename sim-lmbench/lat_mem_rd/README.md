# sim-lat_mem_rd

## sim-lat_mem_rd.c
- 概要
    - メモリRDレイテンシを計測する
- 使用方法
    - -O2でコンパイルする (gcc)
        - ``$ gcc -O2 -o sim-lat_mem_rd sim-lat_mem_rd.c ``
    - 実行引数に最大バッファサイズとstride幅を入力する
        - ``$ ./sim-lat_mem_rd 5M 2K``
    - OneShotモード
        - バッファサイズとstride幅よりも``-o``を付けると指定したバッファサイズのみを計測
            ```sh
            $ ./sim-lat_mem_rd 5M 2K -o
            5.00000,24.021
            ```
    - tasksetオプション
        - cgroupのcpusetを使用して自タスクの実行コアを固定するオプション
            - Yocto Linuxなど，tasksetコマンドが使用できない環境下での使用を想定
        - バッファサイズとstride幅よりも``-t``を付けることによりコア固定
            - ``$ sudo ./sim-lat_mem_rd 5M 2K -t 2``
    - その他
        - -oと-tは併用可能
        - バッファサイズとstride幅以外はオプションの入力順を問わない

## 使用例
### キャッシュパーティショニングが有効かを確認
- 概要
    - ARM DSUによるLLCのWAY操作でWAYパーティショニングをかける
    - WAYパーティショニングが正しく動作しているかを確認する
- WAYパーティショニングをかける
    -  トップのREADMEの「[ARM DSUによるLLCのWAYパーティショニングのかけ方](../../README.md/#arm-dsuによるllcのwayパーティショニングのかけ方)」を参照
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
- 結果をcsvファイルなどに格納してグラフ化する
    - WAY割り付けによりレイテンシの上昇タイミングが変化している場合は，パーティショニングが機能していると考えられる

- 参考)下記グラフはCPU3で使用可能なLLCのWAY数を変化させた時の結果グラフ
    - LLCサイズは2MB，1WAYあたり512KB
    - 上記のようにCPU3上でsim-lat_mem_rdを実行
    - CPU3が使用できる
