/*
 * Host-side replacement for the STM32F7 HAL + CMSIS intrinsics used by the
 * LXR sound engine. Only what the DSP code needs; every intrinsic follows the
 * ARMv7E-M definition so results match the Cortex-M7 integer paths bit-exactly.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef MAQUINA_STM32F7XX_HAL_SHIM_H
#define MAQUINA_STM32F7XX_HAL_SHIM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Arm half-float storage type; x86 GCC/Clang spell it _Float16. */
#if !defined(__arm__) && !defined(__aarch64__)
#define __fp16 _Float16
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- CMSIS SIMD / saturation intrinsics -------------------------------- */
static inline int32_t lxr_sat_(int64_t v, int bits)
{
    const int64_t max = ((int64_t)1 << (bits - 1)) - 1;
    const int64_t min = -((int64_t)1 << (bits - 1));
    return (int32_t)(v > max ? max : v < min ? min : v);
}
#define __SSAT(val, bits) lxr_sat_((int64_t)(int32_t)(val), (bits))
static inline uint32_t __USAT(int32_t v, uint32_t bits)
{
    const int32_t max = (int32_t)((1u << bits) - 1u);
    return (uint32_t)(v > max ? max : v < 0 ? 0 : v);
}

static inline uint32_t lxr_pack16_(int32_t lo, int32_t hi)
{
    return ((uint32_t)(uint16_t)lo) | ((uint32_t)(uint16_t)hi << 16);
}
static inline uint32_t __QADD16(uint32_t a, uint32_t b)
{
    return lxr_pack16_(lxr_sat_((int16_t)a + (int16_t)b, 16),
                       lxr_sat_((int16_t)(a >> 16) + (int16_t)(b >> 16), 16));
}
static inline uint32_t __QSUB16(uint32_t a, uint32_t b)
{
    return lxr_pack16_(lxr_sat_((int16_t)a - (int16_t)b, 16),
                       lxr_sat_((int16_t)(a >> 16) - (int16_t)(b >> 16), 16));
}
static inline uint32_t __SADD16(uint32_t a, uint32_t b)
{
    return lxr_pack16_((int16_t)a + (int16_t)b, (int16_t)(a >> 16) + (int16_t)(b >> 16));
}
/* PKHBT: bottom half of a, (b << shift) top half */
#define __PKHBT(a, b, shift) \
    ((((uint32_t)(a)) & 0x0000FFFFu) | ((((uint32_t)(b)) << (shift)) & 0xFFFF0000u))
/* SMLALD: acc += a.lo*b.lo + a.hi*b.hi (64-bit accumulate) */
static inline uint64_t __SMLALD(uint32_t a, uint32_t b, uint64_t acc)
{
    return (uint64_t)((int64_t)acc + (int64_t)((int16_t)a * (int16_t)b)
                                   + (int64_t)((int16_t)(a >> 16) * (int16_t)(b >> 16)));
}
static inline uint8_t __CLZ(uint32_t v)
{
    return v == 0 ? 32u : (uint8_t)__builtin_clz(v);
}


/* APSR flags: only read by the unused single-sample calcNoise(); no host equivalent. */
typedef union { struct { uint32_t _r:28; uint32_t V:1; uint32_t C:1; uint32_t Z:1; uint32_t N:1; } b; uint32_t w; } APSR_Type;
static inline uint32_t __get_APSR(void) { return 0; }

/* ---- peripherals the engine touches ------------------------------------ */
typedef struct { volatile uint32_t IDR; volatile uint32_t ODR; volatile uint32_t BSRR; } GPIO_TypeDef;
extern GPIO_TypeDef lxr_shim_gpio_all_high; /* jack-detect pins read as "plugged" */
#define GPIOA (&lxr_shim_gpio_all_high)
#define GPIOB (&lxr_shim_gpio_all_high)
#define GPIOC (&lxr_shim_gpio_all_high)
#define GPIOD (&lxr_shim_gpio_all_high)
#define GPIOE (&lxr_shim_gpio_all_high)
#define GPIO_PIN_0  0x0001u
#define GPIO_PIN_1  0x0002u
#define GPIO_PIN_2  0x0004u
#define GPIO_PIN_3  0x0008u
#define GPIO_PIN_4  0x0010u
#define GPIO_PIN_5  0x0020u
#define GPIO_PIN_6  0x0040u
#define GPIO_PIN_7  0x0080u
#define GPIO_PIN_8  0x0100u
#define GPIO_PIN_9  0x0200u
#define GPIO_PIN_10 0x0400u
#define GPIO_PIN_11 0x0800u
#define GPIO_PIN_12 0x1000u
#define GPIO_PIN_13 0x2000u
#define GPIO_PIN_14 0x4000u
#define GPIO_PIN_15 0x8000u

typedef struct { int unused; } RNG_HandleTypeDef;
extern RNG_HandleTypeDef hrng;
uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef* h); /* drum02/engine/lxr_host.c */

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
#define UNUSED(x) ((void)(x))

#ifdef __cplusplus
}
#endif
#endif
