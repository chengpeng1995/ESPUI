#!/usr/bin/env python3
"""Render native LVGL UI artwork from the local, approved HTML specification.

Development-only dependencies: Pillow, Node, Playwright and Chrome/Chromium.
No external service or browser is used by the firmware. Unlike CairoSVG, Chrome
renders the original CSS backdrop blur, saturation, gradients and edge masks.
Override PLAYWRIGHT_MODULE / CHROME_PATH when rebuilding on another machine.
"""
from pathlib import Path
import os
import subprocess
import struct
import tempfile
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
SPEC = ROOT / '圆屏助理-UI设计规范.html'
SIZES = (40, 46, 64, 82, 94, 124)
APPS = ('quota', 'clock', 'timer', 'sensors', 'assistant', 'settings')

# The same DOM and CSS that produce the accepted design, with all information
# and controls removed. Only reusable material, wallpaper and glyph assets are
# rasterized. Text, state, buttons and navigation remain native LVGL widgets.
RENDER = r'''
const fs = require('fs');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE);
(async () => {
  const source = fs.readFileSync(process.env.SPEC_PATH, 'utf8');
  const styles = source.match(/<style>([\s\S]*?)<\/style>/)[1]
    .replace('ellipse 44% 32%', 'ellipse 56% 32%')
    .replace('#FFFFFF17,transparent 74%', '#FFFFFF1B,transparent 74%');
  const defs = source.match(/<defs>([\s\S]*?)<\/defs>/)[1];
  const browser = await chromium.launch({executablePath:process.env.CHROME_PATH,headless:true});
  const page = await browser.newPage({viewport:{width:466,height:466},deviceScaleFactor:2});
  const library = `<svg style="position:absolute;width:0;height:0" xmlns="http://www.w3.org/2000/svg"><defs>${defs}</defs></svg>`;
  const reset = `html,body{margin:0!important;padding:0!important;width:466px!important;height:466px!important;min-width:0!important;background:transparent!important;overflow:hidden!important}.glass-lab{display:block!important;margin:0!important;padding:0!important;border:0!important;border-radius:0!important;width:466px!important;height:466px!important;background:transparent!important;isolation:auto!important}.gl-device-wrap{width:466px!important;height:466px!important;max-width:none!important;filter:none!important;container-type:normal!important}.gl-device{transform:none!important}.gl-device *{transition:none!important;animation:none!important}`;
  const begin = `<style>${styles}\n${reset}</style>${library}<div class="glass-lab"><div class="gl-device-wrap">`;
  const wall = `<div class="gl-wallpaper"><svg viewBox="0 0 466 466"><use href="#liquid-wallpaper"/></svg></div>`;
  const apps = [ ['quota','quota','azure'], ['clock','clock','blue'], ['timer','timer','orange'], ['sensors','sensor','teal'], ['assistant','mic','rose'], ['settings','settings','slate'] ];
  function tile(i, small=false) { return `<span class="gl-app-tile gl-${apps[i][2]}"><svg viewBox="0 0 24 24"><use href="#i-${apps[i][1]}"/></svg></span>`; }
  function launcher(warm) { return `<div class="gl-launcher"><div class="gl-carousel">${[1,2,3].map(i=>`<div class="gl-app ${i===2?'gl-selected':''}" style="--gl-index:${i}">${tile(!warm && i===2 ? 0:i)}</div>`).join('')}</div></div>`; }
  await page.setContent(begin+`<div class="gl-device">${wall}</div></div></div>`);
  await page.locator('.gl-device').screenshot({path:process.env.RENDER_DIR+'/wallpaper.png',omitBackground:true});
  // Reference 320 x 320 warm sheet; cool variants share the same optics and
  // remove the timer's orange reflection for neutral forms/recent task cards.
  for (const [name,w,h,r,warm] of [['sheet_warm',320,320,48,true],['sheet_cool',320,320,48,false],['sheet_confirm',310,230,36,false],['sheet_recent',288,222,34,false],['sheet_task',278,94,26,false]]) {
    const left=(466-w)/2, top=(466-h)/2;
    await page.setContent(begin+`<div class="gl-device gl-panel-open">${wall}${launcher(warm)}<div class="gl-shade"></div><div class="gl-panel" style="left:${left}px;top:${top}px;width:${w}px;height:${h}px;border-radius:${r}px;--gl-reflection-x:50%;--gl-reflection-y:8%"></div></div></div></div>`);
    await page.locator('.gl-panel').screenshot({path:process.env.RENDER_DIR+'/'+name+'.png',omitBackground:true});
  }
  // A native action surface, also without its live label.
  await page.setContent(`<style>${styles}\n${reset}.gl-primary{width:270px!important;height:56px!important;position:absolute;left:0;top:0;box-shadow:inset 0 1px #FFFFFF52,inset 0 -1px #06266430!important}</style><div class="glass-lab"><div class="gl-device" style="border-radius:0;background:transparent;box-shadow:none"><button class="gl-primary"></button></div></div>`);
  await page.locator('.gl-primary').screenshot({path:process.env.RENDER_DIR+'/primary.png',omitBackground:true});
  for (let i=0;i<apps.length;i++) for(const size of [40,46,64,82,94,124]) {
    const mini=size===46;
    const css = `.gl-app-tile{position:absolute!important;left:0;top:0;width:${size}px!important;height:${size}px!important;border-radius:${mini?15:size*35/124}px!important;box-shadow:inset 0 1px 0 #ffffff3a,inset 0 -1px 0 #071C4228!important}.gl-app-tile:after{inset:${mini?3:5*size/124}px!important;border-radius:${mini?12:size*30/124}px!important}.gl-app-tile svg{width:${mini?27:61*size/124}px!important;height:${mini?27:61*size/124}px!important}`;
    await page.setContent(`<style>${styles}\n${reset}\n${css}</style>${library}<div class="glass-lab">${tile(i)}</div>`);
    await page.locator('.gl-app-tile').screenshot({path:process.env.RENDER_DIR+`/icon_${apps[i][0]}_${size}.png`,omitBackground:true});
  }
  await browser.close();
})();
'''

def rounded_native(path, width, height, radius):
    """Clip the generated UI material to its authored round-corner geometry."""
    source = Image.open(path).convert('RGBA')
    mask = Image.new('L', source.size, 0)
    scale = source.width / width
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, source.width-1, source.height-1), radius=radius*scale, fill=255)
    # Preserve the original renderer's alpha for icon and control edges.
    from PIL import ImageChops
    source.putalpha(ImageChops.multiply(source.getchannel('A'), mask))
    return source.resize((width,height),Image.Resampling.LANCZOS)

def encode565(image, alpha):
    """Serpentine Floyd-Steinberg diffusion avoids visible 5/6-bit color bands.

    Output is native little-endian RGB565, followed by A8 when requested. This
    avoids both runtime RGB888 conversion and double-quantization on the panel.
    """
    width,height=image.size
    pixels=list(image.getdata())
    channels = [31,63,31]
    errors=[[0.0]*((width+2)*3),[0.0]*((width+2)*3)]
    rgb=bytearray(width*height*2)
    masks=bytearray(width*height)
    preview=bytearray(width*height*4)
    for y in range(height):
        current=errors[y%2]
        below=errors[(y+1)%2]
        below[:]=[0.0]*len(below)
        forward=(y%2==0)
        direction=1 if forward else -1
        for x in range(width) if forward else range(width-1,-1,-1):
            index=y*width+x
            original=pixels[index]
            q=[]
            for c,maxv in enumerate(channels):
                value=max(0,min(255,original[c]+current[(x+1)*3+c]))
                level=round(value*maxv/255)
                expanded=round(level*255/maxv)
                q.append(level)
                if original[3]>0:
                    error=(value-expanded)*0.88
                    current[(x+1+direction)*3+c]+=error*7/16
                    below[(x+1-direction)*3+c]+=error*3/16
                    below[(x+1)*3+c]+=error*5/16
                    below[(x+1+direction)*3+c]+=error/16
            struct.pack_into('<H',rgb,index*2,(q[0]<<11)|(q[1]<<5)|q[2])
            masks[index]=original[3]
            preview[index*4:index*4+4]=bytes((round(q[0]*255/31),round(q[1]*255/63),round(q[2]*255/31),original[3]))
    return bytes(rgb+masks if alpha else rgb), Image.frombytes('RGBA',image.size,bytes(preview))

def main():
    env=dict(os.environ)
    env.setdefault('PLAYWRIGHT_MODULE',str(Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'))
    env.setdefault('CHROME_PATH','/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
    env['SPEC_PATH']=str(SPEC)
    images=[]
    with tempfile.TemporaryDirectory(prefix='espui-blue-art-') as render_dir:
        env['RENDER_DIR']=render_dir
        subprocess.run(['node','-e',RENDER],env=env,check=True)
        folder=Path(render_dir)
        images.append(('blue_wallpaper',rounded_native(folder/'wallpaper.png',466,466,233),False,'wallpaper.png'))
        for name,w,h,r in [('warm',320,320,48),('cool',320,320,48),('confirm',310,230,36),('recent',288,222,34),('task',278,94,26)]:
            images.append((f'blue_sheet_{name}',rounded_native(folder/f'sheet_{name}.png',w,h,r),True,f'sheet_{name}.png'))
        images.append(('blue_primary',rounded_native(folder/'primary.png',270,56,24),True,'primary.png'))
        for name in APPS:
            for size in SIZES:
                images.append((f'blue_icon_{name}_{size}',rounded_native(folder/f'icon_{name}_{size}.png',size,size,15 if size==46 else size*35/124),True,f'{name}.png' if size==124 else f'{name}_{size}.png' if size in (46,94) else None))
    header=['#pragma once','#include "lvgl.h"','#ifdef __cplusplus','extern "C" {','#endif']
    source=['/* Generated by generate_assets.py from the local approved UI HTML. */','#include "blue_ui_assets.h"','#include <stdint.h>']
    total=0
    for name,im,alpha,preview in images:
        data,dithered=encode565(im,alpha)
        if preview: dithered.save(OUT/preview)
        total+=len(data)
        source.append(f'const __attribute__((aligned(128))) uint8_t {name}_pixels[{len(data)}] = {{')
        for pos in range(0,len(data),24): source.append('    '+','.join(f'0x{byte:02x}' for byte in data[pos:pos+24])+',')
        source.extend(['};',f'''const lv_image_dsc_t {name} = {{
    .header = {{.magic=LV_IMAGE_HEADER_MAGIC, .cf=LV_COLOR_FORMAT_{'RGB565A8' if alpha else 'RGB565'},
               .flags=0, .w={im.width}, .h={im.height}, .stride={im.width*2}}},
    .data_size = sizeof({name}_pixels), .data = {name}_pixels,
}};'''])
        header.append(f'LV_IMAGE_DECLARE({name});')
    header+=['#ifdef __cplusplus','}','#endif','']
    (OUT/'blue_ui_assets.c').write_text('\n'.join(source)+'\n')
    (OUT/'blue_ui_assets.h').write_text('\n'.join(header))
    print(f'{len(images)} native images; {total:,} raw pixel bytes ({total/1024:.1f} KiB).')

if __name__=='__main__': main()
