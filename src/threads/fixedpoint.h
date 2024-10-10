#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (- (1 << 31))
#define f (1 << 14)


/* Function Declaration */
int conv_n_fp (int n);
int conv_x_int_tzero (int n);
int conv_x_int_round (int n);

int add_x_y (int x, int y);
int sub_x_y (int x, int y);
int mul_x_y (int x, int y);
int div_x_y (int x, int y);

int add_x_n (int x, int n);
int sub_x_n (int x, int n);
int mul_x_n (int x, int n);
int div_x_n (int x, int n);

int add_n_x (int n, int x);
int sub_n_x (int n, int x);


/* Function definition */
int conv_n_fp (int n) { return n * f; }
int conv_x_int_tzero (int x) { return x / f; }
int conv_x_int_round (int x) { 
    if(x >= 0) return (x+f/2) / f;
    else       return (x-f/2) / f;
}

int add_x_y (int x, int y) { return x + y; }
int sub_x_y (int x, int y) { return x - y; }
int mul_x_y (int x, int y) { return ((int64_t)x) * y / f; }
int div_x_y (int x, int y) { return ((int64_t)x) * f / y; }

int add_x_n (int x, int n) { return x + n * f; }
int sub_x_n (int x, int n) { return x - n * f; }
int mul_x_n (int x, int n) { return x * n; }
int div_x_n (int x, int n) { return x / n; }

int add_n_x (int n, int x) { return n * f + x; }
int sub_n_x (int n, int x) { return n * f - x; }

#endif