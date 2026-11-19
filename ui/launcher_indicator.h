/* SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 * Adapted from M5StopWatch main/apps/app_launcher/view/view.cpp:
 * PageIndicator index calculation and active/inactive dot sizes.
 * Uses LVGL 9.4 directly so the Waveshare and SDL ports share the same view. */
#pragma once
#include "lvgl.h"
#include <array>
namespace assistant {
class LauncherIndicator {
    std::array<lv_obj_t*,6> dots_{};
    int active_=-1;
public:
    void create(lv_obj_t* parent,int y) {
        active_=-1;
        for(int i=0;i<6;++i) {
            auto* dot=lv_obj_create(parent); lv_obj_remove_style_all(dot);
            lv_obj_set_style_bg_color(dot,lv_color_white(),0);
            lv_obj_set_style_radius(dot,LV_RADIUS_CIRCLE,0);
            lv_obj_set_pos(dot,233+(i*16-40)-4,y); dots_[i]=dot;
        }
        update(0);
    }
    int update(int scroll) {
        int index=((scroll+233)/466)%6; if(index<0)index+=6;
        if(index==active_)return index;
        active_=index;
        for(int i=0;i<6;++i) {
            int size=i==index?12:6; auto* dot=dots_[i];
            lv_obj_set_size(dot,size,size); lv_obj_set_x(dot,233+(i*16-40)-size/2);
            lv_obj_set_style_bg_opa(dot,i==index?255:75,0);
        }
        return index;
    }
};
}
