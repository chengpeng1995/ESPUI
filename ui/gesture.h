#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>
namespace assistant {
enum class GestureZone { None, Top, Bottom, Left, Task, Launcher };
enum class GestureAction { None, Back, Exit, Recent, Previous, Next, Quick, Close, Home };
// Device-pixel thresholds; edge ownership is fixed at pointer-down.
class Gesture {
public:
    static GestureZone zone(int x,int y) {
        const int dx=x-233,dy=y-233,r2=dx*dx+dy*dy;
        if(r2>233*233)return GestureZone::None;
        // Only these small start regions own system input. The transparent
        // handle and the rest of the bottom arc never intercept list scrolling.
        if(x>=173&&x<=293&&y>=8&&y<=24)return GestureZone::Top;
        if(x>=191&&x<=275&&y>=448&&y<=464)return GestureZone::Bottom;
        if(x<=32&&y>=160&&y<=306)return GestureZone::Left;
        return GestureZone::None;
    }
    void begin(GestureZone z,int x,int y,uint64_t now) {zone_=z;x_=x;y_=y;dx_=dy_=0;axis_=0;hold_at_=0;ready_=false;held_=false;last_x_=x;last_y_=y;started_=now;}
    void move(int x,int y,uint64_t now) {
        dx_=x-x_;dy_=y-y_;
        if(!axis_&&std::max(std::abs(dx_),std::abs(dy_))>=12) {
            if(std::abs(dx_)>=std::abs(dy_)*1.5f)axis_=1;
            else if(std::abs(dy_)>=std::abs(dx_)*1.5f)axis_=2;
        }
        if(zone_==GestureZone::Bottom&&axis_==2&&-dy_>=64) {
            if(!hold_at_||std::abs(x-last_x_)>10||std::abs(y-last_y_)>10){hold_at_=now;last_x_=x;last_y_=y;}
            if(now-hold_at_>=280)held_=true;
        }else {hold_at_=0;if(-dy_<64)held_=false;}
        if(zone_==GestureZone::Bottom&&axis_==2&&-dy_>=64)ready_=true;
        if(zone_==GestureZone::Left&&axis_==1&&dx_>=64)ready_=true;
    }
    GestureAction finish(int x,int y,uint64_t now) {
        move(x,y,now);GestureAction result=GestureAction::None;
        if(zone_==GestureZone::Bottom&&axis_==2&&ready_&&-dy_>=64)result=held_?GestureAction::Recent:GestureAction::Exit;
        else if(zone_==GestureZone::Bottom&&axis_==1&&std::abs(dx_)>=64)result=dx_>0?GestureAction::Previous:GestureAction::Next;
        else if(zone_==GestureZone::Left&&axis_==1&&ready_&&dx_>=64)result=GestureAction::Back;
        else if(zone_==GestureZone::Top&&axis_==2&&dy_>=156)result=GestureAction::Quick;
        else if(zone_==GestureZone::Task&&axis_==2&&-dy_>=96)result=GestureAction::Close;
        else if(zone_==GestureZone::Launcher&&axis_==2&&-dy_>=64)result=GestureAction::Home;
        zone_=GestureZone::None;return result;
    }
    bool recent_ready() const {return held_;}
    bool pulling_quick() const {return zone_==GestureZone::Top&&dy_>0&&std::abs(dy_)>std::abs(dx_)*1.5f;}
    int dx() const {return dx_;} int dy() const{return dy_;}
    bool moved() const{return std::max(std::abs(dx_),std::abs(dy_))>=12;}
private:
    GestureZone zone_=GestureZone::None;int x_=0,y_=0,dx_=0,dy_=0,axis_=0,last_x_=0,last_y_=0;uint64_t hold_at_=0,started_=0;bool ready_=false,held_=false;
};
}
