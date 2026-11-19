#include "shell.h"
#include "esp_platform.h"
#include "power_manager.h"
#include "board_display.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_lv_adapter.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_memory_utils.h"
#include "nvs_flash.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cassert>
#include <new>

static assistant::EspPlatform* platform;
static assistant::System* system_state;
static assistant::Shell* shell;
static assistant::PowerManager* power;
static uint32_t refresh_count=0;
static uint64_t stats_at=0;
struct FrameBenchmark {
    bool active=false;
    const char* name="";
    int64_t started=0,last=0,render_start=0;
    unsigned count=0,interval_count=0;
    uint32_t display_frames_at=0;
    uint64_t pixels=0;
    std::array<uint32_t,512> intervals{},render_times{};
    void begin(const char* label) { *this=FrameBenchmark{};name=label;active=true;started=esp_timer_get_time();display_frames_at=assistant_display_completed_frames(); }
    void event(lv_event_t* event) {
        const auto code=lv_event_get_code(event);
        if(code==LV_EVENT_RENDER_READY)++refresh_count;
        if(!active)return;
        auto now=esp_timer_get_time();
        if(code==LV_EVENT_RENDER_START)render_start=now;
        if(code==LV_EVENT_RENDER_READY){
            if(count<render_times.size())render_times[count]=now-render_start;
            if(last&&interval_count<intervals.size())intervals[interval_count++]=now-last;
            last=now;++count;
        }
        if(code==LV_EVENT_FLUSH_START){const auto* a=static_cast<const lv_area_t*>(lv_event_get_param(event));pixels+=lv_area_get_size(a);}
    }
    void finish() {
        active=false;auto elapsed=esp_timer_get_time()-started;
        unsigned samples=std::min(count,static_cast<unsigned>(render_times.size()));
        std::sort(intervals.begin(),intervals.begin()+interval_count);std::sort(render_times.begin(),render_times.begin()+samples);
        auto percentile=[](const auto& values,unsigned n){return n?values[std::min(n-1,n*95/100)]/1000.0:0.0;};
        ESP_LOGI("assistant_bench","BENCH name=%s frames=%u elapsed_ms=%.1f fps=%.2f frame_p95_ms=%.2f render_p95_ms=%.2f pixels_per_frame=%llu heap=%u psram=%u",name,count,elapsed/1000.0,count*1000000.0/elapsed,percentile(intervals,interval_count),percentile(render_times,samples),count?pixels/count:0,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        ESP_LOGI("assistant_bench","PRESENT name=%s completed_frames=%lu fps=%.2f",name,(unsigned long)(assistant_display_completed_frames()-display_frames_at),(assistant_display_completed_frames()-display_frames_at)*1000000.0/elapsed);
        assistant_display_log_stats();
    }
} benchmark;
static void ui_tick(lv_timer_t*){
    std::string command;
    while(platform->take_ui_command(command)){
        // Diagnostic actions act like user input; read-only state polls do not
        // postpone sleep. This also keeps real-device interaction checks honest.
        if(command!="state"&&command!="power state"&&command!="power history"&&command!="power boot"){shell->wake();lv_display_trigger_activity(lv_display_get_default());}
        if(command=="sleep")shell->sleep_now();
        else if(command=="power state")power->log_stats();
        else if(command=="power history")power->log_history();
        else if(command=="power boot")power->log_boot();
        else if(command=="wake")shell->wake();
        else if(command=="exit")shell->exit_app();
        else if(command=="back")shell->back();
        else if(command.rfind("click ",0)==0)shell->activate(command.substr(6));
        else if(command.rfind("set ",0)==0){auto end=command.find(' ',4);if(end!=std::string::npos)shell->set_value(command.substr(4,end-4),command.substr(end+1));}
        else if(command.rfind("app ",0)==0){int i=std::atoi(command.c_str()+4);if(i>=0&&i<6)shell->open(static_cast<assistant::AppId>(i));}
        else if(command.rfind("page ",0)==0){int i=std::atoi(command.c_str()+5);if(i>=0&&i<=static_cast<int>(assistant::Page::TimerOptions))shell->show(static_cast<assistant::Page>(i),false);}
        else if(command.rfind("locale ",0)==0)system_state->set_locale(assistant::parse_locale(command.substr(7)));
        else if(command=="bench scroll"||command=="bench list") {shell->benchmark_scroll(command=="bench list");benchmark.begin(command=="bench list"?"list":"carousel");}
        else if(command=="state"){
            const auto d=platform->snapshot();platform->log("DEVICE_STATE "+shell->debug_state());
            assistant_display_log_stats();
            ESP_LOGI("assistant_memory","STATE heap=%u largest=%u psram=%u integrity=%d",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),heap_caps_check_integrity_all(true));
            ESP_LOGI("assistant_hw","STATE pmu=%d battery_present=%d battery=%d usb=%d rtc=%d imu=%d accel=%.3f,%.3f,%.3f audio=%d recording=%d recorded_ms=%d wifi=%d connected=%d networks=%u ble=%d peers=%u gatt=%u sd=%d",d.pmu,d.battery_present,d.battery,d.usb,d.rtc,d.imu,d.accel[0],d.accel[1],d.accel[2],d.audio_available,d.recording,d.recorded_ms,d.wifi_on,d.wifi_connected,(unsigned)d.networks.size(),d.ble_on,(unsigned)d.ble_devices.size(),(unsigned)d.gatt_services.size(),d.sd_mounted);
        }
    }
    shell->tick();
    if(benchmark.active&&esp_timer_get_time()-benchmark.started>=4800000)benchmark.finish();
    auto now=platform->millis();if(now-stats_at>=10000){
        ESP_LOGI("assistant_stats","refresh_fps=%.1f heap=%u psram=%u min_heap=%u %s",refresh_count*1000.0/(now-stats_at),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),shell->debug_state().c_str());stats_at=now;refresh_count=0;
    }
}
extern "C" void app_main(){
    // Deliberately do not erase NVS on an initialization error: it contains previous firmware settings.
    ESP_ERROR_CHECK(nvs_flash_init());
    usb_serial_jtag_driver_config_t usb{.tx_buffer_size=8192,.rx_buffer_size=4096};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));usb_serial_jtag_vfs_use_driver();
    platform=new assistant::EspPlatform();system_state=new assistant::System(*platform);
    lv_display_t* display=assistant_display_start();ESP_ERROR_CHECK(display?ESP_OK:ESP_FAIL);
    assistant_display_brightness_set(system_state->settings.brightness);
    ESP_ERROR_CHECK(bsp_display_lock(-1));
    shell=new assistant::Shell(*system_state);shell->create(display);
    lv_display_add_event_cb(display,[](lv_event_t* event){benchmark.event(event);},LV_EVENT_ALL,nullptr);
    lv_timer_create(ui_tick,16,nullptr);bsp_display_unlock();
    // GPIO wake is an IRAM interrupt: its context/atomics must remain usable
    // while PSRAM is in half-sleep. Default new() sends this large object to
    // PSRAM with MALLOC_ALWAYSINTERNAL=512, even though its code is in IRAM.
    void* power_storage=heap_caps_malloc(sizeof(assistant::PowerManager),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(power_storage?ESP_OK:ESP_ERR_NO_MEM);
    power=new(power_storage) assistant::PowerManager(*platform,*system_state,*shell);
    assert(esp_ptr_internal(power));
    power->start();platform->start();
    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_LOGI("assistant_ui","ASSISTANT_READY version=2.6.2 display=466x466 protocol=quota-v1");
}
