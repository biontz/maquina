/* Host stub: the firmware's cycle-counter delays are not needed off-target. */
#ifndef DELAY_H_
#define DELAY_H_
#include <stdint.h>
static inline void delay_us(uint32_t us) { (void)us; }
static inline void delay_ms(uint32_t ms) { (void)ms; }
#endif
