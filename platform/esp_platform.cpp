#include "esp_platform.h"
#include "utc_time.h"
#include "board_audio.h"
#include "board_display.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "XPowersAXP2101.hpp"
#include "SensorQMI8658.hpp"
#include "driver/usb_serial_jtag.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
extern "C" {
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
}
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <sys/time.h>

namespace assistant {
namespace {
constexpr const char* TAG="assistant_hw";
uint64_t now_ms(){return esp_timer_get_time()/1000;}
uint8_t bcd(int v){return (v/10)*16+v%10;}
int unbcd(uint8_t v){return (v>>4)*10+(v&15);}
}
struct EspPlatform::Impl {
    std::mutex mutex;
    DeviceState state;
    std::deque<Command> commands;
    std::deque<std::string> quota, ui_commands;
    Settings initial;
    TaskHandle_t service_task=nullptr,audio_task=nullptr;
    SemaphoreHandle_t suspend_done=nullptr;
    std::atomic<int> suspend_generation{0},suspend_ack{0};
    std::atomic<bool> suspend_ok{false};
    bool suspended=false,radios_suspended=false,radio_restore_captured=false,resume_wifi=false,resume_ble=false;
    int ble_peer_type=0,resume_peer_type=0;
    std::string resume_peer,resume_name;
    bool reconnect_ble=false;
    uint64_t ble_restore_deadline=0;
    void (*power_command)(void*,Command)=nullptr;
    void (*power_event)(void*)=nullptr;
    void* power_context=nullptr;
    void event(){if(power_event)power_event(power_context);}
    void notify_service(){if(service_task)xTaskNotifyGive(service_task);}
    void set_audio(int mode){if(mode==1||mode==3||mode==4)audio_busy=true;audio_mode=mode;if(audio_task)xTaskNotifyGive(audio_task);}

    XPowersAXP2101 pmu;
    SensorQMI8658 imu;
    i2c_master_bus_handle_t bus=nullptr;
    i2c_master_dev_handle_t rtc=nullptr;
    esp_io_expander_handle_t expander=nullptr;
    bool pmu_ok=false,imu_ok=false,wifi_initialized=false,wifi_started=false;
    std::atomic<bool> wifi_wanted{false}, reconnect{false}, scan_done{false}, ble_ready{false}, ble_wanted{false}, synced{false};
    std::atomic<uint16_t> connection{BLE_HS_CONN_HANDLE_NONE};
    std::atomic<int> audio_mode{0}, volume{35};
    std::atomic<bool> audio_busy{false};
    std::atomic<uint64_t> retry_at{0};
    std::atomic<int> retries{0};
    uint8_t address_type=0;
    bool nimble_ok=false;
    uint64_t sync_deadline=0;
    int16_t* recording=nullptr;
    size_t recorded_samples=0;
    static Impl* instance;
    template<class F> void change(F fn){std::lock_guard<std::mutex> guard(mutex);fn(state);}
    void notice(TextId text){change([&](DeviceState& d){d.message=text;++d.message_seq;});}
    bool probe(int address){return i2c_master_probe(bus,address,30)==ESP_OK;}
    void write_rtc(time_t epoch){
        if(!rtc)return;
        tm t{};gmtime_r(&epoch,&t);
        uint8_t values[]={0x04,bcd(t.tm_sec),bcd(t.tm_min),bcd(t.tm_hour),bcd(t.tm_mday),bcd(t.tm_wday),bcd(t.tm_mon+1),bcd((t.tm_year+1900)%100)};
        if(i2c_master_transmit(rtc,values,sizeof(values),100)!=ESP_OK)notice(TextId::HardwareError);
    }
    void read_rtc(){
        uint8_t reg=4,values[7];
        if(!rtc||i2c_master_transmit_receive(rtc,&reg,1,values,sizeof(values),100)!=ESP_OK)return;
        if(values[0]&0x80)return; // Oscillator-stop flag means calendar contents cannot be trusted.
        tm t{};t.tm_sec=unbcd(values[0]&0x7f);t.tm_min=unbcd(values[1]&0x7f);t.tm_hour=unbcd(values[2]&0x3f);
        t.tm_mday=unbcd(values[3]&0x3f);t.tm_mon=unbcd(values[5]&0x1f)-1;t.tm_year=100+unbcd(values[6]);
        if(t.tm_year<120||t.tm_year>199||t.tm_mon<0||t.tm_mon>11||t.tm_mday<1||t.tm_mday>31||t.tm_hour>23||t.tm_min>59||t.tm_sec>59)return;
        timeval tv{.tv_sec=utc_epoch(&t),.tv_usec=0};settimeofday(&tv,nullptr);
    }
    void diagnose(){
        std::vector<int> found;
        for(int address:{0x18,0x20,0x34,0x40,0x51,0x5a,0x6b})if(probe(address))found.push_back(address);
        change([&](DeviceState& d){d.i2c_addresses=found;d.touch=std::find(found.begin(),found.end(),0x5a)!=found.end();d.audio_available=std::find(found.begin(),found.end(),0x18)!=found.end()&&std::find(found.begin(),found.end(),0x40)!=found.end();});
        ESP_LOGI(TAG,"I2C devices=%u PMU=%d IMU=%d RTC=%d",(unsigned)found.size(),pmu_ok,imu_ok,rtc!=nullptr);
    }
    void initialize_hardware(){
        bus=bsp_i2c_get_handle();
        pmu_ok=probe(0x34)&&pmu.begin(bus,0x34);
        if(pmu_ok){pmu.enableBattDetection();pmu.enableBattVoltageMeasure();pmu.enableVbusVoltageMeasure();}
        imu_ok=probe(0x6b)&&imu.begin(bus,0x6b);
        if(imu_ok){imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,SensorQMI8658::ACC_ODR_125Hz);imu.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,SensorQMI8658::GYR_ODR_112_1Hz);imu.enableAccelerometer();imu.enableGyroscope();}
        if(probe(0x51)){i2c_device_config_t cfg{};cfg.dev_addr_length=I2C_ADDR_BIT_LEN_7;cfg.device_address=0x51;cfg.scl_speed_hz=100000;i2c_master_bus_add_device(bus,&cfg,&rtc);read_rtc();}
        if(probe(0x20)){expander=bsp_io_expander_init();if(expander)esp_io_expander_set_dir(expander,IO_EXPANDER_PIN_NUM_4,IO_EXPANDER_INPUT);}
        gpio_config_t gpio{};gpio.pin_bit_mask=1ULL<<GPIO_NUM_0;gpio.mode=GPIO_MODE_INPUT;gpio.pull_up_en=GPIO_PULLUP_ENABLE;gpio_config(&gpio);
        change([&](DeviceState& d){d.pmu=pmu_ok;d.imu=imu_ok;d.rtc=rtc!=nullptr;});diagnose();
    }
    void poll_hardware(bool slow){
        float ax=0,ay=0,az=0,gx=0,gy=0,gz=0;
        bool sampled=!suspended&&imu_ok&&imu.getAccelerometer(ax,ay,az)&&imu.getGyroscope(gx,gy,gz);
        uint32_t power=0;if(!suspended&&expander)esp_io_expander_get_level(expander,IO_EXPANDER_PIN_NUM_4,&power);
        bool battery=false,charging=false,usb=false;int percent=-1,mv=0;
        if(slow&&pmu_ok){battery=pmu.isBatteryConnect();charging=pmu.isCharging();usb=pmu.isVbusIn();if(battery){percent=pmu.getBatteryPercent();mv=pmu.getBattVoltage();}}
        bool online=false;if(slow){std::lock_guard<std::mutex> guard(mutex);online=state.wifi_connected;}
        wifi_ap_record_t ap{};bool have_ap=slow&&online&&esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
        change([&](DeviceState& d){
            if(sampled){++d.imu_samples;d.accel={ax,ay,az};d.gyro={gx,gy,gz};}
            d.imu=imu_ok;if(!suspended){d.boot_pressed=gpio_get_level(GPIO_NUM_0)==0;d.power_pressed=(power&IO_EXPANDER_PIN_NUM_4)!=0;}d.usb_host=usb_serial_jtag_is_connected();
            if(slow){d.battery_present=battery;d.charging=battery&&charging;d.usb=usb;d.battery=percent<=100?percent:-1;d.millivolts=mv;d.heap_free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);d.psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);if(have_ap)d.rssi=ap.rssi;}
        });
    }
    static void wifi_event(void* arg,esp_event_base_t base,int32_t id,void* data){
        auto* p=static_cast<Impl*>(arg);
        if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){
            auto* event=static_cast<ip_event_got_ip_t*>(data);char ip[20];snprintf(ip,sizeof(ip),IPSTR,IP2STR(&event->ip_info.ip));
            wifi_ap_record_t ap{};esp_wifi_sta_get_ap_info(&ap);p->retries=0;p->retry_at=0;
            p->change([&](DeviceState& d){d.wifi_connected=true;d.wifi_connecting=false;d.ip=ip;d.rssi=ap.rssi;d.ssid=std::string(reinterpret_cast<char*>(ap.ssid),strnlen(reinterpret_cast<char*>(ap.ssid),32));});p->notice(TextId::Connected);
        }else if(base==WIFI_EVENT&&id==WIFI_EVENT_SCAN_DONE){p->scan_done=true;
        }else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){
            p->change([](DeviceState& d){d.wifi_connected=false;d.wifi_connecting=false;d.ip.clear();});
            if(p->wifi_wanted&&p->reconnect){int attempt=++p->retries;p->retry_at=now_ms()+(attempt<=3?2000:30000);p->notice(TextId::WifiFailed);}
        }
        p->notify_service();
    }
    void initialize_wifi(){
        esp_err_t err=esp_netif_init();if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE)return;
        err=esp_event_loop_create_default();if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE)return;
        if(!esp_netif_create_default_wifi_sta())return;
        wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();if(esp_wifi_init(&config)!=ESP_OK)return;
        esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,this);esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,this);
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);esp_wifi_set_mode(WIFI_MODE_STA);wifi_initialized=true;
    }
    void wifi_enable(bool on){
        if(on&&!wifi_initialized)initialize_wifi();
        if(!on&&!wifi_initialized){change([](DeviceState& d){d.wifi_on=false;});return;}
        if(!wifi_initialized){notice(TextId::HardwareError);return;}
        wifi_wanted=on;
        if(on&&!wifi_started){if(esp_wifi_start()!=ESP_OK){notice(TextId::Failed);return;}wifi_started=true;wifi_config_t cfg{};
            if(esp_wifi_get_config(WIFI_IF_STA,&cfg)==ESP_OK&&cfg.sta.ssid[0]){reconnect=true;retries=0;esp_wifi_connect();change([](DeviceState& d){d.wifi_connecting=true;});}}
        if(!on&&wifi_started){reconnect=false;retry_at=0;esp_wifi_scan_stop();esp_wifi_disconnect();esp_wifi_stop();wifi_started=false;change([](DeviceState& d){d.wifi_connected=false;d.wifi_connecting=false;d.wifi_scanning=false;d.networks.clear();d.ip.clear();});}
        change([&](DeviceState& d){d.wifi_on=on;});
    }
    void collect_networks(){
        uint16_t count=24;wifi_ap_record_t records[24]{};
        if(esp_wifi_scan_get_ap_records(&count,records)!=ESP_OK){notice(TextId::ScanFailed);count=0;}
        std::vector<Network> found;
        for(unsigned i=0;i<count;i++){std::string ssid(reinterpret_cast<char*>(records[i].ssid),strnlen(reinterpret_cast<char*>(records[i].ssid),32));if(ssid.empty())continue;
            if(std::any_of(found.begin(),found.end(),[&](const Network& n){return n.ssid==ssid;}))continue;
            found.push_back({ssid,records[i].rssi,records[i].authmode!=WIFI_AUTH_OPEN});}
        change([&](DeviceState& d){d.networks=found;d.wifi_scanning=false;});
    }
    static void ble_sync(){auto* p=instance;if(p){p->ble_ready=ble_hs_id_infer_auto(0,&p->address_type)==0;p->notify_service();}}
    static void ble_task(void*){nimble_port_run();nimble_port_freertos_deinit();}
    static int service_found(uint16_t,const ble_gatt_error* error,const ble_gatt_svc* service,void* arg){
        auto* p=static_cast<Impl*>(arg);if(error->status==0&&service){char uuid[BLE_UUID_STR_LEN];ble_uuid_to_str(&service->uuid.u,uuid);p->change([&](DeviceState& d){if(d.gatt_services.size()<32)d.gatt_services.push_back(uuid);});}
        else if(error->status!=BLE_HS_EDONE)p->notice(TextId::BleFailed);return 0;
    }
    static int gap_event(ble_gap_event* event,void* arg){
        auto* p=static_cast<Impl*>(arg);
        switch(event->type){
        case BLE_GAP_EVENT_DISC:{
            if(!p->ble_wanted)break;
            ble_hs_adv_fields fields{};ble_hs_adv_parse_fields(&fields,event->disc.data,event->disc.length_data);
            char address[18];auto& a=event->disc.addr.val;snprintf(address,sizeof(address),"%02X:%02X:%02X:%02X:%02X:%02X",a[5],a[4],a[3],a[2],a[1],a[0]);
            std::string name=fields.name?std::string(reinterpret_cast<const char*>(fields.name),fields.name_len):"";
            p->change([&](DeviceState& d){auto it=std::find_if(d.ble_devices.begin(),d.ble_devices.end(),[&](const BleDevice& b){return b.address==address;});
                if(it!=d.ble_devices.end()){it->rssi=event->disc.rssi;if(!name.empty())it->name=name;}
                else if(d.ble_devices.size()<24)d.ble_devices.push_back({name,address,event->disc.rssi,event->disc.addr.type});});break;}
        case BLE_GAP_EVENT_DISC_COMPLETE:p->change([](DeviceState& d){d.ble_scanning=false;});break;
        case BLE_GAP_EVENT_CONNECT:
            if(event->connect.status==0&&p->ble_wanted){p->connection=event->connect.conn_handle;p->change([](DeviceState& d){d.ble_connecting=false;d.ble_connected=true;d.gatt_services.clear();});p->notice(TextId::Connected);ble_gattc_disc_all_svcs(event->connect.conn_handle,service_found,p);}
            else{if(event->connect.status==0)ble_gap_terminate(event->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM);p->change([](DeviceState& d){d.ble_connecting=false;d.ble_connected=false;});if(p->ble_wanted)p->notice(TextId::BleFailed);}break;
        case BLE_GAP_EVENT_DISCONNECT:p->connection=BLE_HS_CONN_HANDLE_NONE;p->change([](DeviceState& d){d.ble_connected=false;d.ble_connecting=false;d.gatt_services.clear();});p->notice(TextId::Disconnected);break;
        default:break;
        }return 0;
    }
    void initialize_ble(){instance=this;nimble_ok=nimble_port_init()==ESP_OK;if(nimble_ok){ble_hs_cfg.sync_cb=ble_sync;nimble_port_freertos_init(ble_task);}}
    bool stop_ble(){
        ble_wanted=false;ble_ready=false;
        if(nimble_ok){
            // nimble_port_stop waits for disconnect/host completion; deinit releases controller PM locks.
            if(nimble_port_stop()!=0)return false;
            if(nimble_port_deinit()!=ESP_OK)return false;
            nimble_ok=false;
        }
        connection=BLE_HS_CONN_HANDLE_NONE;
        change([](DeviceState& d){d.ble_on=d.ble_scanning=d.ble_connecting=d.ble_connected=false;d.gatt_services.clear();});
        return true;
    }
    bool suspend_radios_on_battery(){
        if(radios_suspended)return true;
        {std::lock_guard<std::mutex> guard(mutex);
            if(state.usb||state.usb_host)return true;
            if(!radio_restore_captured){resume_wifi=state.wifi_on;resume_ble=state.ble_on;
                resume_peer=state.ble_connected?state.ble_address:"";resume_name=state.ble_name;resume_peer_type=ble_peer_type;radio_restore_captured=true;}
        }
        esp_sntp_stop();sync_deadline=0;wifi_enable(false);scan_done=false;
        bool ok=stop_ble();radios_suspended=ok;ESP_LOGI(TAG,"RADIOS_SUSPENDED wifi_restore=%d ble_restore=%d ok=%d",resume_wifi,resume_ble,ok);return ok;
    }
    bool suspend_hardware(){
        if(suspended)return true;
        {std::lock_guard<std::mutex> guard(mutex);if(state.recording||state.playing||audio_busy.load()||audio_mode.load()!=0)return false;}
        poll_hardware(true);
        suspended=true;
        // No other task accesses IMU. Keep touch, RTC and existing PMU rails powered.
        if(imu_ok&&(!imu.disableAccelerometer()||!imu.disableGyroscope()))return false;
        if(!suspend_radios_on_battery())return false;
        change([](DeviceState& d){d.peripherals_suspended=true;});
        return true;
    }
    void restore_hardware(){
        if(!suspended)return;
        suspended=false;radios_suspended=false;radio_restore_captured=false;
        if(imu_ok){imu.enableAccelerometer();imu.enableGyroscope();}
        change([](DeviceState& d){d.peripherals_suspended=false;});
        poll_hardware(true);
        if(resume_wifi)wifi_enable(true);
        if(resume_ble){process({Op::BleEnable,1});reconnect_ble=!resume_peer.empty();ble_restore_deadline=now_ms()+5000;}
        resume_wifi=resume_ble=false;
    }
    void sd_mount(bool mount){
        bool mounted=false;{std::lock_guard<std::mutex> guard(mutex);mounted=state.sd_mounted;}
        if(mount==mounted)return;
        esp_err_t result=mount?bsp_sdcard_mount():bsp_sdcard_unmount();uint64_t total=0,free=0;
        if(result==ESP_OK&&mount)esp_vfs_fat_info(BSP_SD_MOUNT_POINT,&total,&free);
        if(result==ESP_OK)change([&](DeviceState& d){d.sd_mounted=mount;d.sd_missing=false;d.sd_total=total;d.sd_free=free;});
        else if(mount)change([](DeviceState& d){d.sd_missing=true;});
        notice(result==ESP_OK?(mount?TextId::Mounted:TextId::Unmounted):(mount?TextId::NoCard:TextId::StorageFailed));
    }
    void process(const Command& c){
        switch(c.op){
        case Op::PowerSuspend:suspend_ok=suspend_hardware();suspend_ack=c.value;xSemaphoreGive(suspend_done);break;
        case Op::PowerResume:restore_hardware();break;
        case Op::ScreenSleep:case Op::ScreenWake:break;
        case Op::Brightness:if(!suspended)assistant_display_brightness_set(std::clamp(c.value,0,100));break;
        case Op::Volume:volume=std::clamp(c.value,0,100);break;
        case Op::WifiEnable:wifi_enable(c.value!=0);break;
        case Op::WifiScan:{if(!wifi_started){notice(TextId::WifiOff);break;}wifi_scan_config_t scan{};scan.show_hidden=false;
            if(esp_wifi_scan_start(&scan,false)==ESP_OK)change([](DeviceState& d){d.wifi_scanning=true;});else notice(TextId::ScanFailed);break;}
        case Op::WifiConnect:{
            if(!wifi_started){notice(TextId::WifiOff);break;}if(c.text.empty()||c.text.size()>32||(!c.extra.empty()&&(c.extra.size()<8||c.extra.size()>63))){notice(TextId::InvalidPassword);break;}
            reconnect=false;retry_at=0;esp_wifi_disconnect();wifi_config_t cfg{};memcpy(cfg.sta.ssid,c.text.data(),c.text.size());memcpy(cfg.sta.password,c.extra.data(),c.extra.size());cfg.sta.pmf_cfg.capable=true;
            if(esp_wifi_set_config(WIFI_IF_STA,&cfg)!=ESP_OK){notice(TextId::WifiFailed);break;}reconnect=true;retries=0;
            if(esp_wifi_connect()!=ESP_OK)notice(TextId::WifiFailed);else change([&](DeviceState& d){d.ssid=c.text;d.wifi_connecting=true;});break;}
        case Op::WifiDisconnect:case Op::WifiForget:
            reconnect=false;retry_at=0;esp_wifi_disconnect();if(c.op==Op::WifiForget){wifi_config_t empty{};esp_wifi_set_config(WIFI_IF_STA,&empty);change([](DeviceState& d){d.ssid.clear();});}change([](DeviceState& d){d.wifi_connected=false;d.wifi_connecting=false;d.ip.clear();});notice(TextId::Disconnected);break;
        case Op::BleEnable:
            if(!c.value){if(!stop_ble())notice(TextId::BleFailed);break;}
            if(!nimble_ok)initialize_ble();
            if(!nimble_ok){notice(TextId::HardwareError);break;}
            ble_wanted=true;change([](DeviceState& d){d.ble_on=true;});break;
        case Op::BleScan:{
            if(!ble_wanted){notice(TextId::BleOff);break;}if(!ble_ready){notice(TextId::QueueBusy);break;}
            ble_gap_disc_cancel();ble_gap_disc_params params{};params.passive=0;params.filter_duplicates=1;params.itvl=160;params.window=80;
            change([](DeviceState& d){d.ble_devices.clear();d.ble_scanning=true;});
            if(ble_gap_disc(address_type,5000,&params,gap_event,this)!=0){change([](DeviceState& d){d.ble_scanning=false;});notice(TextId::ScanFailed);}break;}
        case Op::BleConnect:{
            if(!ble_wanted||!ble_ready){notice(TextId::BleOff);break;}
            if(connection!=BLE_HS_CONN_HANDLE_NONE){notice(TextId::QueueBusy);break;}
            ble_peer_type=c.value;unsigned a[6];ble_addr_t address{};address.type=c.value;
            if(sscanf(c.text.c_str(),"%x:%x:%x:%x:%x:%x",&a[5],&a[4],&a[3],&a[2],&a[1],&a[0])!=6){notice(TextId::BleFailed);break;}
            for(int i=0;i<6;i++)address.val[i]=a[i];ble_gap_disc_cancel();
            change([&](DeviceState& d){d.ble_scanning=false;d.ble_connecting=true;d.ble_address=c.text;d.ble_name=c.extra;});
            if(ble_gap_connect(address_type,&address,10000,nullptr,gap_event,this)!=0){change([](DeviceState& d){d.ble_connecting=false;});notice(TextId::BleFailed);}break;}
        case Op::BleDisconnect:ble_gap_conn_cancel();if(connection!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);break;
        case Op::RecordStart:set_audio(1);break;
        case Op::RecordStop:case Op::AudioStop:set_audio(2);break;
        case Op::AudioPlay:set_audio(3);break;
        case Op::AudioClear:set_audio(5);break;
        case Op::Beep:volume=std::clamp(c.value,0,100);set_audio(4);break;
        case Op::SyncTime:{
            bool online=false;{std::lock_guard<std::mutex> guard(mutex);online=state.wifi_connected;}
            if(!online){notice(TextId::NeedWifi);break;}esp_sntp_stop();synced=false;esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);esp_sntp_setservername(0,"pool.ntp.org");esp_sntp_setservername(1,"time.cloudflare.com");
            esp_sntp_set_time_sync_notification_cb([](timeval*){if(instance){instance->synced=true;instance->notify_service();}});esp_sntp_init();sync_deadline=now_ms()+20000;notice(TextId::Loading);break;}
        case Op::SetTime:{char* end=nullptr;int64_t epoch=strtoll(c.text.c_str(),&end,10);if(!end||*end||epoch<1577836800||epoch>4102444799LL){notice(TextId::InvalidDateTime);break;}timeval tv{.tv_sec=epoch,.tv_usec=0};settimeofday(&tv,nullptr);write_rtc(epoch);notice(TextId::TimeSynced);break;}
        case Op::SdMount:sd_mount(true);break;
        case Op::SdUnmount:sd_mount(false);break;
        case Op::Diagnose:diagnose();notice(TextId::Done);break;
        }
    }
    void work(){
        instance=this;initialize_hardware();wifi_enable(initial.wifi);process({Op::BleEnable,initial.ble});volume=initial.mute?0:initial.volume;
        uint64_t next_poll=0,last_slow=0;
        while(true){
            Command command{};bool pending=false;
            {std::lock_guard<std::mutex> guard(mutex);if(!commands.empty()){command=std::move(commands.front());commands.pop_front();pending=true;}}
            if(pending){process(command);if(command.op==Op::PowerSuspend||command.op==Op::PowerResume)next_poll=0;event();}
            if(scan_done.exchange(false)&&wifi_started)collect_networks();
            auto now=now_ms();
            if(retry_at&&now>=retry_at&&wifi_wanted&&reconnect){retry_at=0;esp_wifi_connect();change([](DeviceState& d){d.wifi_connecting=true;});}
            if(synced.exchange(false)){write_rtc(time(nullptr));sync_deadline=0;notice(TextId::TimeSynced);event();}
            if(sync_deadline&&now>sync_deadline){sync_deadline=0;esp_sntp_stop();notice(TextId::SyncFailed);}
            if(reconnect_ble&&(ble_ready||now>=ble_restore_deadline)){
                reconnect_ble=false;if(ble_ready)process({Op::BleConnect,resume_peer_type,resume_peer,resume_name});else notice(TextId::BleFailed);
            }
            if(now>=next_poll){bool slow=suspended||now-last_slow>=1000;poll_hardware(slow);if(slow)last_slow=now;next_poll=now+(suspended?10000:80);
                if(suspended&&!suspend_radios_on_battery()){
                    // A background radio-stop failure after USB removal is not
                    // a user wake request. Keep the display dark and retry.
                    notice(TextId::SleepFailed);ESP_LOGW(TAG,"RADIO_SUSPEND_RETRY screen remains asleep; next attempt in 10 s");
                }
                if(slow)event();
            }
            // Sleep until real work or the next hardware/deadline sample. Queue arrivals notify immediately.
            {std::lock_guard<std::mutex> guard(mutex);if(!commands.empty())continue;}
            uint64_t due=next_poll;
            if(retry_at)due=std::min(due,retry_at.load());
            if(sync_deadline)due=std::min(due,sync_deadline);
            if(reconnect_ble)due=std::min(due,ble_restore_deadline);
            now=now_ms();ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(due>now?due-now:1));
        }
    }

    void audio(){
        constexpr size_t frames=240,limit=24000*10;
        int16_t input[frames*4],output[frames*2];
        int active=0,last_volume=-1;size_t cursor=0,beep_left=0;
        while(true){
            int requested=audio_mode.exchange(0);
            if(requested){
                if(active){board_audio_close();active=0;change([](DeviceState& d){d.recording=false;d.playing=false;d.audio_level=0;});}
                if(requested==5){if(recording){heap_caps_free(recording);recording=nullptr;}recorded_samples=0;change([](DeviceState& d){d.has_recording=false;d.recorded_ms=0;});}
                else if(requested==1){
                    if(!recording)recording=static_cast<int16_t*>(heap_caps_malloc(limit*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
                    if(recording&&board_audio_record_open()){recorded_samples=0;active=1;last_volume=-1;change([](DeviceState& d){d.recording=true;d.has_recording=false;d.recorded_ms=0;});}
                    else{board_audio_close();notice(TextId::AudioFailed);}
                }else if(requested==3||requested==4){
                    if(requested==3&&!recorded_samples)notice(TextId::NoRecording);
                    else if(board_audio_play_open(volume)){active=requested;cursor=0;beep_left=24000;last_volume=volume;change([](DeviceState& d){d.playing=true;});}
                    else notice(TextId::AudioFailed);
                }
            }
            if(active==1){
                if(!board_audio_read(input,frames*4)){audio_mode=2;notice(TextId::AudioFailed);continue;}
                int peak=0;size_t count=std::min(frames,limit-recorded_samples);
                for(size_t i=0;i<count;i++){int16_t sample=input[i*4];recording[recorded_samples++]=sample;peak=std::max(peak,std::abs(static_cast<int>(sample)));}
                change([&](DeviceState& d){d.has_recording=true;d.recorded_ms=recorded_samples*1000/24000;d.audio_level=peak/32768.0f;});
                if(recorded_samples==limit){audio_mode=2;notice(TextId::Recorded);}
            }else if(active==3||active==4){
                if(last_volume!=volume){board_audio_volume(volume);last_volume=volume;}
                size_t count=active==3?std::min(frames,recorded_samples-cursor):std::min(frames,beep_left);
                for(size_t i=0;i<count;i++){int16_t sample=active==3?recording[cursor+i]:static_cast<int16_t>(2500*std::sin((cursor+i)*2.0*3.141592653589793*880/24000));output[2*i]=output[2*i+1]=sample;}
                if(!count||!board_audio_write(output,count*2)){audio_mode=2;if(count)notice(TextId::AudioFailed);}else{cursor+=count;if(active==4)beep_left-=count;}
            }else {audio_busy=false;if(audio_mode.load()==0)ulTaskNotifyTake(pdTRUE,portMAX_DELAY);}
        }
    }
    void usb(){
        char bytes[128];std::string frame;frame.reserve(2048);bool overflow=false;
        while(true){
            // This IDF API waits for the requested byte count, not just any data.
            // Block for the first byte; drain the rest without a timeout wake-up.
            int n=usb_serial_jtag_read_bytes(bytes,1,portMAX_DELAY);
            if(n>0)n+=usb_serial_jtag_read_bytes(bytes+n,sizeof(bytes)-n,0);
            for(int i=0;i<n;i++){
            if(bytes[i]=='\n'){
                if(!overflow&&!frame.empty()){
                    // Developer fault injection exercises the paused touch
                    // filter without submitting a host action that wakes UI.
                    if(frame=="@UI power test empty-touch"){
                        if(power_command)power_command(power_context,{Op::ScreenWake,static_cast<int>(WakeReason::Touch)});
                    }else{
                        std::lock_guard<std::mutex> guard(mutex);
                        if(frame.rfind("@UI ",0)==0){if(ui_commands.size()<16)ui_commands.push_back(frame.substr(4));}
                        else if(quota.size()<8)quota.push_back(frame);
                    }
                    event();
                }frame.clear();overflow=false;
            }else if(bytes[i]!='\r'){if(frame.size()<2047)frame+=bytes[i];else overflow=true;}
        }}
    }
};
EspPlatform::Impl* EspPlatform::Impl::instance=nullptr;
EspPlatform::EspPlatform():impl_(new Impl){}
EspPlatform::~EspPlatform()=default;
void EspPlatform::start(){
    impl_->initial=load_settings();impl_->suspend_done=xSemaphoreCreateBinary();assert(impl_->suspend_done);
    ESP_ERROR_CHECK(xTaskCreate([](void* p){static_cast<Impl*>(p)->work();},"system_services",12288,impl_.get(),4,&impl_->service_task)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate([](void* p){static_cast<Impl*>(p)->audio();},"audio_service",8192,impl_.get(),5,&impl_->audio_task)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate([](void* p){static_cast<Impl*>(p)->usb();},"quota_receive",6144,impl_.get(),3,nullptr)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
}
uint64_t EspPlatform::millis() const{return now_ms();}
int64_t EspPlatform::utc() const{return time(nullptr);}
int64_t EspPlatform::utc_millis() const{timeval tv{};gettimeofday(&tv,nullptr);return tv.tv_sec*1000LL+tv.tv_usec/1000;}
Settings EspPlatform::load_settings(){
    nvs_handle_t handle;if(nvs_open("assistant_ui",NVS_READONLY,&handle)!=ESP_OK)return {};
    size_t size=0;std::string raw;if(nvs_get_str(handle,"settings",nullptr,&size)==ESP_OK&&size<8192){raw.resize(size);if(nvs_get_str(handle,"settings",raw.data(),&size)!=ESP_OK)raw.clear();}nvs_close(handle);return decode_settings(raw);
}
bool EspPlatform::save_settings(const Settings& s){
    nvs_handle_t handle;if(nvs_open("assistant_ui",NVS_READWRITE,&handle)!=ESP_OK)return false;
    auto raw=encode_settings(s);bool ok=nvs_set_str(handle,"settings",raw.c_str())==ESP_OK&&nvs_commit(handle)==ESP_OK;nvs_close(handle);return ok;
}
void EspPlatform::request(Command command){
    if(command.op==Op::ScreenSleep||command.op==Op::ScreenWake){if(impl_->power_command)impl_->power_command(impl_->power_context,std::move(command));return;}
    {std::lock_guard<std::mutex> guard(impl_->mutex);if(impl_->commands.size()<32)impl_->commands.push_back(std::move(command));else{impl_->state.message=TextId::QueueBusy;++impl_->state.message_seq;}}
    impl_->notify_service();
}
void EspPlatform::power_callbacks(void (*command)(void*,Command),void (*event)(void*),void* context){impl_->power_command=command;impl_->power_event=event;impl_->power_context=context;}
void EspPlatform::power_state(PowerState state,WakeReason reason){impl_->change([&](DeviceState& d){d.power=state;d.wake_reason=reason;});}
bool EspPlatform::suspend_peripherals(){
    int id=++impl_->suspend_generation;request({Op::PowerSuspend,id});const auto until=now_ms()+4000;
    while(now_ms()<until){if(impl_->suspend_ack==id)return impl_->suspend_ok; auto now=now_ms();if(now>=until)break;xSemaphoreTake(impl_->suspend_done,pdMS_TO_TICKS(until-now));}
    return false;
}
void EspPlatform::resume_peripherals(){request({Op::PowerResume});}
bool EspPlatform::take_sleep_query(std::string& line){
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if(impl_->ui_commands.empty()||(impl_->ui_commands.front()!="state"&&impl_->ui_commands.front()!="power state"&&impl_->ui_commands.front()!="power history"&&impl_->ui_commands.front()!="power boot"))return false;
    line=std::move(impl_->ui_commands.front());impl_->ui_commands.pop_front();return true;
}
bool EspPlatform::has_wake_command(){std::lock_guard<std::mutex> guard(impl_->mutex);return std::any_of(impl_->ui_commands.begin(),impl_->ui_commands.end(),[](const auto& c){return c!="state"&&c!="power state"&&c!="power history"&&c!="power boot";});}

DeviceState EspPlatform::snapshot(){std::lock_guard<std::mutex> guard(impl_->mutex);return impl_->state;}
bool EspPlatform::take_quota(std::string& line){std::lock_guard<std::mutex> guard(impl_->mutex);if(impl_->quota.empty())return false;line=std::move(impl_->quota.front());impl_->quota.pop_front();return true;}
bool EspPlatform::take_ui_command(std::string& line){std::lock_guard<std::mutex> guard(impl_->mutex);if(impl_->ui_commands.empty())return false;line=std::move(impl_->ui_commands.front());impl_->ui_commands.pop_front();return true;}
void EspPlatform::host_command(const char* text){printf("%s\n",text);fflush(stdout);}
void EspPlatform::log(const std::string& text){ESP_LOGI("assistant_ui","%s",text.c_str());}
}
