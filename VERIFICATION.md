# Blue 2.6.2 电池待机误亮修正与诊断

日期：2026-10-11。工程：`/Users/chengpeng/Desktop/work/ESPUI`。固件已编译并刷入 `/dev/cu.usbmodem101`，未清除 NVS，原工程保持原状。

## 问题与确认的缺口

用户确认 Blue 2.6.1 的触摸唤醒和轮播长拖「可以了」，但拔线电池待机「还是会自动亮起了」，主要发生在未充电时。保留已接受的轮播逻辑；本轮继续处理电池浅睡眠。

接回设备后运行时间为 55.807 秒，RAM 唤醒历史只有一条初始待机，说明旧历史已因重启丢失；当时没有得到是否手动开关机的回复。首次只读 [诊断](logs/blue-2.6.2/before/wake-diagnostics.json)不能定位上一次亮屏来源。JTAG 读到 `RTC_CNTL_RESET_STATE_REG=0x0000f30c`，CPU0 原因低 6 位为 12（软件 CPU 复位），不能单凭该值认定发生了 panic；2.6.1 尚未保留完整的复位原因提示。

通过 JTAG 核对 2.6.1 实际对象：`power` 和 `touch_wake_context` 都为 **0x3c549cb0（PSRAM）**，见 [内存实读](logs/blue-2.6.2/before/jtag-power-context.log)。PowerManager 大于默认 512 字节内部 RAM 分配阈值，默认 new 将对象送到 PSRAM；触摸 IRAM ISR 却访问其中的通知目标、原子变量。电池浅睡眠的 PSRAM 半休眠使该访问存在缓存不可用风险。USB 的禁止浅睡眠锁会绕过该状态，符合供电差异，但具体现场复位原因仍需新诊断确认。

此外，IDF 启动时启用所有 GPIO 的休眠隔离，而 GPIO39 LCD 复位、GPIO40 触摸复位、GPIO12 LCD 片选此前未保留输出。已确认配置缺口，尚无示波器波形证明复位脚在现场产生过脉冲。依据本地 IDF 5.5.5 `sleep_gpio.c` / Kconfig，并核对 [GPIO API](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/peripherals/gpio.html)。

## 实现

- PowerManager 使用明确内部 RAM 分配和 placement new，启动检查 `esp_ptr_internal`。IRAM ISR 函数和浅睡眠统计回调位于 0x4037…，RTC 记录位于 0x50000000；不关闭正常 Light-sleep 或 USB 保护。
- 屏幕 / 触摸复位和 LCD 片选通过 `gpio_sleep_sel_dis` 保持已有输出；Codec 初始化后也保留功放控制脚的输出，空闲关闭。GPIO11 保持输入上拉、有效低电平唤醒。未修改未知电源轨、充电参数或存储引脚。
- 休眠准备阶段的一份按下报告不再直接恢复，统一参与双报告验证。原先两份新鲜报告、12 ms 最小间隔、72 ms 窗口、首次触摸只唤醒规则保留。
- 新增只读 `@UI power boot`：显示完整复位原因、内部 RAM 检查、GPIO 配置及上次运行的电源阶段 / 来源 / 原因 / 触点类别 / 睡眠回调计数。44 字节 RTC 记录使用魔数和校验值，在支持保留的暖复位后读取，冷上电或校验无效则丢弃，不写 NVS。复位枚举见 [系统 API](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/misc_system_api.html)。
- 既有 RAM 原始报告历史仍保留最近 16 条，重启清空；`host/wake_diagnostics.py` 同时读取历史与复位证据。入口回调尝试数可能包含被 IDF 拒绝的睡眠，不能当作成功驻留。

## 已完成验证

| 检查 | 当前结果 |
| --- | --- |
| 编译与烧录 | [build.log](logs/blue-2.6.2/build.log)、[flash.log](logs/blue-2.6.2/flash.log)成功；三个写入段哈希通过，未擦除 NVS |
| 原生测试 | CTest 1/1，1.72 秒；[详情](logs/blue-2.6.2/simulator-details.log)。覆盖真实 LVGL 指针 200 px 长拖、80 px 回拉、按住保持、松手吸附、手势、报告过滤、配置、截止时间和现场恢复 |
| 真机内存 / GPIO | [只读诊断](logs/blue-2.6.2/usb-diagnostics/wake-diagnostics.json)：context_internal=1，1376 字节；GPIO12/39/40 的 SleepSelEn=0，GPIO11 输入上拉有效 |
| USB 熄屏与空触点 | [idle-regression.json](logs/blue-2.6.2/idle/idle-regression.json)：15 秒自动熄屏，100 次空通知全部拒绝，37.913 秒待机无恢复，frame / TE / IMU 增量均为 0 |
| RTC 暖复位证据 | [USB 受控暖复位诊断](logs/blue-2.6.2/rtc-test-usb/wake-diagnostics.json)：reset=11（USB），boot=2，rtc_previous=1，prior_phase=2，prior_source=usb；运行恢复正常。这是人为测试，不是电池误亮 |
| 离线 HTML | [最终检查](logs/blue-2.6.2/spec-final-check.json)：18 项键盘、覆盖层、窄屏和打印回归通过，无脚本错误。页面与轮播 JS 未改动，专项交互 42 项保留 2.6.1 历史结果，不冒称重测 |

受控 `JTAG reset run` 尝试未恢复 USB 响应，工具日志出现 Cache error，见 [日志](logs/blue-2.6.2/rtc-reset-test.log)。随后使用 [esptool run 原生 USB 复位](logs/blue-2.6.2/rtc-usb-reset.log)恢复现有固件并读取到有效 RTC 前序待机记录；未再次写固件或改 NVS。该调试复位失败与电池浅睡眠必须分开记录。

最后一次 [只读诊断](logs/blue-2.6.2/latest-diagnostics/wake-diagnostics.json)仍为 USB 待机，持续 235.524 秒，cycles=1、failures=0、帧 / TE / IMU 增量均为 0，没有新增触点或恢复；Light-sleep 次数为 0。这个窗口没有取得新版本物理触摸或电池结果。

## 待确认边界

已邀请用户在 2.6.2 轻触确认后拔线静置至少 90 秒，再接回读取历史，当前结果待反馈。**尚不能声称电池误亮已经消除。** USB 测试不覆盖电池真实 Light-sleep、中断时 PSRAM 可用性或复位脚波形。未测量电池电流、物理触摸到画面延迟、100 次真人唤醒、纯电池长期驻留与独立撕裂。

本轮不改轮播绘制、80 MHz QSPI、TE 同步、局部双缓冲和 USB 保护。2.6.0 三次中位完成帧率（轮播 48.61 FPS / 设置 58.46 FPS）仍为历史结果，本轮没有新 FPS 测量。2.6.1 的用户「可以了」是手感与触摸功能反馈，不是光学延迟或触摸成功率统计。

## 交付

[固件 2.6.2](logs/blue-2.6.2/esp32_assistant_ui-2.6.2.bin)：5,686,064 字节，SHA256 `9e4041eff9a6915852d7deec439fab8282e72d2c464610f9fc1ffebce5e00d1c`。

[纯 HTML 规范](圆屏助理-UI设计规范.html)：213,732 字节，SHA256 `ba537a2e9e4f3affccc306d5f0ddb53e26dbc5bcd1f21e5167120376f0e68d5c`。同步电池休眠内存、控制脚和诊断规则；样机仍为 DOM/CSS/内联 SVG，没有截图或远程资源。源码及产物校验见 [构建清单](logs/blue-2.6.2/build-manifest.json)。
