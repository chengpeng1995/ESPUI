#include "shell.h"
#include "blue_theme.h"
#include "fonts.h"
#include <cstdio>
#include <functional>

namespace assistant {
namespace {
using Handler=std::function<void(lv_event_t*)>;
void on(lv_obj_t* obj,lv_event_code_t code,Handler fn) {
    struct Hook {lv_event_code_t code;Handler fn;};auto* hook=new Hook{code,std::move(fn)};
    lv_obj_add_event_cb(obj,[](lv_event_t* event){auto* h=static_cast<Hook*>(lv_event_get_user_data(event));
        if(lv_event_get_code(event)==LV_EVENT_DELETE)delete h;
        else if(lv_event_get_code(event)==h->code)h->fn(event);
    },LV_EVENT_ALL,hook);
}
void plain(lv_obj_t* obj){lv_obj_remove_style_all(obj);lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);}
void text(lv_obj_t* obj,const char* value){if(obj&&std::string(lv_label_get_text(obj))!=value)lv_label_set_text(obj,value);}
void clear_button(lv_obj_t* obj) {
    lv_obj_set_style_bg_opa(obj,0,0);lv_obj_set_style_bg_opa(obj,0,LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj,0,0);lv_obj_set_style_pad_all(obj,0,0);
}
void switch_material(lv_obj_t* sw,bool motion) {
    lv_obj_set_size(sw,56,32);lv_obj_set_ext_click_area(sw,11);lv_obj_set_style_pad_all(sw,0,0);
    lv_obj_set_style_radius(sw,18,0);lv_obj_set_style_bg_color(sw,lv_color_hex(0x556680),0);lv_obj_set_style_bg_opa(sw,150,0);
    lv_obj_set_style_border_width(sw,1,0);lv_obj_set_style_border_color(sw,lv_color_hex(0xD3E4FF),0);lv_obj_set_style_border_opa(sw,45,0);
    const auto checked=static_cast<lv_style_selector_t>(LV_PART_INDICATOR)|LV_STATE_CHECKED;
    lv_obj_set_style_bg_opa(sw,255,checked);lv_obj_set_style_bg_main_opa(sw,255,checked);lv_obj_set_style_bg_grad_opa(sw,255,checked);lv_obj_set_style_bg_color(sw,lv_color_hex(0x4B91FF),checked);lv_obj_set_style_bg_grad_color(sw,lv_color_hex(0x176BEB),checked);lv_obj_set_style_bg_grad_dir(sw,LV_GRAD_DIR_VER,checked);
    lv_obj_set_style_border_width(sw,1,checked);lv_obj_set_style_border_color(sw,lv_color_hex(0x99C3FF),checked);lv_obj_set_style_border_opa(sw,120,checked);
    lv_obj_set_style_pad_all(sw,-4,LV_PART_KNOB);lv_obj_set_style_bg_color(sw,lv_color_hex(0xF5F9FB),LV_PART_KNOB);lv_obj_set_style_bg_grad_color(sw,lv_color_hex(0xD6E1E8),LV_PART_KNOB);lv_obj_set_style_bg_grad_dir(sw,LV_GRAD_DIR_VER,LV_PART_KNOB);lv_obj_set_style_border_width(sw,0,LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sw,5,LV_PART_KNOB);lv_obj_set_style_shadow_opa(sw,45,LV_PART_KNOB);lv_obj_set_style_shadow_offset_y(sw,2,LV_PART_KNOB);lv_obj_set_style_anim_duration(sw,motion?220:0,0);
}
}
void Shell::build_timer(bool reference) {
    lv_obj_set_pos(body_,0,0);lv_obj_set_size(body_,466,466);
    // Applications fill the round screen. Optical sheets are reserved for overlays.
    if(reference){
        auto* time=label(body_,"09:41",75,&font_ui_18,0x8F98A7);lv_obj_set_pos(time,142,30);
        auto* power=label(body_,"USB",60,&font_ui_18,0x8F98A7);lv_obj_set_pos(power,298,30);
    }
    auto* icon=theme::app_icon(body_,2,46);lv_obj_set_pos(icon,98,77);
    auto* heading=label(body_,t(TextId::Timer),222,&font_ui_28_bold);lv_obj_set_pos(heading,158,73);
    auto* subtitle=label(body_,t(reference?TextId::ComponentPreview:sys_.timer.countdown?TextId::Countdown:TextId::Stopwatch),222,&font_ui_18,theme::muted);lv_obj_set_pos(subtitle,158,108);
    dynamic_[0]=label(body_,"",326,&font_digits_64);lv_obj_set_pos(dynamic_[0],70,143);lv_obj_set_style_text_align(dynamic_[0],LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_letter_space(dynamic_[0],-1,0);
    dynamic_[2]=label(body_,"",304,&font_ui_18,theme::muted);lv_obj_set_pos(dynamic_[2],81,208);lv_obj_set_style_text_align(dynamic_[2],LV_TEXT_ALIGN_CENTER,0);
    auto* switch_label=label(body_,t(reference?TextId::ExampleSwitch:TextId::TimerTone),204,&font_cjk_22);lv_obj_set_pos(switch_label,98,240);
    auto* sound=lv_switch_create(body_);lv_obj_set_pos(sound,310,237);switch_material(sound,sys_.settings.animations);
    if(reference?reference_switch_:sys_.timer.sound_enabled)lv_obj_add_state(sound,LV_STATE_CHECKED);
    on(sound,LV_EVENT_VALUE_CHANGED,[this,sound,reference](lv_event_t*){const bool value=lv_obj_has_state(sound,LV_STATE_CHECKED);if(reference)reference_switch_=value;else sys_.timer.sound_enabled=value;});register_control(sound,reference?"reference.switch":"timer.sound");
    auto* primary=button(body_,"",reference?"reference.primary":"timer.toggle",[this,reference]{if(reference){reference_running_=!reference_running_;update_timer();return;}if(sys_.timer.countdown&&sys_.timer.display_ms(sys_.platform.millis())==0)sys_.timer.reset();sys_.timer.toggle(sys_.platform.millis());if(auto* duration=control("timer.minutes"))lv_obj_add_state(duration,LV_STATE_DISABLED);update_timer();},270);
    lv_obj_set_pos(primary,98,sys_.settings.assist_navigation?277:295);lv_obj_set_height(primary,56);lv_obj_set_style_radius(primary,24,0);theme::primary(primary);dynamic_[1]=lv_obj_get_child(primary,0);lv_obj_set_style_text_font(dynamic_[1],&font_ui_22_bold,0);
    auto* settings=button(body_,t(reference?TextId::SwitchState:TextId::TimerSettings),reference?"reference.selection":"timer.settings",[this,reference]{if(reference){reference_alternate_=!reference_alternate_;update_timer();}else show(Page::TimerOptions);},136);clear_button(settings);lv_obj_set_pos(settings,98,sys_.settings.assist_navigation?333:354);lv_obj_set_style_min_height(settings,48,0);lv_obj_set_height(settings,sys_.settings.assist_navigation?48:56);lv_obj_set_style_text_font(lv_obj_get_child(settings,0),&font_ui_18,0);lv_obj_set_style_text_align(lv_obj_get_child(settings,0),LV_TEXT_ALIGN_LEFT,0);
    auto* reset=button(body_,t(reference?TextId::ResetExample:TextId::Reset),reference?"reference.reset":"timer.reset",[this,reference]{
        auto apply=[this,reference]{if(reference){reference_running_=false;reference_alternate_=false;reference_switch_=true;request_rebuild();}else {sys_.timer.reset();if(auto* duration=control("timer.minutes"))lv_obj_remove_state(duration,LV_STATE_DISABLED);update_timer();}};
        if(reference||sys_.timer.running||sys_.timer.accumulated_ms)confirm_action(t(TextId::ConfirmAction),t(TextId::ConfirmReset),apply,TextId::Reset);else apply();
    },112);clear_button(reset);lv_obj_set_pos(reset,256,sys_.settings.assist_navigation?333:354);lv_obj_set_style_min_height(reset,48,0);lv_obj_set_height(reset,sys_.settings.assist_navigation?48:56);lv_obj_set_style_text_font(lv_obj_get_child(reset,0),&font_ui_18,0);lv_obj_set_style_text_align(lv_obj_get_child(reset,0),LV_TEXT_ALIGN_RIGHT,0);
    if(!reference){
        // Keep the existing local diagnostic control contract. User-facing
        // duration/mode editing lives in the dedicated full-screen timer settings page.
        auto* hidden=lv_obj_create(body_);plain(hidden);lv_obj_set_size(hidden,304,200);lv_obj_add_flag(hidden,LV_OBJ_FLAG_HIDDEN);
        dropdown(hidden,TextId::Timer,std::string(t(TextId::Countdown))+"\n"+t(TextId::Stopwatch),sys_.timer.countdown?0:1,"timer.mode",[this](int value){if(value==(sys_.timer.countdown?0:1))return;auto apply=[this,value]{sys_.timer.reset();sys_.timer.countdown=value==0;request_rebuild();};lv_dropdown_set_selected(control("timer.mode"),sys_.timer.countdown?0:1);if(sys_.timer.running||sys_.timer.accumulated_ms)confirm_action(t(TextId::ConfirmAction),t(TextId::ConfirmReset),apply,TextId::Reset);else apply();});
        slider(hidden,TextId::Minutes,sys_.timer.duration_minutes,1,60,"timer.minutes",[this](int value){if(sys_.timer.running||sys_.timer.accumulated_ms)return;sys_.timer.duration_minutes=value;update_timer();});
        if(sys_.timer.running||sys_.timer.accumulated_ms)lv_obj_add_state(control("timer.minutes"),LV_STATE_DISABLED);
    }
    update_timer();
}
void Shell::update_timer() {
    const bool reference=page_==Page::Reference;
    const bool running=reference?reference_running_:sys_.timer.running;
    char value[24];
    if(reference)std::snprintf(value,sizeof(value),"%s",reference_alternate_?"25:00":"12:48");
    else {const auto seconds=(sys_.timer.display_ms(sys_.platform.millis())+999)/1000;if(seconds<3600)std::snprintf(value,sizeof(value),"%02d:%02d",static_cast<int>(seconds/60),static_cast<int>(seconds%60));else std::snprintf(value,sizeof(value),"%d:%02d:%02d",static_cast<int>(seconds/3600),static_cast<int>(seconds/60%60),static_cast<int>(seconds%60));}
    lv_obj_set_style_text_font(dynamic_[0],std::string(value).size()>5?&lv_font_montserrat_48:&font_digits_64,0);
    text(dynamic_[0],value);text(dynamic_[1],t(running?TextId::PauseTiming:!reference&&sys_.timer.accumulated_ms?TextId::ResumeTiming:TextId::StartTiming));
    text(dynamic_[2],t(reference||sys_.timer.countdown?(running?TextId::CountdownRunning:TextId::CountdownPaused):(running?TextId::StopwatchRunning:TextId::StopwatchPaused)));
}
void Shell::build_timer_options() {
    auto* list=scroll_list();
    dropdown(list,TextId::Timer,std::string(t(TextId::Countdown))+"\n"+t(TextId::Stopwatch),sys_.timer.countdown?0:1,"timer.mode",[this](int value){if(value==(sys_.timer.countdown?0:1))return;auto apply=[this,value]{sys_.timer.reset();sys_.timer.countdown=value==0;request_rebuild();};lv_dropdown_set_selected(control("timer.mode"),sys_.timer.countdown?0:1);if(sys_.timer.running||sys_.timer.accumulated_ms)confirm_action(t(TextId::ConfirmAction),t(TextId::ConfirmReset),apply,TextId::Reset);else apply();});
    if(sys_.timer.countdown){slider(list,TextId::Minutes,sys_.timer.duration_minutes,1,60,"timer.minutes",[this](int value){if(sys_.timer.running||sys_.timer.accumulated_ms)return;sys_.timer.duration_minutes=value;});if(sys_.timer.running||sys_.timer.accumulated_ms)lv_obj_add_state(control("timer.minutes"),LV_STATE_DISABLED);}
    auto* done=button(list,t(TextId::Done),"timer.options.done",[this]{show(Page::Timer);});theme::glass(done,true);
}
}
