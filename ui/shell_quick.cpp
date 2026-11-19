#include "shell.h"
#include "blue_theme.h"
#include "fonts.h"
#include <functional>
#include <algorithm>

namespace assistant {
namespace {
using Handler=std::function<void(lv_event_t*)>;
void on(lv_obj_t* obj,lv_event_code_t code,Handler fn) {
    struct Hook {lv_event_code_t code;Handler fn;};auto* hook=new Hook{code,std::move(fn)};
    lv_obj_add_event_cb(obj,[](lv_event_t* event){
        auto* h=static_cast<Hook*>(lv_event_get_user_data(event));
        if(lv_event_get_code(event)==LV_EVENT_DELETE)delete h;
        else if(lv_event_get_code(event)==h->code)h->fn(event);
    },LV_EVENT_ALL,hook);
}
void container(lv_obj_t* obj) {
    lv_obj_remove_style_all(obj);lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_pad_all(obj,0,0);
}
}
void Shell::settle_quick(lv_obj_t* panel,int from){
    lv_obj_set_y(panel,from);
    if(from==78)return;
    if(!sys_.settings.animations){lv_obj_set_y(panel,78);return;}
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,panel);lv_anim_set_values(&a,from,78);lv_anim_set_duration(&a,200);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_set_exec_cb(&a,[](void* obj,int32_t y){lv_obj_set_y(static_cast<lv_obj_t*>(obj),y);});lv_anim_start(&a);
}
void Shell::build_quick(bool preview) {
    lv_obj_set_pos(body_,0,0);lv_obj_set_size(body_,466,466);
    if(!preview)theme::backdrop(body_,quick_backdrop_);
    auto* material=lv_obj_create(body_);container(material);lv_obj_set_pos(material,78,78);lv_obj_set_size(material,310,310);theme::sheet(material);
    if(preview){quick_preview_panel_=material;register_control(material,"quick.preview.panel");}
    else {
        const int from=animate_&&sys_.settings.animations?quick_enter_y_:78;
        settle_quick(material,from);quick_enter_y_=-310;
    }
    auto* heading=label(material,t(TextId::QuickSettings),274,&font_ui_22_bold);lv_obj_set_pos(heading,18,12);lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_CENTER,0);
    auto card=[this,material](int column,const char* symbol,const std::string& title,bool enabled,const std::string& id,std::function<void(bool)> change){
        auto* panel=lv_obj_create(material);container(panel);theme::glass(panel);
        lv_obj_set_size(panel,90,98);lv_obj_set_pos(panel,12+98*column,54);lv_obj_add_flag(panel,LV_OBJ_FLAG_CLICKABLE);
        auto* icon=label(panel,symbol,82,&lv_font_montserrat_20,theme::accent);lv_obj_set_pos(icon,4,5);lv_obj_set_style_text_align(icon,LV_TEXT_ALIGN_CENTER,0);
        auto* name=label(panel,title,88,&font_ui_18,theme::fg);lv_obj_set_pos(name,1,29);lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
        auto* sw=lv_switch_create(panel);lv_obj_set_size(sw,56,32);lv_obj_align(sw,LV_ALIGN_BOTTOM_MID,0,-9);lv_obj_set_ext_click_area(sw,12);
        lv_obj_set_style_bg_color(sw,lv_color_hex(0x364358),0);lv_obj_set_style_border_width(sw,1,0);lv_obj_set_style_border_color(sw,lv_color_hex(0x9CBCDF),0);lv_obj_set_style_border_opa(sw,80,0);
        lv_obj_set_style_bg_color(sw,lv_color_hex(theme::accent),static_cast<lv_style_selector_t>(LV_PART_INDICATOR)|LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw,lv_color_hex(theme::fg),LV_PART_KNOB);lv_obj_set_style_pad_all(sw,-4,LV_PART_KNOB);lv_obj_set_style_anim_duration(sw,sys_.settings.animations?220:0,0);
        if(enabled)lv_obj_add_state(sw,LV_STATE_CHECKED);
        on(sw,LV_EVENT_VALUE_CHANGED,[sw,change](lv_event_t*){change(lv_obj_has_state(sw,LV_STATE_CHECKED));});
        on(panel,LV_EVENT_CLICKED,[sw](lv_event_t*){if(lv_obj_has_state(sw,LV_STATE_CHECKED))lv_obj_remove_state(sw,LV_STATE_CHECKED);else lv_obj_add_state(sw,LV_STATE_CHECKED);lv_obj_send_event(sw,LV_EVENT_VALUE_CHANGED,nullptr);});
        register_control(sw,id);
    };
    card(0,LV_SYMBOL_WIFI,t(TextId::Wifi),sys_.settings.wifi,"quick.wifi",[this](bool value){sys_.settings.wifi=value;sys_.platform.request({Op::WifiEnable,value});sys_.save();});
    card(1,LV_SYMBOL_BLUETOOTH,"BLE",sys_.settings.ble,"quick.ble",[this](bool value){sys_.settings.ble=value;sys_.platform.request({Op::BleEnable,value});sys_.save();});
    card(2,LV_SYMBOL_VOLUME_MID,t(TextId::Mute),sys_.settings.mute,"quick.mute",[this](bool value){sys_.settings.mute=value;sys_.platform.request({Op::Volume,value?0:sys_.settings.volume});sys_.save();});
    auto* brightness=lv_obj_create(material);container(brightness);lv_obj_set_pos(brightness,12,160);lv_obj_set_size(brightness,286,80);
    slider(brightness,TextId::Brightness,sys_.settings.brightness,5,100,"quick.brightness",[this](int value){sys_.settings.brightness=value;sys_.platform.request({Op::Brightness,value});sys_.save();});
    lv_obj_set_height(lv_obj_get_child(brightness,0),80);
    auto* language=button(material,std::string(t(TextId::Language))+"\n"+(sys_.settings.locale==Locale::ZhCN?"English":"简体中文"),"quick.language",[this]{sys_.set_locale(sys_.settings.locale==Locale::ZhCN?Locale::EnUS:Locale::ZhCN);},139);
    lv_obj_set_pos(language,12,246);lv_obj_set_height(language,56);lv_obj_set_style_pad_all(language,8,0);lv_obj_set_style_text_font(lv_obj_get_child(language,0),&font_ui_18,0);
    auto* settings=button(material,t(TextId::Settings),"quick.settings",[this]{open(AppId::Settings);},139);
    lv_obj_set_pos(settings,159,246);lv_obj_set_height(settings,56);lv_obj_set_style_text_font(lv_obj_get_child(settings,0),&font_ui_18,0);
}
void Shell::preview_quick(int distance){
    if(page_==Page::Quick||overlay_||alarm_layer_||asleep_)return;
    if(!quick_preview_){
        quick_preview_=lv_obj_create(root_);container(quick_preview_);lv_obj_set_size(quick_preview_,466,466);
        quick_preview_shade_=lv_obj_create(quick_preview_);container(quick_preview_shade_);lv_obj_set_size(quick_preview_shade_,466,466);lv_obj_set_style_bg_color(quick_preview_shade_,lv_color_black(),0);
        auto* source=body_;body_=quick_preview_;build_quick(true);body_=source;
        lv_obj_move_foreground(gesture_edge_);
    }
    const int progress=std::clamp(distance,0,388);
    lv_obj_set_y(quick_preview_panel_,-310+progress);
    lv_obj_set_style_bg_opa(quick_preview_shade_,progress*150/388,0);
}
void Shell::finish_quick_preview(bool commit,bool discard){
    if(!quick_preview_)return;
    auto* layer=quick_preview_;auto* panel=quick_preview_panel_;auto* shade=quick_preview_shade_;
    quick_enter_y_=commit?lv_obj_get_y(panel):-310;
    if(commit&&!discard)return; // Promote this exact panel; no delete/recreate jump.
    quick_preview_=quick_preview_panel_=quick_preview_shade_=nullptr;
    for(auto it=controls_.begin();it!=controls_.end();)if(it->first.rfind("quick.",0)==0)it=controls_.erase(it);else ++it;
    if(discard||!sys_.settings.animations){lv_obj_delete(layer);return;}
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,panel);lv_anim_set_values(&a,lv_obj_get_y(panel),-310);lv_anim_set_duration(&a,160);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);lv_anim_set_exec_cb(&a,[](void* obj,int32_t y){lv_obj_set_y(static_cast<lv_obj_t*>(obj),y);});lv_anim_start(&a);
    lv_anim_set_var(&a,shade);lv_anim_set_values(&a,lv_obj_get_style_bg_opa(shade,LV_PART_MAIN),0);lv_anim_set_exec_cb(&a,[](void* obj,int32_t v){lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(obj),v,0);});lv_anim_start(&a);
    lv_obj_delete_delayed(layer,160);
}
}
