#!/usr/bin/env python3
"""Generate shared LVGL fonts: UI strings and GB2312 dynamic text."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
FONT_SPECS = (
    ('font_ui_18', 18, 'regular', 'ui'),
    ('font_ui_20', 20, 'regular', 'ui'),
    ('font_ui_24', 24, 'regular', 'ui'),
    ('font_ui_28', 28, 'regular', 'ui'),
    ('font_ui_22_bold', 22, 'bold', 'ui'),
    ('font_ui_28_bold', 28, 'bold', 'ui'),
    ('font_cjk_22', 22, 'regular', 'cjk'),
    ('font_digits_64', 64, 'regular', 'digits'),
)
parser.add_argument('--only', choices=tuple(f[0] for f in FONT_SPECS), action='append', help='Regenerate only named font(s)')
parser.add_argument('--offline', action='store_true', help='Require local font and cached npm packages; never download')
args = parser.parse_args()
SOURCE_DIR = ROOT / 'vendor/fonts'
SOURCE_BASE = 'https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/Sans'
LICENSE_URL = f'{SOURCE_BASE}/LICENSE'
SOURCE_DIR.mkdir(exist_ok=True)
if not (SOURCE_DIR / 'LICENSE').exists():
    if args.offline: raise SystemExit('Missing local font license')
    subprocess.run(['curl','--fail','--location','--retry','2',LICENSE_URL,'--output',str(SOURCE_DIR/'LICENSE')],check=True)
lock = SOURCE_DIR / 'source.json'
locked = json.loads(lock.read_text()) if lock.exists() else {}
license_digest = hashlib.sha256((SOURCE_DIR/'LICENSE').read_bytes()).hexdigest()
if locked.get('license_sha256') and locked['license_sha256'] != license_digest:
    raise SystemExit('Font license checksum differs from locked source')
# Keep the original top-level regular-font lock readable by older tooling.
sources = dict(locked.get('sources', {}))
if locked.get('sha256') and 'regular' not in sources:
    sources['regular'] = {k: locked[k] for k in ('url', 'sha256')}
selected = [spec for spec in FONT_SPECS if not args.only or spec[0] in args.only]
for weight in sorted({spec[2] for spec in selected}):
    filename = f'NotoSansCJKsc-{weight.title()}.otf'
    source = SOURCE_DIR / filename
    url = f'{SOURCE_BASE}/OTF/SimplifiedChinese/{filename}'
    if not source.exists():
        if args.offline: raise SystemExit(f'Missing local font: {source}')
        subprocess.run(['curl','--fail','--location','--retry','2',url,'--output',str(source)],check=True)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    if weight in sources and sources[weight]['sha256'] != digest:
        raise SystemExit(f'{weight.title()} font checksum differs from locked source')
    sources[weight] = {'url': url, 'sha256': digest, 'file': filename}
locked.update(sources.get('regular', {}))
locked.update({'converter': 'lv_font_conv@1.5.3', 'license_url': LICENSE_URL,
               'license_sha256': license_digest,
               'sources': sources})
lock.write_text(json.dumps(locked, indent=2)+'\n')
tool = ROOT / 'scripts/font-tools/node_modules/.bin/lv_font_conv'
if not tool.exists():
    caches = [Path(os.environ['ESPUI_TOOLCHAIN_ROOT'])/'cache/npm'] if os.environ.get('ESPUI_TOOLCHAIN_ROOT') else [ROOT/'.toolchain/cache/npm', ROOT.parent/'.toolchain/cache/npm', Path('/Users/chengpeng/Documents/Codex/2026-10-03/es/.toolchain/cache/npm')]
    cache = next((p for p in caches if p.exists()), ROOT/'.runtime/npm-cache')
    subprocess.run(['npm','ci','--prefix',str(ROOT/'scripts/font-tools'),'--cache',str(cache),'--ignore-scripts','--no-audit','--no-fund']+(['--offline'] if args.offline else []),check=True)
strings = (ROOT/'core/strings.def').read_text()
ui = set(''.join(re.findall(r'"([^"\n]*)"',strings)))
ui.update(chr(i) for i in range(32,127))
ui.update('简体中文English→←×✓●○…：，。℃°−–—%·')
cjk = set(ui)
for high in range(0xA1,0xF8):
    for low in range(0xA1,0xFF):
        try: cjk.update(bytes([high,low]).decode('gb2312'))
        except UnicodeDecodeError: pass
manifest_path = ROOT/'assets/fonts/manifest.json'
manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
for name,size,weight,charset in selected:
    chars = {'ui':ui, 'cjk':cjk, 'digits':set('0123456789:')}[charset]
    source = SOURCE_DIR / sources[weight]['file']
    symbols=''.join(sorted(chars)); output=ROOT/'assets/fonts'/f'{name}.c'
    subprocess.run([str(tool),'--font',str(source),'--size',str(size),'--bpp','4','--format','lvgl','--symbols',symbols,'--no-kerning','--lv-font-name',name,'--lv-fallback','lv_font_montserrat_20','-o',str(output)],check=True)
    manifest[name]={'size':size,'weight':weight,'glyphs':len(chars),'source_bytes':output.stat().st_size,'font_sha256':sources[weight]['sha256']}
    print(name,manifest[name],flush=True)
(ROOT/'assets/fonts/manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
