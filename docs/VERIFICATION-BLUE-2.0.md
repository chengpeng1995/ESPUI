# Blue 2.0 验证记录

日期：2026-10-09。工程：`/Users/chengpeng/Desktop/work/ESPUI`。

本文件只记录当前蓝色玻璃重构版本的证据。**本轮未烧录设备，未进行新版真机显示、手势或性能测量。** 原项目历史记录已经移至 [docs/VERIFICATION-BASELINE.md](docs/VERIFICATION-BASELINE.md)；其中日志、固件哈希和设备备份不属于当前版本。

## 本轮结果登记

当前源码已完成双端构建与以下软件验证。固件版本 **2.0.0**；构建清单见 [logs/blue-ui-build-manifest.json](logs/blue-ui-build-manifest.json)。

| 检查项 | 本轮结果 | 证据 |
| --- | --- | --- |
| Mac SDL2 构建 | 通过 | `logs/blue-ui-simulator-build.log` |
| 共用逻辑与 UI 回归 | CTest 1/1 通过；11,586 次字形检查；100 次导航后控件数 25 → 25 | `logs/blue-ui-simulator-test.log`、`logs/blue-ui-self-test.log` |
| LVGL 指针输入 | 应用列表横滑与点击；六种系统手势、短滑/回拉取消；快捷开关点击；任务卡关闭与后台计时；休眠后边缘唤醒通过 | `tests/self_test.cpp`、`logs/blue-ui-cli-gestures.log` |
| 草稿和提醒 | 快捷面板不能绕过草稿保护；提醒阻断普通导航；停止后恢复日期草稿，离开仍需确认 | `logs/blue-ui-self-test.log`、`logs/blue-ui-overlays.log` |
| 页面截图 | 简体中文 28 页、English 28 页；英文启动日志明确 `locale=en-US` | `.runtime/captures-blue-zh/`、`.runtime/captures-blue-en/` |
| 视觉复核 | 目检首页、应用列表、最近任务、快捷设置、设置、计时、密码、日期、助理及确认/提醒/恢复；已修文字重叠、英文 Show 换行和弹窗可读性 | [截图预览](docs/previews/index.html) |
| ESP32 固件编译 | ESP-IDF 5.5.5 构建通过，esptool image info 校验有效 | `logs/blue-ui-idf-build.log`、`logs/blue-ui-image-info.log` |
| 固件大小 | **4,222,656 B / 8,388,608 B**，占应用分区 50.34% | `build/esp32_assistant_ui.bin`、`logs/blue-ui-idf-size.log` |
| 静态 DIRAM | **182,211 B（53.32%）**；不是运行时堆或 PSRAM 测量 | `logs/blue-ui-idf-size.log` |
| 额度桥接回归 | 本地 Python 协议测试 7/7 通过；未读真实账户或接设备 | `logs/blue-ui-host-tests.log` |
| 设备烧录 / 新版真机性能 | **本轮未执行、未验证** | 无新版设备测量 |

最终固件 SHA-256：

```text
fba286439ab62ae34edc5794774ba6a3ea235743ab7fc88d6a53b771d65cc135
```

可重跑：

```bash
./scripts/simulator.sh test
./scripts/simulator.sh capture --capture-dir .runtime/captures-blue-zh
./scripts/simulator.sh capture --capture-dir .runtime/captures-blue-en --locale en-US
./scripts/simulator.sh capture --input-script tests/ui_review.txt
./scripts/idf.sh build
./scripts/idf.sh size
```

覆盖层脚本使用固定示例日期和模拟计时，截图写入 `.runtime/captures-overlays/`；不访问设备或保存个人设置。当前截图页包含 18 张常规页面 PNG 和 4 张覆盖层 PNG。密码页采用紧凑 QWERTY 键盘，单键小于 48 px 的常规触控目标，是本版的尺寸例外；需真机评估输入准确性。普通确认与主操作按钮保持至少 56 px 高。

## 测试范围

软件回归覆盖以下路径；静音与实际发声、触摸采样等硬件项仍按下节边界另行验证：

- 首页与应用列表分离；六图标横滑、选中后进入应用；默认隐藏辅助导航。
- 左边缘返回、底部回首页、底部上滑停留进入最近任务、底部横滑切换、顶部快捷设置；短距离取消与控件内拖动冲突。
- 最近任务卡上滑关闭一个应用，后台计时继续；重新进入应用恢复相应现场。
- 密码、日期时间和闹钟草稿的保存、取消与未保存确认；快捷面板往返、语言改变、到点提醒期间的草稿保留。
- 提醒阻断普通导航，停止提醒后返回原页面；静音与提醒声音顺序需分别验证。
- 动画开启和关闭均可真实按压；休眠、唤醒、重建及重复导航期间的对象生命周期。
- 额度过期、未知状态和演示标识；Wi-Fi/BLE 模拟失败；硬件缺失；录音离页停止；中英文字形覆盖。

`click` / `set` 诊断命令直接触发控件，不证明触摸命中或手势可用。`pointer` / `swipe` 注入经过 LVGL 输入事件处理，但仍是软件模拟，不代表 CST9217 实际采样或人手操作已经验收。

## 图像、材质与资源边界

新增预渲染资源为一张 466×466 RGB565 壁纸和六应用的四档 RGB565A8 图标，共 934,640 字节（912.7 KiB）原始像素数据。该数字来自资产生成脚本统计，**不是最终固件大小、运行时堆占用或性能测试结果**。

蓝色玻璃使用透明渐变和局部细高光，没有运行时背景模糊与实时折射。模拟器和 HTML 分别使用不同渲染环境；HTML 的浏览器模糊、高光、纵向图标对比演示不能作为固件功能或运行性能的证据。

## 新版真机尚待验证

- 固件烧录及重启；新版字体、图片解码、蓝色层次和实际触控命中。
- 圆弧起手范围、不同速度与方向的手势、取消阈值，以及滑块/键盘/列表冲突。
- 首页、应用列表、最近任务、设置及弹窗的连续帧率、P95 帧间隔、内存、TE 超时与 DMA 错误。
- 新版提醒音、录音回放、息屏唤醒和现场恢复；NVS 设置保持与 RTC 冷断电保持。
- 实际 Wi-Fi 登录与 SNTP、指定 BLE 外设、SD 卡、电池和按键；模拟成功或失败场景不替代这些硬件链路。

原项目最终同步版本约 **29.35 FPS（桌面滑动）/ 29.42 FPS（设置列表）** 是历史测量，仅用于建立后续对比背景。当前新增材质、资源和布局必须重新测量，不能据此宣称 Blue 2.0 达到约 29.4 FPS、持续 30 FPS 或已消除新版撕裂。
