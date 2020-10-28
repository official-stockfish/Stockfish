#ifndef _STOCKFISH_BLAS_H_
#define _STOCKFISH_BLAS_H_

struct ThreadPool;

#if defined (_MSC_VER)
#define SF_BLAS_RESTRICT __restrict
#elif defined (__INTEL_COMPILER)
#define SF_BLAS_RESTRICT restrict
#elif defined (__clang__)
#define SF_BLAS_RESTRICT __restrict__
#elif defined (__GNUC__)
#define SF_BLAS_RESTRICT __restrict__
#endif

namespace Blas {

    enum struct MatrixLayout {
        RowMajor = 101,
        ColMajor = 102
    };

    enum struct MatrixTranspose {
        NoTrans = 111,
        Trans = 112
    };

    void scopy(
        const int N,
        const float * SF_BLAS_RESTRICT X,
        float * SF_BLAS_RESTRICT Y
    );

    void scopy(
        const int N,
        const float * SF_BLAS_RESTRICT X, const int incX,
        float * SF_BLAS_RESTRICT Y, const int incY
    );

    void scopy(
        ThreadPool& thread_pool,
        const int N,
        const float * SF_BLAS_RESTRICT X,
        float * SF_BLAS_RESTRICT Y
    );

    void scopy(
        ThreadPool& thread_pool,
        const int N,
        const float * SF_BLAS_RESTRICT X, const int incX,
        float * SF_BLAS_RESTRICT Y, const int incY
    );

    void sscal(
        const int N,
        const float alpha,
        float * SF_BLAS_RESTRICT X
    );

    void sscal(
        const int N,
        const float alpha,
        float * SF_BLAS_RESTRICT X, const int incX
    );

    void sscal(
        ThreadPool& thread_pool,
        const int N,
        const float alpha,
        float * SF_BLAS_RESTRICT X
    );

    void sscal(
        ThreadPool& thread_pool,
        const int N,
        const float alpha,
        float * SF_BLAS_RESTRICT X, const int incX
    );

    void saxpy(
        const int N,
        const float alpha,
        const float * SF_BLAS_RESTRICT X,
        float * SF_BLAS_RESTRICT Y
    );

    void saxpy(
        const int N,
        const float alpha,
        const float * SF_BLAS_RESTRICT X, const int incX,
        float * SF_BLAS_RESTRICT Y, const int incY
    );

    void saxpy(
        ThreadPool& thread_pool,
        const int N,
        const float alpha,
        const float * SF_BLAS_RESTRICT X,
        float * SF_BLAS_RESTRICT Y
    );

    void saxpy(
        ThreadPool& thread_pool,
        const int N,
        const float alpha,
        const float * SF_BLAS_RESTRICT X, const int incX,
        float * SF_BLAS_RESTRICT Y, const int incY
    );

    void sgemm(
        ThreadPool& thread_pool,
        MatrixLayout layout, MatrixTranspose TransA, MatrixTranspose TransB,
        const int M, const int N, const int K,
        const float alpha,
        const float * SF_BLAS_RESTRICT A, const int lda,
        const float * SF_BLAS_RESTRICT B, const int ldb,
        const float beta,
        float * SF_BLAS_RESTRICT C, const int ldc
    );

    void test(
        ThreadPool& thread_pool
    );

    void bench(
        ThreadPool& thread_pool
    );
}

#endif
