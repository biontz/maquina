/*
 * lxr_arm.h - float->integer conversions with Arm (VCVT/FCVTZ*) semantics:
 * round toward zero, saturate at the 32/64-bit limits, NaN -> 0.
 * Used by the rewritten engine sources on non-Arm targets
 * (tools/arm_semantics.py). Kept out of -ffast-math's reach on purpose:
 * the NaN test uses the bit pattern, not x != x.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef MAQUINA_LXR_ARM_H
#define MAQUINA_LXR_ARM_H
#include <stdint.h>
#include <string.h>

static inline int lxr_arm_isnan_(double x)
{
    uint64_t b;
    memcpy(&b, &x, sizeof b);
    return (b & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (b & 0x000FFFFFFFFFFFFFull);
}
static inline uint32_t lxr_arm_f2u32(double x)
{
    if (lxr_arm_isnan_(x) || x <= 0.0) return 0u;
    if (x >= 4294967295.0) return 0xFFFFFFFFu;
    return (uint32_t)x;
}
static inline int32_t lxr_arm_f2s32(double x)
{
    if (lxr_arm_isnan_(x)) return 0;
    if (x >= 2147483647.0) return INT32_MAX;
    if (x <= -2147483648.0) return INT32_MIN;
    return (int32_t)x;
}
static inline uint64_t lxr_arm_f2u64(double x)
{
    if (lxr_arm_isnan_(x) || x <= 0.0) return 0u;
    if (x >= 18446744073709551615.0) return UINT64_MAX;
    return (uint64_t)x;
}
static inline int64_t lxr_arm_f2s64(double x)
{
    if (lxr_arm_isnan_(x)) return 0;
    if (x >= 9223372036854775807.0) return INT64_MAX;
    if (x <= -9223372036854775808.0) return INT64_MIN;
    return (int64_t)x;
}
#endif
