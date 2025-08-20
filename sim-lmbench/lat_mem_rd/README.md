# sim-lat_mem_rd (メモリReadベンチマーク)

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
        
## Yocto Linuxで使用する方法
- [mem_interf_eval/sim-lmbench/README.md](../README.md) を参照

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

![wayset_mem-latency_graph](https://private-user-images.githubusercontent.com/119314361/479981957-cdedc1e5-9ffd-4f23-867e-a621be77c8ab.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3NTU2ODk2MjcsIm5iZiI6MTc1NTY4OTMyNywicGF0aCI6Ii8xMTkzMTQzNjEvNDc5OTgxOTU3LWNkZWRjMWU1LTlmZmQtNGYyMy04NjdlLWE2MjFiZTc3YzhhYi5wbmc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjUwODIwJTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI1MDgyMFQxMTI4NDdaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT05NmQzNzZhYjAwODY4OTZhOWYxZDg4YjQ3ODgyNjQzNmVhOWJkNmZhOTNhZjQxZDk5YzMzODc4YWZmYTA3ODdmJlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCJ9.ua1mxCxsk--zg87wc9q6rAnVOgzwvF3MVqseSUAa4IY)
