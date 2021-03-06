#include <math.h>
#include <x86intrin.h>

#include <twiddle/hyperloglog/hyperloglog.h>

#ifdef USE_AVX2
/* http://stackoverflow.com/questions/13219146/how-to-sum-m256-horizontally */
static inline float horizontal_sum_avx2(__m256 x)
{
  const __m128 hi_quad = _mm256_extractf128_ps(x, 1);
  const __m128 lo_quad = _mm256_castps256_ps128(x);
  const __m128 sum_quad = _mm_add_ps(lo_quad, hi_quad);
  const __m128 lo_dual = sum_quad;
  const __m128 hi_dual = _mm_movehl_ps(sum_quad, sum_quad);
  const __m128 sum_dual = _mm_add_ps(lo_dual, hi_dual);
  const __m128 lo = sum_dual;
  const __m128 hi = _mm_shuffle_ps(sum_dual, sum_dual, 0x1);
  const __m128 sum = _mm_add_ss(lo, hi);
  return _mm_cvtss_f32(sum);
}

#define _mm256_cntz_epi8(simd)                                                 \
  __builtin_popcount(                                                          \
      _mm256_movemask_epi8(_mm256_cmpeq_epi8(simd, _mm256_setzero_si256())))

#define inverse_power_avx2(simd)                                               \
  _mm256_sub_epi32(ones, _mm256_slli_epi32(_mm256_cvtepu8_epi32(simd), 23))

static inline void hyperloglog_count_avx2(const uint8_t *registers,
                                          uint32_t n_registers,
                                          float *inverse_sum, uint32_t *n_zeros)
{
  const __m256i ones = (__m256i)_mm256_set1_ps(1.0f);
  __m256 agg = _mm256_set1_ps(0.0f);

  for (size_t i = 0; i < n_registers / sizeof(__m256i); ++i) {
    const __m256i simd = _mm256_load_si256((__m256i *)registers + i);
    /* For some reason, VPSRLDQ works on lane of 128bits instead of 256. */
    const __m128i low = _mm256_extracti128_si256(simd, 0);
    const __m128i high = _mm256_extracti128_si256(simd, 1);

    __m256i sums = inverse_power_avx2(low);
    agg = _mm256_add_ps(agg, (__m256)sums);

    sums = inverse_power_avx2(_mm_srli_si128(low, 8));
    agg = _mm256_add_ps(agg, (__m256)sums);

    sums = inverse_power_avx2(high);
    agg = _mm256_add_ps(agg, (__m256)sums);

    sums = inverse_power_avx2(_mm_srli_si128(high, 8));
    agg = _mm256_add_ps(agg, (__m256)sums);

    *n_zeros += _mm256_cntz_epi8(simd);
  }

  *inverse_sum = horizontal_sum_avx2(agg);
}

#elif defined USE_AVX

static inline float horizontal_sum_avx(__m128 x)
{
  x = _mm_hadd_ps(x, x);
  x = _mm_hadd_ps(x, x);
  return _mm_cvtss_f32(x);
}

#define _mm_cntz_epi8(simd)                                                    \
  __builtin_popcount(                                                          \
      _mm_movemask_epi8(_mm_cmpeq_epi8(simd, _mm_setzero_si128())))

#define inverse_power_avx(simd)                                                \
  _mm_sub_epi32(ones, _mm_slli_epi32(_mm_cvtepu8_epi32(simd), 23))

static inline void hyperloglog_count_avx(const uint8_t *registers,
                                         uint32_t n_registers,
                                         float *inverse_sum, uint32_t *n_zeros)
{
  const __m128i ones = (__m128i)_mm_set1_ps(1.0f);
  __m128 agg = _mm_set1_ps(0.0f);

  for (size_t i = 0; i < n_registers / sizeof(__m128i); ++i) {
    const __m128i simd = _mm_load_si128((__m128i *)registers + i);

    __m128i powers = inverse_power_avx(simd);
    agg = _mm_add_ps(agg, (__m128)powers);

    powers = inverse_power_avx(_mm_srli_si128(simd, 4));
    agg = _mm_add_ps(agg, (__m128)powers);

    powers = inverse_power_avx(_mm_srli_si128(simd, 8));
    agg = _mm_add_ps(agg, (__m128)powers);

    powers = inverse_power_avx(_mm_srli_si128(simd, 12));
    agg = _mm_add_ps(agg, (__m128)powers);

    *n_zeros += _mm_cntz_epi8(simd);
  }

  *inverse_sum = horizontal_sum_avx(agg);
}

#endif

static inline void hyperloglog_count_port(const uint8_t *registers,
                                          uint32_t n_registers,
                                          float *inverse_sum, uint32_t *n_zeros)

{
  for (size_t i = 0; i < n_registers; ++i) {
    const uint8_t val = registers[i];
    *inverse_sum += powf(2, -1.0 * val);
    if (val == 0) {
      *n_zeros += 1;
    }
  }
}
