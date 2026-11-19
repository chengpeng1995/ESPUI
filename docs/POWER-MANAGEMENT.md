# Blue 2.6.2 熄屏电源管理

## 使用

设置 → 电源与电池：默认开启「熄屏省电」，可点击「立即熄屏」。自动熄屏仍使用显示设置中的 15 / 30 / 60 秒 / 永不。录音、播放、响铃时拒绝休眠。轻触屏幕唤醒，首次完整触摸不触发页面操作；现场和草稿保留。

「熄屏省电」关闭时仅关屏，保留兼容模式。PWR 保持硬件电源用途，不增加按键轮询唤醒。本功能不包含断电开机闹钟、常亮屏或抬腕唤醒。

## 状态与所有权

`PowerManager` 独立任务负责 Awake → Preparing → Standby → Waking；DisplayOff 为兼容模式。UI 只提交请求。暂停 LVGL 获得确认之后，电源任务才可以执行 `System::tick()`；恢复界面前交回所有权。ISR 与 esp_timer 回调只发送任务通知。

System 增加 `next_deadline_ms()`，返回倒计时与最近启用闹钟的单调时间截止点。到期事件处理后自动亮屏；没有任务时不周期轮询计时器。校时事件会重排下一次截止点，同一闹钟分钟不会重复提醒。计时本身始终按时间差计算，不累计 UI 帧数。

## 硬件行为

| 模块 | 省电待机 | 恢复 |
| --- | --- | --- |
| LVGL | 暂停 worker 和 tick，不运行动画和页面更新 | 保留对象，在亮度为零时完成一帧再亮屏 |
| CO5300 | 停止 TE 中断，Display Off + Sleep In，等待 120 ms | Sleep Out 后等待 120 ms，恢复 TE、Display On |
| ESP32-S3 | 释放界面 CPU 最大频率/禁止睡眠锁；DFS 40–240 MHz、Tickless 自动浅睡眠 | 先恢复界面性能锁 |
| CST9217 | 保持供电，GPIO11 电平唤醒后读取有效触点 | 消费首个触摸，直到释放 |
| QMI8658 | 关闭加速度计与陀螺仪，不读取按键 | 恢复采样；立即更新一次，恢复原轮询节奏 |
| 音频 | 空闲任务等待通知，关闭 Codec/功放及 RX/TX I2S 通道 | 使用时重新打开通道和 Codec |
| Wi-Fi / BLE | 电池供电时暂停；BLE 完整停止 host/controller | 保留用户开关、凭据；Wi-Fi 原策略重连，BLE 对原连接尝试一次 |
| 电量 | 10 秒更新一次 | 立即刷新 |
| USB | 首字节无限等待，再无阻塞读取已有字节；无 100 ms 空轮询 | 短指令与额度帧立即处理 |

CO5300 使用板级标准 Sleep In/Out，不进入现有组件会触发完整初始化的 Deep Standby；不修改 managed_components，也不修改未知 PMU 电源轨或充电参数。

Blue 2.6 改为局部双缓冲刷新后，电源任务在 LVGL 暂停确认之后等待全部排队的显示区域完成传输，再关闭 TE 和发送 Sleep In。唤醒时先恢复屏幕、全屏重绘并等待传输队列清空，再恢复亮度；等待超时记录失败并走恢复路径。该屏障覆盖多区域刷新，不能只看最后一次 DMA 是否结束。8 ms 的触摸读取请求只在运行期间生效，待机保持中断唤醒，不增加触摸轮询。

接电脑时保留 ESP-IDF USB Serial/JTAG 保护，并增加 `usb_host_guard`：检测到主机后保持禁止浅睡眠，直到 PMU 确认 USB 供电断开，避免主机短暂停止 SOF 帧导致通信中断。UI 和外设仍可停用。普通充电器没有主机时，按实际电源锁允许 CPU 浅睡眠；无线仅在电池供电时暂停。待机中拔掉 USB，最迟在下一次 10 秒电源检测时暂停无线。插入电脑不作为直接唤醒源，可触摸亮屏后通信。

## 触点确认与残留中断

Blue 2.5.1 在正常触摸读取和待机触点校验两条路径统一补发 CST9217 报告 ACK（写入 D0 00 AB）。先读取、确认报告已消费，再使用已缓存的坐标，避免把未确认的旧按下报告再次当成触摸。此前 100 次空触点通知通过，未覆盖“旧有效触点残留”这一情况，用户反馈后继续修复。

本地实现依据 `vendor/SensorLib/src/touch/TouchDrvCST92xx.cpp` 的 `getTouchPoints()` 读取握手，并核对[上游 SensorLib 实现](https://github.com/lewisxhe/SensorLib/blob/master/src/touch/TouchDrvCST92xx.cpp)。`TOUCH reads / acks / ack_errors` 记录实际确认写入结果；失败时不能作为有效唤醒。

GPIO11 中断只提交待验证事件，不直接亮屏。Blue 2.6.1 在项目板级封装中读取 D000 并 ACK；完整校验新鲜标记、覆盖手势、触点数量、ID、事件和坐标，清除异常报告缓存，不采用组件的触点数量截断或错误后硬件复位路径。读取不再固定等待 2 ms，保持坐标变换接口。

LVGL 暂停后，电源任务独占触摸 I/O。需要两份 ACK 分隔的新鲜按下报告，至少间隔 12 ms、在 72 ms 窗口内且坐标各轴相差不超过 48 px；最多读取六次。单份按下、覆盖、松手、越界或已消费数据不亮屏。校验期间临时持有禁止浅睡眠锁，结束即释放；正常待机没有触摸轮询。非常短的触碰可能无法确认，应以真实设备检查触摸成功率和耗时。

GPIO11 始终按 CST9217 的有效低电平配置，运行和浅睡眠均启用输入上拉；不再根据瞬时 IRQ 电平改为高电平唤醒。首次触摸仍只唤醒，释放后可操作。到期提醒和明确主机动作独立处理。

拔 USB 后若后台无线暂停失败，屏幕保持黑屏，在下一次 10 秒服务周期重试；恢复所需的原开关和连接信息只捕获一次，不被重试期间的关闭状态覆盖。准备休眠阶段的硬件失败仍恢复运行并提示原因，不能报告成功省电。

## 电池浅睡眠的中断与 GPIO 安全（2.6.2）

2.6.1 的大对象使用默认 `new`，在本工程的 `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=512` 下落入 PSRAM。通过真机 JTAG 实读，`power` 和 `touch_wake_context` 都为 `0x3c549cb0`。代码即使标记 IRAM，中断访问的对象也必须能在 PSRAM 半休眠期间访问。2.6.2 使用 `heap_caps_malloc(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` 加 placement new，并在启动时检查 `esp_ptr_internal`；不关闭正常自动 Light-sleep 或 USB 保护来规避问题。

IDF 5.5.5 的 GPIO 休眠隔离会关闭未特别处理的输出。GPIO39 屏幕复位、GPIO40 触摸复位、GPIO12 LCD 片选通过 `gpio_sleep_sel_dis` 保留现有运行输出；GPIO46 功放初始化后同样保留 Codec 管理的输出状态，空闲时为关闭。GPIO11 继续使用上拉输入和低电平唤醒。未修改电源轨、充电配置或 Flash/PSRAM 引脚。接口规则参见 [Espressif GPIO 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/peripherals/gpio.html)。

准备休眠时发现按下报告，也需要与后续新鲜报告共同确认，不能再仅凭一份按下就恢复；过滤窗口和首次触摸消费规则保持一致。

以上两个缺口为代码与内存实读确认，具体电池误亮是中断异常、GPIO 引发误报还是其他路径，仍需拔线回归和复位记录确认，不能用 USB 测试代替。

## 接口与兼容

- 设置 JSON 保持 version=1，新增 `sleep_power_save`，缺失默认 true；不清空 NVS。
- `DeviceState` 增加 power、wake_reason、usb_host、peripherals_suspended、imu_samples。
- `Op::ScreenSleep/ScreenWake` 提交 UI 电源请求；PowerSuspend/PowerResume 仅用于平台服务串行交接。
- `Platform::utc_millis()` 提供毫秒时钟，用于精确安排闹钟。模拟器实现相同逻辑状态，不模拟真实耗电。
- 准备休眠失败回到运行状态；部分屏幕命令失败仍执行 Sleep Out 恢复。驱动停止失败不会当成已成功进入省电。

## 诊断与证据

`POWER_BOOT` 增加 `esp_reset_reason()`、中断上下文内部内存检查及上一次运行的 RTC 记录。小记录不写 NVS，以魔数和校验值检查；在支持 RTC 保留的暖复位后可用，冷上电或无效校验时丢弃。记录电源阶段、USB / 电池来源、最近原因、触点类别、睡眠入口回调 / 返回回调次数。阶段 0 启动、1 准备、2 待机、3 恢复中、4 运行、5 兼容关屏；入口次数是回调尝试数，可能包含 IDF 拒绝睡眠，不能当作成功驻留。复位原因定义见 [Espressif 系统 API](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/misc_system_api.html)。

USB 开发命令：`@UI sleep`、`@UI wake`、`@UI power state`、`@UI power history`、`@UI power boot`、`@UI state`。只读状态命令不会亮屏或延期自动熄屏。其他开发指令会先恢复 UI；`@UI power test empty-touch` 是唯一不亮屏的故障注入指令，用于验证空触点不会唤醒。

`power history` 在运行和待机均为只读，不亮屏、不延期熄屏。保留最近 16 条 RAM 记录，断开电脑后再连接可读取；设备重启后清空。`PWR_TRACE` 包含序号、单调时间、电池/USB 来源、原因、首次与最后报告原始字节和报告种类。event=0 待机、1 恢复、2 触点确认、3 拒绝；reason 沿用 WakeReason 枚举。该记录只说明固件为何恢复，不能自动判定真实手指意图。

`POWER` 行输出待机次数、失败次数、恢复耗时、拒绝空触点次数 `touch_rejected`、最近唤醒原因、实际浅睡眠累计微秒数、进入次数，以及本次待机的 frame/TE/IMU 计数差。CPU 行输出每核 idle runtime 与时间基准；用两个采样的差值估计 CPU 活跃时间，不把频率变化当成 CPU 使用率。

`esp_pm_dump_locks` 列出剩余电源锁和各频率驻留时间。接电脑时应看到 USB 的禁止浅睡眠锁，此时浅睡眠计数为零是预期行为。断开电脑后的日志留在内存计数中，重新连接并查询即可读取。

运行 `host/power_regression.py --port PORT` 做 USB 真机循环和倒计时检查。它的唤醒来源是 USB 指令，不能称为物理触摸 100 次；模拟器独立验证 100 次 LVGL 指针唤醒。真实触摸、纯电池浅睡眠和电池侧电流须单独记录。

`host/idle_regression.py --port PORT --out logs/blue-2.6.2/idle` 等待当前自动熄屏时间，注入 100 次无触点唤醒通知并继续观察 20 秒，检查待机状态、帧 / TE / IMU 计数。它使用真实 CST9217 读取，但注入通知不是真人触摸，也不能证明所有物理噪声场景。

完整结果见 ../VERIFICATION.md。目标为稳定待机触摸后 ≤300 ms 可见、到点提醒 ≤1 s；未测量电池电流时，不提供省电百分比或续航小时数。
