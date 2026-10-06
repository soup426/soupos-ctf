#pragma once
#include <stdint.h>

#define TIMER_HZ 100

void     timer_init(void);
uint32_t timer_get_ticks(void);
uint32_t timer_get_seconds(void);
