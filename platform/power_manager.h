#pragma once
#include "esp_platform.h"
#include "shell.h"
#include "board_display.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

namespace assistant {
// System ownership: LVGL while running; this task only after adapter pause ACK.
// ISRs/timer callbacks only notify. They never touch System, LVGL or I2C.
class PowerManager {
public:
    PowerManager(EspPlatform& platform,System& system,Shell& shell):platform_(platform),system_(system),shell_(shell){}
    void start();
    void log_stats(); // Caller owns System (LVGL or paused coordinator).
    void log_history(); // Read-only; does not wake or postpone screen timeout.
    void log_boot(); // Reset reason and RTC evidence from the preceding boot.
private:
    EspPlatform& platform_;System& system_;Shell& shell_;
    TaskHandle_t task_=nullptr;
    esp_pm_lock_handle_t cpu_lock_=nullptr,awake_lock_=nullptr,usb_lock_=nullptr;
    esp_timer_handle_t deadline_=nullptr;
    std::atomic<bool> saving_{true};
    std::atomic<int> reason_{static_cast<int>(WakeReason::Touch)};
    std::atomic<uint32_t> wake_at_{0};
    bool locked_=false,panel_sleep_=false,display_off_=false,usb_locked_=false;
    std::atomic<bool> paused_{false};
    std::atomic<uint32_t> entry_frames_{0},entry_te_{0},entry_imu_{0},cycles_{0},failures_{0};
    std::atomic<uint32_t> rejected_touch_{0};
    std::atomic<int64_t> entered_at_{0};std::atomic<int> last_wake_ms_{0};
    struct Trace {uint32_t at=0,sequence=0;int event=0,reason=0;bool usb=false;assistant_touch_sample_t first{},last{};};
    std::array<Trace,16> traces_{};
    portMUX_TYPE trace_mux_=portMUX_INITIALIZER_UNLOCKED;
    uint32_t trace_sequence_=0;
    assistant_touch_sample_t touch_first_{},touch_last_{};
    void trace(int event,WakeReason reason);
    void run();void enter();void restore(WakeReason);void locks(bool);void arm_deadline();void protect_usb();
    void command(Command);
    bool confirm_touch(const assistant_touch_sample_t* first=nullptr);
    static void IRAM_ATTR touch(void*);
};
}
