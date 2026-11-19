#pragma once
#include "system.h"
#include <chrono>
#include <deque>
#include <functional>
namespace assistant {
class SimPlatform:public Platform {
public:
    explicit SimPlatform(std::string settings_file,bool manual_clock=false);
    uint64_t millis() const override;
    int64_t utc() const override;
    Settings load_settings() override;
    bool save_settings(const Settings&) override;
    void request(Command) override;
    DeviceState snapshot() override;
    bool take_quota(std::string&) override;
    void host_command(const char*) override;
    void log(const std::string&) override;
    void receive(const std::string& line);
    void scenario(const std::string& name);
    void advance(uint64_t ms);
    bool quiet=false;
    std::vector<std::string> sent_commands;
private:
    std::string settings_file_,scenario_="normal";
    bool manual_=false;
    uint64_t manual_ms_=1000,record_start_=0,play_until_=0;
    int64_t clock_offset_=0;
    std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
    DeviceState state_;
    bool suspended_wifi_=false,suspended_ble_=false;
    std::deque<std::string> frames_;
    struct Job { uint64_t due; std::function<void()> fn; };
    std::vector<Job> jobs_;
    void message(TextId id);
    void finish_recording();
};
}
