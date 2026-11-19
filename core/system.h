#pragma once
#include "i18n.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace assistant {
enum class AppId { Quota, Clock, Timer, Sensors, Assistant, Settings, Count };
constexpr std::array<TextId,6> app_titles = {TextId::Quota,TextId::Clock,TextId::Timer,TextId::Sensors,TextId::Assistant,TextId::Settings};
struct Alarm { int hour=8, minute=0; bool enabled=true; };
struct Settings {
    Locale locale=Locale::ZhCN;
    int brightness=70, timeout_s=60, volume=35, timezone_min=480;
    bool animations=true, mute=false, wifi=false, ble=false, assist_navigation=false, sleep_power_save=true;
    std::vector<Alarm> alarms;
};
std::string encode_settings(const Settings& settings);
Settings decode_settings(const std::string& json);
struct Network { std::string ssid; int rssi=0; bool secure=true; };
struct BleDevice { std::string name, address; int rssi=0, address_type=0; };
enum class PowerState { Awake, Preparing, Standby, Waking, DisplayOff };
enum class WakeReason { Startup, Touch, Deadline, Host, Recovery };
TextId power_state_text(PowerState state);
TextId wake_reason_text(WakeReason reason);
struct DeviceState {
    PowerState power=PowerState::Awake;
    WakeReason wake_reason=WakeReason::Startup;
    bool usb_host=false, peripherals_suspended=false;
    uint32_t imu_samples=0;
    bool simulated=false, wifi_on=false, wifi_scanning=false, wifi_connecting=false, wifi_connected=false;
    bool ble_on=false, ble_scanning=false, ble_connecting=false, ble_connected=false;
    std::string ssid, ip, ble_name, ble_address;
    int rssi=0;
    std::vector<Network> networks;
    std::vector<BleDevice> ble_devices;
    std::vector<std::string> gatt_services;
    bool pmu=false, battery_present=false, charging=false, usb=false;
    int battery=-1, millivolts=0;
    bool rtc=false, imu=false, sd_mounted=false, sd_missing=false, touch=false, audio_available=false;
    uint64_t sd_total=0, sd_free=0, heap_free=0, psram_free=0;
    std::array<float,3> accel{}, gyro{};
    bool boot_pressed=false, power_pressed=false;
    std::vector<int> i2c_addresses;
    bool recording=false, playing=false, has_recording=false;
    float audio_level=0;
    int recorded_ms=0;
    TextId message=TextId::Ready;
    uint32_t message_seq=0;
};
enum class Op {
    Brightness, Volume, WifiEnable, WifiScan, WifiConnect, WifiDisconnect, WifiForget,
    BleEnable, BleScan, BleConnect, BleDisconnect, RecordStart, RecordStop, AudioPlay,
    AudioStop, AudioClear, Beep, SyncTime, SetTime, SdMount, SdUnmount, Diagnose, ScreenSleep, ScreenWake,
    PowerSuspend, PowerResume // Platform service handoff, never sent by UI controls.
};
struct Command { Op op; int value=0; std::string text, extra; };
class Platform {
public:
    virtual ~Platform()=default;
    virtual uint64_t millis() const=0;
    virtual int64_t utc() const=0;
    virtual int64_t utc_millis() const { return utc()*1000+millis()%1000; }
    virtual Settings load_settings()=0;
    virtual bool save_settings(const Settings& settings)=0;
    virtual void request(Command command)=0;
    virtual DeviceState snapshot()=0;
    virtual bool take_quota(std::string& line)=0;
    virtual void host_command(const char* command)=0;
    virtual void log(const std::string& text)=0;
};
struct QuotaWindow { bool available=false; double used=0; int minutes=0; int64_t reset=0; };
struct QuotaState {
    bool valid=false, test=false;
    uint32_t seq=0;
    int64_t epoch=0, initial_age=0;
    uint64_t received_ms=0;
    QuotaWindow primary, secondary;
    std::string bucket, plan, credits="--";
    int resets=-1;
    bool receive(const std::string& line,uint64_t now_ms);
    int64_t age(uint64_t now_ms) const;
    int64_t remaining_seconds(uint64_t now_ms) const;
};
struct TimerState {
    bool countdown=true, running=false, sound_enabled=true;
    uint64_t started_ms=0, accumulated_ms=0;
    int duration_minutes=5;
    uint64_t elapsed(uint64_t now) const { return accumulated_ms+(running ? now-started_ms : 0); }
    int64_t display_ms(uint64_t now) const;
    void toggle(uint64_t now);
    void reset();
};
class System {
public:
    explicit System(Platform& platform);
    Platform& platform;
    Settings settings;
    DeviceState device;
    QuotaState quota;
    TimerState timer;
    uint32_t revision=1;
    TextId notice=TextId::Ready;
    uint32_t notice_seq=0;
    bool alarm_ringing=false;
    std::array<float,3> imu_zero{};
    void tick();
    // Absolute monotonic deadline; zero means no pending timed service.
    uint64_t next_deadline_ms() const;
    bool save();
    void set_locale(Locale locale);
    void refresh_quota();
    void next_quota();
    void notify(TextId text);
    void dismiss_alarm();
    void zero_imu();
    std::string time_text(bool seconds=false) const;
    std::string date_text() const;
    bool set_local_time(const std::string& value);
private:
    uint32_t last_message_=0;
    uint64_t last_refresh_=0;
    int64_t last_alarm_minute_=-1;
    int64_t last_fired_alarm_minute_=-1;
};
std::string duration_text(int64_t seconds, bool days=false);
}
