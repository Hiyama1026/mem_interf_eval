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
    
