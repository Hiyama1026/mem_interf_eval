# pmu_counter

## 概要
- Linuxでパフォーマンスカウンタを使用するためのプログラム
- 6種類のイベントを持つグループを作成し，それらのカウンタの開始・停止・結果の出力を行う
- 計測を行うプログラムと一緒にコンパイルする事により使用
- 参考：https://learn.arm.com/learning-paths/servers-and-cloud-computing/arm_pmu/perf_event_open/

# pmu_counter.c
## pmu_counter.cの使用方法
- ユーザプログラムと共にコンパイルする
    - ``$ gcc <PATH TO ユーザプログラム> ./pmu_counter.c``

## pmu_counter.cの仕様
### long perf_event_open(省略)
- syscall()を呼び出し，イベントを登録する
- ユーザプログラムからは呼び出す必要がない

### void create_six_event_group(int target_cpu, int event0, int event1, int event2, int event3, int event4, int event5)
- 6個のイベントを1つのグループとして登録する
- 引数
    - 第一引数：計測対象のCPU ID (int)
    - 第二引数～第七引数：登録するイベント番号 (int)
        - イベントIDはCPUのマニュアルを参照
        - 例)Coretex-A76：https://developer.arm.com/documentation/100798/0301/Debug-descriptions/Performance-Monitor-Unit/PMU-events?lang=en
- 先頭に登録したイベント(event0)がグループの親となる

### void reset_and_start_counter(void)
- カウンタをリセットしてから起動する
- ``create_six_event_group()``で登録したイベントグループのカウンタを操作する

### void reset_counter(void)
- カウンタをリセットする
- ``create_six_event_group()``で登録したイベントグループのカウンタを操作する

### void start_counter(void)
- カウンタをリセットせずに起動する
- ``create_six_event_group()``で登録したイベントグループのカウンタを操作する

### void stop_counter(void)
- カウントを停止する
- ``create_six_event_group()``で登録したイベントグループのカウンタを操作する

### void export_counter(char* cnt_name0, char* cnt_name1, char* cnt_name2, char* cnt_name3, char* cnt_name4, char* cnt_name5)
- カウント値の出力のみを行う
- 引数
    - 第一引数～第六引数：イベント名 
        - ``create_six_event_group()``で登録したevent0～event5までのイベント名
        - ``create_six_event_group()``で登録した順に対応
        - 単に出力用であるため，ユーザがわかれば何でもよい
- ``create_six_event_group()``で登録したイベントグループのカウンタを操作する

### void export_and_clean_counter(char* cnt_name0, char* cnt_name1, char* cnt_name2, char* cnt_name3, char* cnt_name4, char* cnt_name5)
- カウント値を出力して，ディスクリプタを削除する
- 引数
    - 第一引数～第六引数：イベント名 
        - ``create_six_event_group()``で登録したevent0～event5までのイベント名
        - ``create_six_event_group()``で登録した順に対応
        - 単に出力用であるため，ユーザがわかれば何でもよい
- ``create_six_event_group()``で登録したイベントグループのカウンタを操作する

## ユーザプログラムでの使用イメージ
```c
target_cpu = 3;
// PMUカウンタグループの生成
create_six_event_group(target_cpu, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d);

reset_and_start_counter();      // PMUカウンタ起動
/*
 * 計測対象のコード
 */
stop_counter();                 // PMUカウンタ停止

// 結果を出力し，ディスクリプタを削除
export_and_clean_counter("L2D_CACHE_WB", "BUS_ACCESS", "MEMORY_ERROR", 
                        "INST_SPEC", "TTBR_WRITE_RETIRED", "BUS_CYCLES");
```

# calculate_clpartcr.c
- gccでコンパイルして使用する
    - ``$ gcc -o calculate_clpartcr calculate_clpartcr.c``
- DSUによるWAYパーティショニングをする際にCLUSTERPARTCRレジスタに書き込む値を計算する
- 実行したら各WAYに割り付けるCPUクラスタIDを入力する
- CLUSTERPARTCRレジスタに書き込む値が16進数で表示される
