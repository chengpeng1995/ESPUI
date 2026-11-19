# Blue 2.2 验证记录

日期：2026-10-09。工程：`/Users/chengpeng/Desktop/work/ESPUI`。

本轮实现用户确认的规则：**应用全屏黑底，只有弹窗保留模糊玻璃质感**。已通过软件验证，尚未烧录设备或测量新版真机性能。[最新截图](docs/fullscreen/index.html) · [上一版历史记录](docs/VERIFICATION-BLUE-2.1.md)。

## 最终构建

固件版本 **2.2.0**。清单为 [fullscreen-ui-build-manifest.json](logs/fullscreen-ui-build-manifest.json)，已核对源码与固件 SHA-256。

| 检查项 | 结果 | 证据 |
| --- | --- | --- |
| SDL2 构建与 CTest | 1/1 通过 | `logs/fullscreen-ui-simulator-test.log` |
| 全屏应用约束 | 六应用与六个代表子页，共12页；466×466不透明黑底、壁纸隐藏、无应用级玻璃外框或关闭X | `tests/self_test.cpp` |
| 指针与手势 | 计时器开始/暂停、开关、重置、设置、示意页上滑退出；原有六种手势与取消回归通过 | `logs/fullscreen-ui-self-test.log` |
| 辅助导航 | 计时器底部操作不与导航条重叠，设置可真实指针点击 | 同上 |
| 字形与生命周期 | 28,637次字形检查；100次导航后对象25→25 | 同上 |
| 表单及覆盖层 | 未保存确认、提醒、停止后恢复通过；脚本2/2页码断言通过 | `logs/fullscreen-ui-overlays.log` |
| 中英文截图 | 各30页，加4张覆盖层 | `.runtime/captures-fullscreen-zh/`、`.runtime/captures-fullscreen-en/`、`.runtime/captures-fullscreen-overlays/` |
| HTML规范v1.4（2026-10-10同步） | 55项交互通过；34状态/64内嵌图像；桌面/窄屏、离线、键盘与打印检查通过 | [文档同步记录](docs/UI-SPEC-VERIFICATION.md) |
| ESP-IDF构建 | 5.5.5构建及image info校验通过 | `logs/fullscreen-ui-idf-build.log`、`logs/fullscreen-ui-image-info.log` |
| 固件大小 | **5,720,096 B，8 MiB应用分区占比68.19%** | `build/esp32_assistant_ui.bin` |
| 静态DIRAM | 182,211 B（53.32%）；非运行时堆或PSRAM测量 | `logs/fullscreen-ui-idf-size.log` |
| 额度协议 | 本地测试7/7通过 | `logs/fullscreen-ui-host-tests.log` |

最终固件 SHA-256：

```text
5dff3d2f94f2d423530e84fff2859083ebf05d84df726e46958e4f9bcbbc3a5c
```

## 视觉与功能边界

计时器与设置移除居中的玻璃外框；普通页面直接显示在黑底上。计时器取消关闭按钮，用既有返回、回首页及最近任务手势退出。主按钮、提示音、设置及重置继续操作真实计时服务；Reference诊断页仅维护独立示意状态。大于一小时的读数改用较小数字字体，保留小时。

首页、应用入口和最近任务保留系统壁纸；首页任务与最近任务卡改为普通实色。确认与提醒保留玻璃材质及输入阻断，关闭后恢复页面与草稿。其模糊透色来自离线纹理，**设备不进行实时背景模糊**，不会根据背后的应用内容实时更新反射。

[视觉复核](docs/fullscreen/QA.md)覆盖中英文计时器、设置、助理以及确认、提醒。批量截图与独立导航截图已按BMP像素核对，Settings→Assistant路径的状态栏和手势条完整；无需每页重启进程。

HTML规范于2026-10-10进一步同步为v1.4：补齐真实设置列表及子页，收录全部30页和4个覆盖层，六应用分别使用对应内容。浏览器只改变本地示例；固件源码与二进制哈希保持2.2.0不变。

`pointer` / `swipe`测试经过LVGL输入处理，但不能代替设备触控采样。密码页紧凑键盘仍有单键小于48px的尺寸例外，需真机评估。

## 复现

```bash
./scripts/simulator.sh test
./scripts/simulator.sh capture --capture-dir .runtime/captures-fullscreen-zh
./scripts/simulator.sh capture --capture-dir .runtime/captures-fullscreen-en --locale en-US
./scripts/simulator.sh capture --input-script tests/ui_review.txt
./scripts/idf.sh build
./scripts/idf.sh size
```

## 真机待验证

烧录与重启、AMOLED显示、手势命中、动画观感、运行时内存、连续帧率、TE/DMA、声音、录音、Wi-Fi/BLE、存储与电源仍未在本版设备验证。原项目约29.35/29.42 FPS属于历史实测，不能代表Blue 2.2。原工程未修改。
