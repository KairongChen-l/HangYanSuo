#include <iostream>
#include <vector>
#include <chrono>
#include <arm_neon.h>  // ARM64 NEON 指令
#include <omp.h>      // OpenMP 并行化

using namespace std;
using namespace std::chrono;

// 矩阵大小和块大小
const int N = 2048;
const int BLOCK_SIZE = 64;

// 初始化矩阵
void initialize_matrices(vector<vector<float>> &A, vector<vector<float>> &B, vector<vector<float>> &C) {
    #pragma omp parallel for
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            A[i][j] = static_cast<float>(rand()) / RAND_MAX;
            B[i][j] = static_cast<float>(rand()) / RAND_MAX;
            C[i][j] = 0.0f;
        }
    }
}

// 使用 NEON 进行矩阵乘法 (C = A * B)
void matrix_multiplication(const vector<vector<float>> &A, const vector<vector<float>> &B, vector<vector<float>> &C) {
    #pragma omp parallel for collapse(2)
    for (int i = 0; i < N; i += BLOCK_SIZE) {
        for (int j = 0; j < N; j += BLOCK_SIZE) {
            for (int k = 0; k < N; k += BLOCK_SIZE) {
                for (int ii = i; ii < i + BLOCK_SIZE; ii++) {
                    for (int jj = j; jj < j + BLOCK_SIZE; jj++) {
                        float32x4_t c_vec = vld1q_f32(&C[ii][jj]);
                        for (int kk = k; kk < k + BLOCK_SIZE; kk += 4) {
                            float32x4_t a_vec = vld1q_f32(&A[ii][kk]);
                            float32x4_t b_vec = vld1q_f32(&B[kk][jj]);
                            c_vec = vmlaq_f32(c_vec, a_vec, b_vec);
                        }
                        vst1q_f32(&C[ii][jj], c_vec);
                    }
                }
            }
        }
    }
}

// 性能测试
void performance_test() {
    // 初始化矩阵
    vector<vector<float>> A(N, vector<float>(N));
    vector<vector<float>> B(N, vector<float>(N));
    vector<vector<float>> C(N, vector<float>(N));
    initialize_matrices(A, B, C);

    // 开始计时
    auto start = high_resolution_clock::now();
    matrix_multiplication(A, B, C);
    auto end = high_resolution_clock::now();

    // 计算运行时间
    double duration = duration_cast<duration<double>>(end - start).count();
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

int main() {
    cout << "矩阵大小: " << N << " x " << N << endl;
    cout << "块大小: " << BLOCK_SIZE << endl;
    performance_test();
    return 0;
}
