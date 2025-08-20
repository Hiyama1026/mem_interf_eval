# sim-lmbench
- LMbenchをシングルスレッド化するなど簡略化したプログラム
- メモリアクセスレイテンシの計測やRead・Write負荷の生成に使用する

## プログラム概要
- lat_mem_rd
    - メモリRDレイテンシやメモリRD負荷を生成する
    - strideアクセスでメモリを読み込む
    - キャッシュパーティショニングが有効化されているかの確認にも使用可能
        - 詳細はlat_mem_rdディレクトリ内のREADMEを参照
- 使用方法・使用用途などはディレクトリ内のREADMEを参照

- bw_mem_wr
    - Comming soon
    
## Yocto Linuxで使用する方法
- コンパイルやtasksetオプション等に必要なパッケージをイメージに組み込む
    - ``<ワークスペースTOP>/build/conf/local.conf``に``IMAGE_INSTALL:append = " <パッケージ名>"``を追加する
        - makeやbuildessentialなどのコンパイル用パッケージ
        - cpusetやlibcgroupなどのtasksetオプション向けパッケージ 他
    - Sulfur向けYoctoでは以下を追加して実施
        - ``IMAGE_INSTALL:append = " cmake make stress-ng packagegroup-core-buildessential libcgroup cpuset apt htop tuna curl"``
        - ターゲットによって名称が異なる，対応・非対応に差がある，元から組み込まれているパッケージに差がある，etc... 可能性あり
        - ``$ bitbake -s``で追加可能なパッケージを確認可能
- ターゲットに本レポジトリのプログラム(sim-lat_mem_rd.cなど)をコピーし，ターゲット上でコンパイル
    - コンパイルコマンドは各プログラムの手順書(README)の通り
    