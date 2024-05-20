#define F (1 << 14)  // 고정 소수점 표현을 위한 상수, 여기서 14는 소수 부분의 비트 수

int fixed_add(int x, int y);
int fixed_sub(int x, int y);
int fixed_add_int(int x, int n);
int fixed_sub_int(int x, int n);
int fixed_mul(int x, int y);
int fixed_div(int x, int y);
int fixed_mul_int(int x, int y);
int fixed_div_int(int x, int y);
int int_to_fixed(int n);
int fixed_to_int_round(int x);
int fixed_to_int_trunc(int x);