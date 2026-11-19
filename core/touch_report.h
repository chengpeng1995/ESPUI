#pragma once
#include <stdbool.h>
#include <stdint.h>

// CST92xx wire report, shared by the board driver and native protocol tests.
typedef enum { TOUCH_PACKET_INVALID, TOUCH_PACKET_CONSUMED, TOUCH_PACKET_RELEASE,
               TOUCH_PACKET_DOWN, TOUCH_PACKET_GESTURE } touch_packet_kind_t;
typedef struct { touch_packet_kind_t kind; uint16_t x,y; uint8_t id; } touch_packet_t;
static inline touch_packet_t touch_packet_decode(const uint8_t raw[10],uint16_t width,uint16_t height){
    touch_packet_t p={TOUCH_PACKET_INVALID,0,0,0};
    if(raw[6]!=0xAB)return p;
    if(raw[0]==0xAB){p.kind=TOUCH_PACKET_CONSUMED;return p;}
    if(raw[4]&0xF0){p.kind=TOUCH_PACKET_GESTURE;return p;}
    const unsigned count=raw[5]&0x7F;
    if(count>1)return p;
    if(count==0||raw[0]==0){p.kind=TOUCH_PACKET_RELEASE;return p;}
    p.id=raw[0]>>4;
    if(p.id!=0)return p; // This board supports one hardware contact.
    const unsigned event=raw[0]&0x0F;
    if(event==4){p.kind=TOUCH_PACKET_RELEASE;return p;}
    if(event!=6)return p;
    p.x=(raw[1]<<4)|(raw[3]>>4);p.y=(raw[2]<<4)|(raw[3]&0x0F);
    if(p.x>=width||p.y>=height){p.kind=TOUCH_PACKET_INVALID;return p;}
    p.kind=TOUCH_PACKET_DOWN;return p;
}

// Two fresh, ACK-separated down reports, at least 12 ms apart. A single
// electrical spike or previously consumed packet must not light the panel.
typedef struct { bool candidate;uint32_t at;uint16_t x,y; } touch_wake_filter_t;
static inline bool touch_wake_filter_sample(touch_wake_filter_t* f,touch_packet_t p,uint32_t now){
    if(p.kind==TOUCH_PACKET_RELEASE||p.kind==TOUCH_PACKET_GESTURE||p.kind==TOUCH_PACKET_INVALID){f->candidate=false;return false;}
    if(p.kind!=TOUCH_PACKET_DOWN)return false;
    int dx=(int)p.x-f->x,dy=(int)p.y-f->y;
    if(f->candidate&&now-f->at<=72&&dx>=-48&&dx<=48&&dy>=-48&&dy<=48)return now-f->at>=12;
    f->candidate=true;f->at=now;f->x=p.x;f->y=p.y;return false;
}
