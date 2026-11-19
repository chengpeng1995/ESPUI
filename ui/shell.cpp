#include "shell.h"
#include "fonts.h"
#include "blue_theme.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/core/lv_obj_event_private.h"
#include "core/easing/ease.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

extern "C" {
LV_IMAGE_DECLARE(icon_watch_face);
LV_IMAGE_DECLARE(icon_stopwatch);
LV_IMAGE_DECLARE(icon_imu);
LV_IMAGE_DECLARE(icon_setup);
LV_IMAGE_DECLARE(icon_fft);
}
namespace assistant {
namespace {
constexpr uint32_t FG=theme::fg, MUTED=theme::muted, PANEL=0x172237, ACCENT=theme::accent;
using Handler=std::function<void(lv_event_t*)>;
void on(lv_obj_t* obj,lv_event_code_t code,Handler fn) {
    struct Hook { lv_event_code_t code; Handler fn; };
    auto* hook=new Hook{code,std::move(fn)};
    lv_obj_add_event_cb(obj,[](lv_event_t* e) {
        auto* h=static_cast<Hook*>(lv_event_get_user_data(e));
        if(lv_event_get_code(e)==LV_EVENT_DELETE)delete h;
        else if(lv_event_get_code(e)==h->code)h->fn(e);
    },LV_EVENT_ALL,hook);
}
void plain(lv_obj_t* obj) {
    lv_obj_remove_style_all(obj); lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(obj,&font_cjk_22,0); lv_obj_set_style_text_color(obj,lv_color_hex(FG),0);
}
void fade_opacity(void* obj,int32_t value){lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj),value,0);}
void button_motion(lv_obj_t* obj,bool enabled,bool scale) {
    static const lv_style_prop_t properties[]={LV_STYLE_TRANSFORM_SCALE_X,LV_STYLE_TRANSFORM_SCALE_Y,LV_STYLE_TRANSLATE_Y,LV_STYLE_BG_COLOR,LV_STYLE_PROP_INV};
    static lv_style_transition_dsc_t transition;
    static const lv_style_prop_t no_properties[]={LV_STYLE_PROP_INV};
    static lv_style_transition_dsc_t disabled;
    lv_style_transition_dsc_init(&transition,properties,lv_anim_path_ease_out,120,0,nullptr);
    lv_style_transition_dsc_init(&disabled,no_properties,lv_anim_path_linear,0,0,nullptr);
    lv_obj_set_style_transition(obj,enabled?&transition:&disabled,0);
    lv_obj_set_style_transform_scale(obj,enabled&&scale?246:256,LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(obj,enabled&&!scale?2:0,LV_STATE_PRESSED);
}
void set_text(lv_obj_t* obj,const std::string& value) {
    if(obj&&value!=lv_label_get_text(obj))lv_label_set_text(obj,value.c_str());
}
std::string pct(int n) { return std::to_string(n)+"%"; }
std::string fixed(float value,int precision=2) { char b[48];snprintf(b,sizeof(b),"%.*f",precision,static_cast<double>(value));return b; }
std::string memory(uint64_t bytes) { return fixed(static_cast<float>(bytes)/(1024*1024),1)+" MB"; }
TextId page_title(Page p) {
    switch(p) {
    case Page::Home:return TextId::Home; case Page::Apps:return TextId::Apps; case Page::Recent:return TextId::Recent; case Page::Quick:return TextId::QuickSettings;
    case Page::Quota:return TextId::Quota; case Page::Clock:return TextId::Clock; case Page::Timer:return TextId::Timer;
    case Page::Sensors:return TextId::Sensors; case Page::Assistant:return TextId::Assistant; case Page::Settings:return TextId::Settings;
    case Page::Reference:return TextId::ComponentPreview;case Page::TimerOptions:return TextId::TimerSettings;
    case Page::Language:return TextId::Language; case Page::Display:return TextId::Display; case Page::Touch:return TextId::TouchTest;
    case Page::Wifi:case Page::WifiPassword:return TextId::Wifi; case Page::Ble:return TextId::Bluetooth;
    case Page::BleDevice:return TextId::DeviceInfo; case Page::Sound:return TextId::Sound; case Page::Audio:return TextId::AudioTest;
    case Page::Power:return TextId::Power; case Page::DateTime:return TextId::DateTime; case Page::SetTime:return TextId::SetDateTime;
    case Page::Storage:return TextId::StorageHardware; case Page::Hardware:return TextId::Hardware; case Page::About:return TextId::About;
    case Page::Licenses:return TextId::Licenses; case Page::Alarms:return TextId::Alarm; case Page::AddAlarm:return TextId::AddAlarm;
    }
    return TextId::Home;
}
class RegisteredApp:public mooncake::AppAbility {
    Platform& platform_; std::string id_;
public:
    RegisteredApp(Platform& p,const std::string& id):platform_(p),id_(id) { setAppInfo().name=id; }
    void onOpen() override { platform_.log("APP_OPEN "+id_); }
    void onClose() override { platform_.log("APP_BACKGROUND "+id_); }
};
constexpr std::array<const char*,6> stable_ids={"quota","clock","timer","sensors","assistant","settings"};
constexpr std::array<Page,6> app_pages={Page::Quota,Page::Clock,Page::Timer,Page::Sensors,Page::Assistant,Page::Settings};
}

Shell::Shell(System& s):sys_(s) {
    last_pages_=app_pages;
    for(int i=0;i<6;++i)app_handles_[i]=apps_.installApp(std::make_unique<RegisteredApp>(s.platform,stable_ids[i]));
}
Shell::~Shell() { apps_.uninstallAllApps(); if(root_)lv_obj_delete(root_);if(quick_backdrop_){lv_image_cache_drop(quick_backdrop_);lv_draw_buf_destroy(quick_backdrop_);} }
void Shell::register_control(lv_obj_t* obj,const std::string& id) { if(!id.empty())controls_[id]=obj; }
lv_obj_t* Shell::control(const std::string& id) const { auto i=controls_.find(id);return i==controls_.end()?nullptr:i->second; }
bool Shell::has_control(const std::string& id) const { return control(id)!=nullptr; }
bool Shell::set_value(const std::string& id,const std::string& value){
    auto* obj=control(id);if(!obj)return false;
    if(lv_obj_check_type(obj,&lv_slider_class)){lv_slider_set_value(obj,std::atoi(value.c_str()),LV_ANIM_OFF);lv_obj_send_event(obj,LV_EVENT_VALUE_CHANGED,nullptr);lv_obj_send_event(obj,LV_EVENT_RELEASED,nullptr);}
    else if(lv_obj_check_type(obj,&lv_switch_class)){if(value!="0"&&value!="1")return false;if(value=="1")lv_obj_add_state(obj,LV_STATE_CHECKED);else lv_obj_remove_state(obj,LV_STATE_CHECKED);lv_obj_send_event(obj,LV_EVENT_VALUE_CHANGED,nullptr);}
    else if(lv_obj_check_type(obj,&lv_dropdown_class)){int i=std::atoi(value.c_str());if(i<0||static_cast<unsigned>(i)>=lv_dropdown_get_option_count(obj))return false;lv_dropdown_set_selected(obj,i);lv_obj_send_event(obj,LV_EVENT_VALUE_CHANGED,nullptr);}
    else if(lv_obj_check_type(obj,&lv_roller_class)){int i=std::atoi(value.c_str());if(i<0||i>=static_cast<int>(lv_roller_get_option_count(obj)))return false;lv_roller_set_selected(obj,i,LV_ANIM_OFF);lv_obj_send_event(obj,LV_EVENT_VALUE_CHANGED,nullptr);}
    else if(lv_obj_check_type(obj,&lv_textarea_class)){lv_textarea_set_text(obj,value.c_str());}
    else return false;return true;
}
void Shell::activate(const std::string& id) { if(auto* obj=control(id)){if(lv_obj_check_type(obj,&lv_switch_class)){if(lv_obj_has_state(obj,LV_STATE_CHECKED))lv_obj_remove_state(obj,LV_STATE_CHECKED);else lv_obj_add_state(obj,LV_STATE_CHECKED);lv_obj_send_event(obj,LV_EVENT_VALUE_CHANGED,nullptr);}else lv_obj_send_event(obj,LV_EVENT_CLICKED,nullptr);} }
void Shell::create(lv_display_t* display) {
    lv_timer_set_period(lv_display_get_refr_timer(display),16);
    display_=display;auto* screen=lv_display_get_screen_active(display);lv_obj_set_style_bg_color(screen,lv_color_black(),0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    root_=lv_obj_create(screen);plain(root_);lv_obj_set_size(root_,466,466);lv_obj_center(root_);lv_obj_set_style_radius(root_,LV_RADIUS_CIRCLE,0);
    // The physical panel already clips its corners. Software clipping forces a
    // full 466 x 466 compositing layer for every scroll frame on the ESP32.
    lv_obj_set_style_radius(root_,sys_.device.simulated?LV_RADIUS_CIRCLE:0,0);
    lv_obj_set_style_clip_corner(root_,sys_.device.simulated,0);lv_obj_set_style_bg_opa(root_,255,0);lv_obj_set_style_bg_color(root_,lv_color_black(),0);wallpaper_=theme::background(root_);
    auto* status_button=button(root_,"","status",[this]{if(!overlay_&&!alarm_layer_)show(Page::Quick);},230);lv_obj_set_pos(status_button,118,28);lv_obj_set_height(status_button,34);lv_obj_set_style_min_height(status_button,34,0);lv_obj_set_style_bg_opa(status_button,0,0);lv_obj_set_style_bg_opa(status_button,0,LV_STATE_PRESSED);lv_obj_set_style_border_width(status_button,0,0);
    status_=lv_obj_get_child(status_button,0);lv_obj_set_style_text_font(status_,&font_ui_18,0);
    title_=label(root_,t(TextId::Apps),340,&font_ui_28);lv_obj_set_pos(title_,63,70);lv_obj_set_style_text_align(title_,LV_TEXT_ALIGN_CENTER,0);
    nav_=lv_obj_create(root_);plain(nav_);lv_obj_set_pos(nav_,124,387);lv_obj_set_size(nav_,218,56);
    const char* symbols[]={LV_SYMBOL_LEFT,LV_SYMBOL_CLOSE,LV_SYMBOL_LIST};
    for(int i=0;i<3;++i) {
        auto* b=button(nav_,symbols[i],std::string("nav.")+std::to_string(i),[this,i]{if(i==0)back();else if(i==1)exit_app();else show(Page::Recent);},64);
        lv_obj_set_pos(b,i*77,0);lv_obj_set_height(b,56);theme::glass(b);
        lv_obj_set_style_text_font(lv_obj_get_child(b,0),&lv_font_montserrat_20,0);
    }
    toast_=label(root_,"",302,&font_ui_18);lv_obj_set_pos(toast_,91,321);lv_obj_set_width(toast_,284);lv_obj_set_style_text_align(toast_,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_bg_color(toast_,lv_color_hex(0x162B4A),0);lv_obj_set_style_bg_opa(toast_,240,0);lv_obj_set_style_radius(toast_,12,0);lv_obj_set_style_pad_all(toast_,8,0);lv_obj_add_flag(toast_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(toast_,LV_OBJ_FLAG_CLICKABLE);
    home_handle_=lv_obj_create(root_);plain(home_handle_);lv_obj_set_size(home_handle_,64,4);lv_obj_set_pos(home_handle_,201,452);lv_obj_set_style_radius(home_handle_,3,0);lv_obj_set_style_bg_opa(home_handle_,120,0);lv_obj_set_style_bg_color(home_handle_,lv_color_hex(0xC3D7F5),0);lv_obj_remove_flag(home_handle_,LV_OBJ_FLAG_CLICKABLE);
    create_gesture_edge();
    on(root_,LV_EVENT_GESTURE,[this](lv_event_t*){if(overlay_||alarm_layer_||asleep_)return;auto* input=lv_indev_active();if(!input)return;auto dir=lv_indev_get_gesture_dir(input);if(page_==Page::Home&&dir==LV_DIR_LEFT){lv_indev_wait_release(input);show(Page::Apps);}else if(page_==Page::Quota&&(dir==LV_DIR_LEFT||dir==LV_DIR_RIGHT)){lv_indev_wait_release(input);sys_.next_quota();}});
    language_rev_=sys_.revision;build();update_status();lv_display_trigger_activity(display_);
}
void Shell::stop_audio_on_leave(Page target) {
    bool leaving=(page_==Page::Assistant||page_==Page::Audio)&&(target!=page_);
    if(leaving){sys_.platform.request({Op::RecordStop});sys_.platform.request({Op::AudioStop});}
}
void Shell::show(Page p,bool animated) {
    if(alarm_layer_)return;
    if(overlay_)return;
    if(p!=Page::Quick&&p!=page_&&form_dirty_&&(page_==Page::WifiPassword||page_==Page::SetTime||page_==Page::AddAlarm||(page_==Page::Quick&&p!=quick_return_))){
        confirm_action(t(TextId::DiscardTitle),t(TextId::DiscardMessage),[this,p,animated]{form_dirty_=false;input_text_.clear();show(p,animated);},TextId::Discard,TextId::KeepEditing);return;
    }
    if(p==Page::SetTime&&input_text_.empty())input_text_=sys_.date_text()+" "+sys_.time_text();
    if(p!=page_&&page_!=Page::Quick&&(p==Page::WifiPassword||p==Page::SetTime||p==Page::AddAlarm)){form_original_=input_text_;form_dirty_=false;}
    if(p==Page::DateTime&&page_!=Page::SetTime)time_return_=page_;
    // A new navigation intent interrupts the old entrance; it must not wait
    // another 180 ms before accepting the next screen or gesture.
    if(p!=page_){animation_at_=0;if(body_){lv_obj_set_x(body_,0);lv_obj_set_style_opa(body_,255,0);}}
    if(p==Page::Quick&&page_!=Page::Quick){
        quick_return_=page_;
        if(quick_backdrop_){lv_image_cache_drop(quick_backdrop_);lv_draw_buf_destroy(quick_backdrop_);}
        if(quick_preview_)lv_obj_add_flag(quick_preview_,LV_OBJ_FLAG_HIDDEN);
        quick_backdrop_=theme::capture_backdrop(root_);
        if(quick_preview_)lv_obj_remove_flag(quick_preview_,LV_OBJ_FLAG_HIDDEN);
    }
    stop_audio_on_leave(p);
    if(list_&&p!=page_)scroll_positions_[page_]=lv_obj_get_scroll_y(list_);
    if(p==Page::Home||p==Page::Recent||p==Page::Apps){if(active_app_>=0)apps_.closeApp(app_handles_[active_app_]);active_app_=-1;}
    pending_=p;dirty_=true;animate_=animated&&sys_.settings.animations;
}
void Shell::request_rebuild(bool animate) {pending_=page_;dirty_=true;animate_=animate&&sys_.settings.animations;}
void Shell::open(AppId app) {
    if(alarm_layer_||overlay_)return;
    if(form_dirty_){confirm_action(t(TextId::DiscardTitle),t(TextId::DiscardMessage),[this,app]{form_dirty_=false;input_text_.clear();open(app);},TextId::Discard,TextId::KeepEditing);return;}
    int i=static_cast<int>(app);if(active_app_>=0&&active_app_!=i)apps_.closeApp(app_handles_[active_app_]);
    active_app_=i;apps_.openApp(app_handles_[i]);recent_.erase(std::remove(recent_.begin(),recent_.end(),app),recent_.end());recent_.insert(recent_.begin(),app);
    show(last_pages_[i]);
}
void Shell::close_app(AppId app) {
    switch_order_.clear();
    const int index=static_cast<int>(app);apps_.uninstallApp(app_handles_[index]);app_handles_[index]=apps_.installApp(std::make_unique<RegisteredApp>(sys_.platform,stable_ids[index]));last_pages_[index]=app_pages[index];recent_.erase(std::remove(recent_.begin(),recent_.end(),app),recent_.end());
    if(app==AppId::Assistant||app==AppId::Settings)sys_.platform.request({Op::AudioClear});
    sys_.platform.log("APP_CLOSE "+std::string(stable_ids[static_cast<int>(app)]));request_rebuild();
}
void Shell::exit_app() {
    if(alarm_layer_||asleep_)return;
    if(overlay_){dismiss_overlay();return;}
    if(page_==Page::Quick){back();return;}
    if(page_==Page::Home)return;
    if(page_==Page::Apps){show(Page::Home);return;}
    if(page_==Page::Recent){show(Page::Apps);return;}
    if(form_dirty_){
        confirm_action(t(TextId::DiscardTitle),t(TextId::DiscardMessage),[this]{form_dirty_=false;input_text_.clear();exit_app();},TextId::Discard,TextId::KeepEditing);
        return;
    }
    // Page ownership also covers diagnostics that open a settings subpage
    // directly. In normal navigation active_app_ preserves Clock/Settings ownership.
    int index=active_app_;
    if(index<0){
        index=static_cast<int>(AppId::Settings);
        for(int i=0;i<6;++i)if(page_==app_pages[i])index=i;
        if(page_==Page::TimerOptions||page_==Page::Reference)index=static_cast<int>(AppId::Timer);
        if(page_==Page::Alarms||page_==Page::AddAlarm)index=static_cast<int>(AppId::Clock);
    }
    stop_audio_on_leave(Page::Apps);
    active_app_=-1;close_app(static_cast<AppId>(index));
    home_index_=index;show(Page::Apps);
}
void Shell::back() {
    if(alarm_layer_)return;
    if(overlay_){dismiss_overlay();return;}
    switch(page_) {
    case Page::Quick:show(quick_return_);break;
    case Page::TimerOptions:show(Page::Timer);break;case Page::Reference:show(Page::Apps);break;
    case Page::WifiPassword:show(Page::Wifi);break;case Page::BleDevice:show(Page::Ble);break;
    case Page::SetTime:show(Page::DateTime);break;case Page::DateTime:show(time_return_);break;case Page::Touch:show(Page::Display);break;
    case Page::Audio:show(Page::Sound);break;case Page::Hardware:show(Page::Storage);break;case Page::Licenses:show(Page::About);break;
    case Page::AddAlarm:show(Page::Alarms);break;case Page::Alarms:show(Page::Clock);break;
    case Page::Language:case Page::Display:case Page::Wifi:case Page::Ble:case Page::Sound:case Page::Power:case Page::Storage:case Page::About:show(Page::Settings);break;
    default:show(Page::Home);break;
    }
}
void Shell::build_home() {
    lv_obj_set_pos(body_,0,0);lv_obj_set_height(body_,466);
    dynamic_[0]=label(body_,sys_.time_text(),306,&font_digits_64);lv_obj_set_pos(dynamic_[0],80,162);lv_obj_set_style_text_align(dynamic_[0],LV_TEXT_ALIGN_CENTER,0);
    dynamic_[1]=label(body_,sys_.date_text(),306,&font_cjk_22,MUTED);lv_obj_set_pos(dynamic_[1],80,248);lv_obj_set_style_text_align(dynamic_[1],LV_TEXT_ALIGN_CENTER,0);
    auto* apps=button(body_,t(TextId::HomeHint),"home.apps",[this]{show(Page::Apps);},252);lv_obj_set_pos(apps,107,344);lv_obj_set_height(apps,56);lv_obj_set_style_bg_opa(apps,0,0);lv_obj_set_style_border_width(apps,0,0);lv_obj_set_style_text_font(lv_obj_get_child(apps,0),&font_ui_18,0);
    lv_obj_set_style_bg_opa(apps,0,LV_STATE_PRESSED);
    update_home();
}
void Shell::update_home() {
    set_text(dynamic_[0],sys_.time_text());set_text(dynamic_[1],sys_.date_text());
}
void Shell::build_apps() {
    launcher_gesture_.begin(GestureZone::None,0,0,0);launcher_dragged_=false;launcher_axis_=0;
    carousel_=lv_obj_create(body_);plain(carousel_);lv_obj_set_pos(carousel_,0,12);lv_obj_set_size(carousel_,466,218);
    // Pointer drag owns displacement; native SCROLL_ONE used to clip long swipes.
    lv_obj_add_flag(carousel_,LV_OBJ_FLAG_PRESS_LOCK);lv_obj_remove_flag(carousel_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(carousel_,LV_DIR_HOR);lv_obj_set_scrollbar_mode(carousel_,LV_SCROLLBAR_MODE_OFF);lv_obj_set_style_anim_duration(carousel_,180,0);
    lv_obj_set_flex_flow(carousel_,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_hor(carousel_,153,0);lv_obj_set_style_pad_column(carousel_,0,0);
    std::array<lv_obj_t*,6> large{},names{};
    bind_launcher_return(carousel_);
    for(int i=0;i<6;++i){auto* tile=lv_obj_create(carousel_);plain(tile);lv_obj_set_size(tile,160,218);lv_obj_add_flag(tile,LV_OBJ_FLAG_SNAPPABLE);lv_obj_remove_flag(tile,LV_OBJ_FLAG_CLICKABLE);
        auto* hit=button(tile,"","app."+std::string(stable_ids[i]),[this,i]{if(launcher_dragged_||std::abs(launcher_gesture_.dy())>=12)return;if(home_index_!=i){home_index_=i;lv_obj_scroll_to_x(carousel_,i*160,sys_.settings.animations?LV_ANIM_ON:LV_ANIM_OFF);}else open(static_cast<AppId>(i));},144);lv_obj_set_pos(hit,8,24);lv_obj_set_height(hit,190);lv_obj_set_style_pad_all(hit,0,0);lv_obj_set_style_bg_opa(hit,0,0);lv_obj_set_style_border_width(hit,0,0);lv_obj_remove_flag(hit,LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_add_flag(hit,LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_style_bg_opa(hit,0,LV_STATE_PRESSED);lv_obj_set_style_border_width(hit,0,LV_STATE_PRESSED);bind_launcher_return(hit);
        large[i]=theme::app_icon(hit,i,124);lv_obj_set_pos(large[i],10,0);
        lv_image_set_pivot(large[i],62,62);
        names[i]=label(hit,t(app_titles[i]),144,&font_cjk_22);lv_obj_set_pos(names[i],0,139);lv_obj_set_height(names[i],30);lv_obj_set_style_text_align(names[i],LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(names[i],LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(names[i],lv_color_hex(i==home_index_?FG:MUTED),0);
    }
    indicator_.create(body_,271);lv_obj_update_layout(carousel_);lv_obj_scroll_to_x(carousel_,home_index_*160,LV_ANIM_OFF);
    auto update=[this,large,names]{
        const int scroll=lv_obj_get_scroll_x(carousel_);const int previous=home_index_;home_index_=std::clamp<int>((scroll+80)/160,0,5);indicator_.update(home_index_*466);
        for(int i=0;i<6;++i){
            const int distance=std::min(160,std::abs(i*160-scroll));
            theme::carousel_icon(large[i],i,distance);
            if(previous!=home_index_)lv_obj_set_style_text_color(names[i],lv_color_hex(i==home_index_?FG:MUTED),0);
        }
    };
    update();on(carousel_,LV_EVENT_SCROLL,[update](lv_event_t*){update();});
}
void Shell::bind_launcher_return(lv_obj_t* obj){
    on(obj,LV_EVENT_PRESSED,[this](lv_event_t*){lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);lv_obj_stop_scroll_anim(carousel_);launcher_start_scroll_=lv_obj_get_scroll_x(carousel_);launcher_axis_=0;launcher_dragged_=false;launcher_gesture_.begin(GestureZone::Launcher,p.x,p.y,sys_.platform.millis());});
    on(obj,LV_EVENT_PRESSING,[this](lv_event_t*){lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);launcher_gesture_.move(p.x,p.y,sys_.platform.millis());int dx=launcher_gesture_.dx(),dy=launcher_gesture_.dy();if(!launcher_axis_){if(std::abs(dx)>=6&&std::abs(dx)>std::abs(dy)*1.5f)launcher_axis_=1;else if(std::abs(dy)>=12&&std::abs(dy)>std::abs(dx)*1.5f)launcher_axis_=2;}if(launcher_axis_==1){launcher_dragged_=true;lv_obj_scroll_to_x(carousel_,std::clamp(launcher_start_scroll_-dx,0,800),LV_ANIM_OFF);}});
    on(obj,LV_EVENT_RELEASED,[this](lv_event_t*){lv_point_t p;auto* input=lv_indev_active();lv_indev_get_point(input,&p);const auto action=launcher_gesture_.finish(p.x,p.y,sys_.platform.millis());if(launcher_axis_==1){launcher_dragged_=true;const int target=std::clamp((launcher_start_scroll_-launcher_gesture_.dx()+80)/160,0,5);lv_obj_scroll_to_x(carousel_,target*160,sys_.settings.animations?LV_ANIM_ON:LV_ANIM_OFF);}else if(action==GestureAction::Home){lv_indev_wait_release(input);show(Page::Home);}});
    on(obj,LV_EVENT_PRESS_LOST,[this](lv_event_t*){launcher_dragged_=true;launcher_axis_=0;launcher_gesture_.begin(GestureZone::None,0,0,0);});
}
void Shell::build_recent() {
    if(recent_.empty()){auto* hint=label(body_,t(TextId::EmptyTasks),282,&font_cjk_22,MUTED);lv_obj_set_pos(hint,92,94);lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);return;}
    recent_index_=std::clamp(recent_index_,0,static_cast<int>(recent_.size())-1);
    carousel_=lv_obj_create(body_);plain(carousel_);lv_obj_set_size(carousel_,466,245);lv_obj_add_flag(carousel_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(carousel_,LV_OBJ_FLAG_SCROLL_ONE);lv_obj_set_scroll_dir(carousel_,LV_DIR_HOR);lv_obj_set_scrollbar_mode(carousel_,LV_SCROLLBAR_MODE_OFF);lv_obj_set_scroll_snap_x(carousel_,LV_SCROLL_SNAP_CENTER);lv_obj_set_flex_flow(carousel_,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_hor(carousel_,89,0);lv_obj_set_style_pad_column(carousel_,16,0);
    for(AppId app:recent_){int i=static_cast<int>(app);auto* card=button(carousel_,"","recent.open."+std::string(stable_ids[i]),[this,app]{if(!task_dragged_)open(app);},288);lv_obj_set_height(card,222);theme::glass(card);lv_obj_add_flag(card,LV_OBJ_FLAG_SNAPPABLE);
        auto* icon=theme::app_icon(card,i,40);lv_obj_set_pos(icon,12,12);auto* name=label(card,t(app_titles[i]),196,&font_ui_28);lv_obj_set_pos(name,62,14);lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
        const auto summary=app==AppId::Timer?duration_text((sys_.timer.display_ms(sys_.platform.millis())+999)/1000):app==AppId::Clock?sys_.time_text():t(TextId::Ready);
        auto* val=label(card,summary,256,app==AppId::Timer||app==AppId::Clock?&lv_font_montserrat_48:&font_ui_28);lv_obj_set_pos(val,8,87);lv_obj_set_style_text_align(val,LV_TEXT_ALIGN_CENTER,0);
        auto* hint=label(card,t(app==AppId::Timer?TextId::CloseKeepsTimer:TextId::RecentHint),254,&font_ui_18,MUTED);lv_obj_set_pos(hint,8,157);lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
        on(card,LV_EVENT_PRESSED,[this](lv_event_t*){lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);task_dragged_=false;task_gesture_.begin(GestureZone::Task,p.x,p.y,sys_.platform.millis());});
        on(card,LV_EVENT_PRESSING,[this,card](lv_event_t*){lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);task_gesture_.move(p.x,p.y,sys_.platform.millis());if(task_gesture_.dy()<-12&&std::abs(task_gesture_.dy())>std::abs(task_gesture_.dx())*1.5){task_dragged_=true;lv_obj_set_style_translate_y(card,std::max(-100,task_gesture_.dy()),0);}});
        on(card,LV_EVENT_RELEASED,[this,card,app](lv_event_t*){lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);const auto action=task_gesture_.finish(p.x,p.y,sys_.platform.millis());lv_obj_set_style_translate_y(card,0,0);if(action==GestureAction::Close){task_dragged_=true;close_app(app);}});
        on(card,LV_EVENT_PRESS_LOST,[this,card](lv_event_t*){task_dragged_=true;lv_obj_set_style_translate_y(card,0,0);});
    }
    auto* hint=label(body_,t(TextId::RecentHint),310,&font_ui_18,MUTED);lv_obj_set_pos(hint,78,258);lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_update_layout(carousel_);lv_obj_scroll_to_x(carousel_,recent_index_*304,LV_ANIM_OFF);on(carousel_,LV_EVENT_SCROLL,[this](lv_event_t*){recent_index_=std::clamp<int>((lv_obj_get_scroll_x(carousel_)+152)/304,0,static_cast<int>(recent_.size())-1);});
}
void Shell::create_gesture_edge() {
    gesture_edge_=lv_obj_create(root_);plain(gesture_edge_);lv_obj_set_size(gesture_edge_,466,466);lv_obj_add_flag(gesture_edge_,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(gesture_edge_,LV_OBJ_FLAG_ADV_HITTEST);lv_obj_remove_flag(gesture_edge_,LV_OBJ_FLAG_GESTURE_BUBBLE);
    on(gesture_edge_,LV_EVENT_HIT_TEST,[this](lv_event_t* e){auto* info=lv_event_get_hit_test_info(e);info->res=!sys_.settings.assist_navigation&&Gesture::zone(info->point->x,info->point->y)!=GestureZone::None;});
    gesture_hint_=label(root_,"",260,&font_ui_18,0xD8E8FF);lv_obj_set_pos(gesture_hint_,103,385);lv_obj_set_style_text_align(gesture_hint_,LV_TEXT_ALIGN_CENTER,0);lv_obj_add_flag(gesture_hint_,LV_OBJ_FLAG_HIDDEN);
    on(gesture_edge_,LV_EVENT_PRESSED,[this](lv_event_t*){lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);edge_active_=true;edge_gesture_.begin(Gesture::zone(p.x,p.y),p.x,p.y,sys_.platform.millis());});
    on(gesture_edge_,LV_EVENT_PRESSING,[this](lv_event_t*){lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);edge_gesture_.move(p.x,p.y,sys_.platform.millis());if(edge_gesture_.pulling_quick())preview_quick(edge_gesture_.dy());else if(quick_preview_)preview_quick(0);});
    on(gesture_edge_,LV_EVENT_RELEASED,[this](lv_event_t*){lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);edge_active_=false;lv_obj_add_flag(gesture_hint_,LV_OBJ_FLAG_HIDDEN);const auto action=edge_gesture_.finish(p.x,p.y,sys_.platform.millis());if(quick_preview_)finish_quick_preview(action==GestureAction::Quick);perform_gesture(action);});
    on(gesture_edge_,LV_EVENT_PRESS_LOST,[this](lv_event_t*){edge_active_=false;lv_obj_add_flag(gesture_hint_,LV_OBJ_FLAG_HIDDEN);if(quick_preview_)finish_quick_preview(false);});
}
void Shell::perform_gesture(GestureAction action) {
    if(alarm_layer_||asleep_)return;
    if(overlay_){if(action==GestureAction::Back||action==GestureAction::Exit)dismiss_overlay();return;}
    switch(action){case GestureAction::Back:back();break;case GestureAction::Exit:exit_app();break;case GestureAction::Recent:show(Page::Recent);break;case GestureAction::Quick:show(Page::Quick);break;case GestureAction::Home:show(Page::Home);break;case GestureAction::Previous:switch_recent(-1);break;case GestureAction::Next:switch_recent(1);break;default:break;}
}
void Shell::switch_recent(int direction) {
    const auto now=sys_.platform.millis();
    if(switch_order_.empty()||now-switched_at_>1200)switch_order_=recent_;
    if(switch_order_.empty())return;
    auto it=active_app_>=0?std::find(switch_order_.begin(),switch_order_.end(),static_cast<AppId>(active_app_)):switch_order_.end();
    int idx=it==switch_order_.end()?0:static_cast<int>(it-switch_order_.begin());
    // Most recent is index zero; a left swipe advances to older tasks.
    if(it!=switch_order_.end())idx=std::clamp(idx+direction,0,static_cast<int>(switch_order_.size())-1);
    switched_at_=now;open(switch_order_[idx]);
}
void Shell::dismiss_overlay() {
    if(!overlay_)return;auto* old=overlay_;overlay_=nullptr;confirm_callback_={};
    controls_.erase("confirm.cancel");controls_.erase("confirm.accept");
    if(sys_.settings.animations){lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,old);lv_anim_set_values(&a,255,0);lv_anim_set_duration(&a,160);lv_anim_set_exec_cb(&a,fade_opacity);lv_anim_start(&a);lv_obj_delete_delayed(old,160);}else lv_obj_delete_async(old);
}
void Shell::confirm_action(const std::string& title,const std::string& message,std::function<void()> action,TextId accept_text,TextId cancel_text) {
    if(overlay_||alarm_layer_)return;
    confirm_callback_=std::move(action);const auto capture_at=sys_.platform.millis();overlay_=theme::overlay(root_);sys_.platform.log("OVERLAY_BACKDROP ms="+std::to_string(sys_.platform.millis()-capture_at)+" cached="+std::to_string(lv_obj_get_child_count(overlay_)>0));
    auto* panel=lv_obj_create(overlay_);plain(panel);lv_obj_set_pos(panel,78,118);lv_obj_set_size(panel,310,230);theme::sheet(panel);lv_obj_set_style_pad_all(panel,18,0);
    auto* heading=label(panel,title,272,&font_ui_28_bold);lv_obj_set_pos(heading,0,0);lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_LEFT,0);
    auto* copy=label(panel,message,272,&font_ui_18,MUTED);lv_obj_set_pos(copy,0,49);lv_obj_set_style_text_align(copy,LV_TEXT_ALIGN_LEFT,0);
    auto* cancel=button(panel,t(cancel_text),"confirm.cancel",[this]{dismiss_overlay();},130);lv_obj_set_pos(cancel,0,137);lv_obj_set_style_pad_all(cancel,6,0);lv_obj_set_style_text_font(lv_obj_get_child(cancel,0),&font_ui_18,0);
    auto* accept=button(panel,t(accept_text),"confirm.accept",[this]{auto action=std::move(confirm_callback_);dismiss_overlay();if(action)action();},130);lv_obj_set_pos(accept,140,137);lv_obj_set_style_pad_all(accept,6,0);lv_obj_set_style_text_font(lv_obj_get_child(accept,0),&font_ui_18,0);theme::glass(accept,true);
    if(sys_.settings.animations){lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,panel);lv_anim_set_values(&anim,134,118);lv_anim_set_duration(&anim,240);lv_anim_set_path_cb(&anim,lv_anim_path_ease_out);lv_anim_set_exec_cb(&anim,[](void* o,int32_t v){lv_obj_set_y(static_cast<lv_obj_t*>(o),v);});lv_anim_start(&anim);}
    // A back-edge remains available above confirmation; it cancels only the overlay.
    lv_obj_move_foreground(gesture_edge_);
}
void Shell::show_alarm() {
    if(alarm_layer_)return;
    if(quick_preview_)finish_quick_preview(true,true);
    alarm_layer_=theme::overlay(root_);
    auto* material=lv_obj_create(alarm_layer_);plain(material);lv_obj_set_pos(material,73,73);lv_obj_set_size(material,320,320);theme::sheet(material);lv_obj_remove_flag(material,LV_OBJ_FLAG_CLICKABLE);
    auto* title=label(alarm_layer_,t(TextId::AlertTitle),300,&font_ui_28);lv_obj_set_pos(title,83,115);lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    auto* reason=label(alarm_layer_,t(sys_.notice),300,&font_ui_18,MUTED);lv_obj_set_pos(reason,83,168);lv_obj_set_style_text_align(reason,LV_TEXT_ALIGN_CENTER,0);
    auto* value=label(alarm_layer_,sys_.notice==TextId::TimerDone?"00:00":sys_.time_text(),304,&font_digits_64);lv_obj_set_pos(value,81,205);lv_obj_set_style_text_align(value,LV_TEXT_ALIGN_CENTER,0);
    auto* stop=button(alarm_layer_,t(TextId::StopReminder),"alert.stop",[this]{sys_.dismiss_alarm();auto* layer=alarm_layer_;alarm_layer_=nullptr;controls_.erase("alert.stop");lv_obj_delete_async(layer);lv_obj_add_flag(toast_,LV_OBJ_FLAG_HIDDEN);},230);lv_obj_set_pos(stop,118,320);theme::glass(stop,true);
}
void Shell::build() {
    const bool promote_quick=pending_==Page::Quick&&quick_preview_;
    if(body_){if(list_)scroll_positions_[page_]=lv_obj_get_scroll_y(list_);lv_obj_delete(body_);}
    if(pending_!=Page::Quick&&quick_backdrop_){lv_image_cache_drop(quick_backdrop_);lv_draw_buf_destroy(quick_backdrop_);quick_backdrop_=nullptr;}
    page_=pending_;if(active_app_>=0&&page_!=Page::Quick&&page_!=Page::Home&&page_!=Page::Recent&&page_!=Page::Apps&&page_!=Page::Reference)last_pages_[active_app_]=page_;dynamic_.fill(nullptr);wave_.fill(nullptr);list_=carousel_=text_input_=nullptr;
    for(auto it=controls_.begin();it!=controls_.end();) {if(it->first=="status"||it->first.rfind("nav.",0)==0||(promote_quick&&it->first.rfind("quick.",0)==0))++it;else it=controls_.erase(it);}
    if(promote_quick){
        body_=quick_preview_;quick_preview_=nullptr;
        auto* material=quick_preview_panel_;quick_preview_panel_=quick_preview_shade_=nullptr;controls_.erase("quick.preview.panel");
        theme::backdrop(body_,quick_backdrop_);
        settle_quick(material,quick_enter_y_);quick_enter_y_=-310;
    }else {body_=lv_obj_create(root_);plain(body_);lv_obj_set_size(body_,466,344);lv_obj_set_pos(body_,0,110);}
    lv_obj_move_to_index(body_,2);
    set_text(title_,t(page_title(page_)));if(page_==Page::Home||page_==Page::Timer||page_==Page::Reference||page_==Page::Quick)lv_obj_add_flag(title_,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(title_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wallpaper_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(root_,lv_color_black(),0);
    lv_obj_set_style_text_font(title_,&font_ui_28_bold,0);
    lv_obj_set_y(control("status"),28);
    if(page_==Page::Reference)lv_obj_add_flag(control("status"),LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(control("status"),LV_OBJ_FLAG_HIDDEN);
    if(sys_.settings.assist_navigation)lv_obj_remove_flag(nav_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(nav_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(nav_);lv_obj_move_foreground(home_handle_);lv_obj_move_foreground(toast_);lv_obj_move_foreground(gesture_edge_);lv_obj_move_foreground(gesture_hint_);
    const auto& d=sys_.device;auto& s=sys_.settings;
    button_motion(control("status"),false,false);
    for(int i=0;i<3;++i)button_motion(control("nav."+std::to_string(i)),s.animations,true);
    auto item_button=[this](lv_obj_t* list,TextId name,Page target){button(list,t(name),text_key(name),[this,target]{show(target);});};
    switch(page_) {
    case Page::Home:build_home();break;case Page::Apps:build_apps();break;
    case Page::Settings:build_settings();break;
    case Page::Recent:build_recent();break;
    case Page::Quick:if(!promote_quick)build_quick();break;
    case Page::Language:{auto* list=scroll_list();label(list,t(TextId::LanguageHint),LV_PCT(100),&font_ui_18,MUTED);
        button(list,std::string(s.locale==Locale::ZhCN?"●  ":"○  ")+"简体中文","language.zh",[this]{sys_.set_locale(Locale::ZhCN);request_rebuild();});
        button(list,std::string(s.locale==Locale::EnUS?"●  ":"○  ")+"English","language.en",[this]{sys_.set_locale(Locale::EnUS);request_rebuild();});break;}
    case Page::Display:{auto* list=scroll_list();slider(list,TextId::Brightness,s.brightness,5,100,"display.brightness",[this](int v){sys_.settings.brightness=v;sys_.platform.request({Op::Brightness,v});sys_.save();});
        int timeout=s.timeout_s==15?0:s.timeout_s==30?1:s.timeout_s==60?2:3;
        dropdown(list,TextId::ScreenTimeout,std::string(t(TextId::Seconds15))+"\n"+t(TextId::Seconds30)+"\n"+t(TextId::Seconds60)+"\n"+t(TextId::Never),timeout,"display.timeout",[this](int i){const int values[]={15,30,60,0};sys_.settings.timeout_s=values[i];sys_.save();});
        toggle(list,TextId::Animations,s.animations,"display.animations",[this](bool v){sys_.settings.animations=v;sys_.save();});toggle(list,TextId::AssistNavigation,s.assist_navigation,"display.navigation",[this](bool v){sys_.settings.assist_navigation=v;sys_.save();});item_button(list,TextId::TouchTest,Page::Touch);break;}
    case Page::Touch:{auto* list=scroll_list();label(list,t(TextId::TouchHint),LV_PCT(100),&font_ui_18,MUTED);auto* area=lv_obj_create(list);plain(area);lv_obj_set_size(area,LV_PCT(100),190);lv_obj_set_style_bg_color(area,lv_color_hex(PANEL),0);lv_obj_set_style_bg_opa(area,255,0);lv_obj_add_flag(area,LV_OBJ_FLAG_CLICKABLE);
        dynamic_[0]=label(area,"x: --   y: --",LV_PCT(100));lv_obj_center(dynamic_[0]);on(area,LV_EVENT_PRESSING,[this](lv_event_t*){lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);set_text(dynamic_[0],"x: "+std::to_string(p.x)+"   y: "+std::to_string(p.y));});break;}
    case Page::Wifi:{auto* list=scroll_list();toggle(list,TextId::Wifi,s.wifi,"wifi.enable",[this](bool v){sys_.settings.wifi=v;sys_.platform.request({Op::WifiEnable,v});sys_.save();});
        dynamic_[0]=label(list,"",LV_PCT(100),&font_ui_18,ACCENT);
        button(list,t(TextId::Scan),"wifi.scan",[this]{sys_.platform.request({Op::WifiScan});});
        if(d.wifi_connected){row(list,TextId::NetworkName,d.ssid);label(list,d.ip+"  "+std::to_string(d.rssi)+" dBm",LV_PCT(100),&font_ui_18,MUTED);button(list,t(TextId::Disconnect),"wifi.disconnect",[this]{sys_.platform.request({Op::WifiDisconnect});});}
        button(list,t(TextId::Forget),"wifi.forget",[this]{confirm_action(t(TextId::ConfirmAction),t(TextId::ConfirmForget),[this]{sys_.platform.request({Op::WifiForget});},TextId::Forget);});
        for(size_t i=0;i<d.networks.size();++i){auto n=d.networks[i];button(list,n.ssid+"  "+std::to_string(n.rssi),"wifi.network."+std::to_string(i),[this,n]{if(network_!=n.ssid)input_text_.clear();network_=n.ssid;network_secure_=n.secure;if(n.secure)show(Page::WifiPassword);else sys_.platform.request({Op::WifiConnect,0,n.ssid,""});});}
        if(d.networks.empty())label(list,t(TextId::NoNetworks),LV_PCT(100),&font_ui_18,MUTED);break;}
    case Page::WifiPassword:build_keyboard(true);break;
    case Page::Ble:{auto* list=scroll_list();toggle(list,TextId::Bluetooth,s.ble,"ble.enable",[this](bool v){sys_.settings.ble=v;sys_.platform.request({Op::BleEnable,v});sys_.save();});
        dynamic_[0]=label(list,"",LV_PCT(100),&font_ui_18,ACCENT);button(list,t(TextId::Scan),"ble.scan",[this]{sys_.platform.request({Op::BleScan});});
        if(d.ble_connected){button(list,d.ble_name.empty()?d.ble_address:d.ble_name,"ble.connected",[this]{ble_name_=sys_.device.ble_name;ble_address_=sys_.device.ble_address;show(Page::BleDevice);});button(list,t(TextId::Disconnect),"ble.disconnect",[this]{sys_.platform.request({Op::BleDisconnect});});}
        for(size_t i=0;i<d.ble_devices.size();++i){auto b=d.ble_devices[i];button(list,b.name.empty()?b.address:b.name,"ble.device."+std::to_string(i),[this,b]{ble_name_=b.name;ble_address_=b.address;ble_type_=b.address_type;show(Page::BleDevice);});}
        if(d.ble_devices.empty())label(list,t(TextId::NoDevices),LV_PCT(100),&font_ui_18,MUTED);break;}
    case Page::BleDevice:{auto* list=scroll_list();label(list,ble_name_,LV_PCT(100));label(list,ble_address_,LV_PCT(100),&font_ui_18,MUTED);
        const bool selected_connected=d.ble_connected&&d.ble_address==ble_address_;
        button(list,t(selected_connected?TextId::Disconnect:TextId::Connect),"ble.connect",[this]{if(sys_.device.ble_connected&&sys_.device.ble_address==ble_address_)sys_.platform.request({Op::BleDisconnect});else sys_.platform.request({Op::BleConnect,ble_type_,ble_address_,ble_name_});});
        label(list,t(TextId::Services),LV_PCT(100),&font_ui_18,ACCENT);if(selected_connected)for(auto& service:d.gatt_services)label(list,service,LV_PCT(100),&font_ui_18);break;}
    case Page::Sound:{auto* list=scroll_list();slider(list,TextId::Volume,s.volume,0,100,"sound.volume",[this](int v){sys_.settings.volume=v;sys_.platform.request({Op::Volume,sys_.settings.mute?0:v});sys_.save();});
        toggle(list,TextId::Mute,s.mute,"sound.mute",[this](bool v){sys_.settings.mute=v;sys_.platform.request({Op::Volume,v?0:sys_.settings.volume});sys_.save();});item_button(list,TextId::AudioTest,Page::Audio);break;}
    case Page::Audio:build_audio(false);break;case Page::Assistant:build_audio(true);break;
    case Page::Power:{auto* list=scroll_list();toggle(list,TextId::SleepPowerSave,s.sleep_power_save,"power.saving",[this](bool v){sys_.settings.sleep_power_save=v;sys_.save();});button(list,t(TextId::SleepNow),"power.sleep",[this]{sleep_now();});dynamic_[4]=row(list,TextId::PowerMode,"");dynamic_[5]=row(list,TextId::LastWake,"");label(list,t(TextId::PowerHint),LV_PCT(100),&font_ui_18,MUTED);dynamic_[0]=row(list,TextId::Battery,"");dynamic_[1]=row(list,TextId::Charging,"");dynamic_[2]=row(list,TextId::UsbPower,"");dynamic_[3]=row(list,TextId::Voltage,"");break;}
    case Page::DateTime:{auto* list=scroll_list();dynamic_[0]=label(list,sys_.date_text()+" "+sys_.time_text(true),LV_PCT(100));
        button(list,t(TextId::SetDateTime),"time.edit",[this]{input_text_=sys_.date_text()+" "+sys_.time_text();show(Page::SetTime);});
        button(list,t(TextId::SyncTime),"time.sync",[this]{sys_.platform.request({Op::SyncTime});});
        std::string zones;for(int h=-12;h<=14;++h){if(!zones.empty())zones+='\n';zones+="UTC"+std::string(h>=0?"+":"")+std::to_string(h);}
        dropdown(list,TextId::Timezone,zones,s.timezone_min/60+12,"time.zone",[this](int i){sys_.settings.timezone_min=(i-12)*60;sys_.save();});row(list,TextId::Rtc,t(d.rtc?TextId::Found:TextId::NotFound));break;}
    case Page::SetTime:build_keyboard(false);break;
    case Page::Storage:{auto* list=scroll_list();dynamic_[0]=row(list,TextId::SdCard,"");dynamic_[1]=row(list,TextId::Capacity,"");dynamic_[2]=row(list,TextId::FreeSpace,"");
        button(list,t(TextId::Mount),"sd.mount",[this]{sys_.platform.request({Op::SdMount});});button(list,t(TextId::Unmount),"sd.unmount",[this]{sys_.platform.request({Op::SdUnmount});});item_button(list,TextId::Hardware,Page::Hardware);break;}
    case Page::Hardware:{auto* list=scroll_list();button(list,t(TextId::Detect),"hardware.detect",[this]{sys_.platform.request({Op::Diagnose});});
        dynamic_[0]=row(list,TextId::Hardware,"");dynamic_[1]=row(list,TextId::BootButton,"");dynamic_[2]=row(list,TextId::PowerButton,"");row(list,TextId::Rtc,t(d.rtc?TextId::Found:TextId::NotFound));row(list,TextId::Sensors,t(d.imu?TextId::Found:TextId::NotFound));row(list,TextId::Microphone,t(d.audio_available?TextId::Found:TextId::NotFound));break;}
    case Page::About:{auto* list=scroll_list();row(list,TextId::Version,"Round Assistant Blue 2.6.2");row(list,TextId::DeviceInfo,d.simulated?"Mac / SDL2":"ESP32-S3 · 16 MB Flash · 8 MB PSRAM");
        dynamic_[0]=row(list,TextId::Uptime,"");dynamic_[1]=row(list,TextId::FreeMemory,"");item_button(list,TextId::Licenses,Page::Licenses);break;}
    case Page::Licenses:{auto* list=scroll_list();label(list,"M5StopWatch / Mooncake / Smooth UI Toolkit / LVGL / XPowersLib / SensorLib\nMIT\n\nESP-IDF\nApache-2.0\n\nNoto Sans CJK\nSIL OFL 1.1",LV_PCT(100),&font_ui_18);break;}
    case Page::Quota:{auto* list=scroll_list();dynamic_[0]=label(list,"--",LV_PCT(100),&lv_font_montserrat_48,ACCENT);lv_obj_set_style_text_align(dynamic_[0],LV_TEXT_ALIGN_CENTER,0);
        dynamic_[1]=label(list,"",LV_PCT(100),&font_ui_18);dynamic_[2]=label(list,"",LV_PCT(100),&font_ui_18,MUTED);dynamic_[3]=label(list,"",LV_PCT(100),&font_ui_18);
        dynamic_[4]=label(list,"",LV_PCT(100),&font_ui_18,MUTED);dynamic_[5]=label(list,"",LV_PCT(100),&font_ui_18,MUTED);
        button(list,t(TextId::Refresh),"quota.refresh",[this]{sys_.refresh_quota();});button(list,t(TextId::NextQuota),"quota.next",[this]{sys_.next_quota();});dynamic_[6]=label(list,"",LV_PCT(100),&font_ui_18,MUTED);break;}
    case Page::Clock:{auto* list=scroll_list();dynamic_[0]=label(list,sys_.time_text(),LV_PCT(100),&lv_font_montserrat_48);lv_obj_set_style_text_align(dynamic_[0],LV_TEXT_ALIGN_CENTER,0);dynamic_[1]=label(list,sys_.date_text(),LV_PCT(100),&font_cjk_22,MUTED);lv_obj_set_style_text_align(dynamic_[1],LV_TEXT_ALIGN_CENTER,0);
        item_button(list,TextId::Alarm,Page::Alarms);item_button(list,TextId::DateTime,Page::DateTime);break;}
    case Page::Alarms:{auto* list=scroll_list();label(list,t(TextId::AlarmHint),LV_PCT(100),&font_ui_18,MUTED);button(list,t(TextId::AddAlarm),"alarm.add",[this]{if(sys_.settings.alarms.size()>=8){sys_.notify(TextId::AlarmLimit);return;}show(Page::AddAlarm);});
        if(s.alarms.empty())label(list,t(TextId::NoAlarms),LV_PCT(100),&font_ui_18,MUTED);
        for(size_t i=0;i<s.alarms.size();++i){char text[32];snprintf(text,sizeof(text),"%02d:%02d  %s",s.alarms[i].hour,s.alarms[i].minute,t(s.alarms[i].enabled?TextId::On:TextId::Off));
            button(list,text,"alarm.toggle."+std::to_string(i),[this,i]{sys_.settings.alarms[i].enabled=!sys_.settings.alarms[i].enabled;sys_.save();request_rebuild();});button(list,t(TextId::Delete),"alarm.delete."+std::to_string(i),[this,i]{confirm_action(t(TextId::ConfirmAction),t(TextId::ConfirmDelete),[this,i]{if(i<sys_.settings.alarms.size()){sys_.settings.alarms.erase(sys_.settings.alarms.begin()+i);sys_.save();request_rebuild();}},TextId::Delete);});}break;}
    case Page::AddAlarm:{auto* list=scroll_list();std::string hours,minutes;for(int i=0;i<60;++i){char n[4];snprintf(n,sizeof(n),"%02d",i);if(i<24){if(i)hours+='\n';hours+=n;}if(i)minutes+='\n';minutes+=n;}
        dropdown(list,TextId::Clock,hours,alarm_hour_,"alarm.hour",[this](int i){alarm_hour_=i;form_dirty_=true;});dropdown(list,TextId::Minutes,minutes,alarm_minute_,"alarm.minute",[this](int i){alarm_minute_=i;form_dirty_=true;});
        button(list,t(TextId::Save),"alarm.save",[this]{sys_.settings.alarms.push_back({alarm_hour_,alarm_minute_,true});if(sys_.save()){form_dirty_=false;show(Page::Alarms);}});break;}
    case Page::Timer:build_timer();break;case Page::Reference:build_timer(true);break;case Page::TimerOptions:build_timer_options();break;
    case Page::Sensors:{auto* list=scroll_list();dynamic_[0]=row(list,TextId::Acceleration,"");dynamic_[1]=row(list,TextId::Gyroscope,"");
        auto* level=lv_obj_create(list);plain(level);lv_obj_set_size(level,LV_PCT(100),90);lv_obj_set_style_bg_color(level,lv_color_hex(PANEL),0);lv_obj_set_style_bg_opa(level,255,0);lv_obj_set_style_radius(level,20,0);
        dynamic_[2]=lv_obj_create(level);plain(dynamic_[2]);lv_obj_set_size(dynamic_[2],20,20);lv_obj_set_style_bg_color(dynamic_[2],lv_color_hex(ACCENT),0);lv_obj_set_style_bg_opa(dynamic_[2],255,0);lv_obj_set_style_radius(dynamic_[2],LV_RADIUS_CIRCLE,0);
        button(list,t(TextId::Zero),"sensors.zero",[this]{sys_.zero_imu();});break;}
    }
    lv_obj_update_layout(body_);if(list_)lv_obj_scroll_to_y(list_,scroll_positions_[page_],LV_ANIM_OFF);
    dirty_=false;language_rev_=sys_.revision;list_fingerprint_.clear();
    if(animate_&&page_!=Page::Quick)animation_at_=sys_.platform.millis();else animation_at_=0;
    update_content();sys_.platform.log("PAGE "+std::to_string(static_cast<int>(page_))+" locale="+locale_code(s.locale));
}
void Shell::update_status() {
    const auto& d=sys_.device;
    std::string status=page_==Page::Home?"":sys_.time_text()+"   ";
    status+=d.wifi_connected?LV_SYMBOL_WIFI:"·";status+="  ";status+=d.ble_on?LV_SYMBOL_BLUETOOTH:"·";status+="  ";
    status+=d.battery_present?(d.battery>=0?std::to_string(d.battery)+"%":"--%"):(d.usb?"USB":"--");if(d.charging)status+=" " LV_SYMBOL_CHARGE;
    set_text(status_,status);
}
void Shell::update_content() {
    const auto& d=sys_.device;const auto now=sys_.platform.millis();
    if(d.wifi_connected&&page_!=Page::WifiPassword&&!form_dirty_)input_text_.clear();
    switch(page_) {
    case Page::Home:update_home();break;
    case Page::Quota:{const auto& q=sys_.quota;
        if(!q.valid){set_text(dynamic_[1],t(TextId::WaitingMac));break;}
        set_text(dynamic_[0],q.primary.available?fixed(static_cast<float>(100-q.primary.used),0)+"%":"--");
        lv_obj_set_style_text_color(dynamic_[0],lv_color_hex(q.primary.available&&q.primary.used>=90?0xFF7F83:ACCENT),0);
        char window_name[80];if(q.primary.minutes%1440==0)snprintf(window_name,sizeof(window_name),t(TextId::DayWindow),q.primary.minutes/1440);else if(q.primary.minutes%60==0)snprintf(window_name,sizeof(window_name),t(TextId::HourWindow),q.primary.minutes/60);else snprintf(window_name,sizeof(window_name),t(TextId::MinuteWindow),q.primary.minutes);
        set_text(dynamic_[1],q.primary.available?std::string(window_name)+" · "+t(TextId::Remaining):t(TextId::NoWindow));
        set_text(dynamic_[2],q.primary.available?std::string(t(TextId::Used))+" "+fixed(q.primary.used,1)+"%":"--");
        set_text(dynamic_[3],std::string(t(TextId::ResetIn))+"\n"+(q.primary.available?duration_text(q.remaining_seconds(now),true):"--"));
        set_text(dynamic_[4],std::string(t(TextId::OtherWindow))+": "+(q.secondary.available?fixed(100-q.secondary.used,0)+"%":t(TextId::Unavailable)));
        set_text(dynamic_[5],std::string(t(TextId::Credits))+" "+q.credits+" · "+t(TextId::Resets)+" "+(q.resets>=0?std::to_string(q.resets):"--"));
        char age[96];snprintf(age,sizeof(age),t(TextId::UpdatedAgo),static_cast<long long>(q.age(now)));
        set_text(dynamic_[6],std::string(t(q.age(now)>90?TextId::Stale:q.test?TextId::TestData:TextId::LiveData))+"\n"+age+"\n"+q.bucket);break;}
    case Page::Clock:set_text(dynamic_[0],sys_.time_text());set_text(dynamic_[1],sys_.date_text());break;
    case Page::Timer:case Page::Reference:update_timer();break;
    case Page::Wifi:set_text(dynamic_[0],t(!d.wifi_on?TextId::WifiOff:d.wifi_scanning?TextId::Scanning:d.wifi_connecting?TextId::Connecting:d.wifi_connected?TextId::Connected:TextId::NotConnected));break;
    case Page::Ble:set_text(dynamic_[0],t(!d.ble_on?TextId::BleOff:d.ble_scanning?TextId::Scanning:d.ble_connecting?TextId::Connecting:d.ble_connected?TextId::Connected:TextId::NotConnected));break;
    case Page::Audio:case Page::Assistant:{
        set_text(dynamic_[0],t(d.recording?TextId::Recording:d.playing?TextId::Playing:d.has_recording?TextId::Recorded:TextId::AudioHint));
        set_text(dynamic_[1],t(d.recording?TextId::Stop:TextId::Record));
        if(!d.recording&&d.has_recording&&!had_recording_){had_recording_=true;reply_at_=now+600;}
        if(page_==Page::Assistant&&d.has_recording)set_text(dynamic_[2],t(now<reply_at_?TextId::Thinking:TextId::DemoReply));
        std::move(wave_values_.begin()+1,wave_values_.end(),wave_values_.begin());wave_values_.back()=d.audio_level;
        for(int i=0;i<24;++i)if(wave_[i]){int h=4+static_cast<int>(std::min(1.0f,wave_values_[i])*54);lv_obj_set_height(wave_[i],h);lv_obj_set_y(wave_[i],(64-h)/2);}break;}
    case Page::Power:set_text(dynamic_[4],t(power_state_text(d.power)));set_text(dynamic_[5],t(wake_reason_text(d.wake_reason)));set_text(dynamic_[0],!d.pmu?t(TextId::HardwareError):d.battery_present?(d.battery>=0?pct(d.battery):t(TextId::Unavailable)):t(TextId::NoBattery));set_text(dynamic_[1],t(d.charging?TextId::Yes:TextId::No));set_text(dynamic_[2],t(d.usb?TextId::Yes:TextId::No));set_text(dynamic_[3],d.battery_present?std::to_string(d.millivolts)+" mV":"--");break;
    case Page::DateTime:set_text(dynamic_[0],sys_.date_text()+" "+sys_.time_text(true));break;
    case Page::Storage:set_text(dynamic_[0],t(d.sd_mounted?TextId::Mounted:d.sd_missing?TextId::NoCard:TextId::Unmounted));set_text(dynamic_[1],d.sd_mounted?memory(d.sd_total):"--");set_text(dynamic_[2],d.sd_mounted?memory(d.sd_free):"--");break;
    case Page::Hardware:{std::string addresses="I2C:";for(int address:d.i2c_addresses){char out[8];snprintf(out,sizeof(out)," %02X",address);addresses+=out;}set_text(dynamic_[0],addresses);set_text(dynamic_[1],t(d.boot_pressed?TextId::Pressed:TextId::Released));set_text(dynamic_[2],t(d.power_pressed?TextId::Pressed:TextId::Released));break;}
    case Page::About:set_text(dynamic_[0],duration_text(now/1000));set_text(dynamic_[1],memory(d.heap_free)+" / PSRAM "+memory(d.psram_free));break;
    case Page::Sensors:{
        auto triple=[](const std::array<float,3>& values){return "X "+fixed(values[0])+"  Y "+fixed(values[1])+"\nZ "+fixed(values[2]);};
        set_text(dynamic_[0],d.imu?triple(d.accel)+" g":t(TextId::ImuUnavailable));set_text(dynamic_[1],d.imu?triple(d.gyro)+" °/s":"--");
        if(dynamic_[2]){int x=159+std::clamp(static_cast<int>((d.accel[0]-sys_.imu_zero[0])*100),-140,140);int y=35+std::clamp(static_cast<int>((d.accel[1]-sys_.imu_zero[1])*45),-30,30);lv_obj_set_pos(dynamic_[2],x,y);}break;}
    default:break;
    }
}
void Shell::power_resume() {
    if(!asleep_)return;
    asleep_=false;if(sleep_)lv_obj_delete(sleep_);sleep_=nullptr;
    lv_display_trigger_activity(display_);updated_at_=0;
}
void Shell::wake() {
    if(!asleep_)return;
    sys_.platform.request({Op::ScreenWake,static_cast<int>(WakeReason::Touch)});
    if(sys_.device.simulated)power_resume();
}
void Shell::sleep_now() {
    if(asleep_)return;
    auto d=sys_.platform.snapshot();
    if(d.recording||d.playing||sys_.alarm_ringing){sys_.notify(TextId::SleepBusy);return;}
    if(quick_preview_)finish_quick_preview(true,true);
    asleep_=true;sleep_=lv_obj_create(root_);plain(sleep_);lv_obj_set_size(sleep_,466,466);
    lv_obj_set_style_bg_opa(sleep_,255,0);lv_obj_set_style_bg_color(sleep_,lv_color_black(),0);lv_obj_add_flag(sleep_,LV_OBJ_FLAG_CLICKABLE);
    // The covering object also consumes the first complete gesture in compatibility mode.
    on(sleep_,LV_EVENT_CLICKED,[this](lv_event_t*){lv_async_call([](void* data){static_cast<Shell*>(data)->wake();},this);});
    sys_.platform.request({Op::ScreenSleep,sys_.settings.sleep_power_save?1:0});
}
void Shell::tick() {
    sys_.tick();const auto now=sys_.platform.millis();
    if(asleep_){
        if(sys_.device.power==PowerState::Awake)power_resume();
        else if(sys_.alarm_ringing){sys_.platform.request({Op::ScreenWake,static_cast<int>(WakeReason::Deadline)});if(sys_.device.simulated)power_resume();}
        if(asleep_)return;
    }
    apps_.update();
    if(sys_.revision!=language_rev_&&!dirty_&&!overlay_&&!alarm_layer_){set_text(toast_,t(sys_.notice));if(sleep_&&lv_obj_get_child_count(sleep_))set_text(lv_obj_get_child(sleep_,0),t(TextId::WakeHint));request_rebuild();}
    if(edge_active_){auto* input=lv_indev_get_next(nullptr);if(input&&lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED){lv_point_t p;lv_indev_get_point(input,&p);edge_gesture_.move(p.x,p.y,now);if(edge_gesture_.recent_ready()){set_text(gesture_hint_,t(TextId::RecentReady));lv_obj_remove_flag(gesture_hint_,LV_OBJ_FLAG_HIDDEN);}}}
    if(dirty_&&!overlay_&&!alarm_layer_&&!asleep_&&(!animation_at_||now-animation_at_>=180)){build();}
    if(animation_at_&&body_){float progress=std::min(1.0f,static_cast<float>(now-animation_at_)/180);float eased=smooth_ui_toolkit::ease::ease_out_cubic(progress);lv_obj_set_x(body_,static_cast<int>((1-eased)*12));if(sys_.device.simulated)lv_obj_set_style_opa(body_,static_cast<int>(100+155*eased),0);if(progress>=1)animation_at_=0;}
    if(!asleep_&&now-updated_at_>=100){updated_at_=now;update_status();update_content();
        if(page_==Page::Wifi||page_==Page::Ble||page_==Page::BleDevice){const auto& d=sys_.device;std::string f=std::to_string(d.networks.size())+":"+std::to_string(d.wifi_connected)+":"+std::to_string(d.ble_devices.size())+":"+std::to_string(d.ble_connected)+":"+std::to_string(d.gatt_services.size());
            for(const auto& n:d.networks)f+=n.ssid;for(const auto& b:d.ble_devices)f+=b.address;
            if(!list_fingerprint_.empty()&&f!=list_fingerprint_)request_rebuild();list_fingerprint_=f;}
    }
    if(sys_.alarm_ringing&&!alarm_layer_){wake();show_alarm();}
    if(sys_.notice_seq!=notice_seq_){notice_seq_=sys_.notice_seq;set_text(toast_,t(sys_.notice));lv_obj_remove_flag(toast_,LV_OBJ_FLAG_HIDDEN);toast_until_=now+3500;if(sys_.alarm_ringing)lv_obj_add_flag(toast_,LV_OBJ_FLAG_HIDDEN);if(sys_.settings.animations){lv_anim_delete(toast_,fade_opacity);lv_anim_t fade;lv_anim_init(&fade);lv_anim_set_var(&fade,toast_);lv_anim_set_values(&fade,0,255);lv_anim_set_duration(&fade,180);lv_anim_set_exec_cb(&fade,fade_opacity);lv_anim_start(&fade);}else fade_opacity(toast_,255);if(sys_.alarm_ringing)wake();}
    if(toast_until_&&now>toast_until_&&!sys_.alarm_ringing){lv_obj_add_flag(toast_,LV_OBJ_FLAG_HIDDEN);toast_until_=0;}
    if(!asleep_&&sys_.settings.timeout_s>0&&!sys_.device.recording&&!sys_.device.playing&&!sys_.alarm_ringing&&lv_display_get_inactive_time(display_)>static_cast<uint32_t>(sys_.settings.timeout_s*1000)) {
        sleep_now();
    }
}
std::string Shell::debug_state() const {return "page="+std::to_string(static_cast<int>(page_))+" locale="+locale_code(sys_.settings.locale)+" recent="+std::to_string(recent_.size())+" quota_seq="+std::to_string(sys_.quota.seq)+" asleep="+std::to_string(asleep_)+" overlay="+std::to_string(overlay_!=nullptr)+" alert="+std::to_string(alarm_layer_!=nullptr)+" animations="+std::to_string(sys_.settings.animations)+" timer_running="+std::to_string(sys_.timer.running)+" timer_ms="+std::to_string(sys_.timer.elapsed(sys_.platform.millis()))+" timeout_s="+std::to_string(sys_.settings.timeout_s)+" saving="+std::to_string(sys_.settings.sleep_power_save)+" wake_reason="+std::to_string(static_cast<int>(sys_.device.wake_reason))+" clock="+sys_.date_text()+"T"+sys_.time_text(true);}
void Shell::benchmark_scroll(bool vertical) {
    wake();show(vertical?Page::Settings:Page::Apps,false);build();lv_obj_update_layout(body_);
    auto* target=vertical?list_:carousel_;
    lv_obj_scroll_to(target,0,0,LV_ANIM_OFF);
    lv_anim_t animation;lv_anim_init(&animation);lv_anim_set_var(&animation,target);
    lv_anim_set_values(&animation,0,vertical?250:160);lv_anim_set_duration(&animation,600);
    lv_anim_set_reverse_duration(&animation,600);lv_anim_set_repeat_count(&animation,4);
    lv_anim_set_path_cb(&animation,lv_anim_path_linear);
    lv_anim_set_exec_cb(&animation,vertical?+[](void* obj,int32_t value){lv_obj_scroll_to_y(static_cast<lv_obj_t*>(obj),value,LV_ANIM_OFF);}: +[](void* obj,int32_t value){lv_obj_scroll_to_x(static_cast<lv_obj_t*>(obj),value,LV_ANIM_OFF);});
    lv_anim_start(&animation);lv_display_trigger_activity(display_);
}
}
