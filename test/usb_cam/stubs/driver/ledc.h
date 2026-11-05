#pragma once
#include <stdint.h>
typedef enum { LEDC_TIMER_0 } ledc_timer_t;
typedef enum { LEDC_CHANNEL_0 } ledc_channel_t;
typedef enum { LEDC_LOW_SPEED_MODE } ledc_mode_t;
uint32_t ledc_get_freq(ledc_mode_t mode, ledc_timer_t timer);
