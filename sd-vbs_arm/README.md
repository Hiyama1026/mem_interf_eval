# SD-VBSをARM上で実行するためのツール

## 概要
- SD-VBS及びCortexSuiteをARM向けに改変するためのパッチ
- Cプログラム実行のみ対応
- SD-VBSダウンロード先
    - 動作確認済みバージョン：Version 1.4CS
    - http://parallel.ucsd.edu/vision/

## 仕様方法
- SD-VBSをクローン
    - 上記ダウンロード先からフォームを入力するとリンクを入手できる
- パッチファイルをSD-VBSと同じディレクトリに移動
    ```sh
    $ ls
    cortexsuite           cortexsuite_arm.patch
    ```
- cortexsuiteに移動してからパッチを適用
    ```sh
    $ cd ~/cortexsuite
    $ patch -p1 < ../cortexsuite_arm.patch      # Apply patch
    ```

## 動作確認状況

![sd-vbs_conf](https://private-user-images.githubusercontent.com/119314361/496165092-627f3120-3654-40ba-a5ed-d44139e70577.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3NTkzMjQyMjMsIm5iZiI6MTc1OTMyMzkyMywicGF0aCI6Ii8xMTkzMTQzNjEvNDk2MTY1MDkyLTYyN2YzMTIwLTM2NTQtNDBiYS1hNWVkLWQ0NDEzOWU3MDU3Ny5wbmc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjUxMDAxJTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI1MTAwMVQxMzA1MjNaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT0xZGYxNDhiMmIxODMxNWVhMmYyNzcwOTRiMjU1OWM5NDEzNzllYjU4ZTc0MjFhOWNkY2I0OGI2OGQ0MmM5ODg0JlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCJ9.NC_EmFZXIOlN1_HwQf9s6vTxg0ukfLMTrrK3EKcB8D8)

## ベンチマーク実行方法
- SD-VBS (vision)
    - 全ベンチ・全データセットを一括実行
        - コンパイルして実行
            ```sh
            cd ~/cortexsuite/vision/benchmarks
            make c-run
            ```
        - コンパイルのみ
            ```sh
            cd ~/cortexsuite/vision/benchmarks
            make compile 
            ```
    - 特定のベンチマーク内の全てのデータセットを一括実行
        - コンパイルして実行
            ```sh
            cd ~/cortexsuite/vision/benchmarks/<ベンチマーク名>
            make c-run
            ```
        - コンパイルのみ
            ```sh
            cd ~/cortexsuite/vision/benchmarks/<ベンチマーク名>
            make compile 
            ```
    - 特定のベンチマーク内の特定のデータセットのみを実行
        - コンパイルして実行
            ```sh
            cd ~/cortexsuite/vision/benchmarks/<ベンチマーク名>/data/<データセット名>
            make c-run
            ```
        - コンパイルのみ
            ```sh
            cd ~/cortexsuite/vision/benchmarks/<ベンチマーク名>/data/<データセット名>
            make compile 
            ```
- cortex
    - ベンチマークをコンパイル
        ```sh
        cd ~/cortexsuite/cortex/<ベンチマーク名>
        make small      # smallのみ
        make medium     # mediumのみ
        make large      # largeのみ
        make compile    # 全データセット一括コンパイル
        ```
    - ベンチマークを実行 (コンパイルしてから実行する)
        ```sh
        make run-small  # smallのみ
        make run-medium # mediumのみ
        make run-large  # largeのみ
        make run-all    # 全データセット一括実行
        ```

## 備考
- 環境によってはcortexのコンパイル時にfscanfのエラーチェックをしていないことに対する警告が出る