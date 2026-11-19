#pragma once

#include <cstdint>
#include "lvgl.h"

namespace assistant::theme {

constexpr uint32_t fg = 0xF3F6FA;
constexpr uint32_t muted = 0xC8D4E5;
constexpr uint32_t accent = 0x4B91FF;
constexpr uint32_t action = 0x176BEB;

// Call before constructing the root's interactive children. The 466 x 466
// wallpaper is a native, error-diffused RGB565 image and owns no input or navigation state.
lv_obj_t* background(lv_obj_t* parent);

// Ordinary controls use opaque dark gray; primary controls use solid blue.
// Preserves children, input events, size, padding, focus and rounded corners.
// Glass optics are reserved for sheet() confirmation and overlay surfaces.
void glass(lv_obj_t* obj, bool primary = false);

// Translucent dark glass with a single subtle 1 px rim, only for overlays.
void sheet(lv_obj_t* obj, bool warm = false);

// One-shot scene blur: quarter-size separable filter then a native RGB565
// backdrop. Never recomputed while dragging/animating; caller owns the buffer.
lv_draw_buf_t* capture_backdrop(lv_obj_t* source);
void backdrop(lv_obj_t* parent, lv_draw_buf_t* buffer);
// Captures before creating the blocking overlay; buffer freed with its image.
lv_obj_t* overlay(lv_obj_t* parent);

// Exact native 270 x 56 blue action surface. Text and hit testing remain live.
// For other button dimensions continue to use glass(obj, true).
void primary(lv_obj_t* obj);

// Stable order: quota, clock, timer, sensors, assistant, settings.
// Native assets exist at 40, 46, 64, 82, 94 and 124 px. Other sizes use the nearest
// native asset, without scaling. Position the returned image within its parent.
// The image itself is not clickable; the containing control owns interaction.
lv_obj_t* app_icon(lv_obj_t* parent, int app_index, int size);
void carousel_icon(lv_obj_t* image,int app_index,int distance);

} // namespace assistant::theme
