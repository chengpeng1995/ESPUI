# 圆屏助理 Blue 2.6.2

面向 Waveshare ESP32-S3-Touch-AMOLED-1.75 的 466×466 圆屏 C++ / LVGL 工程。当前目录 `/Users/chengpeng/Desktop/work/ESPUI` 是本轮蓝色玻璃 UI 重构的新工程，设备端与 Mac SDL2 模拟器共用页面、组件与业务逻辑。

本目录 [圆屏助理-UI设计规范.html](圆屏助理-UI设计规范.html) 已重整为 **v2.0**：组件、表单、页面、覆盖层和动效分章，所有界面示例均为可离线操作的 HTML / CSS 与内联矢量图标，不嵌入截图。统一规则仍为 **应用全屏黑底，只有弹窗使用模糊玻璃质感**，保留蓝色主操作、彩色圆角图标和手势导航。

Blue 2.6.2 在已确认改善的 2.6.1 轮播交互基础上，继续修复仅电池待机时出现的误亮：唤醒中断上下文固定在内部 RAM，屏幕与触摸复位脚、QSPI 片选、空闲功放控制脚保持睡眠输出；休眠准备阶段也验证双触点报告。局部双缓冲绘制与 DMA 并行，保留 TE 防撕裂同步。应用列表上滑回锁屏首页；应用图标、设置及导航列表行取消按住背景高亮和整行缩放。下拉完整面板与手指位移 1:1，松手复用同一面板完成剩余位移；触摸唤醒需要两份新鲜、稳定且间隔至少 12 ms 的有效报告。GPIO11 在运行和浅睡眠都启用上拉，固定用低电平唤醒；覆盖手势、ACK 后旧包、异常数量/ID/坐标均不作为触点。拔 USB 后无线暂停失败保持黑屏并重试，保留原无线启用状态；`@UI power history` 可只读查询最近 16 条进入待机、确认、拒绝和恢复记录；`@UI power boot` 读取复位原因、内部内存检查及 RTC 保留的上次电源阶段，均不亮屏。保留应用退出语义、中央底部 84×16 px 手势区、列表末项停靠、弹窗细边框和场景缓存模糊。原工程 `/Users/chengpeng/Documents/Codex/2026-10-03/es/esp32-assistant-ui` 保持原状。

设备运行、测试与性能结果见 [VERIFICATION.md](VERIFICATION.md)。原项目约 29.4 FPS 仅为历史测量，本轮测量按场景单独登记。HTML 文档仍为纯 DOM 示例，无截图替代界面。

[Blue 2.3 模拟器效果](docs/blue-2.3/index.html) · [构建与验收结果](VERIFICATION.md) · [规范完整同步记录](docs/UI-SPEC-VERIFICATION.md)

## 熄屏省电（Blue 2.4）

设置 → 电源与电池新增「熄屏省电」和「立即熄屏」。省电默认开启，轻触屏幕唤醒，保留当前页面与草稿。倒计时与闹钟按独立截止时间唤醒；录音、播放和响铃期间不待机。

待机暂停 LVGL、TE 中断、IMU 与空闲音频；电池供电时暂停无线并允许 CPU 自动浅睡眠。连接电脑时保留 USB 通信。实际设备结果与未验证项见 [VERIFICATION.md](VERIFICATION.md)，实现、策略及诊断说明见 [POWER-MANAGEMENT.md](docs/POWER-MANAGEMENT.md)。

## 本地预览与测试

在当前工程目录执行。脚本会打印实际使用的工具链路径。

```bash
cd /Users/chengpeng/Desktop/work/ESPUI

# 直接打开 C++ 模拟器，不启动账户额度桥接
./scripts/simulator.sh direct

# 以明确 TEST 标记的示例额度运行模拟器
./scripts/simulator.sh run --mock

# 无窗口批量截图：固定本地示例，不读取个人设置
./scripts/simulator.sh capture --capture-dir .runtime/captures-zh
./scripts/simulator.sh capture --capture-dir .runtime/captures-en --locale en-US

# 公共逻辑、组件及真实 LVGL 指针事件测试
./scripts/simulator.sh test

# 仅编译 / 停止由 Python 包装器启动的模拟器
./scripts/simulator.sh build
./scripts/simulator.sh stop
```

`direct` 从标准输入接收诊断命令，使用本地模拟硬件；没有额度输入时显示等待状态。`run --mock` 启动本地模拟器包装器并注入示例额度。两者都不访问 Mac 的无线设备或麦克风。`capture` 使用 SDL dummy 驱动，输出 466×466 BMP，并使用一次性设置文件；截图代表 LVGL 模拟器渲染，不能替代实际 AMOLED 的显示验收。

模拟器 F1–F6 分别切换正常、断网、连接失败、低电量、无 SD 卡、硬件异常。批量截图也可传入 `--scenario offline` 等场景参数。真实账户模式 `./scripts/simulator.sh run` 与 USB 额度桥接 `./scripts/bridge.sh` 保留供后续联调，本轮预览使用直接或示例模式。

修改源码后重新执行预览命令；已运行的窗口不会热更新。完整轨迹命令和字体工具见 [scripts/README.md](scripts/README.md)。

## 页面与手势（Blue 2.6.2）

首页使用纯黑底，居中显示 64 px 时间与 22 px 日期，底部保留应用入口提示，不显示壁纸、进行中的任务或下一项闹钟卡。后台计时与闹钟继续工作。六应用入口独立为应用列表：额度、时钟、计时器、传感器、助理、设置。当前设备实现采用**横向彩色图标分页**，间距 160 px；起拖 6 px 后按总位移一比一移动，可跨多项和原路回拉，松手后 180 ms 吸附最近项，按住期间没有吸附或惯性；HTML 文档里的纵向布局是对比演示，不是当前固件的切换选项。

系统手势仅由限定起点区域接管。底部 `(191…275,448…464)`，顶部 `(173…293,8…24)`，左侧 `x≤32,y=160…306`，均裁切于圆内；底部其他位置继续交给内容滚动。视觉手势条是 `(201,452,64,4)`，不拦截触摸。除顶部下拉预览外，方向移动达到 12 px 且主轴超过副轴 1.5 倍后锁定；顶部预览从有效竖向位移开始跟手，松手后按提交阈值判断。

| 起点与操作 | 结果 / 阈值 |
| --- | --- |
| 左边缘向右滑 | 返回一级；松手位移至少 64 px |
| 底部短上滑 | 应用内退出并回应用列表，移除该任务；应用列表回锁屏首页，保留任务；松手至少 64 px |
| 底部上滑后停留 | 超过 64 px 后稳定停留约 280 ms，进入最近任务，保留应用 |
| 底部横滑 | 位移至少 64 px，切换最近应用；到头不循环 |
| 最近任务卡向上滑 | 至少 96 px 后关闭任务实例；计时、闹钟服务继续 |
| 顶部向下滑 | 整块面板从 y=-310 出发，与手指位移 1:1，最多 388 px；至少 156 px 松手提交，同一面板完成剩余位移，不足回弹；也可点击状态栏 |
| 首页内容向左滑 | 打开应用列表 |
| 应用列表中部向上滑 | 竖向至少 64 px 返回锁屏首页；不改变最近任务；短滑取消且不误点图标 |

未达阈值时取消。最近任务可横向浏览、点击恢复应用；当前设置子页与列表位置由 Shell 保留。设置 → 显示与手势中可打开辅助导航，显示返回、退出和最近任务按钮；该模式停用边缘系统手势，便于不熟悉手势的用户操作。

快捷设置提供亮度、Wi-Fi、蓝牙、静音和语言等控制，返回原页面。Wi-Fi 密码使用屏幕键盘，日期时间使用有范围与月份天数约束的数字选择器；未保存表单离开前显示确认。危险操作使用确认面板。到点提醒覆盖当前页面并要求停止提醒，关闭后恢复原现场；息屏后的首次触摸只唤醒。

助理仍是明确标注的演示交互，未接真实模型；录音在点击后开始，离开录音页停止，关闭相关任务释放录音缓冲。闹钟要求设备保持开机，息屏不影响计时服务；未实现断电自动开机闹钟。

计时器直接占满圆屏，以 64 px 数字显示未满一小时的读数，270×56 蓝色按钮执行开始、暂停和继续。模式和时长在“计时设置”编辑，重置需要确认。应用通过底部短上滑退出到应用列表、边缘返回和底部横滑切换；计时服务继续后台运行。辅助导航开启时，底部操作自动为导航按钮让位。关闭计时提示音仍显示提醒，不影响闹钟或全局静音。

诊断页 Reference（28）沿用计时器的全屏黑底布局，使用独立示意数据，不影响真实服务。此前 Blue 2.1 的居中面板参考图保留为历史记录，不再代表当前应用结构。

## 背景与材质

首页、应用入口、最近任务、六个应用及其子页、密码与日期编辑器统一使用不透明 `#000000` 底色。普通列表、卡片和控件使用不透明深灰，最近任务卡也不使用模糊材质。彩色图标和蓝色主按钮保持独立视觉层级。

确认、到点提醒和快捷面板打开时缓存当前 LVGL 场景，缩小到四分之一宽高做三轮分离模糊，再一次性展开为带压暗的 RGB565 原生背景。弹窗只画一条低对比 1 px 边框，普通页面保持黑底。缓存关闭时释放；失败时回退为深色遮罩，不暴露底层按钮边缘。模糊近似半径约 10 px，不是浏览器逐帧背景模糊。

`ui/blue_theme.*` 管理颜色与材质，`ui/overlay_backdrop.cpp` 管理场景模糊。应用图标缩放使用 82…124 px 的八档原生资源，避免 ESP32 的实时仿射缩放开销；生成工具为 `assets/blue_ui/generate_motion_assets.py`。操作按钮按压、开关滑移、滑块手柄和页面/弹窗过渡受动画设置控制；应用图标、设置及导航列表行保持静态背景，不按压高亮或缩放整个行。手势拖动的直接反馈继续保留。输入读取定时器请求 8 ms，显示刷新定时器请求 16 ms；实际处理和呈现频率受绘制、局部传输和 TE 同步约束，不能视为 125 Hz 输入或 60 FPS。轮播仅在选中项变化时更新文字颜色和页码。

此前 Blue 2.6.0 同一设备与相同轨迹各测三次，中位完成帧率：应用轮播 29.40 → 48.61 FPS，设置列表 29.37 → 58.46 FPS。局部双缓冲绘制与 DMA 并行，中央变化区域在传输预算允许时提前发送；大范围或顶部更新保留原半帧相位。总线仍为 80 MHz，TE 保留；睡眠前与恢复亮度前等待传输队列完全结束。该组历史结果见 docs/VERIFICATION-BLUE-2.6.0.md；IRQ 至驱动读取的诊断不等于触摸至画面的光学延迟。

列表视口从 y110 延伸至 y454；44 px 留白属于内容内部，最后一行可停在 y410。开启辅助导航时视口缩短至270 px，为可见按钮预留空间。

沿用 CO5300 显示适配、GPIO13 TE 同步和双 DMA 缓冲；Blue 2.6 使用两个全屏容量的绘制缓冲存放局部区域，按变化范围传输。旧版的物理显示稳定性结论不直接延续到新刷新路径；新版的帧间隔、内存占用、TE 超时、DMA 错误和屏幕撕裂分别验证。

## 工程结构

```text
圆屏助理-UI设计规范.html     可离线阅读的设计规范和浏览器交互示例
core/system.*              设置、计时、闹钟、额度协议与平台接口
core/strings.def            中英文界面文案
core/i18n.*                 语言解析与文案查找
ui/shell.*                  首页、应用列表、最近任务、导航及覆盖层
ui/shell_widgets.cpp        通用组件、设置列表、密码与日期表单
ui/shell_timer.cpp          全屏计时器、计时设置及独立示意页
ui/shell_quick.cpp          带场景模糊的快捷设置面板
ui/gesture.h                手势区域、方向锁定、阈值与提交规则
ui/blue_theme.*             蓝色主题与原生尺寸图标 API
ui/launcher_indicator.h     应用分页指示器
assets/blue_ui/             已编译用图片数组、审查 PNG、可选重建脚本
assets/fonts/               共用中英文字体与 64 px 时间数字
platform/esp_platform.*     ESP32 的存储、网络、BLE 与硬件服务
platform/power_manager.*    熄屏状态交接、CPU/USB 电源锁、触摸与到期唤醒
platform/board_display.*    显示同步与传输适配
platform/board_audio.*      ES8311 / ES7210 音频适配
platform/sim_platform.*     模拟硬件、失败场景与测试时钟
main/main.cpp              ESP32 入口及 UI 线程
simulator/main.cpp         SDL2 窗口、截图和指针轨迹输入
scripts/                   当前工程构建、运行与工具链定位
host/                      额度桥接和本地 UI 控制工具
tests/self_test.cpp        公共逻辑与 LVGL 交互回归
docs/VERIFICATION-BASELINE.md  原项目历史记录存档
docs/VERIFICATION-BLUE-2.0.md  2.0 软件验证存档
docs/VERIFICATION-BLUE-2.1.md  2.1 居中面板方案验证存档
docs/reference/             Blue 2.1 历史原图及居中面板对照
docs/fullscreen/            Blue 2.2 全屏黑底应用与弹窗预览
VERIFICATION.md            当前版本验证证据和剩余边界
```

UI 通过 `Platform::request()` 与快照访问业务和硬件，LVGL 与 Mooncake 在 UI 线程使用。切换页面后销毁旧控件，后台计时等服务保留。设备设置保存在 NVS；常规模拟器设置在当前工程 `.runtime/settings.json`，批量截图与测试使用独立临时设置。密码与登录凭据不应写入界面日志。

## 工具链与固件构建

固定依赖版本见 `dependencies.lock`、`upstream-lock.json` 和 [THIRD_PARTY.md](THIRD_PARTY.md)。本机可复用 `/Users/chengpeng/Documents/Codex/2026-10-03/es/.toolchain`，无需修改原工程源码。其他机器设置工具链路径即可：

```bash
export ESPUI_TOOLCHAIN_ROOT=/path/to/.toolchain
export SDL2_ROOT=/opt/homebrew/opt/sdl2
./scripts/simulator.sh build
./scripts/idf.sh build
./scripts/idf.sh size
```

工具链目录包含 `esp-idf/` 与 `espressif/`。脚本优先使用显式变量，否则依次检查当前工程、上级目录和本机原工具链位置；不存在会明确报错。SDL2、CMake/Ninja 和 ESP-IDF 环境要求详见 [scripts/README.md](scripts/README.md)。构建输出分别位于 `build-simulator/` 和 `build/`。

上述命令不烧录设备。后续硬件验收时可使用 `./scripts/idf.sh flash PORT` 和 `./scripts/idf.sh monitor PORT`，先核对目标串口及待测固件；电源管理本轮的实际刷机记录见 VERIFICATION.md。串口监视器和额度桥接不可同时占用同一端口。

字体和图片数组均已提供，普通构建不依赖 Node、图像转换器或网络取图。新增文案后可运行 `python3 scripts/generate_fonts.py --offline`；静态图片重建说明见 [assets/blue_ui/README.md](assets/blue_ui/README.md)。

## 验证记录的使用

当前版本的模拟器构建、测试、截图、固件编译与资源大小只记录在 [VERIFICATION.md](VERIFICATION.md)。[历史存档](docs/VERIFICATION-BASELINE.md) 引用的日志路径、旧固件哈希、备份和烧录结论均属于原项目，没有复制到当前工程，也不是新版的通过证明。
