#pragma once
#include "system.h"
#include "gesture.h"
#include "launcher_indicator.h"
#include "lvgl.h"
#include "mooncake.h"
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace assistant {
enum class Page { Home,Recent,Quick,Quota,Clock,Timer,Sensors,Assistant,Settings,Language,Display,Touch,Wifi,WifiPassword,Ble,BleDevice,Sound,Audio,Power,DateTime,SetTime,Storage,Hardware,About,Licenses,Alarms,AddAlarm,Apps,Reference,TimerOptions };
class Shell {
public:
    explicit Shell(System& system);
    ~Shell();
    void create(lv_display_t* display);
    void tick();
    void show(Page page,bool animate=true);
    void open(AppId app);
    void back();
    void close_app(AppId app);
    void exit_app();
    void wake();
    void sleep_now();
    void power_resume(); // Called only while holding the LVGL lock.
    bool asleep() const { return asleep_; }
    void activate(const std::string& control);
    bool set_value(const std::string& control,const std::string& value);
    bool has_control(const std::string& control) const;
    Page page() const { return page_; }
    size_t recent_count() const { return recent_.size(); }
    lv_obj_t* control(const std::string& key) const;
    std::string debug_state() const;
    void benchmark_scroll(bool vertical=false);
private:
    System& sys_;
    mooncake::Mooncake apps_;
    std::array<int,6> app_handles_{};
    std::vector<AppId> recent_;
    int active_app_=-1, home_index_=0;
    Page page_=Page::Home, pending_=Page::Home;
    Page quick_return_=Page::Home;
    std::array<Page,6> last_pages_{};
    bool dirty_=false, animate_=false, asleep_=false;
    uint32_t language_rev_=0, notice_seq_=0;
    uint64_t animation_at_=0, updated_at_=0, toast_until_=0;
    std::string network_, ble_address_, ble_name_, input_text_;
    int ble_type_=0;
    bool network_secure_=true;
    lv_display_t* display_=nullptr;
    lv_obj_t *root_=nullptr,*wallpaper_=nullptr,*body_=nullptr,*status_=nullptr,*title_=nullptr,*nav_=nullptr,*toast_=nullptr,*sleep_=nullptr;
    lv_obj_t *list_=nullptr,*carousel_=nullptr,*text_input_=nullptr;
    std::array<lv_obj_t*,16> dynamic_{};
    std::array<lv_obj_t*,24> wave_{};
    std::array<float,24> wave_values_{};
    std::map<std::string,lv_obj_t*> controls_;
    std::map<Page,int> scroll_positions_;
    LauncherIndicator indicator_;
    std::string list_fingerprint_;
    int alarm_hour_=8,alarm_minute_=0;
    bool form_dirty_=false;
    std::string form_original_;
    Page time_return_=Page::DateTime;
    lv_obj_t *gesture_edge_=nullptr,*gesture_hint_=nullptr,*overlay_=nullptr,*alarm_layer_=nullptr,*home_handle_=nullptr;
    lv_draw_buf_t* quick_backdrop_=nullptr;
    std::function<void()> confirm_callback_;
    Gesture edge_gesture_,task_gesture_,launcher_gesture_;
    bool edge_active_=false, task_dragged_=false;
    bool launcher_dragged_=false;
    int launcher_start_scroll_=0,launcher_axis_=0;
    int recent_index_=0;
    std::vector<AppId> switch_order_;
    uint64_t switched_at_=0;
    bool had_recording_=false;
    uint64_t reply_at_=0;
    const char* t(TextId id) const { return tr(id,sys_.settings.locale); }
    lv_obj_t* label(lv_obj_t* parent,const std::string& text,int width=LV_SIZE_CONTENT,const lv_font_t* font=nullptr,uint32_t color=0xF5F6F8);
    lv_obj_t* button(lv_obj_t* parent,const std::string& text,const std::string& id,std::function<void()> callback,int width=LV_PCT(100));
    lv_obj_t* row(lv_obj_t* parent,TextId title,const std::string& value);
    lv_obj_t* scroll_list();
    void toggle(lv_obj_t* parent,TextId title,bool checked,const std::string& id,std::function<void(bool)> callback);
    void slider(lv_obj_t* parent,TextId title,int value,int min,int max,const std::string& id,std::function<void(int)> callback);
    void build();
    void build_home();
    void build_apps();
    void bind_launcher_return(lv_obj_t* object);
    void build_recent();
    void build_quick(bool preview=false);
    void preview_quick(int distance);
    void finish_quick_preview(bool commit,bool discard=false);
    void settle_quick(lv_obj_t* panel,int from);
    lv_obj_t *quick_preview_=nullptr,*quick_preview_panel_=nullptr,*quick_preview_shade_=nullptr;
    int quick_enter_y_=-310;
    void build_timer(bool reference=false);
    void build_timer_options();
    void update_timer();
    bool reference_running_=false,reference_switch_=true,reference_alternate_=false;
    void create_gesture_edge();
    void perform_gesture(GestureAction action);
    void switch_recent(int direction);
    void confirm_action(const std::string& title,const std::string& message,std::function<void()> action,TextId accept=TextId::Done,TextId cancel=TextId::Cancel);
    void dismiss_overlay();
    void show_alarm();
    void update_home();
    void build_settings();
    void build_audio(bool assistant);
    void build_keyboard(bool wifi);
    void update_content();
    void update_status();
    void request_rebuild(bool animate=false);
    void stop_audio_on_leave(Page target);
    void register_control(lv_obj_t* object,const std::string& id);
    void dropdown(lv_obj_t* parent,TextId title,const std::string& options,int selected,const std::string& id,std::function<void(int)> callback);
};
}
