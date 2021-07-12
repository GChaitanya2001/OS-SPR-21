#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define fp_t int
#define P 17
#define Q 14
#define f 1<<(Q)

#if P + Q != 31
#error "FATAL ERROR: P + Q != 31."
#endif

#define ADD_INT(x, n) (x) + (n) * (f)
#define SUB_INT(x, n) (x) - (n) * (f)
#define CONVERT_TO_FP(x) (x) * (f)
#define CONVERT_TO_ZERO(x) (x) / (f)
#define CONVERT_TO_NEAREST_INT(x) ((x) >= 0 ? ((x) + (f) / 2) / (f) : ((x) - (f) / 2) / (f))
#define FIXED_POINT_MULT(x, y) ((int64_t)(x)) * (y) / (f)
#define FIXED_POINT_DIV(x, y) ((int64_t)(x)) * (f) / (y)

#endif