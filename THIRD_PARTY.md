# 来源与移植说明

| 来源 | 固定版本 / 许可证 | 本工程使用方式 |
| --- | --- | --- |
| [M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo) | `6b4aa125288b6fe9dca661f10159f6e1e5ee785c` / MIT | 横向圆屏启动器、分页指示器算法、应用组织方式及 5 个图标 |
| [Mooncake](https://github.com/Forairaaaaa/mooncake) | 2.3.3 / MIT | 应用注册、打开、后台、销毁 |
| [Smooth UI Toolkit](https://github.com/Forairaaaaa/smooth_ui_toolkit) | 2.12.1 / MIT | 页面动画缓动，固定源码保留 |
| [LVGL](https://github.com/lvgl/lvgl) | 9.4.0 / MIT | 共用组件、布局、输入、绘制 |
| [ESP-IDF](https://github.com/espressif/esp-idf) | 5.5.5 / Apache-2.0 | Wi-Fi、NimBLE、NVS、FreeRTOS、I²C、USB 等 |
| [Waveshare BSP](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75/versions/3.0.1) | 3.0.1 / Apache-2.0 | 显示、触摸、共享 I²C、SD、IO 扩展器 |
| [微雪官方硬件工程](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75) | `e4344e70c2fa78a13e8a06566507f1ba8af6672a` / MIT；BSP 文件 Apache-2.0 | 引脚参考，音频 STD TX + 4-slot TDM RX 配置移植 |
| [XPowersLib](https://github.com/lewisxhe/XPowersLib) | 锁定提交见 upstream-lock / MIT | AXP2101 状态、测量功能 |
| [SensorLib](https://github.com/lewisxhe/SensorLib) | 锁定提交见 upstream-lock / MIT | QMI8658 读数；固定版本兼容驱动 |
| [Noto Sans CJK](https://github.com/notofonts/noto-cjk) | Sans2.004 / SIL OFL 1.1 | 中文子集字体，保留 OFL |
| [lv_font_conv](https://github.com/lvgl/lv_font_conv) | 1.5.3 / MIT | 可重复生成字形 |
| [SDL2](https://github.com/libsdl-org/SDL) | 本机 Homebrew SDL2 / zlib | Mac 窗口与鼠标输入 |

许可证原文保留于对应 vendor/components/managed_components 目录。移植文件 `ui/launcher_indicator.h`、`assets/icons/*.c`、`platform/board_audio.c` 标记来源。上游 Git 提交不会随构建自动更新。

LVGL 9.5 相关视图调用没有直接带入固件：圆屏视图使用 9.4 API；按钮动画用 9.4 样式过渡；页面动画使用固定 STK 的 ease_out_cubic。没有修改现有 Demo 或全局工具链。

## 板级配置

I²C 总线复用 `bsp_i2c_get_handle()`，SCL GPIO14 / SDA GPIO15。AXP2101 `0x34`、PCF85063 `0x51`、QMI8658 `0x6B`、CST9217 `0x5A`、TCA9554 `0x20`、ES8311 `0x18`、ES7210 `0x40`。只开启 PMU 电池/电压测量，不改变电源轨或充电电压。

音频 MCLK42 / BCLK9 / WS45 / TX8 / RX10，PA46。24 kHz / 16 bit，ES8311 双声道播放，ES7210 4 时隙 TDM 采集，从 MIC1 所在 slot0 提取单声道。此配置来自微雪维护的官方 BSP，独立适配保留原 3.0.1 显示驱动。不得同时调用原 BSP 的 `bsp_audio_init()`，否则会重复初始化 I²S。

`platform/board_display.c` 保留 BSP 的 CO5300 初始化指令、引脚及 6 px 列偏移，复用 CST9217 触摸和 LVGL Adapter 线程/锁。工程自行实现整帧 PSRAM 双缓冲、GPIO13 TE 边沿同步、独立传输任务和两个 20 行内部 DMA 缓冲，避免不同帧混合以及 SPI 为整帧临时分配内部内存。GPIO13 的定义已核对微雪 `HARDWARE_REFERENCE_ZH.md`；80 MHz QSPI + DMA 分块的可行性也参考了 [PrintSphere 的同板型实现](https://github.com/cptkirki/PrintSphere/blob/main/components/esp32_s3_touch_amoled_1_75/esp32_s3_touch_amoled_1_75.c)。本工程的队列和传输完成同步逻辑为独立实现，没有引入该项目依赖。托管组件未修改。亮度命令由传输互斥锁保护，首次绘图在首屏创建后启动。

PCF85063 存 UTC，UI 通过设置中的时区换算。首次 RTC 的 oscillator-stop 标志置位时不采用日期，等待手动设置或 SNTP。读取按键只配置 BOOT GPIO0 与 TCA P4 输入。SD 为 GPIO1/2/3 的 1-bit SDMMC，挂载失败不格式化卡。
