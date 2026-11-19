#include "system.h"
#include "utc_time.h"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>

namespace assistant {
namespace {
const cJSON* item(const cJSON* root,const char* key) { return cJSON_GetObjectItemCaseSensitive(root,key); }
bool number(const cJSON* x,double low,double high) { return cJSON_IsNumber(x)&&std::isfinite(x->valuedouble)&&x->valuedouble>=low&&x->valuedouble<=high; }
int integer(const cJSON* root,const char* key,int fallback,int low,int high) {
    const auto* x=item(root,key); return number(x,low,high)&&std::floor(x->valuedouble)==x->valuedouble ? x->valueint : fallback;
}
bool boolean(const cJSON* root,const char* key,bool fallback) { auto* x=item(root,key); return cJSON_IsBool(x)?cJSON_IsTrue(x):fallback; }
std::string text(const cJSON* root,const char* key,const char* fallback="") { auto* x=item(root,key); return cJSON_IsString(x)?x->valuestring:fallback; }
bool window(const cJSON* root,const char* key,QuotaWindow& out) {
    const auto* w=item(root,key); if(cJSON_IsNull(w))return true;
    if(!cJSON_IsObject(w)||!number(item(w,"used"),0,100)||!number(item(w,"minutes"),1,525600)||!number(item(w,"reset"),0,4102444800.0))return false;
    out={true,item(w,"used")->valuedouble,item(w,"minutes")->valueint,static_cast<int64_t>(item(w,"reset")->valuedouble)}; return true;
}
}
std::string encode_settings(const Settings& s) {
    auto* j=cJSON_CreateObject(); cJSON_AddNumberToObject(j,"version",1);
    cJSON_AddStringToObject(j,"locale",locale_code(s.locale));
    cJSON_AddNumberToObject(j,"brightness",s.brightness); cJSON_AddNumberToObject(j,"timeout",s.timeout_s);
    cJSON_AddNumberToObject(j,"volume",s.volume); cJSON_AddNumberToObject(j,"timezone",s.timezone_min);
    cJSON_AddBoolToObject(j,"assist_navigation",s.assist_navigation); cJSON_AddBoolToObject(j,"animations",s.animations); cJSON_AddBoolToObject(j,"mute",s.mute);
    cJSON_AddBoolToObject(j,"wifi",s.wifi); cJSON_AddBoolToObject(j,"ble",s.ble);
    cJSON_AddBoolToObject(j,"sleep_power_save",s.sleep_power_save);
    auto* a=cJSON_AddArrayToObject(j,"alarms");
    for(const auto& alarm:s.alarms) {
        auto* e=cJSON_CreateObject(); cJSON_AddNumberToObject(e,"hour",alarm.hour); cJSON_AddNumberToObject(e,"minute",alarm.minute);
        cJSON_AddBoolToObject(e,"enabled",alarm.enabled); cJSON_AddItemToArray(a,e);
    }
    char* raw=cJSON_PrintUnformatted(j); std::string result=raw?raw:"{}"; cJSON_free(raw); cJSON_Delete(j); return result;
}
Settings decode_settings(const std::string& raw) {
    Settings s; auto* j=cJSON_Parse(raw.c_str()); if(!j)return s;
    if(integer(j,"version",0,0,100)!=1) { cJSON_Delete(j); return s; }
    s.locale=parse_locale(text(j,"locale")); s.brightness=integer(j,"brightness",70,5,100);
    s.timeout_s=integer(j,"timeout",60,0,60); if(s.timeout_s!=0&&s.timeout_s!=15&&s.timeout_s!=30&&s.timeout_s!=60)s.timeout_s=60;
    s.volume=integer(j,"volume",35,0,100); s.timezone_min=integer(j,"timezone",480,-720,840);
    s.assist_navigation=boolean(j,"assist_navigation",false); s.animations=boolean(j,"animations",true); s.mute=boolean(j,"mute",false);
    s.wifi=boolean(j,"wifi",false); s.ble=boolean(j,"ble",false);
    s.sleep_power_save=boolean(j,"sleep_power_save",true);
    const auto* alarms=item(j,"alarms");
    if(cJSON_IsArray(alarms))for(int i=0;i<std::min(cJSON_GetArraySize(alarms),8);++i) {
        const auto* a=cJSON_GetArrayItem(alarms,i); int h=integer(a,"hour",-1,0,23),m=integer(a,"minute",-1,0,59);
        if(h>=0&&m>=0)s.alarms.push_back({h,m,boolean(a,"enabled",false)});
    }
    cJSON_Delete(j); return s;
}
bool QuotaState::receive(const std::string& line,uint64_t now_ms) {
    if(line.size()>2047)return false;
    const char* end=nullptr; auto* j=cJSON_ParseWithOpts(line.c_str(),&end,true); if(!j)return false;
    QuotaState n;
    bool ok=text(j,"type")=="quota"&&integer(j,"version",0,1,1)==1&&
        number(item(j,"seq"),1,UINT32_MAX)&&number(item(j,"now"),0,4102444800.0)&&number(item(j,"age"),0,86400)&&
        window(j,"primary",n.primary)&&window(j,"secondary",n.secondary);
    if(ok) {
        n.valid=true; n.seq=static_cast<uint32_t>(item(j,"seq")->valuedouble);
        n.epoch=static_cast<int64_t>(item(j,"now")->valuedouble); n.initial_age=static_cast<int64_t>(item(j,"age")->valuedouble);
        n.received_ms=now_ms; n.bucket=text(j,"bucket","--").substr(0,80); n.plan=text(j,"plan","--").substr(0,40);
        n.test=n.bucket.rfind("TEST",0)==0; n.credits=text(j,"credits","--").substr(0,40);
        n.resets=integer(j,"reset_credits",-1,0,1000000); *this=n;
    }
    cJSON_Delete(j); return ok;
}
int64_t QuotaState::age(uint64_t now_ms) const { return initial_age+static_cast<int64_t>((now_ms-received_ms)/1000); }
int64_t QuotaState::remaining_seconds(uint64_t now_ms) const {
    return primary.available ? std::max<int64_t>(0,primary.reset-epoch-static_cast<int64_t>((now_ms-received_ms)/1000)) : 0;
}
int64_t TimerState::display_ms(uint64_t now) const { auto e=static_cast<int64_t>(elapsed(now)); return countdown?std::max<int64_t>(0,duration_minutes*60000LL-e):e; }
void TimerState::toggle(uint64_t now) { if(running)accumulated_ms+=now-started_ms; else started_ms=now; running=!running; }
void TimerState::reset() { running=false; accumulated_ms=0; }
System::System(Platform& p):platform(p),settings(p.load_settings()),device(p.snapshot()) {}
void System::notify(TextId text_id) { notice=text_id; ++notice_seq; }
bool System::save() { ++revision; bool ok=platform.save_settings(settings); if(!ok)notify(TextId::SaveFailed); return ok; }
void System::set_locale(Locale value) { if(value==settings.locale)return; settings.locale=value; save(); platform.log(std::string("LOCALE ")+locale_code(value)); }
void System::tick() {
    device=platform.snapshot();
    if(device.message_seq!=last_message_) { last_message_=device.message_seq; notify(device.message); }
    std::string line;
    while(platform.take_quota(line))if(quota.receive(line,platform.millis())) {
        platform.log("QUOTA_APPLIED seq="+std::to_string(quota.seq));
        platform.host_command(("@CODEX_ACK "+std::to_string(quota.seq)).c_str());
    }
    const auto ms=platform.millis();
    if(timer.countdown&&timer.running&&timer.display_ms(ms)==0) {
        timer.running=false; timer.accumulated_ms=timer.duration_minutes*60000ULL;
        notify(TextId::TimerDone); alarm_ringing=true; platform.request({Op::RecordStop}); platform.request({Op::AudioStop}); if(!settings.mute&&timer.sound_enabled)platform.request({Op::Beep,settings.volume});
    }
    const auto epoch=platform.utc();
    if(epoch>1700000000) {
        const auto minute=(epoch+settings.timezone_min*60)/60;
        if(minute!=last_alarm_minute_) {
            last_alarm_minute_=minute;
            for(const auto& a:settings.alarms)if(a.enabled&&minute!=last_fired_alarm_minute_&&minute%1440==a.hour*60+a.minute) {
                last_fired_alarm_minute_=minute;
                notify(TextId::AlarmRinging); alarm_ringing=true; platform.request({Op::RecordStop}); platform.request({Op::AudioStop}); if(!settings.mute)platform.request({Op::Beep,settings.volume}); break;
            }
        }
    }
}
uint64_t System::next_deadline_ms() const {
    const auto now=platform.millis();uint64_t result=0;
    if(timer.running&&timer.countdown)result=now+std::max<int64_t>(1,timer.display_ms(now));
    const int64_t utc_ms=platform.utc_millis();
    if(utc_ms>1700000000000LL){
        const int64_t local_ms=utc_ms+settings.timezone_min*60000LL;
        const int64_t minute=local_ms/60000,day=minute/1440;
        for(const auto& a:settings.alarms)if(a.enabled){
            int64_t target=day*1440+a.hour*60+a.minute;
            if(target<minute||(target==minute&&(target==last_alarm_minute_||target==last_fired_alarm_minute_)))target+=1440;
            // A backwards clock correction must not replay an already delivered alarm.
            while(target==last_fired_alarm_minute_)target+=1440;
            auto due=now+std::max<int64_t>(1,target*60000-local_ms);
            if(!result||due<result)result=due;
        }
    }
    return result;
}
TextId power_state_text(PowerState state){
    switch(state){case PowerState::Preparing:return TextId::PowerPreparing;case PowerState::Standby:return TextId::PowerStandby;case PowerState::Waking:return TextId::PowerWaking;case PowerState::DisplayOff:return TextId::PowerDisplayOff;default:return TextId::PowerAwake;}
}
TextId wake_reason_text(WakeReason reason){
    switch(reason){case WakeReason::Touch:return TextId::WakeTouch;case WakeReason::Deadline:return TextId::WakeDeadline;case WakeReason::Host:return TextId::WakeHost;case WakeReason::Recovery:return TextId::WakeRecovery;default:return TextId::WakeStartup;}
}
void System::refresh_quota() { const auto now=platform.millis(); if(last_refresh_&&now-last_refresh_<5000)return; last_refresh_=now?now:1; platform.host_command("@CODEX_REFRESH"); notify(TextId::Refreshing); }
void System::next_quota() { platform.host_command("@CODEX_NEXT"); }
void System::dismiss_alarm() { alarm_ringing=false; platform.request({Op::AudioStop}); }
void System::zero_imu() { imu_zero=device.accel; notify(TextId::Zeroed); }
std::string System::time_text(bool seconds) const {
    if(platform.utc()<1700000000)return seconds?"--:--:--":"--:--";
    time_t t=platform.utc()+settings.timezone_min*60; tm local{}; gmtime_r(&t,&local); char out[24]; strftime(out,sizeof(out),seconds?"%H:%M:%S":"%H:%M",&local); return out;
}
std::string System::date_text() const {
    if(platform.utc()<1700000000)return "--";
    time_t t=platform.utc()+settings.timezone_min*60; tm local{}; gmtime_r(&t,&local); char out[32]; strftime(out,sizeof(out),"%Y-%m-%d",&local); return out;
}
bool System::set_local_time(const std::string& value) {
    int y,m,d,h,mi,n=0; if(sscanf(value.c_str(),"%d-%d-%d %d:%d%n",&y,&m,&d,&h,&mi,&n)!=5||static_cast<size_t>(n)!=value.size()||y<2020||y>2099||m<1||m>12||d<1||d>31||h<0||h>23||mi<0||mi>59)return false;
    tm t{}; t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=h;t.tm_min=mi;
    time_t epoch=utc_epoch(&t); if(t.tm_year!=y-1900||t.tm_mon!=m-1||t.tm_mday!=d)return false;
    platform.request({Op::SetTime,0,std::to_string(epoch-settings.timezone_min*60)}); return true;
}
std::string duration_text(int64_t sec,bool days) {
    sec=std::max<int64_t>(0,sec); char out[64];
    if(days)snprintf(out,sizeof(out),"%lldd %02lld:%02lld:%02lld",static_cast<long long>(sec/86400),static_cast<long long>(sec/3600%24),static_cast<long long>(sec/60%60),static_cast<long long>(sec%60));
    else snprintf(out,sizeof(out),"%02lld:%02lld:%02lld",static_cast<long long>(sec/3600),static_cast<long long>(sec/60%60),static_cast<long long>(sec%60));
    return out;
}
}
