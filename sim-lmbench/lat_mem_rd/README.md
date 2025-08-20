# メモリReadベンチマーク sim-lat_mem_rd

## 各プログラムの説明
### sim-lat_mem_rd.c
- 概要
    - メモリRDレイテンシを計測する
    - バッファ内をstrideアクセスして，メモリRead1回に要した時間を報告
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

### inf-sim-lat_mem_rd.c (プログラムメンテナンス中)
- 概要
    - メモリRDトラフィックを生成する
        - 干渉評価時における，負荷をかける側として使用することを想定
    - strideアクセスで無限にメモリRDを行う
- 使用方法
    - ToDo

### pmu-sim-lat_mem_rd.c (プログラムメンテナンス中)
- 概要
    - ToDo

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
### inf-sim-lat_mem_rdのstride幅
- 負荷をかける際のstride幅はキャッシュラインサイズと一致させることで効率よくキャッシュを汚すことができる
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

![wayset_mem-latency_graph](https://private-user-images.githubusercontent.com/119314361/479981957-cdedc1e5-9ffd-4f23-867e-a621be77c8ab.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3NTU2ODk2MjcsIm5iZiI6MTc1NTY4OTMyNywicGF0aCI6Ii8xMTkzMTQzNjEvNDc5OTgxOTU3LWNkZWRjMWU1LTlmZmQtNGYyMy04NjdlLWE2MjFiZTc3YzhhYi5wbmc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjUwODIwJTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI1MDgyMFQxMTI4NDdaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT05NmQzNzZhYjAwODY4OTZhOWYxZDg4YjQ3ODgyNjQzNmVhOWJkNmZhOTNhZjQxZDk5YzMzODc4YWZmYTA3ODdmJlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCJ9.ua1mxCxsk--zg87wc9q6rAnVOgzwvF3MVqseSUAa4IY)
