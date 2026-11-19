# ESPUI 本地工具

工程源代码、字体和 LVGL 依赖位于当前目录。ESP-IDF 安装体积较大，不复制到工程。

`ESPUI_TOOLCHAIN_ROOT` 应指向同时包含 `esp-idf/`、`espressif/` 和 `cache/` 的目录。若未设置，`toolchain.sh` 按顺序检查工程内 `.toolchain`、工程上级 `.toolchain`，最后回退到此机器原项目的 `/Users/chengpeng/Documents/Codex/2026-10-03/es/.toolchain`。脚本会打印实际使用的目录；这是复用工具链，不是复用旧工程代码。显式传入无效路径时直接报错，不会悄悄回退。换机器后请设置变量；需要 ESP-IDF 5.5、对应 Python 环境、CMake 3.30.2、Ninja 1.12.1 及 SDL2（默认 `/opt/homebrew/opt/sdl2`，可用 `SDL2_ROOT` 覆盖）。

```sh
export ESPUI_TOOLCHAIN_ROOT=/path/to/.toolchain
./scripts/simulator.sh build
./scripts/simulator.sh test
./scripts/idf.sh build
```

## 不连接设备或账户的视觉检查

```sh
./scripts/simulator.sh capture --capture-dir .runtime/captures
./scripts/simulator.sh capture --capture-dir .runtime/captures-en --locale en-US
./scripts/simulator.sh capture --input-script /tmp/ui-trajectories.txt
```

`capture` 直接运行 C++ 模拟器，默认 SDL dummy 视频驱动，无窗口、无需硬件、无需网络。使用固定时间和本地额度示例，不读取已保存设置，也不会启动额度桥接。输出 466×466 BMP；批量模式覆盖所有页面。`--scenario offline|connect-failed|low-battery|no-card|hardware-error` 可覆盖硬件异常状态。

轨迹脚本每行一条命令，`#` 开头为注释：

```text
@UI page 0
@UI pointer 233 446 down
@UI pointer 233 380 move
@UI pointer 233 330 up
@UI wait 350
@UI state
@UI capture /tmp/gesture-home.bmp
@UI swipe 233 22 233 130 300
@UI assert-page 2
```

`pointer` 和 `swipe` 通过 LVGL 的真实 `lv_indev` 读取回调送入触摸坐标，包含按下、移动、松开与计时，适合测手势命中、冲突与取消。`click`/`set` 为原诊断控制指令，不作为触摸命中的证据。`assert-page` 失败返回非零退出码。屏幕逻辑坐标均为 466×466。

`direct` 同样绕过 Python 额度桥接，但可打开 SDL 窗口并从标准输入接收命令。`run --mock` 使用明确测试数据；`run` 保留原工程的实时额度模式，只有用户要求时才启动。

## 字体

```sh
python3 scripts/generate_fonts.py --offline
python3 scripts/generate_fonts.py --offline --only font_digits_64
```

`font_digits_64` 只包含 `0123456789:`，用于大号时间数字。其余 UI 字体根据 `core/strings.def` 生成：18／20／24／28 px Regular、22／28 px Bold，以及包含 GB2312 动态文本的 22 px Regular。Bold 来自官方 Noto Sans CJK 2.004 的真实字重；不通过运行时描边伪造。源文件和 OFL 许可位于 `vendor/fonts/`，`source.json` 锁定源 URL、SHA-256 与转换器版本；`manifest.json` 记录生成字库的尺寸、字重和字形数量。

`--offline` 要求字体和 npm 包缓存已存在；缓存遵循 `ESPUI_TOOLCHAIN_ROOT`，未设置时按上述三个位置查找。首次安装 converter 时执行锁文件约束的 `npm ci`，不执行包安装脚本。字体输出已随工程提供，普通构建不需要 Node/npm 或重新生成字体。

### Blue 2.3 设备诊断

USB 串口支持 `@UI wake`、`@UI exit`、`@UI back`，分别唤醒、执行当前应用退出和一级返回。动作命令视为用户活动；只读 `@UI state` 不延迟息屏。命令与真实触摸共用 Shell 业务逻辑，但不经过触控芯片，因此不能替代真机手势成功率测试。
