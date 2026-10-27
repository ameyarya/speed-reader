// Sleep for the simulator: esp_deep_sleep_start() hands control back to the
// script runner, which "wakes" the firmware by running setup() again.
#pragma once
#include <cstdint>
typedef enum { ESP_SLEEP_WAKEUP_UNDEFINED = 0, ESP_SLEEP_WAKEUP_EXT1 = 3 } esp_sleep_source_t;
typedef enum { ESP_EXT1_WAKEUP_ANY_LOW = 0 } esp_sleep_ext1_wakeup_mode_t;
typedef enum { ESP_PD_DOMAIN_RTC_PERIPH = 0 } esp_sleep_pd_domain_t;
typedef enum { ESP_PD_OPTION_ON = 0 } esp_sleep_pd_option_t;
esp_sleep_source_t esp_sleep_get_wakeup_cause();
inline uint64_t esp_sleep_get_ext1_wakeup_status() { return 0; }
inline int esp_sleep_pd_config(esp_sleep_pd_domain_t, esp_sleep_pd_option_t) { return 0; }
inline int esp_sleep_enable_ext1_wakeup_io(uint64_t, esp_sleep_ext1_wakeup_mode_t) { return 0; }
[[noreturn]] void esp_deep_sleep_start();
