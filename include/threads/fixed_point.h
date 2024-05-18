#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h> //int64_t를 인식하게 해줌.

#define F (1 << 14) // fixed point 1
#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (-(1 << 31))

int int_to_fp (int n); // integer를 fixed point로 전환
int fp_to_int (int x); // fp를 int로 전환(버림)
int fp_to_int_round (int x); // fp를 int로 전환(반올림)
int add_fp(int x, int y); // fp의 덧셈
int sub_fp(int x, int y); // fp의 뺄셈(x-y)
int add_mixed(int x, int n); // fp와 int의 덧셈
int sub_mixed(int x, int n); // fp와 int의 뺄셈(x-n)
int mult_fp(int x, int y); // fp의 곱셈
int mult_mixed(int x, int n); // fp와 int의 곱셈
int div_fp(int x, int y); // fp의 나눗셈(x/y)
int div_mixed(int x, int n); // fp와 int의 나눗셈

int int_to_fp (int n) {
  return n * F;
}

int fp_to_int (int x) {
  return x / F;
}

int fp_to_int_round (int x) {
  if (x >= 0) return (x + F / 2) / F;
  else return (x - F / 2) / F;
}

int add_fp (int x, int y) {
  return x + y;
}

int sub_fp (int x, int y) {
  return x - y;
}

int add_mixed (int x, int n) {
  return x + n * F;
}

int sub_mixed (int x, int n) {
  return x - n * F;
}

int mult_fp (int x, int y) {
  return ((int64_t)x) * y / F;
}

int mult_mixed (int x, int n) {
  return x * n;
}

int div_fp (int x, int y) {
  return ((int64_t)x) * F / y;
}

int div_mixed (int x, int n) {
  return x / n;
}

#endif // FIXED_POINT_H
