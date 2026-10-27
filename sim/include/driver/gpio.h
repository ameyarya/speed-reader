#pragma once
typedef int gpio_num_t;
#define GPIO_NUM_11 11
#define GPIO_NUM_12 12
#define GPIO_NUM_38 38
typedef enum { GPIO_MODE_OUTPUT = 0 } gpio_mode_t;
inline int gpio_set_direction(gpio_num_t, gpio_mode_t) { return 0; }
inline int gpio_set_level(gpio_num_t, int) { return 0; }
inline int gpio_hold_en(gpio_num_t) { return 0; }
inline int gpio_hold_dis(gpio_num_t) { return 0; }
inline void gpio_deep_sleep_hold_en() {}
inline void gpio_deep_sleep_hold_dis() {}
