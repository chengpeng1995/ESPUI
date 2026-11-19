#include "sim_platform.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
namespace assistant {
SimPlatform::SimPlatform(std::string path,bool manual):settings_file_(std::move(path)),manual_(manual) {
    state_.simulated=true;state_.pmu=state_.battery_present=state_.usb=state_.rtc=state_.imu=state_.touch=state_.audio_available=true;
    state_.battery=82;state_.millivolts=4070;state_.charging=true;state_.heap_free=210*1024;state_.psram_free=6*1024*1024;
    state_.i2c_addresses={0x18,0x20,0x34,0x40,0x51,0x5a,0x6b};
    const auto settings=load_settings();state_.wifi_on=settings.wifi;state_.ble_on=settings.ble;
}
uint64_t SimPlatform::millis() const {return manual_?manual_ms_:static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start_).count());}
int64_t SimPlatform::utc() const {return (manual_?2000000000+static_cast<int64_t>(manual_ms_/1000):static_cast<int64_t>(time(nullptr)))+clock_offset_;}
void SimPlatform::advance(uint64_t ms){manual_ms_+=ms;}
Settings SimPlatform::load_settings(){std::ifstream in(settings_file_);std::stringstream b;b<<in.rdbuf();return decode_settings(b.str());}
bool SimPlatform::save_settings(const Settings& settings){std::string tmp=settings_file_+".tmp";{std::ofstream out(tmp);if(!out)return false;out<<encode_settings(settings)<<'\n';out.flush();if(!out)return false;}return std::rename(tmp.c_str(),settings_file_.c_str())==0;}
void SimPlatform::message(TextId id){state_.message=id;++state_.message_seq;}
void SimPlatform::receive(const std::string& line){if(frames_.size()>=8)frames_.pop_front();frames_.push_back(line);}
bool SimPlatform::take_quota(std::string& line){if(frames_.empty())return false;line=std::move(frames_.front());frames_.pop_front();return true;}
void SimPlatform::host_command(const char* value){sent_commands.emplace_back(value);if(sent_commands.size()>128)sent_commands.erase(sent_commands.begin());if(!quiet)printf("%s\n",value);}
void SimPlatform::log(const std::string& value){if(!quiet)printf("SIM_LOG %s\n",value.c_str());}
void SimPlatform::finish_recording(){state_.recording=false;state_.has_recording=state_.recorded_ms>0;state_.audio_level=0;message(TextId::Recorded);}
void SimPlatform::scenario(const std::string& name){
    state_.usb=state_.usb_host=name!="battery";
    scenario_=name;state_.sd_missing=name=="no-card";state_.battery_present=name!="no-battery";state_.battery=name=="low-battery"?5:82;state_.charging=name!="low-battery"&&state_.battery_present;
    state_.pmu=state_.rtc=state_.imu=state_.audio_available=name!="hardware-error";
    if(name=="offline"){state_.wifi_connected=false;state_.ip.clear();message(TextId::Offline);}
    if(name=="no-card"){state_.sd_mounted=false;state_.sd_total=state_.sd_free=0;}
    if(name=="hardware-error")message(TextId::HardwareError);
    log("HARDWARE_SCENARIO "+name+" SIMULATED");
}
void SimPlatform::request(Command cmd){
    const auto now=millis();
    auto job=[this,now](int ms,std::function<void()> fn){jobs_.push_back({now+static_cast<uint64_t>(ms),std::move(fn)});};
    switch(cmd.op){
    case Op::ScreenSleep:
        if(state_.recording||state_.playing){message(TextId::SleepBusy);break;}
        if(scenario_=="sleep-failed"){state_.wake_reason=WakeReason::Recovery;message(TextId::SleepFailed);break;}
        state_.power=cmd.value?PowerState::Standby:PowerState::DisplayOff;
        state_.peripherals_suspended=cmd.value;
        suspended_wifi_=suspended_ble_=false;
        if(cmd.value&&!state_.usb){suspended_wifi_=state_.wifi_on;suspended_ble_=state_.ble_on;state_.wifi_on=state_.ble_on=false;state_.wifi_connected=state_.ble_connected=false;}
        break;
    case Op::ScreenWake:
        state_.power=PowerState::Awake;state_.wake_reason=static_cast<WakeReason>(cmd.value);state_.peripherals_suspended=false;
        state_.wifi_on=state_.wifi_on||suspended_wifi_;state_.ble_on=state_.ble_on||suspended_ble_;suspended_wifi_=suspended_ble_=false;break;
    case Op::PowerSuspend:case Op::PowerResume:case Op::Brightness:case Op::Volume:break;
    case Op::WifiEnable:state_.wifi_on=cmd.value;state_.wifi_connected=false;state_.wifi_scanning=false;state_.wifi_connecting=false;state_.networks.clear();break;
    case Op::WifiScan:
        if(!state_.wifi_on){message(TextId::WifiOff);break;}state_.wifi_scanning=true;
        job(450,[this]{state_.wifi_scanning=false;if(!state_.wifi_on)return;state_.networks={{"圆屏实验室",-43,true},{"Long network name for layout testing",-67,true},{"Guest",-72,false}};});break;
    case Op::WifiConnect:
        if(!state_.wifi_on){message(TextId::WifiOff);break;}state_.wifi_connecting=true;
        job(700,[this,cmd]{state_.wifi_connecting=false;if(!state_.wifi_on)return;if(scenario_=="connect-failed"||scenario_=="offline"||cmd.extra=="wrongpass"){message(TextId::WifiFailed);return;}state_.wifi_connected=true;state_.ssid=cmd.text;state_.ip="192.0.2.23";state_.rssi=-43;message(TextId::Connected);});break;
    case Op::WifiDisconnect:case Op::WifiForget:state_.wifi_connected=false;state_.ip.clear();message(TextId::Disconnected);break;
    case Op::BleEnable:state_.ble_on=cmd.value;state_.ble_connected=false;state_.ble_scanning=false;state_.ble_devices.clear();state_.gatt_services.clear();break;
    case Op::BleScan:
        if(!state_.ble_on){message(TextId::BleOff);break;}state_.ble_scanning=true;job(600,[this]{state_.ble_scanning=false;if(state_.ble_on)state_.ble_devices={{"演示传感器","02:00:00:00:00:01",-48,0},{"SIM BLE Monitor","02:00:00:00:00:02",-60,0}};});break;
    case Op::BleConnect:
        if(!state_.ble_on){message(TextId::BleOff);break;}state_.ble_connecting=true;job(600,[this,cmd]{state_.ble_connecting=false;if(!state_.ble_on)return;if(scenario_=="connect-failed"){message(TextId::BleFailed);return;}state_.ble_connected=true;state_.ble_address=cmd.text;state_.ble_name=cmd.extra;state_.gatt_services={"1800 · Generic Access","180A · Device Information","180F · Battery"};message(TextId::Connected);});break;
    case Op::BleDisconnect:state_.ble_connected=false;state_.gatt_services.clear();message(TextId::Disconnected);break;
    case Op::RecordStart:
        if(!state_.audio_available){message(TextId::AudioFailed);break;}state_.recording=true;state_.playing=false;state_.has_recording=false;state_.recorded_ms=0;record_start_=now;message(TextId::Recording);break;
    case Op::RecordStop:if(state_.recording)finish_recording();break;
    case Op::AudioPlay:
        if(!state_.has_recording){message(TextId::NoRecording);break;}state_.playing=true;state_.recording=false;play_until_=now+std::max(500,state_.recorded_ms);break;
    case Op::AudioStop:state_.playing=false;state_.audio_level=0;break;
    case Op::AudioClear:state_.recording=state_.playing=state_.has_recording=false;state_.recorded_ms=0;state_.audio_level=0;break;
    case Op::Beep:break;
    case Op::SyncTime:if(!state_.wifi_connected)message(TextId::NeedWifi);else job(300,[this]{message(TextId::TimeSynced);});break;
    case Op::SetTime:try{clock_offset_+=std::stoll(cmd.text)-utc();message(TextId::TimeSynced);}catch(...){message(TextId::InvalidDateTime);}break;
    case Op::SdMount:if(scenario_=="no-card"){message(TextId::NoCard);break;}state_.sd_mounted=true;state_.sd_total=32ULL*1024*1024*1024;state_.sd_free=29ULL*1024*1024*1024;message(TextId::Mounted);break;
    case Op::SdUnmount:state_.sd_mounted=false;message(TextId::Unmounted);break;
    case Op::Diagnose:message(scenario_=="hardware-error"?TextId::HardwareError:TextId::Ready);break;
    }
}
DeviceState SimPlatform::snapshot(){
    auto now=millis();for(size_t i=0;i<jobs_.size();){if(jobs_[i].due<=now){auto fn=std::move(jobs_[i].fn);jobs_.erase(jobs_.begin()+i);fn();}else ++i;}
    if(state_.imu&&!state_.peripherals_suspended){++state_.imu_samples;state_.accel={0.12f*std::sin(now/1300.f),0.08f*std::cos(now/1700.f),0.99f};state_.gyro={1.3f*std::sin(now/900.f),0.5f,0.1f};}
    if(state_.recording){state_.recorded_ms=static_cast<int>(now-record_start_);if(state_.recorded_ms>=10000)finish_recording();else state_.audio_level=0.1f+0.6f*std::fabs(std::sin(now/150.f));}
    if(state_.playing){if(now>=play_until_){state_.playing=false;state_.audio_level=0;}else state_.audio_level=0.4f*std::fabs(std::sin(now/130.f));}
    return state_;
}
}
