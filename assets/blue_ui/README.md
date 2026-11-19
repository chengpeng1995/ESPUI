# 蓝色玻璃 UI 静态资源

基于工程内已确认的 `圆屏助理-UI设计规范.html` v1.2，并按用户计时器参考图修正材质和原生尺寸。

| 资源 | 数量与格式 | 原始像素字节 |
| --- | --- | --- |
| 圆屏壁纸 | 1 张，466×466 RGB565 | 434,312 |
| 彩色应用图标 | 6 个应用 × 40 / 46 / 64 / 82 / 94 / 124 px，RGB565A8 | 697,464 |
| 玻璃与按钮表面 | 5 种面板和 1 种主按钮，RGB565A8 | 1,143,864 |
| 合计 | **43 张** | **2,275,640（2222.3 KiB）** |

五种面板尺寸为 320×320 暖色、320×320 冷色、310×230 确认、288×222 最近任务和 278×94 首页任务；主按钮为 270×56。它们只包含可复用材质，没有文字和业务状态。标签、开关、按钮事件、导航与计时全部由原生 LVGL 控件处理。

`blue_ui_assets.c` / `.h` 直接参与 ESP32 与模拟器构建。每张数组按 128 字节对齐；RGB565 小端像素平面后接 A8 平面，行宽无额外填充。资源为常量，不需要启动时全量图片解码。PNG 用于开发审查，运行时不加载这些文件。原始像素总量不能作为运行时堆或最终固件大小。

## 材质与主题 API

开发时由本地 Chrome 渲染 HTML 中的柔化透色、饱和度、曲面与不均匀高光，再使用蛇形 Floyd–Steinberg 误差扩散转换为 RGB565，减少暗部色带。暖色面板保留中部橙色、两侧蓝青透色；冷色变体供设置、确认与最近任务使用。

Blue 2.2 起，普通应用是全屏纯黑底，普通控件使用不透明深灰；光学面板仅供确认和提醒等覆盖层使用。保留旧材质资源不表示应用仍以面板打开。

- `theme::sheet(obj, warm)` 通过背景图片设置原生表面，按尺寸选择变体，不插入控件子对象。
- `theme::primary(obj)` 设置 270×56 主按钮表面。
- `theme::app_icon(...)` 支持六档原生尺寸。
- `theme::glass(...)` 为通用控件提供不透明深灰或蓝色实色，不再绘制玻璃渐变。
- `theme::background(...)` 返回系统壁纸对象，应用及子页隐藏此对象。

设备端没有实时背景模糊、折射或持续壁纸动画；预渲染反射不随背后内容实时变化。未修改 TE 同步与触控传输管线，新版真机帧率仍须测量。

## 可选重建

普通编译直接使用已提供的 C 数组，无需 Node、Chrome、Python 图形依赖或网络取图。仅重建资源时需要 Python Pillow、Node、Playwright 和本机 Chrome / Chromium：

```bash
python3 assets/blue_ui/generate_assets.py
```

脚本默认使用本机 `/Applications/Google Chrome.app/Contents/MacOS/Google Chrome` 和 Codex 缓存中的 Playwright。其他机器可以覆盖路径：

```bash
export CHROME_PATH=/path/to/chrome
export PLAYWRIGHT_MODULE=/path/to/node_modules/playwright
python3 assets/blue_ui/generate_assets.py
```

生成器读取本工程 HTML，无需外部图片服务；更新后重新构建模拟器与固件，并复核原生截图。

## Blue 2.3 原生缩放与场景模糊

图标轮播新增 88 / 100 / 106 / 112 / 118 px，共 30 张、998,064 B 原始像素；复用 82 / 94 / 124 px 后组成八档。执行 `python3 assets/blue_ui/generate_motion_assets.py` 可从已有本地图标 PNG 重建。设备没有每帧缩放，标签、滑动和吸附仍是 LVGL 原生交互。

`theme::sheet()` 从 Blue 2.3 起直接绘制半透明深色与单条 1 px 边框；旧五种玻璃背景图仅保留历史生成兼容，不再用于当前弹窗。`ui/overlay_backdrop.cpp` 在打开时捕获当前场景并一次性模糊、压暗与量化，关闭时释放。Flash 链接器可移除未引用的旧表面，以上资源总字节不等于实际固件大小。
