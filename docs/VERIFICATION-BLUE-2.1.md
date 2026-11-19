# Blue 2.1 验证记录

日期：2026-10-09。工程：`/Users/chengpeng/Desktop/work/ESPUI`。

本轮按用户计时器参考图优化原生 LVGL 页面。**尚未烧录设备，未进行新版真机显示、触控或性能测量。** 本文件登记软件证据；[上一轮 Blue 2.0](docs/VERIFICATION-BLUE-2.0.md) 与[原项目历史记录](docs/VERIFICATION-BASELINE.md) 单独归档。

## 本轮结果

固件版本 **2.1.0**。最终构建清单及 31 个源码文件哈希见 [reference-ui-build-manifest.json](logs/reference-ui-build-manifest.json)，已复核清单与当前源码、固件一致。

| 检查项 | 本轮结果 | 证据 |
| --- | --- | --- |
| Mac SDL2 构建 | 通过 | `logs/reference-ui-simulator-build.log` |
| 共用逻辑与 UI 回归 | CTest 1/1；28,637 次字形检查；100 次导航后控件数 25 → 25 | `logs/reference-ui-simulator-test.log`、`logs/reference-ui-self-test.log` |
| 新计时器指针测试 | 开始/暂停、计时提示音开关、重置确认、设置时长并返回通过 | `tests/self_test.cpp`、`logs/reference-ui-self-test.log` |
| 参考示意页指针测试 | 主按钮、开关、重置确认及关闭通过；不修改真实计时服务 | 同上 |
| 导航、表单和提醒回归 | 六种系统手势、取消、草稿保护、后台计时、提醒及现场恢复通过；覆盖层脚本 2/2 页码断言通过 | `logs/reference-ui-self-test.log`、`logs/reference-ui-overlays.log` |
| 原生截图 | 简体中文 30 页、English 30 页、覆盖层 4 页 | `.runtime/captures-reference-zh/`、`.runtime/captures-reference-en/`、`.runtime/captures-reference-overlays/` |
| 视觉复核 | 参考页和所审业务页通过；无剩余明显 P1/P2。英文副标题已改为单行 UI preview | [视觉复核及差异记录](docs/reference-design-qa.md) |
| 离线对照页 | 图片加载、键盘滑块通过；1400×1000 与 390×844 无横向溢出；无 JS 错误 | [对照页](docs/reference/index.html)、`logs/reference-ui-comparison-preview.json` |
| ESP32 构建 | ESP-IDF 5.5.5 构建通过；esptool image info 校验有效 | `logs/reference-ui-idf-build.log`、`logs/reference-ui-image-info.log` |
| 固件大小 | **5,721,488 B / 8,388,608 B，68.21%** | `build/esp32_assistant_ui.bin`、`logs/reference-ui-idf-size.log` |
| 静态 DIRAM | **182,211 B（53.32%）**；非运行时堆或 PSRAM 测量 | `logs/reference-ui-idf-size.log` |
| 额度协议回归 | 本地 Python 测试 7/7；未读取真实账户或接设备 | `logs/reference-ui-host-tests.log` |
| 设备烧录及新版真机性能 | **未执行、未验证** | 无新版设备测量 |

最终固件 SHA-256：

```text
b9ec71800993cbc8d5ee902ec7ff0153222e63dcd4ba41ca7216468e931fab0c
```

可重跑：

```bash
./scripts/simulator.sh test
./scripts/simulator.sh capture --capture-dir .runtime/captures-reference-zh
./scripts/simulator.sh capture --capture-dir .runtime/captures-reference-en --locale en-US
./scripts/simulator.sh capture --input-script tests/ui_review.txt
./scripts/idf.sh build
./scripts/idf.sh size
```

## 交互与截图证据边界

本轮参考页 Reference（28）仅用于同状态视觉比较，使用独立示意数据；实际 Timer（5）连接真实计时服务，TimerOptions（29）用于模式及时长设置。两条路径分别以 LVGL 指针输入验证。提示音开关关闭时仍弹出到点提醒，且不改变闹钟和全局静音设置；实际声音须在设备验证。

原有应用列表横滑、边缘返回、回首页、最近任务、任务关闭、快捷面板、短滑及回拉取消、后台计时、草稿与提醒恢复、息屏唤醒和反复导航均保留回归。`pointer` / `swipe` 经过 LVGL 输入处理，但不能替代 CST9217 真实采样或人手触控验收；诊断 `click` / `set` 本身不证明命中范围。

修复了 SDL 显示创建覆盖手动时间回调的问题：现在在创建显示后设置模拟时钟，批量截图能够推进开关和弹层动画，不再停留在动画首帧。静态截图仍不能证明动效帧率。界面动画设置的开关已通过软件回归，真机观感待测。

对照页保留用户原图、优化前计时器、当前参考页与实际业务页截图。关键中文字形位置已对齐参考，部分字形宽高约有 1–2 px 差异；开放字体、RGB565 量化与 JPEG 来源使结果并非逐像素相同。[常规截图页](docs/previews/index.html) 包含 18 张常规页面及 4 张覆盖层 PNG。

密码页紧凑 QWERTY 键盘的单键小于 48 px，是沿用的触控尺寸例外；此次计时器材质优化没有改变该键盘，需真机评估输入准确性。

## 资源与性能边界

43 张静态资源共 **2,275,640 B（2222.3 KiB）原始像素数据**：壁纸、六档彩色图标、五种玻璃表面和主按钮表面。此数值不是最终固件大小、运行时内存或性能结果。

本地开发工具预渲染柔化透色、方向高光与曲面，再通过误差扩散转为 RGB565。设备复用无文字的材质，文字、按钮、开关和业务状态由真实控件绘制；不将完整参考截图替代交互，也不运行实时背景模糊或折射。静态暖色反射不会根据背后应用内容实时变化。

## 真机待验证

- 烧录、重启、新字体和图片在 AMOLED 上的显示、触控命中与开关动效。
- 手势起点、速度、方向、取消阈值，与滑块、键盘和列表的冲突。
- 连续帧率、P95 帧间隔、运行时堆与 PSRAM、TE 超时和 DMA 错误。
- 提醒声音、录音回放、息屏唤醒、现场恢复、NVS 设置及 RTC 冷断电保持。
- 实际 Wi-Fi / SNTP、BLE 外设、SD 卡、电池与按键。

原项目约 **29.35 FPS（桌面滑动）/ 29.42 FPS（设置列表）** 仅为历史测量，不能据此宣称 Blue 2.1 已达到约 29.4 FPS 或完成设备验收。
