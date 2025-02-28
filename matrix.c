#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <time.h>

#define N 2048  // 矩阵大小 N x N

// 初始化矩阵
void initialize_matrix(float *matrix) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            matrix[i * N + j] = (float)(rand()) / RAND_MAX;
        }
    }
}

// 矩阵乘法 C = A * B
void matrix_multiplication(float *A, float *B, float *C, int start_row, int end_row) {
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < N; k++) {
                sum += A[i * N + k] * B[k * N + j];
            }
            C[(i - start_row) * N + j] = sum;
        }
    }
}

int main(int argc, char *argv[]) {
    int rank, size;
    double start_time, end_time;
    double init_time, compute_time, gather_time;

    // 初始化 MPI 环境
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // 检查矩阵行数是否能被进程数整除
    if (N % size != 0) {
        if (rank == 0) {
            printf("错误: 矩阵行数 %d 无法被进程数 %d 整除\n", N, size);
        }
        MPI_Finalize();
        return -1;
    }

    int rows_per_process = N / size;

    // 分配内存
    float *A = NULL;
    float *B = (float *)malloc(N * N * sizeof(float));
    float *C = NULL;
    float *C_final = NULL;
    float *A_part = (float *)malloc(rows_per_process * N * sizeof(float));
    float *C_part = (float *)malloc(rows_per_process * N * sizeof(float));

    // 进程 0 初始化矩阵 A 和 B
    if (rank == 0) {
        A = (float *)malloc(N * N * sizeof(float));
        C_final = (float *)malloc(N * N * sizeof(float));
        
        start_time = MPI_Wtime();
        initialize_matrix(A);
        initialize_matrix(B);
        end_time = MPI_Wtime();
        init_time = end_time - start_time;
    }

    // 广播 B 给所有进程
    MPI_Bcast(B, N * N, MPI_FLOAT, 0, MPI_COMM_WORLD);

    // 进程 0 分发 A 的部分数据
    MPI_Scatter(A, rows_per_process * N, MPI_FLOAT,
                A_part, rows_per_process * N, MPI_FLOAT,
                0, MPI_COMM_WORLD);

    // 开始矩阵乘法计算
    start_time = MPI_Wtime();
    matrix_multiplication(A_part, B, C_part, 0, rows_per_process);
    end_time = MPI_Wtime();
    compute_time = end_time - start_time;

    // 收集 C 的部分结果到 C_final
    MPI_Gather(C_part, rows_per_process * N, MPI_FLOAT,
               C_final, rows_per_process * N, MPI_FLOAT,
               0, MPI_COMM_WORLD);

    // 进程 0 输出运行时间
    if (rank == 0) {
        end_time = MPI_Wtime();
        gather_time = end_time - start_time;
        double total_time = init_time + compute_time + gather_time;
        printf("矩阵初始化时间: %.3f 秒\n", init_time);
        printf("矩阵乘法计算时间: %.3f 秒\n", compute_time);
        printf("结果收集时间: %.3f 秒\n", gather_time);
        printf("总运行时间: %.3f 秒\n", total_time);
    }

    // 释放内存
    free(B);
    free(A_part);
    free(C_part);
    if (rank == 0) {
        free(A);
        free(C_final);
    }

    // 结束 MPI 环境
    MPI_Finalize();
    return 0;
}
