/*
 * コンパイル: $ gcc -O2 -Wall -o parse_numbers parse_numbers.c
 * 
 * 引数で渡されたコンマ区切りの数字を解析し、int型の配列に格納するプログラム
 * 例: ./parse_numbers "1,2,3,4"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

/**
 * 文字列がint型の範囲内の有効な数値かどうかをチェックし、変換を行う
 * @param str 変換する文字列
 * @param result 変換結果を格納するポインタ
 * @return 成功時は0、エラー時は-1
 */
int safe_string_to_int(const char *str, int *result) {
    char *endptr;
    long val;
    
    // 空文字列チェック
    if (str == NULL || *str == '\0') {
        return -1;
    }
    
    // 先頭の空白文字をスキップ
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    
    // 再度空文字列チェック
    if (*str == '\0') {
        return -1;
    }
    
    errno = 0;
    val = strtol(str, &endptr, 10);
    
    // 変換エラーチェック
    if (errno == ERANGE || val > INT_MAX || val < INT_MIN) {
        return -1;
    }
    
    // 末尾に不正な文字がないかチェック
    while (*endptr == ' ' || *endptr == '\t') {
        endptr++;
    }
    
    if (*endptr != '\0') {
        return -1;
    }
    
    *result = (int)val;
    return 0;
}

/**
 * コンマ区切りの文字列を解析してint配列に格納する
 * @param input 入力文字列
 * @param numbers 結果を格納する配列のポインタ（動的に確保される）
 * @param count 要素数を格納するポインタ
 * @return 成功時は0、エラー時は-1
 */
int parse_comma_separated_numbers(const char *input, int **numbers, int *count) {
    if (input == NULL || numbers == NULL || count == NULL) {
        return -1;
    }
    
    // 入力文字列をコピー（strtokは元の文字列を変更するため）
    char *input_copy = malloc(strlen(input) + 1);
    if (input_copy == NULL) {
        fprintf(stderr, "エラー: メモリ確保に失敗しました\n");
        return -1;
    }
    strcpy(input_copy, input);
    
    // 最初にコンマの数をカウントして配列サイズを決定
    int comma_count = 0;
    for (const char *p = input; *p; p++) {
        if (*p == ',') {
            comma_count++;
        }
    }
    int expected_count = comma_count + 1;
    
    // 配列を動的に確保
    *numbers = malloc(expected_count * sizeof(int));
    if (*numbers == NULL) {
        fprintf(stderr, "エラー: メモリ確保に失敗しました\n");
        free(input_copy);
        return -1;
    }
    
    *count = 0;
    char *token = strtok(input_copy, ",");
    
    while (token != NULL && *count < expected_count) {
        int num;
        if (safe_string_to_int(token, &num) != 0) {
            fprintf(stderr, "エラー: 無効な数値です: '%s'\n", token);
            free(*numbers);
            free(input_copy);
            *numbers = NULL;
            return -1;
        }
        
        (*numbers)[*count] = num;
        (*count)++;
        token = strtok(NULL, ",");
    }
    
    free(input_copy);
    
    // トークンが残っている場合はエラー
    if (token != NULL) {
        fprintf(stderr, "エラー: 予期しない数の要素があります\n");
        free(*numbers);
        *numbers = NULL;
        return -1;
    }
    
    // 空の入力の場合
    if (*count == 0) {
        fprintf(stderr, "エラー: 有効な数値が見つかりませんでした\n");
        free(*numbers);
        *numbers = NULL;
        return -1;
    }
    
    return 0;
}

/**
 * 配列の内容をコンソールに出力する
 * @param numbers 出力する配列
 * @param count 配列の要素数
 */
void print_array(const int *numbers, int count) {
    printf("解析結果: [");
    for (int i = 0; i < count; i++) {
        printf("%d", numbers[i]);
        if (i < count - 1) {
            printf(", ");
        }
    }
    printf("]\n");
    printf("要素数: %d\n", count);
}

int main(int argc, char *argv[]) {
    // 引数チェック
    if (argc != 2) {
        fprintf(stderr, "使用方法: %s \"数値1,数値2,数値3,...\"\n", argv[0]);
        fprintf(stderr, "例: %s \"1,2,3,4\"\n", argv[0]);
        return 1;
    }
    
    int *numbers = NULL;
    int count = 0;
    
    // 文字列を解析
    if (parse_comma_separated_numbers(argv[1], &numbers, &count) != 0) {
        // エラーメッセージは関数内で出力済み
        return 1;
    }
    
    // 結果を出力
    print_array(numbers, count);
    
    // メモリを解放
    free(numbers);
    
    return 0;
}