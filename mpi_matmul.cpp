#include <iostream>
#include <vector>
#include <chrono>
#include <mpi.h>   // MPI 库

using namespace std;
using namespace std::chrono;

// 矩阵大小
const int N = 2048;

// 初始化矩阵
void initialize_matrices(vector<vector<float>> &A, vector<vector<float>> &B, vector<vector<float>> &C) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            A[i][j] = static_cast<float>(rand()) / RAND_MAX;
            B[i][j] = static_cast<float>(rand()) / RAND_MAX;
            C[i][j] = 0.0f;
        }
    }
}

// 矩阵乘法 (C = A * B)
void matrix_multiplication(const vector<vector<float>> &A, const vector<vector<float>> &B, vector<vector<float>> &C, int start_row, int end_row) {
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < N; k++) {
                sum += A[i][k] * B[k][j];
            }
            C[i][j] = sum;
        }
    }
}

// 性能测试
void performance_test(int rank, int size) {
    // 矩阵初始化
    vector<vector<float>> A(N, vector<float>(N));
    vector<vector<float>> B(N, vector<float>(N));
    vector<vector<float>> C(N, vector<float>(N));

    // 进程 0 初始化矩阵并广播 B
    if (rank == 0) {
        initialize_matrices(A, B, C);
    }

    // 广播矩阵 B 给所有进程
    for (int i = 0; i < N; i++) {
        MPI_Bcast(&B[i][0], N, MPI_FLOAT, 0, MPI_COMM_WORLD);
    }

    // 计算每个进程负责的行数
    int rows_per_process = N / size;
    int start_row = rank * rows_per_process;
    int end_row = (rank + 1) * rows_per_process;

    // 每个进程分配部分 A 和 C
    vector<vector<float>> A_part(rows_per_process, vector<float>(N));
    vector<vector<float>> C_part(rows_per_process, vector<float>(N, 0.0f));

    // 进程 0 分发 A 的部分数据
    if (rank == 0) {
        for (int i = 1; i < size; i++) {
            int start = i * rows_per_process;
            MPI_Send(&A[start][0], rows_per_process * N, MPI_FLOAT, i, 0, MPI_COMM_WORLD);
        }
        // 进程 0 自己的部分
        for (int i = 0; i < rows_per_process; i++) {
            for (int j = 0; j < N; j++) {
                A_part[i][j] = A[i][j];
            }
        }
    } else {
        // 接收 A 的部分数据
        MPI_Recv(&A_part[0][0], rows_per_process * N, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    // 开始计时
    auto start = high_resolution_clock::now();
    matrix_multiplication(A_part, B, C_part, 0, rows_per_process);
    auto end = high_resolution_clock::now();

    // 收集 C 的部分结果
    if (rank == 0) {
        for (int i = 0; i < rows_per_process; i++) {
            for (int j = 0; j < N; j++) {
                C[i][j] = C_part[i][j];
            }
        }
        for (int i = 1; i < size; i++) {
            int start = i * rows_per_process;
            MPI_Recv(&C[start][0], rows_per_process * N, MPI_FLOAT, i, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    } else {
        MPI_Send(&C_part[0][0], rows_per_process * N, MPI_FLOAT, 0, 1, MPI_COMM_WORLD);
    }

    // 计算运行时间
    double duration = duration_cast<duration<double>>(end - start).count();
    if (rank == 0) {
        cout << "矩阵乘法运行时间: " << duration << " 秒" << endl;

        // 计算内存带宽
        double memory_accessed = 3.0 * N * N * sizeof(float);  // A, B, C 矩阵
        double memory_bandwidth = (memory_accessed / (1024.0 * 1024.0 * 1024.0)) / duration;
        cout << "内存带宽: " << memory_bandwidth << " GB/s" << endl;

        // 计算 FLOPS
        double flops = 2.0 * N * N * N / duration;
        double gflops = flops / (1024.0 * 1024.0 * 1024.0);
        cout << "浮点运算性能: " << gflops << " GFLOPS" << endl;
    }
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (N % size != 0) {
        if (rank == 0) {
            cout << "错误: 矩阵行数无法被进程数整除！" << endl;
        }
        MPI_Finalize();
        return -1;
    }

    performance_test(rank, size);

    MPI_Finalize();
    return 0;
}
