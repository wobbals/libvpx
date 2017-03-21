/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "third_party/googletest/src/include/gtest/gtest.h"

#include "./vp9_rtcd.h"

#include "test/acm_random.h"
#include "test/buffer.h"
#include "test/register_state_check.h"

namespace {

using ::libvpx_test::ACMRandom;
using ::libvpx_test::Buffer;

typedef void (*TemporalFilterFunc)(uint8_t *a, unsigned int stride, uint8_t *b,
                                   unsigned int w, unsigned int h,
                                   int filter_strength, int filter_weight,
                                   unsigned int *accumulator, uint16_t *count);

// Calculate the difference between 'a' and 'b', sum in blocks of 9, and apply
// filter based on strength and weight. Store the resulting filter amount in
// 'count' and apply it to 'b' and store it in 'accumulator'.
void reference_filter(const Buffer<uint8_t> &a, const Buffer<uint8_t> &b, int w,
                      int h, int filter_strength, int filter_weight,
                      Buffer<unsigned int> *accumulator,
                      Buffer<uint16_t> *count) {
  Buffer<int> diff_sq = Buffer<int>(w, h, 0);
  diff_sq.Set(0);

  int rounding = 0;
  if (filter_strength > 0) {
    rounding = 1 << (filter_strength - 1);
  }

  // Calculate all the differences. Avoids re-calculating a bunch of extra
  // values.
  for (int height = 0; height < h; ++height) {
    for (int width = 0; width < w; ++width) {
      int diff = a.TopLeftPixel()[height * a.stride() + width] -
                 b.TopLeftPixel()[height * b.stride() + width];
      diff_sq.TopLeftPixel()[height * diff_sq.stride() + width] = diff * diff;
    }
  }

  // For any given point, sum the neighboring values and calculate the
  // modifier.
  for (int height = 0; height < h; ++height) {
    for (int width = 0; width < w; ++width) {
      // Determine how many values are being summed.
      int summed_values = 9;

      if (height == 0 || height == (h - 1)) {
        summed_values -= 3;
      }

      if (width == 0 || width == (w - 1)) {
        if (summed_values == 6) {  // corner
          summed_values -= 2;
        } else {
          summed_values -= 3;
        }
      }

      // Sum the diff_sq of the surrounding values.
      int sum = 0;
      for (int idy = -1; idy <= 1; ++idy) {
        for (int idx = -1; idx <= 1; ++idx) {
          const int y = height + idy;
          const int x = width + idx;

          // If inside the border.
          if (y >= 0 && y < h && x >= 0 && x < w) {
            sum += diff_sq.TopLeftPixel()[y * diff_sq.stride() + x];
          }
        }
      }

      sum *= 3;
      sum /= summed_values;
      sum += rounding;
      sum >>= filter_strength;

      // Clamp the value and invert it.
      if (sum > 16) sum = 16;
      sum = 16 - sum;

      sum *= filter_weight;

      count->TopLeftPixel()[height * count->stride() + width] += sum;
      accumulator->TopLeftPixel()[height * accumulator->stride() + width] +=
          sum * b.TopLeftPixel()[height * b.stride() + width];
    }
  }
}

class TemporalFilterTest : public ::testing::TestWithParam<TemporalFilterFunc> {
 public:
  virtual void SetUp() {
    filter_func_ = GetParam();
    rnd_.Reset(ACMRandom::DeterministicSeed());
  }

 protected:
  TemporalFilterFunc filter_func_;
  ACMRandom rnd_;
};

TEST_P(TemporalFilterTest, CompareReferenceRandom) {
  const int width = 24;
  const int height = 32;
  Buffer<uint8_t> a = Buffer<uint8_t>(width, height, 8);
  // The second buffer must not have any border.
  Buffer<uint8_t> b = Buffer<uint8_t>(width, height, 0);
  Buffer<unsigned int> accum_ref = Buffer<unsigned int>(width, height, 0);
  Buffer<unsigned int> accum_chk = Buffer<unsigned int>(width, height, 0);
  Buffer<uint16_t> count_ref = Buffer<uint16_t>(width, height, 0);
  Buffer<uint16_t> count_chk = Buffer<uint16_t>(width, height, 0);

  a.Set(&rnd_, &ACMRandom::Rand8);
  b.Set(&rnd_, &ACMRandom::Rand8);

  for (int filter_strength = 0; filter_strength < 10; ++filter_strength) {
    for (int filter_weight = 0; filter_weight < 10; ++filter_weight) {
      accum_ref.Set(rnd_.Rand8());
      accum_chk.CopyFrom(accum_ref);
      count_ref.Set(rnd_.Rand8());
      count_chk.CopyFrom(count_ref);
      reference_filter(a, b, width, height, filter_strength, filter_weight,
                       &accum_ref, &count_ref);
      filter_func_(a.TopLeftPixel(), a.stride(), b.TopLeftPixel(), width,
                   height, filter_strength, filter_weight,
                   accum_chk.TopLeftPixel(), count_chk.TopLeftPixel());
      ASSERT_TRUE(accum_chk.CheckValues(accum_ref));
      ASSERT_TRUE(count_chk.CheckValues(count_ref));
    }
  }
}

INSTANTIATE_TEST_CASE_P(C, TemporalFilterTest,
                        ::testing::Values(&vp9_temporal_filter_apply_c));

/* TODO(johannkoenig): https://bugs.chromium.org/p/webm/issues/detail?id=1378
#if HAVE_SSE2
INSTANTIATE_TEST_CASE_P(SSE2, TemporalFilterTest,
                        ::testing::Values(&vp9_temporal_filter_apply_sse2));
#endif  // HAVE_SSE2
*/
}  // namespace