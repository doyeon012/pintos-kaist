# 
#define FIXED_POINT_MULTIPLIER (1 << 14) // f의 값에 따라 적절히 조정

// n을 고정소수점으로 변환
#define TO_FIXED_POINT(n) ((n) * FIXED_POINT_MULTIPLIER)

// x를 정수로 변환 (0으로 반올림)
#define TO_INTEGER_TRUNCATE(x) ((x) / FIXED_POINT_MULTIPLIER)

// x를 정수로 변환 (가장 가까운 정수로 반올림)
#define TO_INTEGER_ROUND(x) (((x) >= 0) ? (((x) + FIXED_POINT_MULTIPLIER / 2) / FIXED_POINT_MULTIPLIER) : (((x) - FIXED_POINT_MULTIPLIER / 2) / FIXED_POINT_MULTIPLIER))

// x와 y를 더함
#define ADD(x, y) ((x) + (y))

// x에서 y를 뺌
#define SUBTRACT(x, y) ((x) - (y))

// x와 n을 더함
#define ADD_INT(x, n) ((x) + TO_FIXED_POINT(n))

// x에서 n을 뺌
#define SUBTRACT_INT(x, n) ((x) - TO_FIXED_POINT(n))

// x와 y를 곱함
#define MULTIPLY(x, y) (((int64_t)(x)) * (y) / FIXED_POINT_MULTIPLIER)

// x를 n으로 곱함
#define MULTIPLY_INT(x, n) ((x) * (n))

// x를 y로 나눔
#define DIVIDE(x, y) (((int64_t)(x)) * FIXED_POINT_MULTIPLIER / (y))

// x를 n으로 나눔
#define DIVIDE_INT(x, n) ((x) / (n))
