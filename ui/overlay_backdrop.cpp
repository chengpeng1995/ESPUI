#include "blue_theme.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace assistant::theme {
namespace {
// A 4:1 reduction keeps work bounded. Three radius-2 passes approximate a
// Gaussian with sigma ~10 display pixels and remove crisp underlying edges.
struct Pixel { uint8_t r,g,b; };
void box(const Pixel* src,Pixel* dst,int w,int h,bool horizontal) {
    const int lines=horizontal?h:w, length=horizontal?w:h;
    for(int line=0;line<lines;++line) {
        auto at=[&](int p){p=std::clamp(p,0,length-1);return horizontal?line*w+p:p*w+line;};
        int r=0,g=0,b=0;
        for(int k=-2;k<=2;++k){auto p=src[at(k)];r+=p.r;g+=p.g;b+=p.b;}
        for(int pos=0;pos<length;++pos){
            dst[at(pos)]={uint8_t(r/5),uint8_t(g/5),uint8_t(b/5)};
            auto out=src[at(pos-2)],in=src[at(pos+3)];
            r+=in.r-out.r;g+=in.g-out.g;b+=in.b-out.b;
        }
    }
}
}
lv_draw_buf_t* capture_backdrop(lv_obj_t* source) {
    auto* buffer=lv_snapshot_take(source,LV_COLOR_FORMAT_RGB565);
    if(!buffer)return nullptr;
    const int width=buffer->header.w,height=buffer->header.h;
    const int w=(width+3)/4,h=(height+3)/4;
    auto* small=static_cast<Pixel*>(std::malloc(w*h*sizeof(Pixel)));
    auto* temp=static_cast<Pixel*>(std::malloc(w*h*sizeof(Pixel)));
    if(!small||!temp){std::free(small);std::free(temp);lv_draw_buf_destroy(buffer);return nullptr;}
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        unsigned r=0,g=0,b=0,n=0;
        for(int dy=0;dy<4&&y*4+dy<height;++dy){
            auto* row=reinterpret_cast<const uint16_t*>(buffer->data+(y*4+dy)*buffer->header.stride);
            for(int dx=0;dx<4&&x*4+dx<width;++dx){
                const uint16_t p=row[x*4+dx];r+=((p>>11)&31)<<3;g+=((p>>5)&63)<<2;b+=(p&31)<<3;++n;
            }
        }
        small[y*w+x]={uint8_t(r/n),uint8_t(g/n),uint8_t(b/n)};
    }
    for(int pass=0;pass<3;++pass){box(small,temp,w,h,true);box(temp,small,w,h,false);}
    static constexpr int dither[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    for(int y=0;y<height;++y){
        auto* row=reinterpret_cast<uint16_t*>(buffer->data+y*buffer->header.stride);
        const int sy=y/4,ny=std::min(sy+1,h-1),fy=y&3;
        for(int x=0;x<width;x+=4){
            const int sx=x/4,nx=std::min(sx+1,w-1);
            const Pixel a=small[sy*w+sx],b=small[sy*w+nx],c=small[ny*w+sx],d=small[ny*w+nx];
            const int rl=a.r*(4-fy)+c.r*fy,gl=a.g*(4-fy)+c.g*fy,bl=a.b*(4-fy)+c.b*fy;
            const int dr=b.r*(4-fy)+d.r*fy-rl,dg=b.g*(4-fy)+d.g*fy-gl,db=b.b*(4-fy)+d.b*fy-bl;
            int r=rl*4,g=gl*4,blue=bl*4;
            for(int k=0;k<4&&x+k<width;++k){
                // Bilinear stepping and the 50% cool shade use fixed-point
                // math; dither removes quantization rings on RGB565 AMOLED.
                const int noise=dither[y&3][k];
                const int qr=std::min(31,((r>>5)+1+(noise>>1))>>3);
                const int qg=std::min(63,((g>>5)+3+(noise>>2))>>2);
                const int qb=std::min(31,((blue>>5)+8+(noise>>1))>>3);
                row[x+k]=uint16_t((qr<<11)|(qg<<5)|qb);r+=dr;g+=dg;blue+=db;
            }
        }
    }
    std::free(small);std::free(temp);
    lv_draw_buf_flush_cache(buffer,nullptr);
    return buffer;
}
void backdrop(lv_obj_t* parent,lv_draw_buf_t* buffer) {
    lv_obj_set_style_bg_color(parent,lv_color_hex(0x050B14),0);
    lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);
    if(!buffer)return; // A solid dim background is safe under memory pressure.
    auto* image=lv_image_create(parent);lv_image_set_src(image,buffer);lv_obj_set_pos(image,0,0);
    lv_obj_remove_flag(image,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_move_background(image);
}
lv_obj_t* overlay(lv_obj_t* parent) {
    auto* buffer=capture_backdrop(parent);
    auto* layer=lv_obj_create(parent);lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer,466,466);lv_obj_remove_flag(layer,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(layer,LV_OBJ_FLAG_CLICKABLE);
    backdrop(layer,buffer);
    if(buffer){
        auto* image=lv_obj_get_child(layer,0);
        lv_obj_add_event_cb(image,[](lv_event_t* e){
            auto* data=static_cast<lv_draw_buf_t*>(lv_event_get_user_data(e));
            lv_image_cache_drop(data);lv_draw_buf_destroy(data);
        },LV_EVENT_DELETE,buffer);
    }
    return layer;
}
} // namespace assistant::theme
