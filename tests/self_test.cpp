#include "shell.h"
#include "sim_platform.h"
#include "fonts.h"
#include "blue_ui_assets.h"
#include "touch_report.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace {
assistant::SimPlatform* test_platform=nullptr;
lv_point_t pointer{};lv_indev_state_t pointer_state=LV_INDEV_STATE_RELEASED;
void read_pointer(lv_indev_t*,lv_indev_data_t* data){data->point=pointer;data->state=pointer_state;}
uint32_t ticks(){return static_cast<uint32_t>(test_platform->millis());}
void flush(lv_display_t* d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);}
void require(bool pass,const char* name){if(!pass){fprintf(stderr,"SELF_TEST_FAIL %s\n",name);std::exit(1);}}
size_t objects(lv_obj_t* p){size_t count=1;for(uint32_t i=0;i<lv_obj_get_child_count(p);++i)count+=objects(lv_obj_get_child(p,i));return count;}
bool visible_image_source(lv_obj_t* obj,const void* source){
    if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return false;
    if(lv_obj_get_style_bg_image_src(obj,LV_PART_MAIN)==source)return true;
    if(lv_obj_check_type(obj,&lv_image_class)&&lv_image_get_src(obj)==source)return true;
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i)if(visible_image_source(lv_obj_get_child(obj,i),source))return true;
    return false;
}
uint32_t next_cp(const unsigned char*& text){uint32_t cp=*text++;if(cp<128)return cp;int n=cp<224?1:cp<240?2:3;cp&=(1u<<(6-n))-1;for(int i=0;i<n;++i)cp=(cp<<6)|(*text++&63);return cp;}
}
int run_self_test(){
    using namespace assistant;
    {
        uint8_t raw[10]={6,14,12,0x90,0,1,0xAB,0,0,0}; // x233/y192
        auto p=touch_packet_decode(raw,466,466);require(p.kind==TOUCH_PACKET_DOWN&&p.x==233&&p.y==192,"valid CST92xx down packet");
        touch_wake_filter_t f{};require(!touch_wake_filter_sample(&f,p,100),"one down report cannot wake");require(!touch_wake_filter_sample(&f,p,105),"reports too close cannot wake");require(touch_wake_filter_sample(&f,p,112),"two fresh stable reports confirm touch");
        raw[0]=0xAB;require(touch_packet_decode(raw,466,466).kind==TOUCH_PACKET_CONSUMED,"ACKed packet cannot invent contact");
        raw[0]=6;raw[4]=0x80;require(touch_packet_decode(raw,466,466).kind==TOUCH_PACKET_GESTURE,"cover gesture is not a touch");raw[4]=0;
        raw[5]=2;require(touch_packet_decode(raw,466,466).kind==TOUCH_PACKET_INVALID,"multi-contact noise is rejected instead of clamped");raw[5]=1;
        raw[0]=0x16;require(touch_packet_decode(raw,466,466).kind==TOUCH_PACKET_INVALID,"invalid track is rejected");raw[0]=6;raw[1]=0xFF;require(touch_packet_decode(raw,466,466).kind==TOUCH_PACKET_INVALID,"out of range packet cannot wake");raw[1]=14;
        raw[0]=4;auto release=touch_packet_decode(raw,466,466);require(release.kind==TOUCH_PACKET_RELEASE,"release event decoded");f={};touch_wake_filter_sample(&f,p,100);require(!touch_wake_filter_sample(&f,release,112)&&!touch_wake_filter_sample(&f,p,125),"isolated down followed by release is rejected");
        f={};touch_wake_filter_sample(&f,p,100);require(!touch_wake_filter_sample(&f,p,180),"expired first contact does not confirm");
        puts("TOUCH_PACKET_SELF_TEST_OK strict_decode=1 stable_wake=1");
    }
    const std::string file="/tmp/round-assistant-test-"+std::to_string(getpid())+".json";
    unlink(file.c_str());SimPlatform platform(file,true);platform.quiet=true;test_platform=&platform;
    Settings settings;settings.animations=false;settings.timeout_s=0;require(platform.save_settings(settings),"save defaults");
    require(decode_settings("bad").locale==Locale::ZhCN,"bad config defaults Chinese");
    require(decode_settings("{\"version\":1,\"locale\":\"xx\",\"brightness\":-1}").brightness==70,"bad values rejected");
    require(decode_settings("{\"version\":1}").sleep_power_save,"legacy settings default to saving");
    settings.sleep_power_save=false;require(!decode_settings(encode_settings(settings)).sleep_power_save,"power setting roundtrip");settings.sleep_power_save=true;
    {
        SimPlatform clock(file+".clock",true);clock.quiet=true;System service(clock);
        service.settings.alarms.clear();require(service.next_deadline_ms()==0,"no deadline without timed work");
        service.timer.duration_minutes=1;service.timer.toggle(clock.millis());
        require(service.next_deadline_ms()==clock.millis()+60000,"countdown uses absolute deadline");
        clock.advance(60001);service.tick();require(service.alarm_ringing&&!service.timer.running,"countdown expires without UI ticks");
        auto seq=service.notice_seq;service.tick();require(service.notice_seq==seq,"countdown delivered once");service.dismiss_alarm();service.timer.reset();
        auto minute=(clock.utc()+service.settings.timezone_min*60)/60+1;
        service.settings.alarms={{static_cast<int>(minute%1440/60),static_cast<int>(minute%60),true}};
        auto deadline=service.next_deadline_ms();require(deadline>clock.millis()&&deadline<=clock.millis()+60000,"next wall clock alarm scheduled");
        clock.advance(deadline-clock.millis()+5);service.tick();require(service.alarm_ringing,"wall clock alarm without UI ticks");
        service.dismiss_alarm();const auto alarm_epoch=clock.utc();seq=service.notice_seq;
        clock.request({Op::SetTime,0,std::to_string(alarm_epoch-120)});service.tick();
        clock.request({Op::SetTime,0,std::to_string(alarm_epoch)});service.tick();require(service.notice_seq==seq+2&&!service.alarm_ringing,"clock rewind does not repeat delivered alarm");
        require(service.next_deadline_ms()>clock.millis()+60000,"delivered alarm rescheduled to a later day");
    }
    settings.locale=Locale::EnUS;settings.alarms={{9,30,true}};require(decode_settings(encode_settings(settings)).alarms[0].minute==30,"settings round trip");
    settings.locale=Locale::ZhCN;settings.alarms.clear();platform.save_settings(settings);
    System system(platform);lv_init();lv_tick_set_cb(ticks);auto* display=lv_display_create(466,466);static uint8_t pixels[466*32*2];
    lv_display_set_buffers(display,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    size_t glyphs=0;
    for(int i=0;i<static_cast<int>(TextId::Count);++i)for(Locale locale:{Locale::ZhCN,Locale::EnUS}){
        const auto* text=reinterpret_cast<const unsigned char*>(tr(static_cast<TextId>(i),locale));require(*text!=0,"translation not empty");
        while(*text){uint32_t cp=next_cp(text);if(cp<32)continue;for(const lv_font_t* font:{&font_ui_18,&font_ui_20,&font_ui_24,&font_ui_28,&font_ui_22_bold,&font_ui_28_bold,&font_cjk_22}){lv_font_glyph_dsc_t desc{};require(font->get_glyph_dsc(font,&desc,cp,0),"all translated glyphs exist");++glyphs;}}
    }
    {
        auto* input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_display(input,display);lv_indev_set_read_cb(input,read_pointer);
        Shell shell(system);shell.create(display);
        auto pump=[&](int ms=300){for(int t=0;t<ms;t+=10){platform.advance(10);shell.tick();lv_timer_handler();}};
        pump();require(shell.has_control("home.apps"),"home has app entry");require(lv_obj_has_flag(lv_obj_get_parent(shell.control("nav.1")),LV_OBJ_FLAG_HIDDEN),"navigation buttons hidden by default");shell.activate("home.apps");pump();require(shell.has_control("app.settings"),"launcher registers settings");shell.activate("app.settings");pump();shell.activate("app.settings");pump();require(shell.page()==Page::Settings,"launcher click opens settings");
        shell.activate("settings.Language");pump();shell.activate("language.en");pump();require(system.settings.locale==Locale::EnUS,"English selected");require(platform.load_settings().locale==Locale::EnUS,"English persisted");
        shell.activate("language.zh");pump();require(platform.load_settings().locale==Locale::ZhCN,"Chinese persisted");
        shell.open(AppId::Settings);pump();shell.show(Page::Sound,false);pump();shell.show(Page::Home,false);pump();shell.open(AppId::Settings);pump();require(shell.page()==Page::Sound,"returning app restores settings subpage");
        shell.open(AppId::Quota);pump();shell.activate("status");pump();shell.activate("quick.language");pump();require(shell.page()==Page::Quick&&system.settings.locale==Locale::EnUS,"quick locale change retains current page");shell.back();pump();require(shell.page()==Page::Quota,"quick settings returns to original app");
        shell.show(Page::Display,false);pump();require(shell.set_value("display.animations","1"),"animation setting controlled");pump();require(lv_obj_get_style_transition(shell.control("nav.1"),LV_PART_MAIN)!=nullptr,"existing navigation animations enabled");shell.set_value("display.animations","0");pump();require(lv_obj_get_style_transition(shell.control("nav.1"),LV_PART_MAIN)->time==0,"existing navigation animations disabled");
        system.timer.countdown=true;system.timer.duration_minutes=1;system.timer.toggle(platform.millis());shell.open(AppId::Timer);pump();shell.show(Page::Home,false);pump(1500);require(system.timer.elapsed(platform.millis())>=1800,"timer continues in background");
        system.set_locale(Locale::EnUS);pump();require(system.timer.running,"language switch preserves running timer");system.timer.reset();
        const std::string quota="{\"type\":\"quota\",\"version\":1,\"seq\":7,\"now\":2000000000,\"age\":0,\"primary\":{\"used\":95,\"minutes\":300,\"reset\":2000000123},\"secondary\":null}";
        platform.receive(quota);pump();require(system.quota.valid&&system.quota.primary.used==95,"quota accepted");auto left=system.quota.remaining_seconds(platform.millis());pump(2100);require(system.quota.remaining_seconds(platform.millis())==left-2,"quota countdown ticks");
        std::string invalid=quota;invalid.replace(invalid.find("95"),2,"101");require(!system.quota.receive(invalid,platform.millis())&&system.quota.seq==7,"invalid quota preserves last state");
        platform.advance(91000);pump();require(system.quota.age(platform.millis())>90,"stale quota detected");
        platform.request({Op::WifiEnable,1});platform.request({Op::WifiScan});pump(600);require(system.device.networks.size()==3,"async Wi-Fi scan");
        platform.scenario("connect-failed");platform.request({Op::WifiConnect,1,"圆屏实验室","wrongpass"});pump(900);require(!system.device.wifi_connected&&system.device.message==TextId::WifiFailed,"Wi-Fi failure");
        platform.scenario("normal");platform.request({Op::WifiConnect,1,"圆屏实验室","test-pass"});pump(900);require(system.device.wifi_connected,"Wi-Fi recovery");
        platform.request({Op::BleEnable,1});platform.request({Op::BleScan});pump(700);require(system.device.ble_devices.size()==2,"BLE scan");platform.request({Op::BleConnect,0,"02:00:00:00:00:01","演示传感器"});pump(700);require(system.device.gatt_services.size()==3,"BLE service discovery");
        platform.request({Op::BleDisconnect});platform.scenario("connect-failed");platform.request({Op::BleConnect,0,"02:00:00:00:00:01","演示传感器"});pump(700);require(!system.device.ble_connected&&system.device.message==TextId::BleFailed,"BLE failure state");platform.scenario("normal");
        shell.open(AppId::Assistant);pump();shell.activate("audio.record");pump(600);require(system.device.recording,"explicit record starts");shell.show(Page::Home,false);pump();require(!system.device.recording,"leaving releases microphone");
        shell.show(Page::Recent,false);pump();shell.close_app(AppId::Assistant);pump();require(!system.device.has_recording,"closing app releases recording buffer");
        platform.scenario("no-card");platform.request({Op::SdMount});pump();require(!system.device.sd_mounted,"no-card state");platform.scenario("hardware-error");pump();require(!system.device.imu&&!system.device.audio_available,"hardware error state");platform.scenario("normal");
        require(!system.set_local_time("2026-02-30 12:00"),"invalid calendar date rejected");require(system.set_local_time("2026-10-09 18:30"),"valid time accepted");pump();require(system.time_text()=="18:30","timezone conversion");
        auto stroke=[&](int x1,int y1,int x2,int y2,int hold=0){pointer={x1,y1};pointer_state=LV_INDEV_STATE_PRESSED;pump(40);for(int step=1;step<=8;++step){pointer={x1+(x2-x1)*step/8,y1+(y2-y1)*step/8};pump(20);}if(hold)pump(hold);pointer_state=LV_INDEV_STATE_RELEASED;pump(360);};
        require(Gesture::zone(233,456)==GestureZone::Bottom,"bottom handle has narrow gesture ownership");
        require(Gesture::zone(180,440)==GestureZone::None&&Gesture::zone(233,430)==GestureZone::None,"bottom content does not belong to system gestures");
        require(Gesture::zone(233,18)==GestureZone::Top&&Gesture::zone(18,233)==GestureZone::Left,"top and back edges remain available");
        Gesture threshold;threshold.begin(GestureZone::Bottom,233,456,1);threshold.move(233,380,30);
        require(threshold.finish(233,400,60)==GestureAction::None,"reversing below 64 cancels after crossing threshold");
        for(int i=0;i<6;++i){
            auto app=static_cast<AppId>(i);shell.open(app);pump();const auto count=shell.recent_count();
            stroke(233,456,233,350);require(shell.page()==Page::Apps&&shell.recent_count()+1==count,"each app exit removes its recent instance");
        }
        shell.open(AppId::Timer);pump();system.timer.reset();system.timer.toggle(platform.millis());
        stroke(233,456,233,350);require(system.timer.running&&shell.page()==Page::Apps,"app exit keeps timer service running");system.timer.reset();
        shell.open(AppId::Settings);pump();shell.show(Page::Display,false);pump();stroke(233,456,233,350);shell.open(AppId::Settings);pump();require(shell.page()==Page::Settings,"exit a subpage resets app entry");
        const auto count_before_hold=shell.recent_count();stroke(233,456,233,350,320);require(shell.page()==Page::Recent&&shell.recent_count()==count_before_hold,"hold opens recents without closing app");
        shell.open(AppId::Settings);pump();
        for(Locale locale:{Locale::ZhCN,Locale::EnUS}){
            system.set_locale(locale);pump();auto* last=shell.control("settings.About");auto* list=lv_obj_get_parent(last);
            lv_obj_scroll_to_y(list,10000,LV_ANIM_OFF);pump();lv_area_t bounds;lv_obj_get_coords(last,&bounds);
            require(bounds.y2<=410&&bounds.y1>=110,"last settings row fits inside circular visible area in both languages");
            stroke(233,426,233,240);require(shell.page()==Page::Settings,"bottom list drag does not exit application");
        }
        shell.show(Page::DateTime,false);pump();shell.activate("time.edit");pump();shell.set_value("input","2028-02-29 12:34");
        const auto draft_recent=shell.recent_count();stroke(233,456,233,350);require(shell.has_control("confirm.cancel")&&shell.page()==Page::SetTime,"exit asks before discarding form");
        shell.activate("confirm.cancel");pump();require(shell.recent_count()==draft_recent&&std::string(lv_textarea_get_text(shell.control("input")))=="2028-02-29 12:34","exit cancellation preserves draft and recent app");
        stroke(233,456,233,350);shell.activate("confirm.accept");pump();require(shell.page()==Page::Apps&&shell.recent_count()+1==draft_recent,"confirmed form exit removes owning app");
        shell.open(AppId::Clock);pump();system.settings.assist_navigation=true;system.save();pump();shell.activate("nav.1");pump();require(shell.page()==Page::Apps,"assistance navigation exits rather than Home");system.settings.assist_navigation=false;system.save();pump();
        // Six apps and representative settings/form pages must expose the same
        // full-screen black canvas; launcher wallpaper may remain allocated hidden.
        const Page black_pages[]={Page::Quota,Page::Clock,Page::Timer,Page::Sensors,Page::Assistant,Page::Settings,Page::Display,Page::Wifi,Page::DateTime,Page::SetTime,Page::TimerOptions,Page::Reference};
        for(Page page:black_pages){
            shell.show(page,false);pump();auto* root=lv_obj_get_parent(shell.control("status"));
            require(lv_obj_get_width(root)==466&&lv_obj_get_height(root)==466,"application canvas fills 466x466 display");
            require(lv_color_eq(lv_obj_get_style_bg_color(root,LV_PART_MAIN),lv_color_black())&&lv_obj_get_style_bg_opa(root,LV_PART_MAIN)==LV_OPA_COVER,"application canvas is opaque pure black");
            require(!visible_image_source(root,&blue_wallpaper),"application and settings pages hide launcher wallpaper");
            for(const lv_image_dsc_t* sheet:{&blue_sheet_warm,&blue_sheet_cool,&blue_sheet_confirm,&blue_sheet_recent,&blue_sheet_task})require(!visible_image_source(root,sheet),"ordinary application pages have no optical sheet background");
            require(!shell.has_control("timer.close")&&!shell.has_control("reference.close"),"applications use gestures instead of close buttons");
        }
        // The reference specimen uses real controls but never mutates a live timer.
        shell.show(Page::Reference,false);pump();const auto reference_duration=system.timer.duration_minutes;
        stroke(233,323,233,323);require(!system.timer.running,"reference primary does not start service");require(std::string(lv_label_get_text(lv_obj_get_child(shell.control("reference.primary"),0)))==tr(TextId::PauseTiming,system.settings.locale),"reference primary has active state");
        stroke(339,254,339,254);require(!lv_obj_has_state(shell.control("reference.switch"),LV_STATE_CHECKED),"reference switch responds to touch");
        stroke(324,382,324,382);require(shell.has_control("confirm.accept"),"reference reset opens confirmation");shell.activate("confirm.accept");pump();require(lv_obj_has_state(shell.control("reference.switch"),LV_STATE_CHECKED)&&system.timer.duration_minutes==reference_duration,"reference reset preserves business state");
        stroke(233,448,233,342);require(shell.page()==Page::Apps,"reference exits to the application list");
        shell.open(AppId::Timer);pump();stroke(233,323,233,323);require(system.timer.running,"new timer primary starts service");stroke(339,254,339,254);require(!system.timer.sound_enabled,"timer sound toggle controls only timer tone");
        stroke(233,323,233,323);require(!system.timer.running&&system.timer.accumulated_ms>0,"new timer primary pauses elapsed state");stroke(324,382,324,382);require(shell.has_control("confirm.accept"),"new timer reset requires confirmation");shell.activate("confirm.accept");pump();
        stroke(146,382,146,382);require(shell.page()==Page::TimerOptions,"timer settings reachable by touch");shell.set_value("timer.minutes","12");shell.activate("timer.options.done");pump();require(shell.page()==Page::Timer&&system.timer.duration_minutes==12,"timer options save and return");system.timer.duration_minutes=1;system.timer.sound_enabled=true;system.timer.reset();
        system.settings.assist_navigation=true;system.save();pump();
        lv_area_t settings_bounds,reset_bounds,nav_bounds;lv_obj_get_coords(shell.control("timer.settings"),&settings_bounds);lv_obj_get_coords(shell.control("timer.reset"),&reset_bounds);lv_obj_get_coords(lv_obj_get_parent(shell.control("nav.1")),&nav_bounds);
        require(settings_bounds.y2<nav_bounds.y1&&reset_bounds.y2<nav_bounds.y1,"timer actions remain above visible assistance navigation");
        stroke(146,357,146,357);require(shell.page()==Page::TimerOptions,"timer settings remains touchable with assistance navigation");
        system.settings.assist_navigation=false;system.save();pump();shell.show(Page::Timer,false);pump();
        shell.show(Page::Apps,false);pump();shell.activate("app.quota");pump();stroke(310,220,125,220);stroke(233,195,233,195);require(shell.page()==Page::Clock,"real launcher swipe and center tap opens adjacent app");
        shell.show(Page::Clock,true);pump(30);shell.show(Page::Apps,false);pump(30);require(shell.page()==Page::Apps,"rapid navigation interrupts entrance rather than waiting 180 ms");
        shell.open(AppId::Clock);pump();stroke(233,448,233,342);require(shell.page()==Page::Apps,"real pointer bottom swipe exits to the application list");
        const auto launcher_recent=shell.recent_count();
        stroke(233,235,233,145);require(shell.page()==Page::Home&&shell.recent_count()==launcher_recent,"launcher content swipe returns to lock home without closing a task");
        shell.show(Page::Apps,false);pump();stroke(233,448,233,342);require(shell.page()==Page::Home,"launcher bottom swipe returns to lock home");
        shell.show(Page::Apps,false);pump();stroke(233,235,233,199);require(shell.page()==Page::Apps,"short launcher content swipe cancels");
        shell.show(Page::Apps,false);pump();
        auto* drag_carousel=lv_obj_get_parent(lv_obj_get_parent(shell.control("app.clock")));lv_obj_scroll_to_x(drag_carousel,160,LV_ANIM_OFF);pump();const int before_drag=lv_obj_get_scroll_x(drag_carousel);
        pointer={380,235};pointer_state=LV_INDEV_STATE_PRESSED;pump(60);pointer={180,235};pump(60);
        require(lv_obj_get_scroll_x(drag_carousel)==before_drag+200,"launcher long drag follows 200 px without one-item clipping");pointer={260,235};pump(60);
        require(lv_obj_get_scroll_x(drag_carousel)==before_drag+120,"launcher reverse follows exactly 80 px");pump(100);require(lv_obj_get_scroll_x(drag_carousel)==before_drag+120,"held launcher does not run snap or inertia");
        pointer_state=LV_INDEV_STATE_RELEASED;pump(300);require(shell.page()==Page::Apps,"long launcher drag does not click an application");require(lv_obj_get_scroll_x(drag_carousel)%160==0,"launcher snaps only after release");
        shell.show(Page::Apps,false);pump();lv_obj_scroll_to_x(lv_obj_get_parent(lv_obj_get_parent(shell.control("app.clock"))),160,LV_ANIM_OFF);pump();
        pointer={233,195};pointer_state=LV_INDEV_STATE_PRESSED;pump(60);
        auto* held_app=shell.control("app.clock");require(lv_obj_has_state(held_app,LV_STATE_PRESSED)&&lv_obj_get_style_bg_opa(held_app,LV_PART_MAIN)==0&&lv_obj_get_style_transform_scale_x(held_app,LV_PART_MAIN)==256,"launcher press has no selected background or affine transform");
        pointer_state=LV_INDEV_STATE_RELEASED;pump(300);shell.show(Page::Apps,false);pump();
        stroke(233,448,233,342,320);require(shell.page()==Page::Recent,"real pointer hold opens recent");
        stroke(18,233,120,233);require(shell.page()==Page::Home,"real pointer left edge returns");
        shell.open(AppId::Clock);pump();stroke(233,448,233,406);require(shell.page()==Page::Clock,"short edge swipe cancels");
        shell.show(Page::Home,false);pump();require(!shell.has_control("home.task"),"minimal home has no task card");
        const auto idle_home_objects=objects(lv_display_get_screen_active(display));
        pointer={233,18};pointer_state=LV_INDEV_STATE_PRESSED;pump(40);pointer={233,60};pump(60);
        require(shell.page()==Page::Home&&shell.has_control("quick.preview.panel"),"pull preview appears before commit");
        auto preview_y=lv_obj_get_y(shell.control("quick.preview.panel"));require(preview_y==-310+42,"whole quick panel tracks pointer displacement one for one");
        pointer={233,35};pump(60);require(lv_obj_get_y(shell.control("quick.preview.panel"))==preview_y-25,"reverse pull has exact one for one displacement");
        pointer_state=LV_INDEV_STATE_RELEASED;pump(300);require(shell.page()==Page::Home&&!shell.has_control("quick.preview.panel"),"short pull cancels to source");
        require(objects(lv_display_get_screen_active(display))==idle_home_objects,"cancelled pull releases preview objects");
        pointer={233,18};pointer_state=LV_INDEV_STATE_PRESSED;pump(40);pointer={233,300};pump(60);
        auto* dragged_panel=shell.control("quick.preview.panel");auto* dragged_switch=shell.control("quick.wifi");
        pointer_state=LV_INDEV_STATE_RELEASED;pump(300);
        require(shell.page()==Page::Quick&&shell.control("quick.wifi")==dragged_switch&&lv_obj_get_parent(lv_obj_get_parent(dragged_switch))==dragged_panel,"release promotes the same panel and controls");
        shell.back();pump();require(shell.page()==Page::Home,"promoted quick restores source");
        shell.show(Page::Settings,false);pump();auto* held_row=shell.control(std::string("settings.")+text_key(TextId::Language));lv_obj_scroll_to_y(lv_obj_get_parent(held_row),0,LV_ANIM_OFF);pump();
        lv_area_t held_bounds;lv_obj_get_coords(held_row,&held_bounds);pointer={static_cast<lv_coord_t>((held_bounds.x1+held_bounds.x2)/2),static_cast<lv_coord_t>((held_bounds.y1+held_bounds.y2)/2)};pointer_state=LV_INDEV_STATE_PRESSED;pump(80);
        require(lv_obj_has_state(held_row,LV_STATE_PRESSED)&&lv_obj_get_style_bg_opa(held_row,LV_PART_MAIN)==0&&lv_obj_get_style_transform_scale_x(held_row,LV_PART_MAIN)==256,"settings row is held without selected background or scale");pointer_state=LV_INDEV_STATE_RELEASED;pump(300);
        shell.open(AppId::Clock);pump();

        stroke(233,18,233,240);require(shell.page()==Page::Quick,"real pointer top swipe opens quick settings");bool wifi_before=system.settings.wifi;stroke(135,205,135,205);require(system.settings.wifi!=wifi_before,"quick switch touch toggles once");stroke(135,205,135,205);require(system.settings.wifi==wifi_before,"quick switch can return to original state");shell.back();pump();require(shell.page()==Page::Clock,"quick preserves source");
        // Reach the threshold and then reverse: release below 64 px cancels.
        pointer={233,448};pointer_state=LV_INDEV_STATE_PRESSED;pump(40);pointer={233,360};pump(80);pointer={233,420};pump(80);pointer_state=LV_INDEV_STATE_RELEASED;pump(300);require(shell.page()==Page::Clock,"reverse gesture cancels");
        shell.open(AppId::Timer);pump();shell.open(AppId::Clock);pump();stroke(233,448,130,448);require(shell.page()==Page::Timer,"bottom horizontal switches to previous recent task");stroke(233,448,335,448);require(shell.page()==Page::Clock,"bottom horizontal reverses recent order");
        shell.open(AppId::Timer);pump();system.timer.toggle(platform.millis());shell.show(Page::Recent,false);pump();auto count=shell.recent_count();stroke(233,272,233,163);require(shell.recent_count()+1==count,"real task swipe closes only one app");require(system.timer.running,"closing task preserves timer");system.timer.reset();
        shell.show(Page::DateTime,false);pump();shell.activate("time.edit");pump();require(shell.set_value("input","2028-02-29 12:34"),"date input accepted");shell.show(Page::Home,false);pump();require(shell.has_control("confirm.cancel")&&shell.page()==Page::SetTime,"unsaved draft blocks navigation");shell.activate("confirm.cancel");pump();require(shell.page()==Page::SetTime&&std::string(lv_textarea_get_text(shell.control("input")))=="2028-02-29 12:34","cancel preserves draft");
        shell.show(Page::Quick,false);pump();shell.show(Page::Recent,false);pump();require(shell.has_control("confirm.cancel")&&shell.page()==Page::Quick,"quick panel cannot bypass draft protection");shell.activate("confirm.cancel");pump();shell.back();pump();shell.show(Page::Home,false);pump();require(shell.has_control("confirm.accept"),"draft remains dirty through quick panel");shell.activate("confirm.cancel");pump();shell.activate("input.save");pump();require(shell.page()==Page::DateTime&&system.time_text()=="12:34","date wheels preserve leap-day validation");
        shell.show(Page::AddAlarm,false);pump();shell.set_value("alarm.hour","10");pump();system.timer.duration_minutes=1;system.timer.toggle(platform.millis());platform.advance(60010);pump();require(system.alarm_ringing&&shell.has_control("alert.stop"),"alarm gets blocking full-screen layer");stroke(233,448,233,342);require(shell.page()==Page::AddAlarm&&system.alarm_ringing,"exit gesture cannot dismiss alarm");shell.activate("alert.stop");pump();require(!system.alarm_ringing&&shell.page()==Page::AddAlarm,"alarm restores editing scene");shell.show(Page::Home,false);pump();require(shell.has_control("confirm.accept"),"alarm preserves dirty form");shell.activate("confirm.accept");pump();system.timer.reset();
        shell.show(Page::Home,false);pump();system.settings.timeout_s=1;lv_display_trigger_activity(display);pump(1200);require(shell.debug_state().find("asleep=1")!=std::string::npos,"idle enters sleep");system.set_locale(Locale::ZhCN);pump(200);stroke(233,448,233,448);require(shell.debug_state().find("asleep=0")!=std::string::npos,"first edge touch wakes after settings changed asleep");system.settings.timeout_s=0;
        // Logical sleep/wake and pointer tests share the actual firmware Shell.
        platform.scenario("battery");platform.request({Op::WifiEnable,1});platform.request({Op::BleEnable,0});pump();
        const auto powered_objects=objects(lv_display_get_screen_active(display));
        for(int i=0;i<100;++i){
            shell.sleep_now();pump(20);require(shell.asleep()&&system.device.power==PowerState::Standby,"optimized standby state");
            require(!system.device.wifi_on&&!system.device.ble_on,"battery standby suspends radios");
            auto samples=system.device.imu_samples;pump(40);require(system.device.imu_samples==samples,"standby does not sample IMU");
            stroke(233,233,233,233);require(!shell.asleep()&&shell.page()==Page::Home,"first pointer only wakes");
            require(system.device.wifi_on&&!system.device.ble_on,"wake restores original wireless enablement");
        }
        require(objects(lv_display_get_screen_active(display))==powered_objects,"100 touch wake cycles retain no extra objects");
        platform.scenario("sleep-failed");shell.sleep_now();pump();require(!shell.asleep()&&system.device.power==PowerState::Awake,"failed sleep returns to usable UI");
        platform.scenario("normal");system.settings.sleep_power_save=false;shell.sleep_now();pump();require(system.device.power==PowerState::DisplayOff,"saving disabled uses display only mode");stroke(233,233,233,233);system.settings.sleep_power_save=true;
        platform.request({Op::RecordStart});pump(30);shell.sleep_now();pump(30);require(!shell.asleep(),"recording blocks sleep");platform.request({Op::RecordStop});pump();
        shell.show(Page::DateTime,false);pump();shell.activate("time.edit");pump();shell.set_value("input","2028-02-29 12:34");
        system.timer.reset();system.timer.duration_minutes=1;system.timer.toggle(platform.millis());shell.sleep_now();pump(20);
        platform.advance(60010);pump(30);require(!shell.asleep()&&system.alarm_ringing&&shell.has_control("alert.stop"),"timer wakes sleeping form");
        shell.activate("alert.stop");pump();require(shell.page()==Page::SetTime&&std::string(lv_textarea_get_text(shell.control("input")))=="2028-02-29 12:34","alarm wake preserves draft");
        system.timer.reset();shell.activate("input.cancel");pump();if(shell.has_control("confirm.accept")){shell.activate("confirm.accept");pump();}
        printf("POWER_SELF_TEST_OK touch_cycles=100 deadline_tests=6 failure_recovery=1 audio_inhibitor=1 draft_preserved=1\n");
        shell.show(Page::Home,false);pump();size_t before=objects(lv_display_get_screen_active(display));
        for(int i=0;i<100;++i){auto app=static_cast<AppId>(i%6);shell.open(app);pump(20);shell.show(Page::Recent,false);pump(20);shell.close_app(app);pump(20);shell.show(Page::Home,false);pump(20);}
        require(objects(lv_display_get_screen_active(display))==before,"100 navigation cycles retain no extra UI objects");
        shell.show(Page::Home,false);pump();const auto overlay_objects=objects(lv_display_get_screen_active(display));
        for(int i=0;i<20;++i){shell.show(Page::Quick,false);pump();shell.back();pump();}
        require(objects(lv_display_get_screen_active(display))==overlay_objects,"repeated cached quick backdrops release UI objects");
        system.set_locale(Locale::ZhCN);pump();
        printf("SELF_TEST_OK glyph_checks=%zu navigation_cycles=100 widgets_before=%zu widgets_after=%zu\n",glyphs,before,objects(lv_display_get_screen_active(display)));
        puts("SELF_TEST_OK real pointer gestures, confirmation drafts, blocking alerts, locale persistence, timer background, quota stale/validation, Wi-Fi/BLE failures, audio lifecycle, hardware missing, date validation");
        printf("SELF_TEST_OK fullscreen_black_pages=%zu no_visible_wallpaper no_application_glass_sheet no_close_buttons\n",sizeof(black_pages)/sizeof(black_pages[0]));
    }
    lv_deinit();unlink(file.c_str());return 0;
}
