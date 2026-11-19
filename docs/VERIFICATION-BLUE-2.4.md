# Blue 2.4 熄屏电源管理验证

日期：2026-10-10。工程：`/Users/chengpeng/Desktop/work/ESPUI`。设备为 Waveshare ESP32-S3-Touch-AMOLED-1.75，466×466，ESP32-S3 revision 0.2、16 MB Flash、8 MB PSRAM。当前串口 `/dev/cu.usbmodem1101`。

## 实现与交付

- 默认开启「设置 → 电源与电池 → 熄屏省电」，增加立即熄屏、当前电源模式和最近唤醒原因。旧配置缺少该字段按开启处理，设置版本保持 1。
- 独立 PowerManager 任务交接 System 所有权，暂停 LVGL worker/tick，关闭 TE，等待显示传输结束，执行 CO5300 标准 Sleep In/Out。恢复时先重绘再恢复亮度，不重新创建页面对象。
- 停止 IMU 采样；音频空闲时关闭 Codec 和 I2S 通道、任务等待通知。电池待机暂停 Wi-Fi/BLE，唤醒异步恢复原启用状态。录音、播放、响铃阻止待机。
- 一次性截止时间调度倒计时和闹钟，校时后重新安排；USB 收包和服务队列改为事件唤醒。
- 启用 DFS 40–240 MHz、Tickless Idle 和自动 Light-sleep。保留 SDK USB 保护，并增加主机连接保持锁，确认拔线后才释放。没有调用强制睡眠绕过 USB 保护。
- 首次触摸只唤醒，等待松手。GPIO11 唤醒使用低电平，首次中断立即屏蔽重复触发，恢复时重新配置下降沿。
- HTML 规范同步电源页面与独立策略说明；示意保持纯 DOM/CSS/SVG，不使用截图。

实现与诊断接口见 [POWER-MANAGEMENT.md](docs/POWER-MANAGEMENT.md)。原工程没有修改；本工程无 Git 仓库，未执行提交或推送。修改前源文件备份在 `.runtime/power-2.4-backup/sources.tgz`。

## 本地验证

| 项目 | 结果 / 范围 |
| --- | --- |
| ESP-IDF 构建 | 5.5.5 构建成功；见 [build.log](logs/power-2.4/build.log) |
| 模拟器 CTest | 1/1 通过；100 次真实 LVGL 指针熄屏/唤醒、失败恢复、录音阻止休眠、草稿保留、兼容模式、无线开关恢复 |
| 计时与闹钟逻辑 | 无 UI tick 的倒计时/闹钟到期、重复提醒防护、校时重排、旧配置兼容通过 |
| 字体与对象 | 30,681 次字形检查；100 次导航前后对象均为 25；见 [simulator-test.log](logs/power-2.4/simulator-test.log) |
| HTML 规范 | 本地 file URL 打开；1440 / 390 / 320 px 无横向溢出；默认开关、键盘唤醒和设置保留通过；无外部请求和脚本错误 |
| HTML 原有交互 | final-check 覆盖目录/键盘/窄屏/打印回归；新电源检查见 [html-power-check.json](logs/power-2.4/html-power-check.json) |

模拟器不模拟真实 CPU 睡眠、电池电流或 CST9217 硬件触摸。

## 真机验证

最终固件已刷入。引导程序、分区表、应用三段均经 esptool 校验；没有擦除 NVS。见 [flash.log](logs/power-2.4/flash.log)。固件 5,674,592 字节，占 8 MiB 应用分区约 67.65%。

[100 次 USB 真机循环](logs/power-2.4/device-regression.json)通过：恢复耗时 183–188 ms，P95 187 ms，休眠失败 0 次；每次待机的 frame / TE / IMU 增量均为 0。只读状态查询重试 2 次。循环后等待完整一分钟倒计时，到点唤醒并显示提醒，停止提醒后仍停留在原日期编辑表单。测试日期未保存到 RTC；测试计时已清理。

[补充设备检查](logs/power-2.4/device-followup.json)通过：关闭省电使用兼容 DisplayOff；录音和播放均阻止待机；熄屏时接收明确标为 TEST 的额度帧并返回 ACK，屏幕保持熄灭。测试帧仅在 RAM，随后复位清理。

USB 待机约 12.75 秒采样区间内：Core 0 活跃比例约 0.184%，Core 1 约 0.001%；frame / TE / IMU 增量均为 0，浅睡眠累计为 0（USB 锁生效）。包含诊断采样开销，只是该短区间的任务运行统计，不代表整机功耗。

倒计时启动日志到恢复完成为 60.374 秒，扣除一分钟倒计时约 374 ms；恢复流程自身记录 373 ms，满足该次 ≤1 秒的提醒目标。这不是触摸延迟测量。

真实触摸待用户现场确认：最终固件复位后已进入电源页并熄屏，日志见 [physical-touch.log](logs/power-2.4/physical-touch.log)。截至本次记录尚未收到触摸唤醒事件。纯电池浅睡眠、Wi-Fi/BLE 停止/恢复及 100 次真人触摸循环尚未完成，不能标为通过。

## 证据边界

- USB 指令触发的 100 次真机唤醒不等于 100 次真人触摸；真实触摸次数单独登记。
- 串口日志可能因 IDF SOF 连接判定丢弃部分输出，测试只对只读查询重试，记录次数，不重复发送修改操作。
- 不以 USB 接电脑时的浅睡眠数据代表纯电池行为；也不把任务活跃比例推算成电池电流。
- 没有电池侧电流测量设备，不给出省电百分比或续航小时数。
- 已连接 AP/BLE 设备的重连、BLE 重连失败及所有长时间电池场景，需单独实测；不得以模拟器开关恢复代替配对/联网证据。
- 触摸至可见 ≤300 ms、到点提醒延迟 ≤1 s 是目标。串口记录的重绘完成耗时不替代光学测量。
- 原项目约 29.4 FPS 是历史数据。本轮未重测 UI 场景帧率，历史 UI 验证见 [Blue 2.3](docs/VERIFICATION-BLUE-2.3.md)。

最终二进制 SHA-256：

```text
8e6d5e7015312e63f5cafece3f836d5d8263906903f4e9d787104871dd9441c4
```

完整哈希清单见 [build-manifest.json](logs/power-2.4/build-manifest.json)。
