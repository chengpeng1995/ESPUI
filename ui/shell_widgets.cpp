#include "shell.h"
#include "fonts.h"
#include "blue_theme.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>
namespace assistant {
namespace {
constexpr uint32_t FG=theme::fg, MUTED=theme::muted, PANEL=0x16283F, ACCENT=theme::accent;
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
    lv_obj_remove_style_all(obj);lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(obj,&font_cjk_22,0);lv_obj_set_style_text_color(obj,lv_color_hex(FG),0);
}
void fade_opacity(void* obj,int32_t value){lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj),value,0);}
void appear(lv_obj_t* obj,bool enabled) {
    if(!enabled)return;
    lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,obj);lv_anim_set_values(&anim,120,255);
    lv_anim_set_duration(&anim,160);lv_anim_set_exec_cb(&anim,fade_opacity);lv_anim_start(&anim);
}
void button_motion(lv_obj_t* obj,bool enabled,bool scale) {
    static const lv_style_prop_t properties[]={LV_STYLE_TRANSFORM_SCALE_X,LV_STYLE_TRANSFORM_SCALE_Y,LV_STYLE_TRANSLATE_Y,LV_STYLE_BG_COLOR,LV_STYLE_PROP_INV};
    static const lv_style_prop_t no_properties[]={LV_STYLE_PROP_INV};
    static lv_style_transition_dsc_t transition,instant;
    lv_style_transition_dsc_init(&transition,properties,lv_anim_path_ease_out,120,0,nullptr);
    lv_style_transition_dsc_init(&instant,no_properties,lv_anim_path_linear,0,0,nullptr);
    lv_obj_set_style_transition(obj,enabled?&transition:&instant,0);
    lv_obj_set_style_transform_scale(obj,enabled&&scale?246:256,LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(obj,enabled&&!scale?2:0,LV_STATE_PRESSED);
}
void set_text(lv_obj_t* obj,const std::string& value) {
    if(obj&&value!=lv_label_get_text(obj))lv_label_set_text(obj,value.c_str());
}
std::string number_options(int first,int last,bool pad=true) {
    std::string result;for(int i=first;i<=last;++i){char b[12];snprintf(b,sizeof(b),pad?"%02d":"%d",i);if(i>first)result+='\n';result+=b;}return result;
}
int month_days(int year,int month) {
    const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return days[std::clamp(month,1,12)-1]+(month==2&&year%4==0&&(year%100!=0||year%400==0));
}
}
lv_obj_t* Shell::label(lv_obj_t* parent,const std::string& value,int width,const lv_font_t* font,uint32_t color) {
    auto* obj=lv_label_create(parent);lv_label_set_text(obj,value.c_str());
    lv_obj_set_style_text_font(obj,font?font:&font_cjk_22,0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_obj_set_width(obj,width);lv_label_set_long_mode(obj,LV_LABEL_LONG_WRAP);return obj;
}
lv_obj_t* Shell::button(lv_obj_t* parent,const std::string& value,const std::string& id,std::function<void()> callback,int width) {
    auto* obj=lv_button_create(parent);lv_obj_set_size(obj,width,LV_SIZE_CONTENT);lv_obj_set_style_min_height(obj,56,0);
    theme::glass(obj);lv_obj_set_style_bg_color(obj,lv_color_hex(0x2A507F),LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj,28,0);lv_obj_set_style_pad_all(obj,13,0);
    lv_obj_set_style_transform_pivot_x(obj,LV_PCT(50),0);lv_obj_set_style_transform_pivot_y(obj,LV_PCT(50),0);
    // Short, local press transforms; animations disabled means no scale layer.
    const bool navigation=parent==list_||id.rfind("app.",0)==0||id=="status"||id=="home.apps";
    button_motion(obj,sys_.settings.animations&&!navigation,true);
    if(navigation){
        lv_obj_set_style_bg_color(obj,lv_obj_get_style_bg_color(obj,LV_PART_MAIN),LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj,lv_obj_get_style_bg_opa(obj,LV_PART_MAIN),LV_STATE_PRESSED);
        lv_obj_set_style_border_color(obj,lv_obj_get_style_border_color(obj,LV_PART_MAIN),LV_STATE_PRESSED);
    }
    auto* text=label(obj,value,LV_PCT(100),&font_cjk_22);
    lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(text);
    on(obj,LV_EVENT_CLICKED,[this,callback=std::move(callback),id](lv_event_t*) {
        sys_.platform.log("UI_CLICK "+id);callback();
    });
    register_control(obj,id);return obj;
}
lv_obj_t* Shell::row(lv_obj_t* parent,TextId title,const std::string& value) {
    auto* obj=lv_obj_create(parent);plain(obj);theme::glass(obj);lv_obj_set_size(obj,LV_PCT(100),LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(obj,14,0);lv_obj_set_flex_flow(obj,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(obj,6,0);
    label(obj,t(title),LV_PCT(100),&font_ui_18,MUTED);return label(obj,value,LV_PCT(100));
}
lv_obj_t* Shell::scroll_list() {
    auto* obj=lv_obj_create(body_);plain(obj);lv_obj_set_pos(obj,81,0);lv_obj_set_size(obj,304,sys_.settings.assist_navigation?270:344);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(obj,LV_DIR_VER);lv_obj_set_scrollbar_mode(obj,LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_hor(obj,4,0);lv_obj_set_style_pad_top(obj,2,0);lv_obj_set_style_pad_bottom(obj,sys_.settings.assist_navigation?12:44,0);
    lv_obj_set_style_pad_row(obj,10,0);lv_obj_set_flex_flow(obj,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(obj,lv_color_hex(ACCENT),LV_PART_SCROLLBAR);lv_obj_set_style_width(obj,3,LV_PART_SCROLLBAR);
    list_=obj;return obj;
}
void Shell::toggle(lv_obj_t* parent,TextId name,bool checked,const std::string& id,std::function<void(bool)> callback) {
    auto* row_obj=lv_obj_create(parent);plain(row_obj);theme::glass(row_obj);lv_obj_set_size(row_obj,LV_PCT(100),78);
    lv_obj_set_style_pad_hor(row_obj,14,0);
    auto* text=label(row_obj,t(name),180,&font_cjk_22);lv_obj_align(text,LV_ALIGN_LEFT_MID,0,0);
    auto* obj=lv_switch_create(row_obj);lv_obj_set_size(obj,56,32);lv_obj_align(obj,LV_ALIGN_RIGHT_MID,0,0);lv_obj_set_ext_click_area(obj,12);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0x364358),0);lv_obj_set_style_bg_opa(obj,210,0);
    lv_obj_set_style_border_color(obj,lv_color_hex(0x8296B7),0);lv_obj_set_style_border_width(obj,1,0);lv_obj_set_style_border_opa(obj,75,0);
    lv_obj_set_style_bg_color(obj,lv_color_hex(ACCENT),static_cast<lv_style_selector_t>(LV_PART_INDICATOR)|static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(obj,lv_color_hex(FG),LV_PART_KNOB);lv_obj_set_style_pad_all(obj,-4,LV_PART_KNOB);
    lv_obj_set_style_anim_duration(obj,sys_.settings.animations?220:0,0);
    if(checked)lv_obj_add_state(obj,LV_STATE_CHECKED);
    on(obj,LV_EVENT_VALUE_CHANGED,[obj,callback=std::move(callback)](lv_event_t*){callback(lv_obj_has_state(obj,LV_STATE_CHECKED));});register_control(obj,id);
}
void Shell::slider(lv_obj_t* parent,TextId name,int value,int min,int max,const std::string& id,std::function<void(int)> callback) {
    auto* box=lv_obj_create(parent);plain(box);theme::glass(box);lv_obj_set_size(box,LV_PCT(100),100);lv_obj_set_style_pad_all(box,14,0);
    auto* txt=label(box,std::string(t(name))+"  "+std::to_string(value),LV_PCT(100),&font_ui_18);lv_obj_set_pos(txt,0,0);
    auto* obj=lv_slider_create(box);lv_obj_set_size(obj,LV_PCT(92),9);lv_obj_align(obj,LV_ALIGN_BOTTOM_MID,0,-10);
    lv_slider_set_range(obj,min,max);lv_slider_set_value(obj,value,LV_ANIM_OFF);lv_obj_set_ext_click_area(obj,22);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0x314258),0);lv_obj_set_style_bg_color(obj,lv_color_hex(ACCENT),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(obj,lv_color_hex(FG),LV_PART_KNOB);lv_obj_set_style_pad_all(obj,9,LV_PART_KNOB);
    lv_obj_set_style_pad_all(obj,11,static_cast<lv_style_selector_t>(LV_PART_KNOB)|LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj,1,LV_PART_KNOB);lv_obj_set_style_border_color(obj,lv_color_hex(0x99C4FF),LV_PART_KNOB);
    on(obj,LV_EVENT_VALUE_CHANGED,[this,obj,txt,name](lv_event_t*){set_text(txt,std::string(t(name))+"  "+std::to_string(lv_slider_get_value(obj)));});
    on(obj,LV_EVENT_RELEASED,[obj,callback](lv_event_t*){callback(lv_slider_get_value(obj));});register_control(obj,id);
}
void Shell::dropdown(lv_obj_t* parent,TextId name,const std::string& options,int selected,const std::string& id,std::function<void(int)> callback) {
    label(parent,t(name),LV_PCT(100),&font_ui_18,MUTED);
    auto* obj=lv_dropdown_create(parent);lv_dropdown_set_options(obj,options.c_str());lv_dropdown_set_selected(obj,selected);lv_obj_set_size(obj,LV_PCT(100),56);
    theme::glass(obj);lv_obj_set_style_text_font(obj,&font_cjk_22,0);lv_obj_set_style_text_color(obj,lv_color_hex(FG),0);lv_obj_set_style_pad_ver(obj,13,0);
    auto* options_obj=lv_dropdown_get_list(obj);theme::glass(options_obj);lv_obj_set_style_text_font(options_obj,&font_cjk_22,0);
    lv_obj_set_style_text_color(options_obj,lv_color_hex(FG),0);lv_obj_set_style_max_height(options_obj,224,0);
    lv_obj_set_style_pad_ver(options_obj,14,0);lv_obj_set_style_text_line_space(options_obj,24,0);
    lv_obj_set_style_bg_color(options_obj,lv_color_hex(0x23548B),LV_PART_SELECTED);lv_obj_set_style_bg_opa(options_obj,210,LV_PART_SELECTED);
    lv_obj_set_style_text_color(options_obj,lv_color_hex(FG),LV_PART_SELECTED);lv_obj_set_style_radius(options_obj,14,LV_PART_SELECTED);
    on(obj,LV_EVENT_READY,[this,options_obj](lv_event_t*){appear(options_obj,sys_.settings.animations);});
    auto center_options=[obj,options_obj](lv_event_t*){
        // LVGL positions the menu after READY. User click/key callbacks run
        // after the class handler and can safely bring it into the safe area.
        if(!lv_dropdown_is_open(obj))return;
        lv_obj_set_width(options_obj,288);lv_obj_update_layout(options_obj);lv_obj_align(options_obj,LV_ALIGN_CENTER,0,0);
    };
    on(obj,LV_EVENT_CLICKED,center_options);on(obj,LV_EVENT_KEY,center_options);
    on(obj,LV_EVENT_VALUE_CHANGED,[obj,callback=std::move(callback)](lv_event_t*){callback(lv_dropdown_get_selected(obj));});register_control(obj,id);
}
void Shell::build_settings() {
    // Settings is a normal full-screen app; rows scroll directly over black.
    auto* list=scroll_list();

    const std::pair<TextId,Page> entries[]={{TextId::Language,Page::Language},{TextId::Display,Page::Display},{TextId::Wifi,Page::Wifi},{TextId::Bluetooth,Page::Ble},{TextId::Sound,Page::Sound},{TextId::Power,Page::Power},{TextId::DateTime,Page::DateTime},{TextId::StorageHardware,Page::Storage},{TextId::About,Page::About}};
    const int icons[]={1,0,3,3,4,2,1,5,5};
    for(size_t i=0;i<std::size(entries);++i){auto name=entries[i].first;auto target=entries[i].second;
        std::string state;
        switch(target){
        case Page::Language:state=sys_.settings.locale==Locale::ZhCN?"简体中文":"English";break;
        case Page::Display:state=std::to_string(sys_.settings.brightness)+"%";break;
        case Page::Wifi:state=t(sys_.device.wifi_connected?TextId::Connected:sys_.device.wifi_on?TextId::NotConnected:TextId::Off);break;
        case Page::Ble:state=t(sys_.device.ble_connected?TextId::Connected:sys_.device.ble_on?TextId::NotConnected:TextId::Off);break;
        case Page::Sound:state=sys_.settings.mute?t(TextId::Mute):std::to_string(sys_.settings.volume)+"%";break;
        case Page::Power:state=sys_.device.usb?t(TextId::UsbPower):sys_.device.battery>=0?std::to_string(sys_.device.battery)+"%":t(TextId::NoBattery);break;
        case Page::DateTime:state=sys_.time_text();break;
        case Page::Storage:state=t(sys_.device.sd_mounted?TextId::Mounted:sys_.device.sd_missing?TextId::NoCard:TextId::Unmounted);break;
        default:state="Round Assistant";break;
        }
        auto* item=button(list,"",std::string("settings.")+text_key(name),[this,target]{show(target);});lv_obj_set_height(item,sys_.settings.locale==Locale::EnUS?96:80);lv_obj_set_style_pad_all(item,12,0);lv_obj_set_style_bg_opa(item,0,0);lv_obj_set_style_border_width(item,0,0);
        lv_obj_set_style_bg_opa(item,0,LV_STATE_PRESSED);lv_obj_set_style_border_width(item,0,LV_STATE_PRESSED);
        auto* title=lv_obj_get_child(item,0);lv_obj_set_width(title,200);lv_label_set_text(title,t(name));lv_obj_set_style_text_font(title,&font_cjk_22,0);
        lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_LEFT,0);lv_obj_align(title,LV_ALIGN_TOP_LEFT,56,0);
        auto* badge=theme::app_icon(item,icons[i],40);lv_obj_align(badge,LV_ALIGN_LEFT_MID,0,0);lv_obj_remove_flag(badge,LV_OBJ_FLAG_CLICKABLE);
        auto* detail=label(item,state,200,&font_ui_18,MUTED);lv_obj_align(detail,LV_ALIGN_BOTTOM_LEFT,56,0);
        // Two lines are reserved for English titles; the status stays separate.
        lv_label_set_long_mode(title,LV_LABEL_LONG_WRAP);lv_label_set_long_mode(detail,LV_LABEL_LONG_DOT);
    }
}
void Shell::build_audio(bool assistant) {
    auto* list=scroll_list();
    auto* intro=lv_obj_create(list);plain(intro);lv_obj_set_size(intro,LV_PCT(100),58);
    auto* badge=theme::app_icon(intro,4,40);lv_obj_align(badge,LV_ALIGN_LEFT_MID,0,0);
    auto* hint=label(intro,t(assistant?TextId::Demo:TextId::Microphone),228,&font_ui_18,ACCENT);lv_obj_align(hint,LV_ALIGN_RIGHT_MID,0,0);
    dynamic_[0]=label(list,t(assistant?TextId::DemoGreeting:TextId::Ready),LV_PCT(100));
    auto* graph=lv_obj_create(list);plain(graph);theme::glass(graph);lv_obj_set_size(graph,LV_PCT(100),64);lv_obj_set_style_pad_all(graph,0,0);
    for(int i=0;i<24;++i){wave_[i]=lv_obj_create(graph);plain(wave_[i]);lv_obj_set_size(wave_[i],6,4);lv_obj_set_pos(wave_[i],25+i*10,30);lv_obj_set_style_bg_opa(wave_[i],255,0);lv_obj_set_style_bg_color(wave_[i],lv_color_hex(i%3==0?0xA7D1FF:ACCENT),0);lv_obj_set_style_radius(wave_[i],3,0);}
    auto* rec=button(list,t(TextId::Record),"audio.record",[this]{if(sys_.device.recording)sys_.platform.request({Op::RecordStop});else {had_recording_=false;sys_.platform.request({Op::RecordStart});}});theme::glass(rec,true);dynamic_[1]=lv_obj_get_child(rec,0);
    button(list,t(TextId::Play),"audio.play",[this]{sys_.platform.request({Op::AudioPlay});});
    if(assistant)dynamic_[2]=label(list,sys_.device.has_recording?t(TextId::DemoReply):t(TextId::AssistantHint),LV_PCT(100),&font_cjk_22,MUTED);
}
void Shell::build_keyboard(bool wifi) {
    text_input_=lv_textarea_create(body_);lv_obj_set_style_text_font(text_input_,&font_cjk_22,0);theme::glass(text_input_);
    lv_obj_set_style_text_color(text_input_,lv_color_hex(FG),0);lv_textarea_set_one_line(text_input_,true);lv_textarea_set_max_length(text_input_,wifi?63:16);
    lv_textarea_set_password_mode(text_input_,wifi);lv_textarea_set_text(text_input_,input_text_.c_str());register_control(text_input_,"input");
    auto accept=[this,wifi]{
        if(wifi){
            if(network_secure_&&(input_text_.size()<8||input_text_.size()>63)){sys_.notify(TextId::InvalidPassword);return;}
            sys_.platform.request({Op::WifiConnect,network_secure_?1:0,network_,input_text_});form_dirty_=false;show(Page::Wifi);
        }else if(sys_.set_local_time(input_text_)){form_dirty_=false;show(Page::DateTime);}else sys_.notify(TextId::InvalidDateTime);
    };
    if(wifi){
        auto* caption=label(body_,network_,304,&font_ui_18,MUTED);lv_obj_set_pos(caption,81,0);lv_label_set_long_mode(caption,LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_pos(text_input_,81,27);lv_obj_set_size(text_input_,236,52);
        auto* reveal=button(body_,t(TextId::RevealPassword),"input.show",[this]{
            bool password=lv_textarea_get_password_mode(text_input_);lv_textarea_set_password_mode(text_input_,!password);
            if(auto* control_obj=control("input.show"))set_text(lv_obj_get_child(control_obj,0),t(password?TextId::HidePassword:TextId::RevealPassword));
        },64);lv_obj_set_pos(reveal,321,27);lv_obj_set_style_pad_all(reveal,4,0);lv_obj_set_height(reveal,56);
        lv_obj_set_style_text_font(lv_obj_get_child(reveal,0),&font_ui_18,0);
        auto* keyboard=lv_keyboard_create(body_);lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(keyboard,64,85);lv_obj_set_size(keyboard,338,136);lv_keyboard_set_textarea(keyboard,text_input_);
        lv_obj_set_style_bg_opa(keyboard,0,0);lv_obj_set_style_border_width(keyboard,0,0);lv_obj_set_style_pad_all(keyboard,0,0);
        lv_obj_set_style_text_font(keyboard,&lv_font_montserrat_16,LV_PART_ITEMS);lv_obj_set_style_radius(keyboard,9,LV_PART_ITEMS);
        lv_obj_set_style_bg_color(keyboard,lv_color_hex(PANEL),LV_PART_ITEMS);lv_obj_set_style_bg_opa(keyboard,225,LV_PART_ITEMS);
        lv_obj_set_style_border_width(keyboard,1,LV_PART_ITEMS);lv_obj_set_style_border_color(keyboard,lv_color_hex(0x526D91),LV_PART_ITEMS);lv_obj_set_style_border_opa(keyboard,100,LV_PART_ITEMS);
        lv_obj_set_style_text_color(keyboard,lv_color_hex(FG),LV_PART_ITEMS);lv_obj_set_style_bg_color(keyboard,lv_color_hex(0x2865AB),static_cast<lv_style_selector_t>(LV_PART_ITEMS)|LV_STATE_PRESSED);
        on(text_input_,LV_EVENT_VALUE_CHANGED,[this](lv_event_t*){input_text_=lv_textarea_get_text(text_input_);form_dirty_=input_text_!=form_original_;});
        on(keyboard,LV_EVENT_READY,[accept](lv_event_t*){accept();});on(keyboard,LV_EVENT_CANCEL,[this](lv_event_t*){back();});
    }else{
        // Keep the previous text input registered for host automation, while
        // touch editing uses bounded numeric wheels and valid calendar days.
        lv_obj_add_flag(text_input_,LV_OBJ_FLAG_HIDDEN);
        auto* hint=label(body_,t(TextId::AdjustDateTime),304,&font_ui_18,MUTED);lv_obj_set_pos(hint,81,0);lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
        struct DateEditor {std::array<lv_obj_t*,5> wheels{};std::array<int,5> values{2026,1,1,8,0};bool updating=false;};
        auto editor=std::make_shared<DateEditor>();
        auto read_input=[editor](const std::string& value){
            int y,m,d,h,n;if(sscanf(value.c_str(),"%d-%d-%d %d:%d",&y,&m,&d,&h,&n)!=5)return false;
            if(y<2000||y>2099||m<1||m>12||d<1||d>month_days(y,m)||h<0||h>23||n<0||n>59)return false;
            editor->values={y,m,d,h,n};return true;
        };
        read_input(input_text_);
        const TextId names[]={TextId::Year,TextId::Month,TextId::Day,TextId::Hour,TextId::Minute};
        const int starts[]={2000,1,1,0,0};const int ends[]={2099,12,31,23,59};
        const int xs[]={81,197,295,124,242};const int widths[]={108,90,90,100,100};
        for(int i=0;i<5;++i){int y=i<3?29:128;
            auto* caption=label(body_,t(names[i]),widths[i],&font_ui_18,MUTED);lv_obj_set_pos(caption,xs[i],y);lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);
            auto* wheel=lv_roller_create(body_);editor->wheels[i]=wheel;theme::glass(wheel);lv_obj_set_pos(wheel,xs[i],y+23);lv_obj_set_width(wheel,widths[i]);
            lv_obj_set_style_text_font(wheel,&font_cjk_22,0);lv_obj_set_style_text_color(wheel,lv_color_hex(MUTED),0);lv_obj_set_style_text_line_space(wheel,3,0);
            lv_roller_set_options(wheel,number_options(starts[i],ends[i],i!=0).c_str(),LV_ROLLER_MODE_NORMAL);lv_roller_set_visible_row_count(wheel,3);lv_obj_set_height(wheel,72);
            lv_obj_set_style_text_color(wheel,lv_color_hex(FG),LV_PART_SELECTED);lv_obj_set_style_bg_color(wheel,lv_color_hex(0x295687),LV_PART_SELECTED);
            lv_obj_set_style_bg_opa(wheel,190,LV_PART_SELECTED);lv_obj_set_style_radius(wheel,12,LV_PART_SELECTED);
            lv_obj_set_style_anim_duration(wheel,sys_.settings.animations?220:0,0);
            register_control(wheel,std::string("input.")+text_key(names[i]));
        }
        auto sync_wheels=[editor]{
            editor->updating=true;auto& v=editor->values;
            v[2]=std::min(v[2],month_days(v[0],v[1]));lv_roller_set_options(editor->wheels[2],number_options(1,month_days(v[0],v[1])).c_str(),LV_ROLLER_MODE_NORMAL);
            lv_roller_set_selected(editor->wheels[0],v[0]-2000,LV_ANIM_OFF);lv_roller_set_selected(editor->wheels[1],v[1]-1,LV_ANIM_OFF);
            lv_roller_set_selected(editor->wheels[2],v[2]-1,LV_ANIM_OFF);lv_roller_set_selected(editor->wheels[3],v[3],LV_ANIM_OFF);lv_roller_set_selected(editor->wheels[4],v[4],LV_ANIM_OFF);
            editor->updating=false;
        };sync_wheels();
        for(int i=0;i<5;++i)on(editor->wheels[i],LV_EVENT_VALUE_CHANGED,[this,editor,sync_wheels,i](lv_event_t*){
            if(editor->updating)return;const int base[]={2000,1,1,0,0};editor->values[i]=lv_roller_get_selected(editor->wheels[i])+base[i];sync_wheels();
            auto& v=editor->values;char value[24];snprintf(value,sizeof(value),"%04d-%02d-%02d %02d:%02d",v[0],v[1],v[2],v[3],v[4]);lv_textarea_set_text(text_input_,value);
        });
        on(text_input_,LV_EVENT_VALUE_CHANGED,[this,editor,read_input,sync_wheels](lv_event_t*){
            input_text_=lv_textarea_get_text(text_input_);form_dirty_=input_text_!=form_original_;
            if(!editor->updating&&read_input(input_text_))sync_wheels();
        });
    }
    auto* cancel=button(body_,t(TextId::Cancel),"input.cancel",[this]{back();},146);lv_obj_set_pos(cancel,81,232);lv_obj_set_height(cancel,56);
    auto* save=button(body_,t(wifi?TextId::Connect:TextId::Save),"input.save",accept,146);lv_obj_set_pos(save,239,232);lv_obj_set_height(save,56);theme::glass(save,true);
}
}
