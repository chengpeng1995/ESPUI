#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include "touch_report.h"
#ifdef __cplusplus
extern "C" {
#endif
lv_display_t* assistant_display_start(void);
esp_err_t assistant_display_brightness_set(int brightness);
void assistant_display_log_stats(void);
uint32_t assistant_display_completed_frames(void);
esp_err_t assistant_display_sleep(void);
esp_err_t assistant_display_wait_idle(void);
esp_err_t assistant_display_wake(void);
void assistant_display_touch_wake_callback(void (*callback)(void*),void* context);
void assistant_display_touch_guard(bool armed,bool consume_until_release);
// Call only after the LVGL worker is paused: the coordinator owns touch I/O.
esp_err_t assistant_display_touch_wake_read(bool* pressed);
typedef struct { touch_packet_t packet;uint8_t raw[10];int irq; } assistant_touch_sample_t;
esp_err_t assistant_display_touch_wake_sample(assistant_touch_sample_t* sample);
esp_err_t assistant_display_touch_wake_arm(void);
uint32_t assistant_display_te_edges(void);
#ifdef __cplusplus
}
#endif
