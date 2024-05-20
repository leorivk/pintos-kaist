#include <stdint.h>
#include "threads/fixed_point.h"

int int_to_fixed(int n) {
    return n * F;
}

int fixed_to_int_trunc(int x) {
    return x / F;
}

int fixed_to_int_round(int x) {
    if (x >= 0) {
        return (x + F / 2) / F;
    } else {
        return (x - F / 2) / F;
    }
}

int fixed_add(int x, int y) {
    return x + y;
}

int fixed_sub(int x, int y) {
    return x - y;
}

int fixed_add_int(int x, int n) {
    return x + n * F;
}

int fixed_sub_int(int x, int n) {
    return x - n * F;
}

int fixed_mul(int x, int y) {
    return (int)((((int64_t) x) * y) / F);
}

int fixed_mul_int(int x, int n) {
    return x * n;
}

int fixed_div(int x, int y) {
    return (int)((((int64_t) x) * F) / y);
}

int fixed_div_int(int x, int n) {
    return x / n;
}