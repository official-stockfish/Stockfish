#include "stockfish_blas.h"

#include "thread.h"

#include <cstring>
#include <random>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>

#if defined(USE_SSE2)
#include <xmmintrin.h>
#endif

#if defined (USE_SSE3)
#include <pmmintrin.h>
#endif

#if defined(USE_BLAS)
#include <cblas.h>
#endif

namespace Blas {
    void scopy(
        const int N,
        const float * SF_BLAS_RESTRICT X,
        float * SF_BLAS_RESTRICT Y
    )
    {
        std::memcpy(Y, X, sizeof(float) * N);
    }

    void scopy(
        const int N,
        const float * SF_BLAS_RESTRICT X, const int incX,
        float * SF_BLAS_RESTRICT Y, const int incY
    )
    {
        if (incX == 1 && incY == 1)
        {
            scopy(N, X, Y);
        }
        else
        {
            for(int i = 0; i < N; ++i)
            {
                *Y = *X;
                X += incX;
                Y += incY;
            }
        }
    }

    void scopy(
        ThreadPool&,
        const int N,
        const float * SF_BLAS_RESTRICT X,
        float * SF_BLAS_RESTRICT Y
    )
    {
        scopy(N, X, Y);
    }

    void scopy(
        ThreadPool&,
        const int N,
        const float * SF_BLAS_RESTRICT X, const int incX,
        float * SF_BLAS_RESTRICT Y, const int incY
    )
    {
        scopy(N, X, incX, Y, incY);
    }

    void sscal(
        const int N,
        const float alpha,
        float * SF_BLAS_RESTRICT X
    )
    {
#if defined (USE_SSE2)

        const __m128 alpha4 = _mm_set1_ps(alpha);

        int i = 0;
        for(; i < N - 31; i += 32)
        {
            __m128 x0 = _mm_loadu_ps(X + i +  0);
            __m128 x1 = _mm_loadu_ps(X + i +  4);
            __m128 x2 = _mm_loadu_ps(X + i +  8);
            __m128 x3 = _mm_loadu_ps(X + i + 12);
            __m128 x4 = _mm_loadu_ps(X + i + 16);
            __m128 x5 = _mm_loadu_ps(X + i + 20);
            __m128 x6 = _mm_loadu_ps(X + i + 24);
            __m128 x7 = _mm_loadu_ps(X + i + 28);

            x0 = _mm_mul_ps(x0, alpha4);
            x1 = _mm_mul_ps(x1, alpha4);
            x2 = _mm_mul_ps(x2, alpha4);
            x3 = _mm_mul_ps(x3, alpha4);
            x4 = _mm_mul_ps(x4, alpha4);
            x5 = _mm_mul_ps(x5, alpha4);
            x6 = _mm_mul_ps(x6, alpha4);
            x7 = _mm_mul_ps(x7, alpha4);

            _mm_storeu_ps(X + i +  0, x0);
            _mm_storeu_ps(X + i +  4, x1);
            _mm_storeu_ps(X + i +  8, x2);
            _mm_storeu_ps(X + i + 12, x3);
            _mm_storeu_ps(X + i + 16, x4);
            _mm_storeu_ps(X + i + 20, x5);
            _mm_storeu_ps(X + i + 24, x6);
            _mm_storeu_ps(X + i + 28, x7);
        }

        for(; i < N; ++i)
        {
            X[i] *= alpha;
        }

#else

        for(int i = 0; i < N; ++i)
        {
            X[i] *= alpha;
        }

#endif
    }

    void sscal(
        const int N,
        const float alpha,
        float * SF_BLAS_RESTRICT X, const int incX
    )
    {
        if (incX == 1)
        {
            sscal(N, alpha, X);
        }
        else
        {
            for(int i = 0; i < N; ++i)
            {
                *X *= alpha;
                X += incX;
            }
        }
    }

    void sscal(
        ThreadPool&,
        const int N,
        const float alpha,
        float * SF_BLAS_RESTRICT X
    )
    {
        sscal(N, alpha, X);
    }

    void sscal(
        ThreadPool&,
        const int N,
        const float alpha,
        float *X, const int incX
    )
    {
        sscal(N, alpha, X, incX);
    }

    void saxpy(
        const int N,
        const float alpha,
        const float * SF_BLAS_RESTRICT X,
        float * SF_BLAS_RESTRICT Y
    )
    {

        for(int i = 0; i < N; ++i)
        {
            Y[i] += X[i] * alpha;
        }

    }

    void saxpy(
        const int N,
        const float alpha,
        const float * SF_BLAS_RESTRICT X, const int incX,
        float * SF_BLAS_RESTRICT Y, const int incY
    )
    {
        if (incX == 1 && incY == 1)
        {
            saxpy(N, alpha, X, Y);
        }
        else
        {
            for(int i = 0; i < N; ++i)
            {
                *Y += *X * alpha;
                Y += incY;
                X += incX;
            }
        }
    }

    void saxpy(
        ThreadPool&,
        const int N,
        const float alpha,
        const float * SF_BLAS_RESTRICT X,
        float * SF_BLAS_RESTRICT Y
    )
    {
        saxpy(N, alpha, X, Y);
    }

    void saxpy(
        ThreadPool&,
        const int N,
        const float alpha,
        const float * SF_BLAS_RESTRICT X, const int incX,
        float * SF_BLAS_RESTRICT Y, const int incY
    )
    {
        saxpy(N, alpha, X, incX, Y, incY);
    }

#if defined (USE_SSE3)
    inline __m128 m128_hadd_ps(__m128 a, __m128 b, __m128 c, __m128 d)
    {
        const __m128 t0 = _mm_hadd_ps(a, b);
        const __m128 t1 = _mm_hadd_ps(c, d);
        return _mm_hadd_ps(t0, t1);
    }
#endif

#if defined (USE_SSE2)

    inline void transpose4x4_sse2(
        const float* SF_BLAS_RESTRICT A, const int lda,
        float* SF_BLAS_RESTRICT B, const int ldb
    )
    {
        __m128 row1 = _mm_loadu_ps(&A[0 * lda]);
        __m128 row2 = _mm_loadu_ps(&A[1 * lda]);
        __m128 row3 = _mm_loadu_ps(&A[2 * lda]);
        __m128 row4 = _mm_loadu_ps(&A[3 * lda]);

        _MM_TRANSPOSE4_PS(row1, row2, row3, row4);

        _mm_storeu_ps(&B[0 * ldb], row1);
        _mm_storeu_ps(&B[1 * ldb], row2);
        _mm_storeu_ps(&B[2 * ldb], row3);
        _mm_storeu_ps(&B[3 * ldb], row4);
    }

    void transpose_sse2(
        const int N, const int M,
        const float* SF_BLAS_RESTRICT A, const int lda,
        float* SF_BLAS_RESTRICT B, const int ldb
    )
    {
        static constexpr int block_size = 16;

        for (int n = 0; n < N; n += block_size)
        {
            for (int m = 0; m < M; m += block_size)
            {
                const int max_n2 = n + block_size < N ? n + block_size : N;
                const int max_m2 = m + block_size < M ? m + block_size : M;

                int n2 = n;
                for (; n2 < max_n2 - 3; n2 += 4)
                {
                    int m2 = m;
                    for (; m2 < max_m2 - 3; m2 += 4)
                    {
                        transpose4x4_sse2(
                            &A[n2 * lda + m2], lda,
                            &B[m2 * ldb + n2], ldb
                        );
                    }

                    for (; m2 < max_m2; ++m2)
                    {
                        B[m2 * ldb + n2 + 0] = A[(n2 + 0) * lda + m2];
                        B[m2 * ldb + n2 + 1] = A[(n2 + 1) * lda + m2];
                        B[m2 * ldb + n2 + 2] = A[(n2 + 2) * lda + m2];
                        B[m2 * ldb + n2 + 3] = A[(n2 + 3) * lda + m2];
                    }
                }

                for (; n2 < max_n2; ++n2)
                {
                    for (int m2 = m; m2 < max_m2; ++m2)
                    {
                        B[m2 * ldb + n2] = A[n2 * lda + m2];
                    }
                }
            }
        }
    }
#endif

    void transpose(
        const int N, const int M,
        const float * SF_BLAS_RESTRICT A, const int lda,
        float* SF_BLAS_RESTRICT B, const int ldb
    )
    {
#if defined (USE_SSE2)

        transpose_sse2(
            N, M,
            A, lda,
            B, ldb
        );

#else

        for(int r = 0; r < N; ++r)
        {
            for (int c = 0; c < M; ++c)
            {
                B[c*ldb + r] = A[r*lda + c];
            }
        }

#endif
    }

    void sgemm_row_major_transpose_right(
        ThreadPool& thread_pool,
        const int M, const int N, const int K,
        const float alpha,
        const float * SF_BLAS_RESTRICT A, const int lda,
        const float * SF_BLAS_RESTRICT B, const int ldb,
        const float beta,
        float * SF_BLAS_RESTRICT C, const int ldc
    )
    {

#if defined(USE_SSE3)

        const __m128 alpha4 = _mm_set1_ps(alpha);
        const __m128 beta4 = _mm_set1_ps(beta);

        std::atomic<int> m_atomic = 0;
        thread_pool.execute_with_workers(
            [
                M, N, K,
                alpha, alpha4,
                A, lda,
                B, ldb,
                beta, beta4,
                C, ldc,
                &m_atomic
            ](Thread&) {
                for (;;)
                {
                    const int m = m_atomic.fetch_add(2);
                    if (m >= M - 1)
                        break;

                    int n = 0;
                    for (; n < N - 3; n += 4)
                    {
                        //        mn
                        __m128 sum00 = _mm_setzero_ps();
                        __m128 sum01 = _mm_setzero_ps();
                        __m128 sum02 = _mm_setzero_ps();
                        __m128 sum03 = _mm_setzero_ps();
                        __m128 sum10 = _mm_setzero_ps();
                        __m128 sum11 = _mm_setzero_ps();
                        __m128 sum12 = _mm_setzero_ps();
                        __m128 sum13 = _mm_setzero_ps();

                        // Horizontal sum of elements in sum[m][n] corresponds to
                        // the final element in the C.

                        int k = 0;
                        for (; k < K - 3; k += 4)
                        {
                            const __m128 a0 = _mm_loadu_ps(&A[(m+0)*lda+k+0]);
                            const __m128 a1 = _mm_loadu_ps(&A[(m+1)*lda+k+0]);

                            const __m128 b0 = _mm_loadu_ps(&B[(n+0)*ldb+k+0]);
                            const __m128 b1 = _mm_loadu_ps(&B[(n+1)*ldb+k+0]);
                            const __m128 b2 = _mm_loadu_ps(&B[(n+2)*ldb+k+0]);
                            const __m128 b3 = _mm_loadu_ps(&B[(n+3)*ldb+k+0]);

                            sum00 = _mm_add_ps(sum00, _mm_mul_ps(a0, b0));
                            sum01 = _mm_add_ps(sum01, _mm_mul_ps(a0, b1));
                            sum02 = _mm_add_ps(sum02, _mm_mul_ps(a0, b2));
                            sum03 = _mm_add_ps(sum03, _mm_mul_ps(a0, b3));
                            sum10 = _mm_add_ps(sum10, _mm_mul_ps(a1, b0));
                            sum11 = _mm_add_ps(sum11, _mm_mul_ps(a1, b1));
                            sum12 = _mm_add_ps(sum12, _mm_mul_ps(a1, b2));
                            sum13 = _mm_add_ps(sum13, _mm_mul_ps(a1, b3));
                        }

                        for(; k < K; k += 1)
                        {
                            const float a0 = A[(m+0)*lda+k+0];
                            const float a1 = A[(m+1)*lda+k+0];

                            const float b0 = B[(n+0)*ldb+k+0];
                            const float b1 = B[(n+1)*ldb+k+0];
                            const float b2 = B[(n+2)*ldb+k+0];
                            const float b3 = B[(n+3)*ldb+k+0];

                            // Since all will be summed vertically anyway we can
                            // just add to the first element.
                            // Other elements are left unmodified.
                            sum00 = _mm_add_ss(sum00, _mm_set_ss(a0 * b0));
                            sum01 = _mm_add_ss(sum01, _mm_set_ss(a0 * b1));
                            sum02 = _mm_add_ss(sum02, _mm_set_ss(a0 * b2));
                            sum03 = _mm_add_ss(sum03, _mm_set_ss(a0 * b3));
                            sum10 = _mm_add_ss(sum10, _mm_set_ss(a1 * b0));
                            sum11 = _mm_add_ss(sum11, _mm_set_ss(a1 * b1));
                            sum12 = _mm_add_ss(sum12, _mm_set_ss(a1 * b2));
                            sum13 = _mm_add_ss(sum13, _mm_set_ss(a1 * b3));
                        }

                        __m128 s0 = m128_hadd_ps(sum00, sum01, sum02, sum03);
                        __m128 s1 = m128_hadd_ps(sum10, sum11, sum12, sum13);
                        s0 = _mm_mul_ps(s0, alpha4);
                        s1 = _mm_mul_ps(s1, alpha4);

                        __m128 c0 = _mm_loadu_ps(&C[(m+0)*ldc+(n+0)]);
                        __m128 c1 = _mm_loadu_ps(&C[(m+1)*ldc+(n+0)]);
                        c0 = _mm_mul_ps(c0, beta4);
                        c1 = _mm_mul_ps(c1, beta4);

                        c0 = _mm_add_ps(c0, s0);
                        c1 = _mm_add_ps(c1, s1);

                        _mm_storeu_ps(&C[(m+0)*ldc+(n+0)], c0);
                        _mm_storeu_ps(&C[(m+1)*ldc+(n+0)], c1);
                    }

                    for(; n < N; n += 1)
                    {
                        float sum0 = 0.0f;
                        float sum1 = 0.0f;

                        for (int k = 0; k < K; ++k)
                        {
                            const float a0 = A[(m+0)*lda+k+0];
                            const float a1 = A[(m+1)*lda+k+0];

                            const float b0 = B[(n+0)*ldb+k+0];

                            sum0 += a0 * b0;
                            sum1 += a1 * b0;
                        }

                        C[(m+0)*ldc+(n+0)] = C[(m+0)*ldc+(n+0)] * beta + sum0 * alpha;
                        C[(m+1)*ldc+(n+0)] = C[(m+1)*ldc+(n+0)] * beta + sum1 * alpha;
                    }
                }
            }
        );

        int m = M - (M % 2);
        for (; m < M; m += 1)
        {
            for (int n = 0; n < N; n += 1)
            {
                float sum = 0.0f;

                for (int k = 0; k < K; k += 1)
                {
                    sum += A[m*lda + k] * B[n*ldb + k];
                }

                C[m*ldc + n] = C[m*ldc + n] * beta + sum * alpha;
            }
        }

        thread_pool.wait_for_workers_finished();

#else

        thread_pool.for_each_index_with_workers(
            0, M,
            [&](Thread&, int m) {
                for (int n = 0; n < N; n += 1)
                {
                    float sum = 0.0f;

                    for (int k = 0; k < K; k += 1)
                    {
                        sum += A[m*lda + k] * B[n*ldb + k];
                    }

                    C[m*ldc + n] = C[m*ldc + n] * beta + sum * alpha;
                }
            }
        );
        thread_pool.wait_for_workers_finished();

#endif
    }

    void sgemm_row_major_transpose_right(
        const int M, const int N, const int K,
        const float alpha,
        const float * SF_BLAS_RESTRICT A, const int lda,
        const float * SF_BLAS_RESTRICT B, const int ldb,
        const float beta,
        float * SF_BLAS_RESTRICT C, const int ldc
    )
    {

#if defined(USE_SSE3)

        const __m128 alpha4 = _mm_set1_ps(alpha);
        const __m128 beta4 = _mm_set1_ps(beta);

        int m = 0;
        for (; m < M - 1; m += 2)
        {
            int n = 0;
            for (; n < N - 3; n += 4)
            {
                //        mn
                __m128 sum00 = _mm_setzero_ps();
                __m128 sum01 = _mm_setzero_ps();
                __m128 sum02 = _mm_setzero_ps();
                __m128 sum03 = _mm_setzero_ps();
                __m128 sum10 = _mm_setzero_ps();
                __m128 sum11 = _mm_setzero_ps();
                __m128 sum12 = _mm_setzero_ps();
                __m128 sum13 = _mm_setzero_ps();

                // Horizontal sum of elements in sum[m][n] corresponds to
                // the final element in the C.

                int k = 0;
                for (; k < K - 3; k += 4)
                {
                    const __m128 a0 = _mm_loadu_ps(&A[(m+0)*lda+k+0]);
                    const __m128 a1 = _mm_loadu_ps(&A[(m+1)*lda+k+0]);

                    const __m128 b0 = _mm_loadu_ps(&B[(n+0)*ldb+k+0]);
                    const __m128 b1 = _mm_loadu_ps(&B[(n+1)*ldb+k+0]);
                    const __m128 b2 = _mm_loadu_ps(&B[(n+2)*ldb+k+0]);
                    const __m128 b3 = _mm_loadu_ps(&B[(n+3)*ldb+k+0]);

                    sum00 = _mm_add_ps(sum00, _mm_mul_ps(a0, b0));
                    sum01 = _mm_add_ps(sum01, _mm_mul_ps(a0, b1));
                    sum02 = _mm_add_ps(sum02, _mm_mul_ps(a0, b2));
                    sum03 = _mm_add_ps(sum03, _mm_mul_ps(a0, b3));
                    sum10 = _mm_add_ps(sum10, _mm_mul_ps(a1, b0));
                    sum11 = _mm_add_ps(sum11, _mm_mul_ps(a1, b1));
                    sum12 = _mm_add_ps(sum12, _mm_mul_ps(a1, b2));
                    sum13 = _mm_add_ps(sum13, _mm_mul_ps(a1, b3));
                }

                for(; k < K; k += 1)
                {
                    const float a0 = A[(m+0)*lda+k+0];
                    const float a1 = A[(m+1)*lda+k+0];

                    const float b0 = B[(n+0)*ldb+k+0];
                    const float b1 = B[(n+1)*ldb+k+0];
                    const float b2 = B[(n+2)*ldb+k+0];
                    const float b3 = B[(n+3)*ldb+k+0];

                    // Since all will be summed vertically anyway we can
                    // just add to the first element.
                    // Other elements are left unmodified.
                    sum00 = _mm_add_ss(sum00, _mm_set_ss(a0 * b0));
                    sum01 = _mm_add_ss(sum01, _mm_set_ss(a0 * b1));
                    sum02 = _mm_add_ss(sum02, _mm_set_ss(a0 * b2));
                    sum03 = _mm_add_ss(sum03, _mm_set_ss(a0 * b3));
                    sum10 = _mm_add_ss(sum10, _mm_set_ss(a1 * b0));
                    sum11 = _mm_add_ss(sum11, _mm_set_ss(a1 * b1));
                    sum12 = _mm_add_ss(sum12, _mm_set_ss(a1 * b2));
                    sum13 = _mm_add_ss(sum13, _mm_set_ss(a1 * b3));
                }

                __m128 s0 = m128_hadd_ps(sum00, sum01, sum02, sum03);
                __m128 s1 = m128_hadd_ps(sum10, sum11, sum12, sum13);
                s0 = _mm_mul_ps(s0, alpha4);
                s1 = _mm_mul_ps(s1, alpha4);

                __m128 c0 = _mm_loadu_ps(&C[(m+0)*ldc+(n+0)]);
                __m128 c1 = _mm_loadu_ps(&C[(m+1)*ldc+(n+0)]);
                c0 = _mm_mul_ps(c0, beta4);
                c1 = _mm_mul_ps(c1, beta4);

                c0 = _mm_add_ps(c0, s0);
                c1 = _mm_add_ps(c1, s1);

                _mm_storeu_ps(&C[(m+0)*ldc+(n+0)], c0);
                _mm_storeu_ps(&C[(m+1)*ldc+(n+0)], c1);
            }

            for(; n < N; n += 1)
            {
                float sum0 = 0.0f;
                float sum1 = 0.0f;

                for (int k = 0; k < K; ++k)
                {
                    const float a0 = A[(m+0)*lda+k+0];
                    const float a1 = A[(m+1)*lda+k+0];

                    const float b0 = B[(n+0)*ldb+k+0];

                    sum0 += a0 * b0;
                    sum1 += a1 * b0;
                }

                C[(m+0)*ldc+(n+0)] = C[(m+0)*ldc+(n+0)] * beta + sum0 * alpha;
                C[(m+1)*ldc+(n+0)] = C[(m+1)*ldc+(n+0)] * beta + sum1 * alpha;
            }
        }

        for (; m < M; m += 1)
        {
            for (int n = 0; n < N; n += 1)
            {
                float sum = 0.0f;

                for (int k = 0; k < K; k += 1)
                {
                    sum += A[m*lda + k] * B[n*ldb + k];
                }

                C[m*ldc + n] = C[m*ldc + n] * beta + sum * alpha;
            }
        }

#else

        for (int m = 0; m < M; m += 1)
        {
            for (int n = 0; n < N; n += 1)
            {
                float sum = 0.0f;

                for (int k = 0; k < K; k += 1)
                {
                    sum += A[m*lda + k] * B[n*ldb + k];
                }

                C[m*ldc + n] = C[m*ldc + n] * beta + sum * alpha;
            }
        }

#endif
    }

    // The pointer to the storage returned by this function
    // is valid until the next call to this function from
    // the same thread with the same idx.
    // This is an unsafe function and should be used with caution
    // and only within this translation unit.
    // The number of buffers available is just enough to make
    // all functions here work.
    float* get_thread_local_temporary_storage(
        int requested_size, int idx
    )
    {
        static constexpr int MAX_NUM_BUFFERS = 2;

        static thread_local int s_data_size[MAX_NUM_BUFFERS] = {0};
        static thread_local std::unique_ptr<float[]> s_data[MAX_NUM_BUFFERS];

        if (requested_size > s_data_size[idx])
        {
            s_data[idx] = std::make_unique<float[]>(requested_size);
            s_data_size[idx] = requested_size;
        }

        return s_data[idx].get();
    }

    void sgemm_row_major_transpose_none(
        ThreadPool& thread_pool,
        const int M, const int N, const int K,
        const float alpha,
        const float * SF_BLAS_RESTRICT A, const int lda,
        const float * SF_BLAS_RESTRICT B, const int ldb,
        const float beta,
        float * SF_BLAS_RESTRICT C, const int ldc
    )
    {
        constexpr static int temporary_buffer_index = 1;

        auto B_tr = get_thread_local_temporary_storage(K * N, temporary_buffer_index);

        transpose(
            K, N,
            B, ldb,
            B_tr, K
        );

        sgemm_row_major_transpose_right(
            thread_pool,
            M, N, K,
            alpha,
            A, lda,
            B_tr, K,
            beta,
            C, ldc
        );
    }

    void sgemm_row_major_transpose_none(
        const int M, const int N, const int K,
        const float alpha,
        const float * SF_BLAS_RESTRICT A, const int lda,
        const float * SF_BLAS_RESTRICT B, const int ldb,
        const float beta,
        float * SF_BLAS_RESTRICT C, const int ldc
    )
    {
        constexpr static int temporary_buffer_index = 1;

        auto B_tr = get_thread_local_temporary_storage(K * N, temporary_buffer_index);

        transpose(
            K, N,
            B, ldb,
            B_tr, K
        );

        sgemm_row_major_transpose_right(
            M, N, K,
            alpha,
            A, lda,
            B_tr, K,
            beta,
            C, ldc
        );
    }

    void sgemm_row_major(
        ThreadPool& thread_pool,
        MatrixTranspose TransA, MatrixTranspose TransB,
        const int M, const int N, const int K,
        const float alpha,
        const float * SF_BLAS_RESTRICT A, const int lda,
        const float * SF_BLAS_RESTRICT B, const int ldb,
        const float beta,
        float * SF_BLAS_RESTRICT C, const int ldc
    )
    {
        constexpr static int temporary_buffer_index = 0;

        if (TransA == MatrixTranspose::Trans && TransB == MatrixTranspose::Trans)
        {
            auto A_tr = get_thread_local_temporary_storage(K * M, temporary_buffer_index);

            transpose(
                K, M,
                A, lda,
                A_tr, K
            );

            sgemm_row_major_transpose_right(
                thread_pool,
                M, N, K,
                alpha,
                A_tr, K,
                B, ldb,
                beta,
                C, ldc
            );
        }
        else if (TransA == MatrixTranspose::NoTrans && TransB == MatrixTranspose::Trans)
        {
            sgemm_row_major_transpose_right(
                thread_pool,
                M, N, K,
                alpha,
                A, lda,
                B, ldb,
                beta,
                C, ldc
            );
        }
        else if (TransA == MatrixTranspose::Trans && TransB == MatrixTranspose::NoTrans)
        {
            auto A_tr = get_thread_local_temporary_storage(K * M, temporary_buffer_index);

            transpose(
                K, M,
                A, lda,
                A_tr, K
            );

            sgemm_row_major_transpose_none(
                thread_pool,
                M, N, K,
                alpha,
                A_tr, K,
                B, ldb,
                beta,
                C, ldc
            );
        }
        else // no transpositions
        {
            sgemm_row_major_transpose_none(
                thread_pool,
                M, N, K,
                alpha,
                A, lda,
                B, ldb,
                beta,
                C, ldc
            );
        }
    }

    void sgemm_row_major(
        MatrixTranspose TransA, MatrixTranspose TransB,
        const int M, const int N, const int K,
        const float alpha,
        const float * SF_BLAS_RESTRICT A, const int lda,
        const float * SF_BLAS_RESTRICT B, const int ldb,
        const float beta,
        float * SF_BLAS_RESTRICT C, const int ldc
    )
    {
        constexpr static int temporary_buffer_index = 0;

        if (TransA == MatrixTranspose::Trans && TransB == MatrixTranspose::Trans)
        {
            auto A_tr = get_thread_local_temporary_storage(K * M, temporary_buffer_index);

            transpose(
                K, M,
                A, lda,
                A_tr, K
            );

            sgemm_row_major_transpose_right(
                M, N, K,
                alpha,
                A_tr, K,
                B, ldb,
                beta,
                C, ldc
            );
        }
        else if (TransA == MatrixTranspose::NoTrans && TransB == MatrixTranspose::Trans)
        {
            sgemm_row_major_transpose_right(
                M, N, K,
                alpha,
                A, lda,
                B, ldb,
                beta,
                C, ldc
            );
        }
        else if (TransA == MatrixTranspose::Trans && TransB == MatrixTranspose::NoTrans)
        {
            auto A_tr = get_thread_local_temporary_storage(K * M, temporary_buffer_index);

            transpose(
                K, M,
                A, lda,
                A_tr, K
            );

            sgemm_row_major_transpose_none(
                M, N, K,
                alpha,
                A_tr, K,
                B, ldb,
                beta,
                C, ldc
            );
        }
        else // no transpositions
        {
            sgemm_row_major_transpose_none(
                M, N, K,
                alpha,
                A, lda,
                B, ldb,
                beta,
                C, ldc
            );
        }
    }

    void sgemm(
        ThreadPool& thread_pool,
        MatrixLayout layout, MatrixTranspose TransA, MatrixTranspose TransB,
        const int M, const int N, const int K,
        const float alpha,
        const float * SF_BLAS_RESTRICT A, const int lda,
        const float * SF_BLAS_RESTRICT B, const int ldb,
        const float beta,
        float * SF_BLAS_RESTRICT C, const int ldc
    )
    {
        if (layout == MatrixLayout::RowMajor)
        {
            sgemm_row_major(
                thread_pool,
                TransA, TransB,
                M, N, K,
                alpha,
                A, lda,
                B, ldb,
                beta,
                C, ldc
            );
        }
        else
        {
            sgemm_row_major(
                thread_pool,
                TransB, TransA,
                N, M, K,
                alpha,
                B, ldb,
                A, lda,
                beta,
                C, ldc
            );
        }
    }


    void sgemm(
        MatrixLayout layout, MatrixTranspose TransA, MatrixTranspose TransB,
        const int M, const int N, const int K,
        const float alpha,
        const float * SF_BLAS_RESTRICT A, const int lda,
        const float * SF_BLAS_RESTRICT B, const int ldb,
        const float beta,
        float * SF_BLAS_RESTRICT C, const int ldc
    )
    {
        if (layout == MatrixLayout::RowMajor)
        {
            sgemm_row_major(
                TransA, TransB,
                M, N, K,
                alpha,
                A, lda,
                B, ldb,
                beta,
                C, ldc
            );
        }
        else
        {
            sgemm_row_major(
                TransB, TransA,
                N, M, K,
                alpha,
                B, ldb,
                A, lda,
                beta,
                C, ldc
            );
        }
    }

    std::vector<float> generate_random_matrix(int rows, int cols)
    {
        std::vector<float> m(rows * cols);

        std::mt19937_64 rng;
        std::uniform_real_distribution<float> d(-1.0, 1.0);

        for(auto& v : m)
        {
            v = d(rng);
        }

        return m;
    }

    std::vector<float> generate_zero_matrix(int rows, int cols)
    {
        return std::vector<float>(rows * cols, 0.0f);
    }

    float matrix_relative_error(
        const std::vector<float>& ref,
        const std::vector<float>& our
    )
    {
        double sum = 0.0;
        double diff_sum = 0.0;

        for(size_t i = 0; i < ref.size(); ++i)
        {
            sum += std::abs(ref[i]);
            diff_sum += std::abs(ref[i] - our[i]);
        }

        return diff_sum / sum;
    }

    float norm(
        const std::vector<float>& v
    )
    {
        double sum = 0.0;

        for(auto& e : v)
        {
            sum += e * e;
        }

        return std::sqrt(sum);
    }

#if defined (USE_BLAS)

    CBLAS_LAYOUT matrix_layout_to_blas_layout(MatrixLayout layout)
    {
        if (layout == MatrixLayout::RowMajor)
            return CblasRowMajor;
        else if (layout == MatrixLayout::ColMajor)
            return CblasColMajor;

        return static_cast<CBLAS_LAYOUT>(-1);
    }

    const char* matrix_layout_to_string(MatrixLayout layout)
    {
        if (layout == MatrixLayout::RowMajor)
            return "RowMajor";
        else if (layout == MatrixLayout::ColMajor)
            return "ColMajor";

        return "INVALID";
    }

    CBLAS_TRANSPOSE matrix_transpose_to_blas_transpose(MatrixTranspose tr)
    {
        if (tr == MatrixTranspose::NoTrans)
            return CblasNoTrans;
        else if (tr == MatrixTranspose::Trans)
            return CblasTrans;

        return static_cast<CBLAS_TRANSPOSE>(-1);
    }

    const char* matrix_transpose_to_string(MatrixTranspose tr)
    {
        if (tr == MatrixTranspose::NoTrans)
            return "NoTrans";
        else if (tr == MatrixTranspose::Trans)
            return "Trans";

        return "INVALID";
    }

    void test_sgemm(
        ThreadPool& thread_pool,
        MatrixLayout layout, MatrixTranspose trA, MatrixTranspose trB,
        int M, int N, int K
    )
    {
        auto A = generate_random_matrix(M * 2, K * 2);
        auto B = generate_random_matrix(K * 2, N * 2);
        auto C_ref = generate_random_matrix(M * 2, N * 2);
        auto C_our = C_ref;

        std::cout
            << matrix_layout_to_string(layout) << ' '
            << matrix_transpose_to_string(trA) << ' '
            << matrix_transpose_to_string(trB) << '\n';

        std::cout << "A norm: " << norm(A) << '\n';
        std::cout << "B norm: " << norm(B) << '\n';
        std::cout << "C norm: " << norm(C_ref) << '\n';

        const int lda = (trA == MatrixTranspose::NoTrans) == (layout == MatrixLayout::RowMajor) ? K * 2 : M * 2;
        const int ldb = (trB == MatrixTranspose::NoTrans) == (layout == MatrixLayout::RowMajor) ? N * 2 : K * 2;
        const int ldc = (layout == MatrixLayout::RowMajor) ? N * 2 : M * 2;

        cblas_sgemm(
            matrix_layout_to_blas_layout(layout),
            matrix_transpose_to_blas_transpose(trA),
            matrix_transpose_to_blas_transpose(trB),
            M, N, K,
            1.0,
            A.data(), lda,
            B.data(), ldb,
            1.0,
            C_ref.data(), ldc
        );

        sgemm(
            thread_pool,
            layout, trA, trB,
            M, N, K,
            1.0,
            A.data(), lda,
            B.data(), ldb,
            1.0,
            C_our.data(), ldc
        );

        std::cout << "C_ref norm: " << norm(C_ref) << '\n';
        std::cout << "C_our norm: " << norm(C_our) << '\n';
        std::cout << "Relative error: " << matrix_relative_error(C_ref, C_our) << '\n';

        std::cout << '\n';
    }

    void test_sgemm(
        ThreadPool& thread_pool
    )
    {
        constexpr int M = 57;
        constexpr int N = 127;
        constexpr int K = 31;

        std::cout << "SGEMM test:\n";

        for(auto layout : { MatrixLayout::RowMajor, MatrixLayout::ColMajor })
        {
            for(auto trA : { MatrixTranspose::NoTrans, MatrixTranspose::Trans })
            {
                for(auto trB : { MatrixTranspose::NoTrans, MatrixTranspose::Trans })
                {
                    test_sgemm(
                        thread_pool,
                        layout, trA, trB,
                        M, N, K
                    );
                }
            }
        }
    }

    void bench_sgemm(
        ThreadPool& thread_pool,
        MatrixLayout layout, MatrixTranspose trA, MatrixTranspose trB,
        int M, int N, int K
    )
    {
        constexpr int num_iters = 1000;

        auto A = generate_random_matrix(M * 2, K * 2);
        auto B = generate_random_matrix(K * 2, N * 2);
        auto C_ref = generate_random_matrix(M * 2, N * 2);
        auto C_our = C_ref;

        std::cout
            << matrix_layout_to_string(layout) << ' '
            << matrix_transpose_to_string(trA) << ' '
            << matrix_transpose_to_string(trB) << '\n';

        std::cout << "A norm: " << norm(A) << '\n';
        std::cout << "B norm: " << norm(B) << '\n';
        std::cout << "C norm: " << norm(C_ref) << '\n';

        const int lda = (trA == MatrixTranspose::NoTrans) == (layout == MatrixLayout::RowMajor) ? K * 2 : M * 2;
        const int ldb = (trB == MatrixTranspose::NoTrans) == (layout == MatrixLayout::RowMajor) ? N * 2 : K * 2;
        const int ldc = (layout == MatrixLayout::RowMajor) ? N * 2 : M * 2;

        auto t0_ref = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < num_iters; ++i)
        {
            cblas_sgemm(
                matrix_layout_to_blas_layout(layout),
                matrix_transpose_to_blas_transpose(trA),
                matrix_transpose_to_blas_transpose(trB),
                M, N, K,
                1.0,
                A.data(), lda,
                B.data(), ldb,
                -0.5,
                C_ref.data(), ldc
            );
        }
        auto t1_ref = std::chrono::high_resolution_clock::now();
        auto diff_ref = t1_ref - t0_ref;

        auto t0_our = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < num_iters; ++i)
        {
            sgemm(
                thread_pool,
                layout, trA, trB,
                M, N, K,
                1.0,
                A.data(), lda,
                B.data(), ldb,
                -0.5,
                C_our.data(), ldc
            );
        }
        auto t1_our = std::chrono::high_resolution_clock::now();
        auto diff_our = t1_our - t0_our;

        std::cout << "C_ref norm: " << norm(C_ref) << '\n';
        std::cout << "C_our norm: " << norm(C_our) << '\n';
        std::cout << "Relative error: " << matrix_relative_error(C_ref, C_our) << '\n';
        std::cout << "Ref time: " << std::chrono::duration_cast<std::chrono::nanoseconds>(diff_ref).count() << " [ns]\n";
        std::cout << "Our time: " << std::chrono::duration_cast<std::chrono::nanoseconds>(diff_our).count() << " [ns]\n";

        std::cout << '\n';
    }

    void bench_sgemm(
        ThreadPool& thread_pool
    )
    {
        constexpr int M = 107;
        constexpr int N = 213;
        constexpr int K = 57;

        std::cout << "SGEMM benchmark:\n";

        for(auto layout : { MatrixLayout::RowMajor, MatrixLayout::ColMajor })
        {
            for(auto trA : { MatrixTranspose::NoTrans, MatrixTranspose::Trans })
            {
                for(auto trB : { MatrixTranspose::NoTrans, MatrixTranspose::Trans })
                {
                    bench_sgemm(
                        thread_pool,
                        layout, trA, trB,
                        M, N, K
                    );
                }
            }
        }
    }

#endif

    void print_arch()
    {
#if defined (USE_SSE3)
        std::cout << "Using the sse3 implementation.\n";
#elif defined (USE_SSE2)
        std::cout << "Using the sse2 implementation.\n";
#else
        std::cout << "Using the base implementation.\n";
#endif
    }

    void test(
        ThreadPool& thread_pool
    )
    {
#if defined (USE_BLAS)
        print_arch();
        test_sgemm(thread_pool);
#else
        std::cout << "Blas tests are only runnable when USE_BLAS is defined.\n";
        (void)thread_pool;
#endif
    }

    void bench(
        ThreadPool& thread_pool
    )
    {
#if defined (USE_BLAS)
        print_arch();
        bench_sgemm(thread_pool);
#else
        std::cout << "Blas benchmarks are only runnable when USE_BLAS is defined.\n";
        (void)thread_pool;
#endif
    }
}