#include "shell.h"
#include "sim_platform.h"
#include <SDL.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>

int run_self_test();
namespace {
bool manual_ticks = false;
uint32_t simulated_ticks = 1000;
uint32_t ticks() { return manual_ticks ? simulated_ticks : SDL_GetTicks(); }
struct Pointer { lv_point_t point{233,233}; lv_indev_state_t state = LV_INDEV_STATE_RELEASED; } pointer;
struct FixtureSettings {
    std::string path;
    explicit FixtureSettings(bool enabled) {
        if(!enabled) return;
        char pattern[]="/tmp/espui-capture-settings-XXXXXX";
        const int fd=mkstemp(pattern);
        if(fd>=0) { close(fd);path=pattern; }
    }
    ~FixtureSettings() {
        if(path.empty()) return;
        std::remove(path.c_str());std::remove((path+".tmp").c_str());
    }
};
void read_pointer(lv_indev_t*, lv_indev_data_t* data) {
    data->point = pointer.point;
    data->state = pointer.state;
}
int watch(void*, SDL_Event* event) {
    if(event->type == SDL_KEYDOWN && !event->key.repeat) {
        const char* scenarios[] = {"normal","offline","connect-failed","low-battery","no-card","hardware-error"};
        if(event->key.keysym.sym >= SDLK_F1 && event->key.keysym.sym <= SDLK_F6)
            printf("@HW_SCENARIO %s\n",scenarios[event->key.keysym.sym-SDLK_F1]);
    }
    return 1;
}
bool screenshot(lv_display_t* display, const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if(!parent.empty()) std::filesystem::create_directories(parent);
    lv_obj_invalidate(lv_display_get_screen_active(display));
    lv_refr_now(display);
    auto* renderer = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(display));
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0,466,466,24,SDL_PIXELFORMAT_RGB24);
    if(!surface) { fprintf(stderr,"CAPTURE_ERROR %s\n",SDL_GetError()); return false; }
    const bool ok = SDL_RenderReadPixels(renderer,nullptr,surface->format->format,surface->pixels,surface->pitch) == 0
        && SDL_SaveBMP(surface,path.c_str()) == 0;
    SDL_FreeSurface(surface);
    if(ok) printf("CAPTURE_OK %s\n",path.c_str());
    else fprintf(stderr,"CAPTURE_ERROR %s\n",SDL_GetError());
    return ok;
}
void usage(const char* command) {
    printf("Usage: %s [--self-test] [--capture-dir DIR] [--input-script FILE] [--locale zh-CN|en-US|en] [--scenario NAME]\n"
           "Capture and script modes use deterministic local fixtures, no network or hardware.\n"
           "Script commands: @UI page N, @UI app N, @UI click ID, @UI set ID VALUE,\n"
           " @UI pointer X Y down|move|up, @UI swipe X1 Y1 X2 Y2 MS, @UI wait MS,\n"
           " @UI capture FILE.bmp, @UI assert-page N, @UI state.\n",command);
}
}
int main(int argc, char** argv) {
    setvbuf(stdout,nullptr,_IOLBF,0);
    std::string capture_dir, input_script, locale, scenario = "normal";
    for(int i=1;i<argc;++i) {
        const std::string arg = argv[i];
        if(arg == "--self-test") return run_self_test();
        if(arg == "--help") { usage(argv[0]); return 0; }
        if((arg == "--capture-dir" || arg == "--input-script" || arg == "--locale" || arg == "--scenario") && i+1<argc) {
            const std::string value = argv[++i];
            if(arg == "--capture-dir") capture_dir=value;
            else if(arg == "--input-script") input_script=value;
            else if(arg == "--locale") {
                if(value!="en" && value!="en-US" && value!="zh-CN") { usage(argv[0]);return 2; }
                locale=value=="en" ? "en-US" : value;
            }
            else scenario=value;
        } else { usage(argv[0]); return 2; }
    }
    manual_ticks = !capture_dir.empty() || !input_script.empty();
    if(manual_ticks && !std::getenv("SDL_VIDEODRIVER")) SDL_setenv("SDL_VIDEODRIVER","dummy",0);
    const char* settings = std::getenv("ASSISTANT_SETTINGS");
    // Batch review uses a disposable settings file, never the user's saved settings.
    FixtureSettings fixture_settings(manual_ticks);
    if(manual_ticks && fixture_settings.path.empty()) { perror("Temporary settings");return 1; }
    assistant::SimPlatform platform(manual_ticks ? fixture_settings.path : (settings ? settings : ".runtime/settings.json"),manual_ticks);
    platform.quiet = manual_ticks;
    assistant::System system(platform);
    if(!locale.empty()) system.set_locale(assistant::parse_locale(locale));
    platform.scenario(scenario);
    lv_init();
    auto* display = lv_sdl_window_create(466,466);
    if(!display) return 1;
    // SDL installs SDL_GetTicks while creating its first display. Override only
    // afterwards so capture/input scripts advance LVGL animations together with
    // the simulator clock instead of repeatedly sampling their first frame.
    lv_tick_set_cb(ticks);
    lv_sdl_window_set_resizeable(display,false);
    lv_sdl_window_set_title(display,"Round Assistant | SIMULATED HARDWARE | F1-F6 scenarios");
    if(manual_ticks) SDL_HideWindow(lv_sdl_window_get_window(display));
    else lv_sdl_mouse_create();
    auto* injected = lv_indev_create();
    lv_indev_set_type(injected,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(injected,display);
    lv_indev_set_read_cb(injected,read_pointer);
    SDL_AddEventWatch(watch,nullptr);
    int result = 0;
    {
        assistant::Shell shell(system);
        shell.create(display);
        auto pump = [&](uint32_t ms) {
            for(uint32_t elapsed=0;elapsed<ms;elapsed+=5) {
                if(manual_ticks) { simulated_ticks+=5; platform.advance(5); }
                shell.tick();
                lv_timer_handler();
                if(!manual_ticks) SDL_Delay(5);
            }
        };
        auto move_pointer = [&](int x,int y,lv_indev_state_t state) {
            pointer.point.x=std::clamp(x,0,465); pointer.point.y=std::clamp(y,0,465); pointer.state=state;
            lv_indev_read(injected);
            pump(5);
        };
        if(manual_ticks) {
            const std::string now=std::to_string(platform.utc());
            platform.receive("{\"type\":\"quota\",\"version\":1,\"seq\":1,\"now\":"+now+
                ",\"age\":0,\"bucket\":\"DEMO offline fixture\",\"plan\":\"fixture\",\"primary\":{\"used\":38,\"minutes\":300,\"reset\":"+
                std::to_string(platform.utc()+3661)+",\"reset_local\":\"05/18 12:34 UTC+8\"},\"secondary\":null,\"credits\":\"--\",\"reset_credits\":null}");
        }
        pump(400);
        bool running=true;
        auto command = [&](const std::string& frame) {
            if(frame.empty() || frame[0]=='#') return;
            if(frame.rfind("@UI click ",0)==0) shell.activate(frame.substr(10));
            else if(frame.rfind("@UI set ",0)==0) {
                auto end=frame.find(' ',8);
                if(end!=std::string::npos) shell.set_value(frame.substr(8,end-8),frame.substr(end+1));
            } else if(frame.rfind("@UI page ",0)==0) {
                const int p=std::atoi(frame.c_str()+9);
                if(p>=0 && p<=static_cast<int>(assistant::Page::TimerOptions)) shell.show(static_cast<assistant::Page>(p),false);
            } else if(frame.rfind("@UI app ",0)==0) {
                const int i=std::atoi(frame.c_str()+8);
                if(i>=0 && i<6) shell.open(static_cast<assistant::AppId>(i));
            } else if(frame.rfind("@UI locale ",0)==0) {
                const std::string value=frame.substr(11);
                system.set_locale(assistant::parse_locale(value=="en" ? "en-US" : value));
            }
            else if(frame.rfind("@UI pointer ",0)==0) {
                int x,y;std::string state;std::istringstream input(frame.substr(12));
                if(input>>x>>y>>state && (state=="down" || state=="move" || state=="up"))
                    move_pointer(x,y,state=="up" ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED);
                else { fprintf(stderr,"INVALID_POINTER %s\n",frame.c_str());result=1; }
            } else if(frame.rfind("@UI swipe ",0)==0) {
                int x1,y1,x2,y2,duration;std::istringstream input(frame.substr(10));
                if(input>>x1>>y1>>x2>>y2>>duration) {
                    duration=std::clamp(duration,20,10000);
                    move_pointer(x1,y1,LV_INDEV_STATE_PRESSED);
                    for(int elapsed=5;elapsed<=duration;elapsed+=5)
                        move_pointer(x1+(x2-x1)*elapsed/duration,y1+(y2-y1)*elapsed/duration,LV_INDEV_STATE_PRESSED);
                    move_pointer(x2,y2,LV_INDEV_STATE_RELEASED);
                    pump(300);
                } else { fprintf(stderr,"INVALID_SWIPE %s\n",frame.c_str());result=1; }
            } else if(frame.rfind("@UI wait ",0)==0) pump(std::clamp(std::atoi(frame.c_str()+9),0,60000));
            else if(frame.rfind("@UI capture ",0)==0) { pump(100);if(!screenshot(display,frame.substr(12))) result=1; }
            else if(frame.rfind("@UI assert-page ",0)==0) {
                pump(300);
                const int expected=std::atoi(frame.c_str()+16),actual=static_cast<int>(shell.page());
                if(actual!=expected) { fprintf(stderr,"ASSERT_PAGE_FAIL expected=%d actual=%d\n",expected,actual);result=1; }
                else printf("ASSERT_PAGE_OK %d\n",actual);
            } else if(frame=="@UI state") { pump(20);printf("SIM_STATE %s\n",shell.debug_state().c_str()); }
            else if(frame.rfind("@HW ",0)==0) platform.scenario(frame.substr(4));
            else if(frame.rfind("@SIM_MODE ",0)==0) lv_sdl_window_set_title(display,("Round Assistant | SIM HARDWARE | "+frame.substr(10)+" | F1-F6").c_str());
            else if(frame=="@UI quit") running=false;
            else platform.receive(frame);
        };
        printf("SIM_READY display=466x466 protocol=quota-v1 input=lv_indev locale=%s\n",assistant::locale_code(system.settings.locale));
        if(!input_script.empty()) {
            std::ifstream input(input_script);
            if(!input) { fprintf(stderr,"Cannot read input script: %s\n",input_script.c_str());result=1; }
            for(std::string line;running && std::getline(input,line);) { command(line);pump(30); }
        } else if(!capture_dir.empty()) {
            const char* pages[] = {"home","recent","quick","quota","clock","timer","sensors","assistant","settings","language","display","touch","wifi","wifi-password","ble","ble-device","sound","audio","power","date-time","set-time","storage","hardware","about","licenses","alarms","add-alarm","apps","reference","timer-options"};
            static_assert(sizeof(pages)/sizeof(pages[0])==static_cast<size_t>(assistant::Page::TimerOptions)+1,"Keep screenshot page names aligned with Page");
            for(int app=0;app<6;++app) { shell.open(static_cast<assistant::AppId>(app));pump(300); }
            for(size_t p=0;p<sizeof(pages)/sizeof(pages[0]);++p) {
                shell.show(static_cast<assistant::Page>(p),false);pump(400);
                char name[96];std::snprintf(name,sizeof(name),"%02zu-%s.bmp",p,pages[p]);
                if(!screenshot(display,(std::filesystem::path(capture_dir)/name).string())) result=1;
            }
        } else {
            int flags=fcntl(STDIN_FILENO,F_GETFL,0);
            if(flags>=0) fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK);
            std::string frame;char bytes[512];bool overflow=false;uint64_t last_print=0;
            while(running && lv_display_get_next(nullptr)) {
                ssize_t count;
                while((count=read(STDIN_FILENO,bytes,sizeof(bytes)))>0) {
                    for(ssize_t i=0;i<count;++i) {
                        if(bytes[i]=='\n') { if(!overflow && !frame.empty()) command(frame);frame.clear();overflow=false; }
                        else if(bytes[i]!='\r') { if(frame.size()<4095) frame+=bytes[i];else overflow=true; }
                    }
                }
                if(count==0) running=false;
                if(count<0 && errno!=EAGAIN && errno!=EWOULDBLOCK) running=false;
                pump(5);
                if(platform.millis()-last_print>5000) { printf("SIM_STATE %s\n",shell.debug_state().c_str());last_print=platform.millis(); }
            }
        }
    }
    SDL_DelEventWatch(watch,nullptr);
    lv_sdl_quit();
    return result;
}
