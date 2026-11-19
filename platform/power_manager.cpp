#include "power_manager.h"
#include "board_display.h"
#include "bsp/esp-bsp.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_memory_utils.h"
#include "driver/gpio.h"
#include <algorithm>
#include <inttypes.h>

namespace assistant {
namespace {
constexpr uint32_t SLEEP=1,WAKE=2,DATA=4,DEADLINE=8;
constexpr const char* TAG="assistant_power";
portMUX_TYPE metrics_mux=portMUX_INITIALIZER_UNLOCKED;
uint64_t light_sleep_us=0;uint32_t light_sleep_entries=0;
// A small RTC record survives supported warm resets. It is diagnostic data,
// not application state: cold power-on or a bad checksum discards it.
enum {MAGIC,BOOT,PHASE,REASON,SOURCE,AT,ENTRIES,EXITS,KIND,XY,CHECK,WORDS};
struct BootEvidence {uint32_t value[WORDS];};
RTC_NOINIT_ATTR BootEvidence rtc_evidence;
BootEvidence previous_evidence{};
portMUX_TYPE evidence_mux=portMUX_INITIALIZER_UNLOCKED;
esp_reset_reason_t boot_reset=ESP_RST_UNKNOWN;
bool previous_valid=false;
uint32_t IRAM_ATTR evidence_checksum(const BootEvidence& e){uint32_t n=0xE52D76A2;for(int i=0;i<CHECK;++i)n^=e.value[i];return n;}
void init_evidence(){
    boot_reset=esp_reset_reason();
    previous_evidence=rtc_evidence;
    previous_valid=boot_reset!=ESP_RST_POWERON&&previous_evidence.value[MAGIC]==0x504D2602&&previous_evidence.value[CHECK]==evidence_checksum(previous_evidence);
    rtc_evidence={};rtc_evidence.value[MAGIC]=0x504D2602;rtc_evidence.value[BOOT]=previous_valid?previous_evidence.value[BOOT]+1:1;
    rtc_evidence.value[CHECK]=evidence_checksum(rtc_evidence);
}
void evidence_phase(unsigned phase,WakeReason reason,const DeviceState& d){
    const auto at=static_cast<uint32_t>(esp_timer_get_time()/1000);
    portENTER_CRITICAL(&evidence_mux);
    rtc_evidence.value[PHASE]=phase;rtc_evidence.value[REASON]=static_cast<unsigned>(reason);
    rtc_evidence.value[SOURCE]=d.usb||d.usb_host;rtc_evidence.value[AT]=at;
    rtc_evidence.value[CHECK]=evidence_checksum(rtc_evidence);portEXIT_CRITICAL(&evidence_mux);
}
esp_err_t IRAM_ATTR sleep_enter(int64_t,void*){
    portENTER_CRITICAL(&evidence_mux);++rtc_evidence.value[ENTRIES];rtc_evidence.value[CHECK]=evidence_checksum(rtc_evidence);portEXIT_CRITICAL(&evidence_mux);return ESP_OK;
}
esp_err_t IRAM_ATTR sleep_exit(int64_t us,void*){
    portENTER_CRITICAL(&metrics_mux);light_sleep_us+=us>0?us:0;++light_sleep_entries;portEXIT_CRITICAL(&metrics_mux);
    portENTER_CRITICAL(&evidence_mux);++rtc_evidence.value[EXITS];rtc_evidence.value[CHECK]=evidence_checksum(rtc_evidence);portEXIT_CRITICAL(&evidence_mux);return ESP_OK;
}
}
void PowerManager::locks(bool acquire){
    if(acquire==locked_)return;
    if(acquire){ESP_ERROR_CHECK(esp_pm_lock_acquire(awake_lock_));ESP_ERROR_CHECK(esp_pm_lock_acquire(cpu_lock_));}
    else{ESP_ERROR_CHECK(esp_pm_lock_release(cpu_lock_));ESP_ERROR_CHECK(esp_pm_lock_release(awake_lock_));}
    locked_=acquire;
}
void PowerManager::protect_usb(){
    // A host can briefly stop SOF packets without being unplugged. Keep its
    // connection alive until the PMU confirms VBUS was physically removed.
    const auto d=platform_.snapshot();
    portENTER_CRITICAL(&evidence_mux);rtc_evidence.value[SOURCE]=d.usb||d.usb_host;rtc_evidence.value[CHECK]=evidence_checksum(rtc_evidence);portEXIT_CRITICAL(&evidence_mux);
    if(d.usb_host&&!usb_locked_){ESP_ERROR_CHECK(esp_pm_lock_acquire(usb_lock_));usb_locked_=true;}
    else if(usb_locked_&&d.pmu&&!d.usb&&!d.usb_host){ESP_ERROR_CHECK(esp_pm_lock_release(usb_lock_));usb_locked_=false;}
}
void PowerManager::start(){
    assert(esp_ptr_internal(this));
    init_evidence();log_boot();
    const esp_pm_config_t config={.max_freq_mhz=240,.min_freq_mhz=40,.light_sleep_enable=true};
    ESP_ERROR_CHECK(esp_pm_configure(&config));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"ui_active",&cpu_lock_));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"ui_awake",&awake_lock_));locks(true);
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"usb_host_guard",&usb_lock_));
    esp_pm_sleep_cbs_register_config_t metrics{};metrics.enter_cb=sleep_enter;metrics.exit_cb=sleep_exit;
    ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&metrics));
    esp_timer_create_args_t timer{};timer.name="service_deadline";timer.arg=this;
    timer.callback=[](void* arg){auto* p=static_cast<PowerManager*>(arg);xTaskNotify(p->task_,DEADLINE,eSetBits);};
    ESP_ERROR_CHECK(esp_timer_create(&timer,&deadline_));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    ESP_ERROR_CHECK(xTaskCreate([](void* p){static_cast<PowerManager*>(p)->run();},"power_manager",8192,this,6,&task_)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    platform_.power_callbacks([](void* p,Command c){static_cast<PowerManager*>(p)->command(c);},[](void* p){auto* m=static_cast<PowerManager*>(p);xTaskNotify(m->task_,DATA,eSetBits);},this);
    assistant_display_touch_wake_callback(touch,this);
}
void PowerManager::command(Command c){
    if(c.op==Op::ScreenSleep){
        saving_=c.value!=0;
        platform_.power_state(PowerState::Preparing,platform_.snapshot().wake_reason);
        xTaskNotify(task_,SLEEP,eSetBits);
    }else{
        reason_=c.value;wake_at_=static_cast<uint32_t>(esp_timer_get_time()/1000);xTaskNotify(task_,WAKE,eSetBits);
    }
}
void IRAM_ATTR PowerManager::touch(void* arg){
    auto* p=static_cast<PowerManager*>(arg);
    p->reason_=static_cast<int>(WakeReason::Touch);
    // Keep the first edge so a held finger cannot make latency appear smaller.
    uint32_t empty=0;p->wake_at_.compare_exchange_strong(empty,static_cast<uint32_t>(esp_timer_get_time()/1000));
    BaseType_t wake=pdFALSE;xTaskNotifyFromISR(p->task_,WAKE,eSetBits,&wake);if(wake)portYIELD_FROM_ISR();
}
void PowerManager::arm_deadline(){
    esp_timer_stop(deadline_);
    if(!paused_)return;
    auto due=system_.next_deadline_ms();if(due){auto now=platform_.millis();ESP_ERROR_CHECK(esp_timer_start_once(deadline_,std::max<uint64_t>(1,due>now?due-now:1)*1000));}
}
bool PowerManager::confirm_touch(const assistant_touch_sample_t* first){
    // Own touch I/O while LVGL is paused. No automatic light sleep between the
    // ACK-separated samples, and no periodic touch reads once this check ends.
    ESP_ERROR_CHECK(esp_pm_lock_acquire(awake_lock_));
    touch_wake_filter_t filter{};touch_first_={};touch_last_={};
    esp_err_t err=ESP_OK;bool confirmed=false;
    for(int i=0;i<6;++i){
        if(i)vTaskDelay(pdMS_TO_TICKS(12));
        if(!i&&first)touch_last_=*first;
        else err=assistant_display_touch_wake_sample(&touch_last_);
        if(!i)touch_first_=touch_last_;
        if(err==ESP_OK&&touch_wake_filter_sample(&filter,touch_last_.packet,static_cast<uint32_t>(esp_timer_get_time()/1000))){confirmed=true;break;}
        if(!i&&touch_last_.packet.kind!=TOUCH_PACKET_DOWN)break;
    }
    if(confirmed){trace(2,WakeReason::Touch);ESP_LOGI(TAG,"TOUCH_WAKE_CONFIRMED x=%u y=%u",touch_last_.packet.x,touch_last_.packet.y);}
    else {vTaskDelay(pdMS_TO_TICKS(12));trace(3,WakeReason::Touch);}
    ESP_ERROR_CHECK(esp_pm_lock_release(awake_lock_));
    if(confirmed)return true;
    ++rejected_touch_;wake_at_=0;
    ESP_LOGI(TAG,"TOUCH_WAKE_REJECTED count=%lu read=%s irq=%d packet=%d",(unsigned long)rejected_touch_,esp_err_to_name(err),gpio_get_level(GPIO_NUM_11),touch_first_.packet.kind);
    ESP_ERROR_CHECK(assistant_display_touch_wake_arm());
    return false;
}
void PowerManager::enter(){
    if(paused_||display_off_)return;
    // Shell publishes the request during a UI callback. Never pause while owning its mutex.
    if(bsp_display_lock(1000)!=ESP_OK){platform_.power_state(PowerState::Awake,WakeReason::Recovery);return;}
    bool requested=shell_.asleep();bsp_display_unlock();if(!requested)return;
    evidence_phase(1,platform_.snapshot().wake_reason,platform_.snapshot());
    if(!saving_){
        assistant_display_brightness_set(0);display_off_=true;platform_.power_state(PowerState::DisplayOff,platform_.snapshot().wake_reason);evidence_phase(5,platform_.snapshot().wake_reason,platform_.snapshot());return;
    }
    wake_at_=0;
    gpio_intr_disable(GPIO_NUM_11);
    assistant_display_touch_guard(false,true);
    if(esp_lv_adapter_pause(1000)!=ESP_OK){++failures_;restore(WakeReason::Recovery);return;}
    paused_=true;
    // Drain a pending release before arming. IRQ level alone is not a touch.
    assistant_touch_sample_t first{};const auto touch_status=assistant_display_touch_wake_sample(&first);
    if(touch_status==ESP_OK&&first.packet.kind==TOUCH_PACKET_DOWN&&confirm_touch(&first)){restore(WakeReason::Touch);return;}
    ESP_ERROR_CHECK(assistant_display_touch_wake_arm());
    system_.tick();
    auto due=system_.next_deadline_ms();auto now=platform_.millis();
    if(system_.alarm_ringing||system_.device.recording||system_.device.playing||(due&&due<=now+600)){
        restore(system_.alarm_ringing?WakeReason::Deadline:WakeReason::Recovery);return;
    }
    if(!platform_.suspend_peripherals()){++failures_;system_.notify(TextId::SleepFailed);restore(WakeReason::Recovery);return;}
    if(wake_at_.load()!=0){
        const auto reason=static_cast<WakeReason>(reason_.load());
        if(reason!=WakeReason::Touch||confirm_touch()){restore(reason);return;}
    }
    assistant_display_brightness_set(0);
    panel_sleep_=true; // Even a partial command failure requires Sleep Out recovery.
    if(assistant_display_sleep()!=ESP_OK){++failures_;system_.notify(TextId::SleepFailed);restore(WakeReason::Recovery);return;}
    platform_.power_state(PowerState::Standby,system_.device.wake_reason);
    entry_frames_=assistant_display_completed_frames();entry_te_=assistant_display_te_edges();entry_imu_=platform_.snapshot().imu_samples;
    entered_at_=esp_timer_get_time();++cycles_;
    evidence_phase(2,platform_.snapshot().wake_reason,platform_.snapshot());
    trace(0,platform_.snapshot().wake_reason);
    arm_deadline();
    ESP_LOGI(TAG,"STANDBY cycle=%lu usb_host=%d frames=%lu te=%lu imu=%lu",(unsigned long)cycles_,platform_.snapshot().usb_host,(unsigned long)entry_frames_,(unsigned long)entry_te_,(unsigned long)entry_imu_);
    protect_usb();locks(false);
}
void PowerManager::restore(WakeReason reason){
    evidence_phase(3,reason,platform_.snapshot());
    trace(1,reason);
    locks(true);esp_timer_stop(deadline_);
    gpio_intr_disable(GPIO_NUM_11);gpio_wakeup_disable(GPIO_NUM_11);
    gpio_set_intr_type(GPIO_NUM_11,GPIO_INTR_NEGEDGE);
    const uint32_t started=wake_at_.exchange(0);const uint32_t begin=started?started:static_cast<uint32_t>(esp_timer_get_time()/1000);
    assistant_display_touch_guard(false,reason==WakeReason::Touch);
    platform_.power_state(PowerState::Waking,reason);
    platform_.resume_peripherals();
    if(panel_sleep_){
        esp_err_t result=assistant_display_wake();
        if(result!=ESP_OK){++failures_;ESP_LOGE(TAG,"Panel wake failed: %s",esp_err_to_name(result));result=assistant_display_wake();}
        if(result!=ESP_OK)ESP_LOGE(TAG,"Panel recovery requires inspection");
        panel_sleep_=false;
    }
    // Worker is still paused: remove the guard and present one complete frame in the dark.
    if(bsp_display_lock(1000)==ESP_OK){
        platform_.power_state(PowerState::Awake,reason);shell_.power_resume();system_.tick();shell_.tick();
        lv_obj_invalidate(lv_screen_active());lv_refr_now(lv_display_get_default());
        if(assistant_display_wait_idle()!=ESP_OK){++failures_;ESP_LOGE(TAG,"Wake redraw transfer timeout");}
        assistant_display_brightness_set(system_.settings.brightness);
        bsp_display_unlock();
    }else{++failures_;platform_.power_state(PowerState::Awake,WakeReason::Recovery);ESP_LOGE(TAG,"UI recovery lock timeout");}
    gpio_intr_enable(GPIO_NUM_11);
    if(paused_)ESP_ERROR_CHECK(esp_lv_adapter_resume());
    paused_=false;display_off_=false;last_wake_ms_=static_cast<uint32_t>(esp_timer_get_time()/1000)-begin;
    evidence_phase(4,reason,platform_.snapshot());
    ESP_LOGI(TAG,"AWAKE reason=%d visible_ms=%d failures=%lu",static_cast<int>(reason),last_wake_ms_.load(),(unsigned long)failures_);
}
void PowerManager::run(){
    while(true){
        uint32_t events=0;xTaskNotifyWait(0,UINT32_MAX,&events,portMAX_DELAY);
        protect_usb();
        if(events&SLEEP)enter();
        if((events&WAKE)&&(paused_||display_off_||platform_.snapshot().power==PowerState::Preparing)){
            const auto reason=static_cast<WakeReason>(reason_.load());
            if(!paused_||reason!=WakeReason::Touch||confirm_touch())restore(reason);
        }
        if(!paused_)continue;
        if(platform_.has_wake_command()){restore(WakeReason::Host);continue;}
        system_.tick();
        if(system_.alarm_ringing){restore(WakeReason::Deadline);continue;}
        std::string command;
        while(platform_.take_sleep_query(command)){
            if(command=="power history")log_history();
            else if(command=="power boot")log_boot();
            else {platform_.log("DEVICE_STATE "+shell_.debug_state());log_stats();}
        }
        arm_deadline();
    }
}
void PowerManager::log_stats(){
    uint64_t us;uint32_t entries;portENTER_CRITICAL(&metrics_mux);us=light_sleep_us;entries=light_sleep_entries;portEXIT_CRITICAL(&metrics_mux);
    auto d=platform_.snapshot();auto total=esp_timer_get_time();
    ESP_LOGI(TAG,"POWER state=%d usb_host=%d suspended=%d cycles=%lu failures=%lu wake_ms=%d light_sleep_us=%llu light_sleep_entries=%lu uptime_us=%lld standby_ms=%lld frames_delta=%lu te_delta=%lu imu_delta=%lu touch_rejected=%lu wake_reason=%d",
        static_cast<int>(d.power),d.usb_host,d.peripherals_suspended,(unsigned long)cycles_,(unsigned long)failures_,last_wake_ms_.load(),(unsigned long long)us,(unsigned long)entries,(long long)total,
        (long long)(paused_?(total-entered_at_)/1000:0),(unsigned long)(assistant_display_completed_frames()-entry_frames_),(unsigned long)(assistant_display_te_edges()-entry_te_),(unsigned long)(d.imu_samples-entry_imu_),(unsigned long)rejected_touch_,static_cast<int>(d.wake_reason));
    for(int core=0;core<2;++core){TaskStatus_t idle{};vTaskGetInfo(xTaskGetIdleTaskHandleForCore(core),&idle,pdFALSE,eInvalid);
        ESP_LOGI(TAG,"CPU core=%d idle_runtime=%llu total_runtime_us=%lld",core,(unsigned long long)idle.ulRunTimeCounter,(long long)total);}
    esp_pm_dump_locks(stdout);
}
void PowerManager::trace(int event,WakeReason reason){
    const auto d=platform_.snapshot();Trace t;t.at=static_cast<uint32_t>(esp_timer_get_time()/1000);t.event=event;t.reason=static_cast<int>(reason);t.usb=d.usb||d.usb_host;
    if(reason==WakeReason::Touch&&(event==2||event==3)){t.first=touch_first_;t.last=touch_last_;}
    if(event==2||event==3){portENTER_CRITICAL(&evidence_mux);rtc_evidence.value[KIND]=t.last.packet.kind;rtc_evidence.value[XY]=(static_cast<uint32_t>(t.last.packet.x)<<16)|t.last.packet.y;rtc_evidence.value[CHECK]=evidence_checksum(rtc_evidence);portEXIT_CRITICAL(&evidence_mux);}
    portENTER_CRITICAL(&trace_mux_);t.sequence=++trace_sequence_;traces_[(t.sequence-1)%traces_.size()]=t;portEXIT_CRITICAL(&trace_mux_);
}
void PowerManager::log_history(){
    std::array<Trace,16> copy;uint32_t sequence;
    portENTER_CRITICAL(&trace_mux_);copy=traces_;sequence=trace_sequence_;portEXIT_CRITICAL(&trace_mux_);
    const uint32_t start=sequence>copy.size()?sequence-copy.size():0;
    ESP_LOGI(TAG,"POWER_HISTORY total=%lu count=%lu",(unsigned long)sequence,(unsigned long)(sequence-start));
    for(uint32_t i=start;i<sequence;++i){const auto& t=copy[i%copy.size()];char first[21],last[21];for(int j=0;j<10;++j){snprintf(first+2*j,3,"%02X",t.first.raw[j]);snprintf(last+2*j,3,"%02X",t.last.raw[j]);}
        ESP_LOGI(TAG,"PWR_TRACE seq=%lu at_ms=%lu event=%d reason=%d source=%s first=%s last=%s first_kind=%d last_kind=%d irq=%d",(unsigned long)t.sequence,(unsigned long)t.at,t.event,t.reason,t.usb?"usb":"battery",first,last,t.first.packet.kind,t.last.packet.kind,t.last.irq);
    }
}
void PowerManager::log_boot(){
    BootEvidence current;portENTER_CRITICAL(&evidence_mux);current=rtc_evidence;portEXIT_CRITICAL(&evidence_mux);
    const auto& p=previous_evidence.value;
    ESP_LOGI(TAG,"POWER_BOOT reset=%d rtc_previous=%d boot=%lu context_internal=%d context_bytes=%u prior_phase=%lu prior_reason=%lu prior_source=%s prior_at_ms=%lu prior_sleep_entries=%lu prior_sleep_exits=%lu prior_packet=%lu prior_xy=%08lx current_phase=%lu current_source=%s current_sleep_entries=%lu current_sleep_exits=%lu",
        boot_reset,previous_valid,(unsigned long)current.value[BOOT],esp_ptr_internal(this),(unsigned)sizeof(*this),
        (unsigned long)(previous_valid?p[PHASE]:0),(unsigned long)(previous_valid?p[REASON]:0),previous_valid?(p[SOURCE]?"usb":"battery"):"unknown",(unsigned long)(previous_valid?p[AT]:0),
        (unsigned long)(previous_valid?p[ENTRIES]:0),(unsigned long)(previous_valid?p[EXITS]:0),(unsigned long)(previous_valid?p[KIND]:0),(unsigned long)(previous_valid?p[XY]:0),
        (unsigned long)current.value[PHASE],current.value[SOURCE]?"usb":"battery",(unsigned long)current.value[ENTRIES],(unsigned long)current.value[EXITS]);
    gpio_dump_io_configuration(stdout,(1ULL<<BSP_LCD_RST)|(1ULL<<BSP_LCD_TOUCH_RST)|(1ULL<<BSP_LCD_CS)|(1ULL<<BSP_LCD_TOUCH_INT));
}
}
