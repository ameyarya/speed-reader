#pragma once
#include "gpio.h"
typedef enum { RTC_GPIO_MODE_INPUT_ONLY = 0 } rtc_gpio_mode_t;
inline int rtc_gpio_init(gpio_num_t) { return 0; }
inline int rtc_gpio_deinit(gpio_num_t) { return 0; }
inline int rtc_gpio_set_direction(gpio_num_t, rtc_gpio_mode_t) { return 0; }
inline int rtc_gpio_pullup_en(gpio_num_t) { return 0; }
inline int rtc_gpio_pulldown_dis(gpio_num_t) { return 0; }
inline int rtc_gpio_get_level(gpio_num_t) { return 1; }
