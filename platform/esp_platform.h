#pragma once
#include "system.h"
#include <memory>
namespace assistant {
class EspPlatform:public Platform {
public:
    EspPlatform();
    ~EspPlatform() override;
    void start();
    uint64_t millis() const override;
    int64_t utc() const override;
    int64_t utc_millis() const override;
    Settings load_settings() override;
    bool save_settings(const Settings&) override;
    void request(Command) override;
    DeviceState snapshot() override;
    bool take_quota(std::string&) override;
    bool take_ui_command(std::string&);
    void host_command(const char*) override;
    void log(const std::string&) override;
    void power_callbacks(void (*command)(void*,Command),void (*event)(void*),void* context);
    void power_state(PowerState,WakeReason);
    bool suspend_peripherals();
    void resume_peripherals();
    bool has_wake_command();
    bool take_sleep_query(std::string&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
