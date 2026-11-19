#include "blue_theme.h"
#include "../assets/blue_ui/blue_ui_assets.h"
#include "../assets/blue_ui/blue_ui_motion_assets.h"

#include <algorithm>
#include <cstdlib>

namespace assistant::theme {
namespace {

constexpr int sizes[] = {40, 46, 64, 82, 94, 124};
#define MOTION_ICONS(app) {&blue_icon_##app##_82,&blue_icon_##app##_88,&blue_icon_##app##_94,&blue_icon_##app##_100,&blue_icon_##app##_106,&blue_icon_##app##_112,&blue_icon_##app##_118,&blue_icon_##app##_124}
const lv_image_dsc_t* const motion_icons[][8]={
    MOTION_ICONS(quota),MOTION_ICONS(clock),MOTION_ICONS(timer),
    MOTION_ICONS(sensors),MOTION_ICONS(assistant),MOTION_ICONS(settings)
};
#undef MOTION_ICONS
const lv_image_dsc_t* const icons[][6] = {
    {&blue_icon_quota_40, &blue_icon_quota_46, &blue_icon_quota_64, &blue_icon_quota_82, &blue_icon_quota_94, &blue_icon_quota_124},
    {&blue_icon_clock_40, &blue_icon_clock_46, &blue_icon_clock_64, &blue_icon_clock_82, &blue_icon_clock_94, &blue_icon_clock_124},
    {&blue_icon_timer_40, &blue_icon_timer_46, &blue_icon_timer_64, &blue_icon_timer_82, &blue_icon_timer_94, &blue_icon_timer_124},
    {&blue_icon_sensors_40, &blue_icon_sensors_46, &blue_icon_sensors_64, &blue_icon_sensors_82, &blue_icon_sensors_94, &blue_icon_sensors_124},
    {&blue_icon_assistant_40, &blue_icon_assistant_46, &blue_icon_assistant_64, &blue_icon_assistant_82, &blue_icon_assistant_94, &blue_icon_assistant_124},
    {&blue_icon_settings_40, &blue_icon_settings_46, &blue_icon_settings_64, &blue_icon_settings_82, &blue_icon_settings_94, &blue_icon_settings_124},
};

void non_interactive(lv_obj_t* obj) {
    lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                           LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_PRESS_LOCK));
}

} // namespace

lv_obj_t* background(lv_obj_t* parent) {
    auto* image = lv_image_create(parent);
    lv_image_set_src(image, &blue_wallpaper);
    lv_obj_set_pos(image, 0, 0);
    lv_obj_set_size(image, 466, 466);
    non_interactive(image);
    lv_obj_add_flag(image, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_move_background(image);
    return image;
}

void glass(lv_obj_t* obj, bool primary) {
    // This long-standing API is also used by ordinary cards and controls.
    // Keep them opaque on black app pages; only sheet() owns blurred optics.
    lv_obj_set_style_radius(obj, primary ? 28 : 24, 0);
    lv_obj_set_style_bg_image_src(obj, nullptr, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(primary ? action : 0x181A20), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_main_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(primary ? 0x347BE2 : 0x30343D), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(fg), 0);
    lv_obj_set_style_outline_color(obj, lv_color_hex(accent), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(obj, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(obj, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(primary ? 0x1358C7 : 0x252A33), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, lv_color_hex(primary ? 0x5591EC : 0x404957), LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
}

void sheet(lv_obj_t* obj, bool warm) {
    (void)warm;
    lv_obj_set_style_bg_image_src(obj, nullptr, 0);
    lv_obj_set_style_radius(obj, lv_obj_get_style_width(obj,LV_PART_MAIN)==320?48:36, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x192537), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x101D31), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(obj, 218, 0);
    // One rim only. No bright bottom glow, no opaque pre-rendered sheet image.
    lv_obj_set_style_border_color(obj, lv_color_hex(0xB4CCEE), 0);
    lv_obj_set_style_border_opa(obj, 54, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(fg), 0);
}

void primary(lv_obj_t* obj) {
    glass(obj, true);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_bg_image_src(obj, &blue_primary, 0);
    lv_obj_set_style_bg_image_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_tiled(obj, false, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 24, 0);
    lv_obj_set_style_bg_image_recolor(obj, lv_color_hex(0x80B2FF), LV_STATE_PRESSED);
    lv_obj_set_style_bg_image_recolor_opa(obj, 38, LV_STATE_PRESSED);
}

lv_obj_t* app_icon(lv_obj_t* parent, int app_index, int size) {
    const int index = std::max(0, std::min(5, app_index));
    int variant = 0;
    for (int i = 1; i < 6; ++i) {
        if (std::abs(size - sizes[i]) < std::abs(size - sizes[variant])) variant = i;
    }
    auto* image = lv_image_create(parent);
    lv_image_set_src(image, icons[index][variant]);
    lv_obj_set_size(image, sizes[variant], sizes[variant]);
    non_interactive(image);
    lv_obj_add_flag(image, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
    return image;
}

void carousel_icon(lv_obj_t* image,int app_index,int distance) {
    // Eight 6 px steps match the number of frames in a short 30 FPS snap.
    // Native RGB565A8 avoids the ESP32 software affine-transform bottleneck.
    const int step=std::clamp((160-distance)*7/160,0,7);
    const auto* src=motion_icons[std::clamp(app_index,0,5)][step];
    if(lv_image_get_src(image)==src)return;
    lv_image_set_src(image,src);lv_obj_set_size(image,src->header.w,src->header.h);
    const int inset=(124-src->header.w)/2;lv_obj_set_pos(image,10+inset,inset);
}

} // namespace assistant::theme
