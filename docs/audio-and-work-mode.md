# Volume and Work Mode

## Public API findings

The Volume page uses Core Audio's public C HAL interface so the app can keep its existing deployment targets: macOS 10.15.4 on Intel and macOS 11 on Apple silicon. Availability depends on the selected device as well as the OS. The newer Swift Core Audio wrappers are not required.

| Requested feature | Reliable public path | Limit |
| --- | --- | --- |
| System output volume and mute | Read and set the default output device's volume/mute properties after checking that they exist and are writable | Digital, HDMI, USB and other devices can expose fixed or external-only controls |
| Current output device | Enumerate available HAL devices and read/set the default output device | Apps using an explicitly selected device can ignore the system default |
| Input device | Read/set the default input among eligible devices | Selecting a device does not record microphone audio or change an app's own explicit selection |
| Output left/right balance | Device stereo pan, or writable stereo channel volumes when appropriate | Mono and devices without suitable controls remain unavailable |
| Connected headphones | Available audio devices with headphone terminal/connection metadata | Bluetooth transport alone does not prove that a device is headphones; paired but disconnected accessories are not active audio devices |
| Audio output applications | Public HAL process objects, output-device membership and activity flags on macOS 14.2+ | Available output processes can remain listed while idle; helpers appear separately and older systems may not expose this information |
| Per-process attenuation | Explicitly enabled public process tap and temporary private aggregate device on macOS 14.2+ | Experimental; only the selected process and supported stereo routes, not every helper belonging to an app |
| Focus / Do Not Disturb | Run two configured, user-owned Shortcuts through Apple's public CLI on macOS 13+ | One-time setup is required; direct Focus status access remains read-only, and previous schedules cannot be fully restored |

Hardware controls check capabilities and read back real values. A missing control is disabled rather than simulated. Detailed device and process metadata listeners refresh the Volume page while it is active; leaving the page removes them. Event Log uses a separate device-list listener for physical connection changes. Those controls do not capture samples. Separately, an explicitly enabled per-process route captures and processes samples continuously until stopped, including while Volume or the HUD is hidden. There is no audio meter, recording to disk, installed driver, or private API.

Sources inspected include Apple's [hardware-volume guidance](https://developer.apple.com/library/archive/qa/qa1016/_index.html), [HAL process properties](https://developer.apple.com/documentation/coreaudio/audiohardwareprocess), [device controls](https://developer.apple.com/documentation/coreaudio/audiohardwarecontrol), and [Focus status API](https://developer.apple.com/documentation/intents/infocusstatuscenter), together with the installed macOS 15.5 and 26.2 SDK headers. The old example's deprecated calls are not used; the implementation uses `AudioObjectGetPropertyData`, `AudioObjectIsPropertySettable`, property listeners and `AudioObjectSetPropertyData`.

## Experimental per-process volume

The app implements a separate routing engine because HAL does not expose a general writable per-process gain property. Move a listed output process's slider below 100% to start its temporary mixer. The same row shows the requested level while startup is pending. Moving an enabled slider back to 100% sets unity gain while keeping the same route active or preparing; lowering it again adjusts that route without recreating its tap or aggregate. There is no app submenu or × button. macOS may request System Audio Recording permission. Captured audio is processed in memory; it is not recorded or saved.

On macOS 14.2 and newer, the engine creates a public process tap and a private aggregate device that forwards to the current physical output. It does not change the system's default output device. The original process stays unmuted while the route prepares; forwarding and original-output suppression begin only after a usable callback is received. The gain control attenuates the selected process from 100% to 0%. Apple documents the underlying APIs in [Capturing system audio with Core Audio taps](https://developer.apple.com/documentation/coreaudio/capturing-system-audio-with-core-audio-taps) and [`CATapDescription.muteBehavior`](https://developer.apple.com/documentation/coreaudio/catapdescription/mutebehavior).

Support is deliberately narrow: the current built-in or USB device must have exactly two output channels, no input channels, and standard left/right mapping. The process must output only to that device. Tap, physical output and aggregate streams must use supported Float32 stereo layouts and matching sample rates. Bluetooth, other transports, multichannel or duplex devices, incompatible formats and multiple-output processes are rejected; the engine does not add arbitrary channel or sample-rate conversion.

Leaving Volume stops its hardware metadata listeners but keeps an opted-in route running. Returning an enabled slider to 100% keeps its route running at unity gain. An idle row at 100% does not start capture. Sleep, session suspension, quit, process loss, or relevant device/format changes also stop routing; there is no automatic restart. Pending and error states remain inline. A route owns real-time processing and a health timer, so it has a different CPU and energy cost from an idle HUD.

**The user confirmed audible Bilibili attenuation and normal playback restoration with the previous × Stop control on the tested Mac.** The current 100% behavior keeps the route running at unity gain; it is not the former Stop/restore operation. That updated behavior has not received a separate listening test. Earlier startup tests exposed HAL waits; metadata and route operations now run on workers with cancellation and retained cleanup. The successful listening check does not establish other-device compatibility, simultaneous-app independence, audio quality, latency or failure recovery. See [the per-app audio guide](per-app-audio.md) for architecture and limitations, and [TESTING.md](../TESTING.md) for recorded verification. No driver installation is required.

## Volume controls

Open **Volume** in the HUD. Choose output or input through the inline device picker. Drag the output volume or left/right balance track when enabled; the mute button is available only when supported. The lower panel shows detected headphones/Bluetooth outputs and available audio-output processes. Use the wheel or trackpad to scroll the app list continuously inside the circular interface.

Input selection changes the default input device only. Headphone detection does not provide accessory battery levels or pair new Bluetooth devices. The app list hides input-only, self and incompatible entries, and retains available output processes while idle or after their route stops. Lowering a slider below 100% starts that process's route; opening the page or leaving an idle slider at 100% does not. Pending and failed cleanup remain visible inline.

## Work Mode

- Work Mode fills the enlarged **440 × 440-point** center host with a **430-point-diameter** dial, a large clock, and compact controls inside the ring. Other modules retain their existing 400 × 334-point canvases.
- Choose **Countdown** or **Stopwatch**. Countdown presets are **5, 30 and 60 minutes**, with **30 minutes** as the initial duration.
- **Custom** accepts minutes or `minutes:seconds`, from one second to 24 hours. Editing happens inline before a session starts. Return saves and Escape cancels; cancel clears validation errors. A duration field left open when the session starts cannot reconfigure that active timer.
- Start a session, then use **Pause + Reset** while running or **Resume + Reset** while paused. Reset ends the session, clears elapsed time, cancels its timers, and restores the configured countdown duration (or zero for the stopwatch). There is no Stop control. An active/paused badge remains in the shared HUD header when visiting another module. The menu-bar Work Mode entry reports completion and can reopen the timer.
- Starting fades out the mode choices and presets while the clock grows and moves upward into their space. Running and paused sessions keep this expanded layout; reset and completion restore the configuration area. The transition lasts about 0.44 seconds, can reverse during rapid actions, and adds no repeating timer. Hidden or still-fading-in controls cannot receive clicks or accessibility actions. The timer's state labels are **RUNNING** and **PAUSED** in both app languages. There is no manual Focus button or normal Focus footer text.
- The countdown starts with a full theme-colored ring. Its remaining arc shrinks with its moving endpoint traveling counter-clockwise. A stopwatch uses a subtle moving marker. Core Animation owns one continuous track only while this module is visible and running. Action feedback uses brief depth presses and clipped clock movement. Reduce Motion removes that movement and settles layout changes instantly. Text updates once per second while running and visible; there is no application-owned frame loop.
- Sessions survive closing the HUD or changing sections. They are local to the running app and reset when it quits. Elapsed time uses a monotonic continuous clock, including sleep. A hidden countdown keeps one completion deadline; a hidden stopwatch has no update timer. Suspension removes timers, and waking reconciles elapsed time.

### Automatic Focus setup

Automatic Focus does **not** work on every Mac supported by EndfieldHUD, or immediately after installing the app. It requires **macOS 13 or later**, an available `/usr/bin/shortcuts`, and two correctly configured shortcuts in the current user's library. Apple introduced **Get Current Focus** in [macOS 13.0](https://support.apple.com/en-bn/101583); earlier supported systems can use the timer but this app disables its automatic Focus integration. The implementation has no processor-specific branch, so eligible Intel and Apple silicon Macs use the same path. That is compatibility by design, not completed testing on every model or OS version.

The app invokes the shortcuts through Apple's public [`shortcuts run` interface](https://support.apple.com/guide/shortcuts-mac/apd455c82f02/mac), using **Get Current Focus** and **Set Focus**. It does not write Focus preferences or use private APIs. The current app, DMG and source repository do **not** include installable `.shortcut` files, an import link, or an automatic installer. Each user must create the two shortcuts manually in Apple's Shortcuts app. Create exactly one shortcut with each name below, and return the exact lowercase result text from each branch without alerts or input prompts.

| Shortcut name | Actions and required result |
| --- | --- |
| `EndfieldCharge Focus Start` | Get Current Focus. If any Focus is already active, leave it unchanged and output `preserved`. Otherwise turn **Do Not Disturb** on **until turned off**, then output `enabled`. |
| `EndfieldCharge Focus End` | Get Current Focus and its name. If the name is **Do Not Disturb** or **勿扰模式**, turn Do Not Disturb off and output `released`. For any other current mode, or no current Focus, leave it unchanged and output `unchanged`. |

Use Text followed by **Stop and Output** to return each result. Keep the two shortcut names unchanged; the app checks that both exist uniquely before invoking Start. A Mac using another system language may need its localized Do Not Disturb name in the End shortcut's comparison. The user owns these shortcuts and can inspect their actions in Shortcuts.

Run and inspect both shortcuts in Shortcuts before relying on Work Mode. Respond to any access request there: Apple's [Shortcuts privacy guide](https://support.apple.com/guide/shortcuts-mac/apd961a4fc65/mac) explains that individual actions can request access and that a denied request can prevent execution. EndfieldHUD does not bypass those prompts or require Accessibility, Full Disk Access, or System Audio Recording for this Focus path. Keep the shortcuts free of interactive questions: Apple documents that an input prompt pauses a command-line run. Verify that Start enables Do Not Disturb only when no Focus is active, preserves an existing Focus, and that End releases the owned Do Not Disturb session. Do not rename the shortcuts after this check.

Starting a timer invokes Start asynchronously. An existing Focus is preserved, and the app schedules End only after Start has explicitly returned `enabled`. Pause/resume keeps the same Focus session. Reset, completion and normal quit request End for an owned session; a reset received during Start waits for that result before cleanup. Commands run serially, so a new Start waits for a pending End. Closing the HUD or switching sections does not end Work Mode or its Focus session. Diagnostics never run these shortcuts.

Unavailable Shortcuts, missing setup, failures or an unconfirmed change appear as a short footer message; the timer still works. A command taking more than 20 seconds shows a waiting message and remains pending. Normal quit waits up to eight seconds for cleanup. A stalled shortcut, forced termination or failure can leave Focus unconfirmed, so the app does not claim complete restoration. It cannot restore previous schedules or distinguish its own Do Not Disturb from the user manually enabling the same mode again during the session. A different current Focus is left untouched by the provided End shortcut; use Control Center to inspect or change the actual mode when a failure is reported.

The configured shortcuts were tested on this Mac through the public CLI: Start enabled Do Not Disturb when none was active, a repeated Start preserved it, End released it, and a repeated End left the state unchanged. The final app was also checked through its real Work Mode controls: Start enabled Focus, Reset released it, and a separate normal quit released an active session. Other Macs, schedules and failure paths still require validation. Controller tests use injected commands; current end-to-end evidence is recorded in [TESTING.md](../TESTING.md).

## 中文

**音量：** 使用公开 Core Audio API，按设备实际能力提供系统输出音量、静音、输出／输入设备选择和左右平衡。没有可写控制的设备会显示不可用。耳机依据音频设备元数据识别；蓝牙设备不会自动被当成耳机。macOS 14.2 及以上可显示检测到的音频活动进程，旧系统明确提示不支持。活动表示正在进行音频 I/O，不代表一定有可听声音。

**独立进程音量为实验性功能，需要 macOS 14.2 或更新版本，限制说明保留在文档中。** 列表连续滚动，保留空闲及停止混音后仍可用的输出进程，隐藏仅输入、本应用及不兼容条目。滑块低于 100% 时启动临时混音；已启用的滑块回到 100% 时以单位增益继续使用同一路由，不销毁重建 Tap 或聚合设备；不需要子菜单或 × 按钮，空闲滑块维持 100% 不会捕获音频。启动和错误状态在行内显示。系统可能请求系统音频录制权限。不会切换系统默认输出，不会录制或保存音频，也不安装驱动或使用私有 API。仅支持当前内建或 USB 输出设备的标准左右双声道、无输入声道、Float32 格式及匹配采样率；蓝牙、多声道、带输入的设备和多输出进程不受支持。

离开音量页面会移除详细硬件与进程信息监听，但事件日志的设备列表监听和已启用的混音仍独立运行。睡眠、会话暂停、退出或相关进程／路由／格式变化会停止混音，不会自动重启。**用户已在本机确认哔哩哔哩滑块可降低实际音量，旧版 × 停止按钮能恢复正常播放。** 当前回到 100% 会保留路由并使用单位增益，与原来的停止／恢复直通操作不同；新行为尚未单独进行听感复测。早期测试发现的 HAL 等待由后台执行、取消和资源保留机制处理；其他设备、多应用同时独立调节、音质、延迟与异常恢复尚待验证。详见[独立应用音量说明](per-app-audio.md)。

**工作模式：** 使用 440 × 440 点中央区域，430 点直径的大圆环包围时钟和全部控制。提供倒计时与秒表，预设为 5／30／60 分钟，默认 30 分钟，并支持开始前自定义时长。运行时显示“暂停＋重置”，暂停时显示“继续＋重置”，不再提供“结束”按钮。重置会结束会话、清零已用时间、取消计时器，并恢复配置的倒计时时长或秒表的零值。主题色圆环从完整开始，剩余弧线的端点逆时针缩短；秒表使用轻微运动的标记。自定义可输入分钟或 `分钟:秒`，范围为一秒至 24 小时；Esc 取消并清除错误。会话开始后，先前打开的输入框也不能再修改正在运行的计时。关闭浮层或切换模块不会中断会话；退出应用会清空。时间包含睡眠期间，唤醒后校正；隐藏秒表无重复计时器，隐藏倒计时仅保留一次到期任务。

开始时，上方模式与预设按钮逐渐淡出，时钟向上移动并放大；暂停时保持放大，重置或完成后恢复。过渡约 0.44 秒，可在连续操作中反向衔接，不新增重复计时器。隐藏或尚未完成淡入的按钮不接受点击，也不暴露辅助功能操作。计时状态在中英文界面均显示 **RUNNING／PAUSED**；原先的手动专注按钮与常驻说明已移除。

运行时共享浮层标题区显示工作状态；计时页面使用轻微圆环动效，隐藏后停止。系统“减弱动态效果”会禁用连续运动，并立即完成布局变化。

**自动专注模式需要 macOS 13 或更新版本、可用的 `/usr/bin/shortcuts` 和一次性快捷指令设置，并非所有受支持的 Mac 都能使用，也不是安装应用后立即可用。** Intel 与 Apple silicon 使用相同实现，但未在所有型号或系统版本上实测；较旧系统仍可使用计时器。当前应用、DMG 和源码未附带可导入的快捷指令文件、导入链接或自动安装器。每位用户需在 Apple“快捷指令”中手动创建上述两个名称完全一致且不重复的快捷指令。`EndfieldCharge Focus Start` 读取当前专注模式：已有任意模式时不修改，并输出 `preserved`；没有时开启勿扰模式，持续到手动关闭，并输出 `enabled`。`EndfieldCharge Focus End` 读取当前模式名称：为 `Do Not Disturb` 或 `勿扰模式` 时关闭勿扰并输出 `released`，否则不修改并输出 `unchanged`。各分支可使用“文本”和“停止并输出”返回这些小写结果，不添加弹窗或输入请求。其他系统语言需在 End 中加入对应的勿扰名称比较。

先在“快捷指令”中分别检查并运行，处理其可能提出的访问请求，再验证开启、保留已有专注及关闭行为。拒绝访问或需要用户输入会使自动运行失败或等待。该专注功能本身不需要辅助功能、完全磁盘访问或系统音频录制权限，也不会绕过系统权限。

应用仅通过公开 `shortcuts run` 命令串行执行这两个用户拥有的快捷指令，不读取或改写私有专注设置文件。开始计时时自动调用 Start；已存在的专注模式会保留，只有返回 `enabled` 才视为由本次工作会话启用。暂停／继续保持同一专注会话；重置、完成和正常退出时请求 End。若重置发生在 Start 进行中，则等待确认后再清理；新 Start 等待进行中的 End。隐藏浮层或切换模块不会结束会话，诊断不会执行这些快捷指令。

系统不支持、缺少设置、失败或未确认的状态会在底部显示简短提示，计时器仍可继续工作。命令超过 20 秒显示等待提示，但不会假装已取消；正常退出最多等待八秒。命令卡住、强制退出或失败时不能保证清理完成；应用也无法恢复原来的自动日程，或区分用户在会话中再次手动开启的同一个勿扰模式。End 不会关闭其他名称的当前专注模式，遇到错误可在控制中心检查实际状态。

本机已通过公开命令验证两个快捷指令：无专注时 Start 开启勿扰，重复 Start 保留，End 关闭，重复 End 不修改。最终开发版也通过实际工作模式按钮验证：开始自动开启勿扰，重置释放勿扰；另一次运行中的会话在正常退出时也成功释放。其他 Mac、自动日程及全部失败路径仍需验证；具体记录见 [TESTING.md](../TESTING.md)。
