# Blue 2.6.1 轮播跟手与电池误亮修正

日期：2026-10-11。工程 `/Users/chengpeng/Desktop/work/ESPUI`，Waveshare ESP32-S3-Touch-AMOLED-1.75，466×466，ESP-IDF 5.5.5 / LVGL 9.4.0 / BSP 3.0.1。固件已经刷入 `/dev/cu.usbmodem101`，保留 NVS，原工程保持原状。此前记录见 [Blue 2.6.0](docs/VERIFICATION-BLUE-2.6.0.md)。

## 触发与修正

用户补充应用列表左右拖动不跟手，以及**电池供电、无触碰、无到点提醒时屏幕自行亮起**。上一版 USB 观察不能作为该电池路径的验收。

轮播原先启用 LVGL SCROLL_ONE，拖动会被限制在相邻项；HTML 示例还把拖动截在 ±110 px 并固定只换一项。现改为项目内直接处理位移：横向间距 160 px，6 px 起拖并锁定方向，以按下时滚动位置减去手指总位移定位，范围 0–800。支持长拖和反向回拉；按住期间不执行惯性或吸附，松手后 180 ms 吸附最近项。按下可立即中断正在进行的吸附；拖动不打开图标，中央上滑回首页保留。

本轮找到三条需修正的待机路径，不将源码推断冒充现场复现：

- 组件只检查尾标记与按下事件，异常触点数量被截为一个，覆盖手势、触点 ID 和坐标未完整过滤。板级读取现统一解析原始 D000 报告并 ACK，拒绝已消费、覆盖、异常数量/ID/事件/坐标，异常包清空缓存；运行与待机采用同一解码。取消固定 2 ms 读取等待和失败后触摸硬件复位，避免额外延迟或伪输入。
- GPIO11 原来按瞬时电平选择相反唤醒电平，没有显式待机上拉。现固定有效低电平唤醒，运行和浅睡眠均启用输入上拉；电源任务收到中断后，需要两份 ACK 分隔的新鲜稳定按下报告，至少间隔 12 ms、72 ms 窗口内、两轴位移分别 ≤48 px。最多六次读取；单份噪声不亮屏。仅校验期间临时持有禁止浅睡眠锁，待机无触摸轮询。
- 拔 USB 后后台无线暂停失败，原代码明确发送 ScreenWake(Recovery)。现保持黑屏并在 10 秒服务周期重试；原启用状态和连接信息只捕获一次，失败重试不覆盖恢复状态。进入休眠前的准备失败仍恢复运行并反馈原因。

GPIO 电平唤醒依据 [ESP-IDF Sleep Modes](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/sleep_modes.html)核对；报告过滤与 ACK 依据本地及[上游 SensorLib CST92xx 实现](https://github.com/lewisxhe/SensorLib/blob/master/src/touch/TouchDrvCST92xx.cpp)核对。本项目没有修改 managed_components、无线凭据或未知电源轨。

## 诊断

新增只读 `@UI power history`，在运行和待机均不亮屏、不延期熄屏。RAM 保存最近 16 条进入待机、恢复、触点确认和拒绝记录：序号、单调时间、USB/电池来源、WakeReason、首末原始报告与解码种类。重启后清空。可在拔线复现后接回电脑读取；必要时先触摸恢复 USB 通信，并注明这次触摸。

`host/wake_diagnostics.py --out DIR` 保存原始串口和 JSON，不发唤醒命令。该历史说明固件为何恢复，不能独自判断是否有人触摸。USB 内核保护不被绕过。

## 已完成验证

| 检查 | 结果 |
| --- | --- |
| 编译与刷入 | ESP-IDF 成功；bootloader、分区表和应用三段哈希校验通过；[build.log](logs/blue-2.6.1/build.log)、[flash.log](logs/blue-2.6.1/flash.log) |
| 原生模拟器 | 1/1 CTest，1.88 秒；[详情](logs/blue-2.6.1/simulator-details.log)。真实 LVGL 指针验证 200 px 长拖、80 px 回拉、按住无吸附、松手停靠、不误开应用、上滑返回 |
| 报告与确认逻辑 | 有效坐标、旧 ACK、覆盖、异常数量/ID/坐标、释放、单份按下、过早/过期报告和稳定双报告检查通过 |
| 既有逻辑 | 30,681 次字形、100 次导航对象 22→22、100 次模拟器指针唤醒与 6 组截止时间检查通过；不代表真人硬件循环 |
| USB 自动熄屏 | 15 秒自动熄屏；100 次空触点通知全拒绝，37.839 秒待机观察无恢复，frame/TE/IMU 增量为 0；[idle-regression.json](logs/blue-2.6.1/idle/idle-regression.json) |
| 只读历史 | 查询后仍 state=2、cycles=2、failures=0，待机计数继续增长，frame/TE/IMU 增量为 0；[wake-diagnostics.json](logs/blue-2.6.1/usb-diagnostics/wake-diagnostics.json) |
| HTML | 最终 42 项专项检查及 18 项交互/窄屏/打印回归通过；[专项](logs/blue-2.6.1/spec-interactions.json)、[最终文件哈希](logs/blue-2.6.1/spec-final-check.json) |

80 秒只读 [USB 监测](logs/blue-2.6.1/touch/physical-touch-monitor.json)没有触点或恢复事件，末次已待机 92.494 秒；因此该窗口没有验证新双报告规则下的真实触摸成功率。100 次空通知是故障注入，不是 100 次真人触摸。

再次只读查询时，当前 USB 待机已持续 406.365 秒，cycles 仍为 2、touch_rejected 仍为 100，没有新增恢复或拒绝记录，frame/TE/IMU 增量仍为 0；[最新诊断](logs/blue-2.6.1/latest-diagnostics/wake-diagnostics.json)。该结果仍限于 USB，不能代替电池验证。

刷机前的 [2.6.0 日志](logs/blue-2.6.1/before/physical-touch-monitor.log)包含一份触点报告与 Touch 恢复记录，但日志在重新连接时可能包含旧缓冲，用户意图未知，且浅睡眠累计为 0；不能将它归为已复现的电池误亮原因。

## 待验证边界

用户已被邀请确认新版本触摸唤醒和长拖手感，并拔线至少 60 秒观察，再接回读取历史。当前尚未收到这轮现场结果，电池误亮是否彻底消除不能报告为已通过。未完成纯电池 Light-sleep 驻留、电池电流、物理触摸至画面延迟、独立撕裂测量、100 次真人唤醒及无线暂停失败的设备故障注入。非常短的触碰可能被连续报告规则过滤，需要真实设备确认成功率。

此前 2.6.0 的三次中位完成帧率为轮播 48.61 FPS、设置列表 58.46 FPS，本轮不将这些历史数值标为 2.6.1 新实测，也不以帧率推断跟手延迟。保留 80 MHz QSPI、TE 同步、局部双缓冲和睡眠传输屏障，未取消 USB 禁止浅睡眠保护。

## 交付

[固件 2.6.1](logs/blue-2.6.1/esp32_assistant_ui-2.6.1.bin)：5,681,632 字节，SHA256 `5e079b248978c3eb368e0c4de2ee1a60c2dc3905884e4b52cc522067524dddab`。

[离线 HTML 规范](圆屏助理-UI设计规范.html)：213,473 字节，SHA256 `faa645f97c9af8418b57a77611c0070eb91bfe075141bbd2a99a5adbb8775caa`。全部样机仍使用 DOM/CSS/内联 SVG，没有截图、Canvas 或远程资源。轮播定位和图标独立缩放、页面规则、电源规则及历史性能标签已同步；日志 PNG/PDF 只用于检查。[构建清单](logs/blue-2.6.1/build-manifest.json)保留源码与产物哈希。

## 后续现场反馈（2026-10-11）

用户回复真实触摸唤醒和长拖测试「可以了」，拔线电池待机则「还是会自动亮起了」，并补充「主要还是没充电的时候会亮」。不能把本版 USB 待机结果当作电池误亮通过。
