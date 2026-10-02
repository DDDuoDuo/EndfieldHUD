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
| Focus / Do Not Disturb | This integration branch uses public Control Center Accessibility controls on macOS 11+; two user-owned Shortcuts on macOS 13+ remain a fallback | Requires permission and recognizable controls, or configured shortcuts; previous schedules and same-mode user changes cannot be fully restored |

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

**This integration branch** first tries Control Center through the public Accessibility API on **macOS 11 or later**. Grant EndfieldHUD access in **System Settings → Privacy & Security → Accessibility**; older systems use **System Preferences → Security & Privacy → Accessibility**. Work Mode provides a link to these settings when access is missing. The app never grants access itself or repeatedly prompts for it. Returning to the app after granting access can retry a still-active session that was blocked only by the missing permission.

The app briefly opens Control Center, reads the current Focus and leaves an existing mode unchanged. When none is active, it enables Do Not Disturb and checks the result. This requires recognizable system controls; an OS update or an already-open Control Center window can prevent the direct path. It does not use a private Focus API, edit system settings files or poll Focus while idle. If access is missing or controls cannot be recognized before a toggle, it can use the configured shortcuts below on macOS 13+. If a toggle may already have happened, it reports the uncertain result without trying a second path.

**The stable v1.0.1 download and original Watch branch use only Shortcuts.** For those builds, or as an optional fallback in this integration branch, use **macOS 13 or later**, an available `/usr/bin/shortcuts`, and two correctly configured shortcuts in the current user's library. Work Mode's timer works without either automation path. The native Control Center layout was inspected read-only on macOS 15.7.4; actual toggling is covered by synthetic tests. Other macOS versions, including Tahoe, and Intel hardware have not received live validation of this path.

#### Optional Shortcuts fallback and stable-release setup

The shortcut path uses Apple's public [`shortcuts run` interface](https://support.apple.com/guide/shortcuts-mac/apd455c82f02/mac), with **Get Current Focus** and **Set Focus**. The app and source repository do **not** include installable `.shortcut` files, an import link, or an automatic installer. To use this path, create exactly one shortcut with each name below in Apple's Shortcuts app, and return the exact lowercase result text from each branch without alerts or input prompts.

| Shortcut name | Actions and required result |
| --- | --- |
| `EndfieldCharge Focus Start` | Get Current Focus. If any Focus is already active, leave it unchanged and output `preserved`. Otherwise turn **Do Not Disturb** on **until turned off**, then output `enabled`. |
| `EndfieldCharge Focus End` | Get Current Focus and its name. If the name is **Do Not Disturb** or **勿扰模式**, turn Do Not Disturb off and output `released`. For any other current mode, or no current Focus, leave it unchanged and output `unchanged`. |

Use Text followed by **Stop and Output** to return each result. Keep the two shortcut names unchanged; the app checks that both exist uniquely before invoking Start. A Mac using another system language may need its localized Do Not Disturb name in the End shortcut's comparison. The user owns these shortcuts and can inspect their actions in Shortcuts.

Run and inspect both shortcuts in Shortcuts before relying on them. Respond to any access request there; see Apple's [Shortcuts privacy guide](https://support.apple.com/guide/shortcuts-mac/apd961a4fc65/mac). The shortcut path does not require EndfieldHUD's Accessibility permission, Full Disk Access or System Audio Recording. Keep the shortcuts free of interactive questions, which can pause a command-line run. Verify that Start enables Do Not Disturb only when no Focus is active, preserves an existing Focus, and that End releases the owned Do Not Disturb session. Do not rename the shortcuts after this check.

#### Session behavior and validation

Starting a timer starts Focus automation asynchronously. The app owns a Focus session only after confirming that it enabled Do Not Disturb, and uses the same control path to end it. Pause/resume keeps that session. Reset, completion and normal quit request cleanup for an owned session; a reset received during startup waits for the result before cleanup. Operations run serially. Closing the HUD or switching sections does not end Work Mode or its Focus session. Isolated diagnostics do not change system Focus.

Missing Accessibility access, unavailable controls, failures or an unconfirmed change can appear as a short footer message; the former reminder to create shortcuts has been removed. The timer still works. An operation taking more than 20 seconds shows a waiting message and remains pending. Normal quit waits up to eight seconds for cleanup. A stalled operation, forced termination or failure can leave Focus active or unconfirmed. The app cannot restore previous schedules or distinguish its own Do Not Disturb from the user manually enabling the same mode again during the session. It leaves a different current Focus untouched; use Control Center to inspect the actual mode after a failure.

The previous shortcut implementation was tested on this Mac through the public CLI and real Work Mode controls: Start enabled or preserved Focus as appropriate, and Reset or normal quit released an owned session. That historical evidence does not verify the new Control Center path. Current controller and executor tests use injected system responses; native Control Center inspection was read-only. See [integration validation](integration-preservation.md) and the historical [testing record](../TESTING.md).

## 中文

**音量：** 使用公开 Core Audio API，按设备实际能力提供系统输出音量、静音、输出／输入设备选择和左右平衡。没有可写控制的设备会显示不可用。耳机依据音频设备元数据识别；蓝牙设备不会自动被当成耳机。macOS 14.2 及以上可显示检测到的音频活动进程，旧系统明确提示不支持。活动表示正在进行音频 I/O，不代表一定有可听声音。

**独立进程音量为实验性功能，需要 macOS 14.2 或更新版本，限制说明保留在文档中。** 列表连续滚动，保留空闲及停止混音后仍可用的输出进程，隐藏仅输入、本应用及不兼容条目。滑块低于 100% 时启动临时混音；已启用的滑块回到 100% 时以单位增益继续使用同一路由，不销毁重建 Tap 或聚合设备；不需要子菜单或 × 按钮，空闲滑块维持 100% 不会捕获音频。启动和错误状态在行内显示。系统可能请求系统音频录制权限。不会切换系统默认输出，不会录制或保存音频，也不安装驱动或使用私有 API。仅支持当前内建或 USB 输出设备的标准左右双声道、无输入声道、Float32 格式及匹配采样率；蓝牙、多声道、带输入的设备和多输出进程不受支持。

离开音量页面会移除详细硬件与进程信息监听，但事件日志的设备列表监听和已启用的混音仍独立运行。睡眠、会话暂停、退出或相关进程／路由／格式变化会停止混音，不会自动重启。**用户已在本机确认哔哩哔哩滑块可降低实际音量，旧版 × 停止按钮能恢复正常播放。** 当前回到 100% 会保留路由并使用单位增益，与原来的停止／恢复直通操作不同；新行为尚未单独进行听感复测。早期测试发现的 HAL 等待由后台执行、取消和资源保留机制处理；其他设备、多应用同时独立调节、音质、延迟与异常恢复尚待验证。详见[独立应用音量说明](per-app-audio.md)。

**工作模式：** 使用 440 × 440 点中央区域，430 点直径的大圆环包围时钟和全部控制。提供倒计时与秒表，预设为 5／30／60 分钟，默认 30 分钟，并支持开始前自定义时长。运行时显示“暂停＋重置”，暂停时显示“继续＋重置”，不再提供“结束”按钮。重置会结束会话、清零已用时间、取消计时器，并恢复配置的倒计时时长或秒表的零值。主题色圆环从完整开始，剩余弧线的端点逆时针缩短；秒表使用轻微运动的标记。自定义可输入分钟或 `分钟:秒`，范围为一秒至 24 小时；Esc 取消并清除错误。会话开始后，先前打开的输入框也不能再修改正在运行的计时。关闭浮层或切换模块不会中断会话；退出应用会清空。时间包含睡眠期间，唤醒后校正；隐藏秒表无重复计时器，隐藏倒计时仅保留一次到期任务。

开始时，上方模式与预设按钮逐渐淡出，时钟向上移动并放大；暂停时保持放大，重置或完成后恢复。过渡约 0.44 秒，可在连续操作中反向衔接，不新增重复计时器。隐藏或尚未完成淡入的按钮不接受点击，也不暴露辅助功能操作。计时状态在中英文界面均显示 **RUNNING／PAUSED**；原先的手动专注按钮与常驻说明已移除。

运行时共享浮层标题区显示工作状态；计时页面使用轻微圆环动效，隐藏后停止。系统“减弱动态效果”会禁用连续运动，并立即完成布局变化。

**本整合分支在 macOS 11 或更新版本上优先通过公开辅助功能接口操作控制中心。** 请在“系统设置 → 隐私与安全性 → 辅助功能”中允许 EndfieldHUD；旧系统对应“系统偏好设置 → 安全性与隐私 → 辅助功能”。缺少权限时，工作模式提供打开设置的入口，不会自行授权或反复弹窗。授权后返回应用，仍在运行且仅因权限受阻的会话可以重试。应用会短暂打开控制中心，读取现有专注状态；已有模式时保留，没有时才开启勿扰并回读确认。不使用私有专注 API、不修改系统设置文件，也不在空闲时轮询。

控制中心的控件必须可识别；系统版本变化或用户已打开的控制中心窗口可能使该路径不可用。若在切换前发现缺少权限或无法识别控件，可在 macOS 13+ 使用已配置的快捷指令后备；若切换可能已经发生，则报告未确认状态，不会再通过另一条路径重试。计时器始终可独立使用。本机仅在 macOS 15.7.4 上只读检查了原生控件，实际切换由模拟测试覆盖；Tahoe、其他系统版本与 Intel 硬件尚未完成此路径的现场验证。

**稳定版 v1.0.1 下载与原 Watch 分支仍只使用快捷指令。** 对这些版本，或为本整合分支配置可选后备，需要 macOS 13+、可用的 `/usr/bin/shortcuts`，以及手动创建的两个唯一快捷指令。应用不附带可导入文件或自动安装器。`EndfieldCharge Focus Start` 读取当前专注：已有模式时保留并输出 `preserved`；没有时开启勿扰，持续到手动关闭，并输出 `enabled`。`EndfieldCharge Focus End` 在当前模式名为 `Do Not Disturb` 或 `勿扰模式` 时关闭并输出 `released`，否则输出 `unchanged`。用“文本”和“停止并输出”返回结果，不添加弹窗或输入请求；其他系统语言需在 End 中加入对应的勿扰名称。先在“快捷指令”中检查、运行并处理其访问请求。仅此快捷指令路径不需要 EndfieldHUD 的辅助功能、完全磁盘访问或系统音频录制权限。

开始计时时异步执行自动专注；只有确认本次启用了勿扰才取得会话所有权，并通过同一路径清理。暂停／继续保留会话，重置、完成和正常退出请求清理；启动途中重置会等待结果。操作串行执行，隐藏 HUD 或切换模块不会结束会话，隔离诊断不会改变系统专注。缺少辅助功能权限、控件不可用、失败或结果未确认时可显示简短提示；原先要求创建快捷指令的提醒已移除。操作超过 20 秒显示等待提示，正常退出最多等待八秒。卡住、强制退出或失败可能留下仍开启或未确认的状态。应用无法恢复原有日程，也无法区分用户再次手动开启的同一勿扰模式；其他当前专注不会被关闭。

此前快捷指令实现已在本机通过公开命令和工作模式按钮验证开启、保留及清理行为；这些历史结果不能代替新控制中心路径的实测。当前控制器与执行器测试使用模拟系统响应，原生控件仅做只读检查。参见[整合验证](integration-preservation.md)及[历史测试记录](../TESTING.md)。
