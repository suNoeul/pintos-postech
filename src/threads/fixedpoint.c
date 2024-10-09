#include "threads/fixedpoint.h"
#include <stdint.h>

#define f (1<<14)

int convert2fp(int n) {
    return n * f;
}
int convert_x2int_tz(int x) {
    return x / f;
} /*rounding toward zero*/

int convert_x2int_near(int x) {
    return x >= 0 ? (x + f / 2) / f : (x - f / 2) / f;
} /*rounding to neareast*/

int add_fp(int x, int y) {
    return  x + y;
}
int sub_fp(int x, int y) {
    return x - y;
}

int add_int(int x, int n) {
    return x + n*f;
}

int sub_int (int x, int n) {
    return x - n*f;
}
    
int mul_fp(int x, int y) {
    return ((int64_t) x) * y / f;
}
int mul_int(int x, int n) {
    return x * n;
}
int div_fp(int x, int y) {
    return ((int64_t) x) * f / y;
}
int div_int(int x, int n) {
    return x / n;
}