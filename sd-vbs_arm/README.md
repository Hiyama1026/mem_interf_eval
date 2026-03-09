# SD-VBSをARM上で実行するためのツール

## 概要
- SD-VBS及びCortexSuiteをARM向けに改変するためのパッチ
- Cプログラム実行のみ対応
- SD-VBSダウンロード先
    - 動作確認済みバージョン：Version 1.4CS
    - http://parallel.ucsd.edu/vision/

## 使用方法
- SD-VBSをクローン
    - 上記ダウンロード先からフォームを入力するとリンクを入手できる
- パッチファイルをSD-VBSと同じ階層のディレクトリに置く
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

### SD-VBS (vision)
| --- | **disparity** | **localization** | **mser** | **multi_ncut** | **pca** | **sift** | **stitch** | **svm** | **texture_synthesis** | **tracking** |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **cif**      | ○ | ○ | ○ | ○ | ×(err) | ○ | ×(err) | ○ | ○ | ○ |
| **fullhd**   | ○ | ×(存在しない) | ○ | ○ | ×(err) | ○ | ×(err) | ×(存在しない) | ○ | ○ |
| **qcif**     | ○ | ○ | ○ | ○ | ×(err) | ○ | ×(err) | ○ | ○ | ○ |
| **sim**      | ○ | ○ | ○ | ○ | ×(err) | ○ | ×(err) | ○ | ○ | ○ |
| **sim_fast** | ○ | ○ | ○ | ○ | ×(err) | ○ | ×(err) | ○ | ○ | ○ |
| **sqcif**    | ○ | ○ | ○ | ○ | ×(err) | ○ | ×(err) | ○ | ○ | ○ |
| **vga**      | ○ | ○ | ○ | ○ | ×(err) | ○ | ×(err) | ×(存在しない) | ○ | ○ |

### CortexSuite
| ベンチマーク | clustering | lda | liblinear | motion-estimation | srr | svd3 | word2vec | pca | rbm | sphinx | cnn |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| 状況 | ○ | ○ | ○ | ○ | ○ | ○ | ○ | △ | × | × | × |

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