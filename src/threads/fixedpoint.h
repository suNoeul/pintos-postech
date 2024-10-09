#ifndef THREADS_FIXEDPOINT_H
#define THREADS_FIXEDPOINT_H

int convert_n2fp(int n);
int convert_x2int_tz(int x); /*rounding toward zero*/
int convert_x2int_near(int x); /*rounding to neareast*/
int add_fp(int x, int y);
int sub_fp(int x, int y);
int add_int(int x, int n);
int sub_int (int x, int n);
int mul_fp(int x, int y);
int mul_int(int x, int n);
int div_fp(int x, int y);
int div_int(int x, int y);


#endif /* threads/fixedpoint.h */