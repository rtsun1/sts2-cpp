#pragma once
#include <cstdio>
#include <cstdlib>
// Never rely on assert/NDEBUG: diagnostic and performance builds are explicit.
#if STS2_DEFENSIVE_VALIDATION
#define STS2_CHECK_INVARIANT(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "sts2 invariant failed: %s at %s:%d\n", #condition, __FILE__, __LINE__); \
    std::abort(); } } while (false)
#else
#define STS2_CHECK_INVARIANT(condition) do {} while (false)
#endif
