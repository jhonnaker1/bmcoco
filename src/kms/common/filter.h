//
// filter.h
//
// The HVS's scaling filter kernels, worked out from their definitions:
//  - Smooth: the Mitchell-Netravali cubic with B = C = 1/3 (D. P. Mitchell
//    and A. N. Netravali, "Reconstruction Filters in Computer Graphics",
//    SIGGRAPH 1988).
//  - Sharp: a box one source pixel wide (nearest neighbour).
//
// A kernel is 32 signed 9-bit coefficients sampled at (i - 15.5) / 8
// source pixels from the centre, symmetric, 256 = one; an output pixel
// uses the four i = p, p + 8, p + 16, p + 24 for its phase p (0..7), which
// are made to add up to 256. The HVS takes them as 11 words.
//
// This file is part of BMCoco, a bare metal Tandy Color Computer emulator for
// the Raspberry Pi. See LICENSE for licensing details.
//

#ifndef BMCOCO_KMS_COMMON_FILTER_H_
#define BMCOCO_KMS_COMMON_FILTER_H_

#include <stdint.h>

namespace kms {

enum class Filter { Smooth, Sharp };

// The 32 coefficients.
void KernelCoefficients(Filter filter, int coeff[32]);

// The 11 words the HVS reads (3 coefficients of 9 bits per word; words
// 6-10 mirror words 4-0).
void KernelWords(Filter filter, uint32_t words[11]);

} // namespace kms

#endif
