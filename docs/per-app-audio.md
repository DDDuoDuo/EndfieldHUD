# Experimental per-app audio attenuation

The Volume module implements experimental independent attenuation for selected Core Audio processes on macOS 14.2 or later. Each enabled process has its own tap, private aggregate device, render callback and gain between 0% and 100%. Its DSP applies gain to Float32 audio samples without changing the system output volume or another route's gain. Multiple explicitly enabled processes use independent routes. The user confirmed audible Bilibili attenuation and restoration through the previous Stop control on the tested Mac; simultaneous-app independence and other devices remain unverified. The current interface keeps an enabled route running at unity gain when its slider returns to 100%; an idle slider at 100% starts nothing.

Synthetic PCM and injected HAL lifecycle tests pass. Two early consented quiet-tone attempts on macOS 15.7.4 encountered HAL startup waits: first in `AudioDeviceStart`, then in `AudioDeviceCreateIOProcID`, with a simultaneous device metadata read also waiting. After moving metadata and routing to workers and adding the readiness checks, the HUD remained responsive and a Bilibili helper route activated with audio-only permission granted. The user then confirmed that its direct slider audibly reduced volume and the adjacent × restored normal playback. This verifies that workflow on the tested Mac, not broad hardware compatibility, simultaneous-app independence, latency, sound quality or crash recovery.

## Adjusting a process

1. Open Volume and scroll its app list with the wheel or trackpad. Rows move continuously rather than paging. Eligible output processes remain listed while idle; input-only, self and incompatible entries are hidden.
2. Move a process's slider below 100% to request its level and start its route. Opening the list or leaving an idle slider at 100% does not start capture.
3. Allow macOS System Audio Recording access if requested, and play audio in that app. While preparing, the slider stores the latest requested level and direct playback remains unmuted; this pending value is not a claim that attenuation is active.
4. Once the route validates its PCM callback and receives nonzero audio, its first forwarded buffer uses the latest requested gain, including zero. Move an enabled slider back to 100% for unity gain on the same route. This preserves the tap and aggregate while preparing or active; lowering the slider again changes gain without restarting capture. Available idle processes also keep their rows. There is no × button or app submenu; pending and error states appear inline.

The bundle needs `NSAudioCaptureUsageDescription`. The exact privacy panel name varies by macOS version. The app uses only Apple's public process-tap APIs; it does not install a driver, use private permission calls, intercept every app, record audio to files, upload samples, or change the default input/output device. Normal macOS permission controls still apply.

Before activation, a small control mailbox stores slider intent without forwarding samples. The final arming operation reads that mailbox atomically with cancellation; zero starts silent from the first processed sample. Later active changes use a short ramp to limit clicks. Gain never exceeds unity. A 60-second priming timeout requests cleanup if no usable audio arrives, including a silent app or a permission-denied stream. The original app is never muted during priming.

Enabled routes continue at 100% and while the HUD is hidden. Sessions end on relevant device/process changes, sleep, session suspension or quit, and are not automatically restored. A failed row set to 100% requests cleanup again rather than resuming processing. The app owner calls `stopAll()` for sleep/session termination. Leaving the Volume page can keep explicitly enabled processing active; continuous audio processing necessarily uses resources while a route exists.

## Narrow initial compatibility

The current route requires all of the following, checked again against live HAL metadata before activation:

- macOS 14.2 or later, a live selected HAL process, and a PID/bundle identity that still matches the selection. EndfieldCharge cannot capture itself.
- The process outputs only to the selected device, which is still the system's default output.
- A built-in or USB device with exactly two output channels and no input channels on that audio device object. A Mac can expose separate speakers and microphone objects; the separate microphone object is not included.
- Standard left/right preferred channels `[1, 2]`. Reversed, unknown, mono and surround mappings are rejected.
- One Float32 stereo stream for the physical output, the tap and each side of the private aggregate. Interleaved and noninterleaved layouts are supported. Rates must match, and buffer sizes must be within the implementation's validated limits.

Bluetooth, AirPlay, HDMI, aggregate/virtual devices, devices exposing input channels on the same object, multichannel formats, mismatched rates and custom multi-device app output are deliberately unavailable in this first implementation. It does not perform software sample-rate conversion or spatial-audio reconstruction. Protected audio or OS restrictions may prevent a route from becoming active.

The process list may expose browser/helper processes separately. This implementation controls the explicitly selected HAL process; it does not claim to aggregate every helper belonging to a named application.

## Routing and failure handling

`CATapDescription(stereoMixdownOfProcesses:)` includes only the selected process and creates a private tap. The tap initially uses `.unmuted`. A private stacked aggregate includes only the selected physical output and tap, with the output selected as its main time source and tap drift compensation enabled. Subdevice channel counts come from live hardware rather than dictionary overrides; strict preflight and aggregate checks still reject any physical input channels or unexpected layout. `TapAutoStart` remains false because the SDK documents that true makes `AudioDeviceStart` wait for tapped audio. No global tap, microphone input, default-device reassignment, physical sample-rate write or physical volume write is used.

After aggregate creation, a temporary liveness listener and short cancellable waits allow up to two seconds for `DeviceIsAlive`, before creating the IOProc. The worker rechecks cancellation before and after subsequent resource creation. This readiness check is a bounded startup precaution, not a proven remedy for the observed HAL waits. Even a property getter can block inside HAL.

The IOProc starts with its render output silenced. It validates channel buffers/frame counts and requires a nonzero, finite captured signal. Immediately before activation, process/device identity, channel mapping, streams and formats are revalidated. Only then does the engine set `.mutedWhenTapped` and enable the processed output. This mute mode suppresses original playback only while an audio client is reading the tap; it is not an unconditional process mute.

The render callback uses preallocated POD state and public atomic operations for gain, mode and health flags. It performs no allocation, locks, logging, Foundation calls or dispatch. Only the callback writes its smoothing accumulator. Nonfinite source samples become zero. Main-thread controls do not read or expose captured sample buffers.

Each production route's HAL reads, creation, start, property handling and cleanup belong to one serial worker. Route-start and cleanup requests return immediately on the main thread. A 15-second main-thread startup deadline requests cancellation if HAL has not returned; a five-second cleanup deadline explains when release is still pending. These deadlines never pretend to interrupt a synchronous HAL call or free its resources. Once the call returns, cancellation prevents later activation and the same worker finishes cleanup. Pending slider updates are coalesced rather than accumulating behind a stalled call.

Property listeners monitor process/device topology and the validated stream/tap formats. A change in output device, stream identity, channel order, rate or encoding requests cleanup and requires another explicit start. A worker watchdog exists only while a route is preparing or active. If callbacks stall or reject a format, it reports failure to the main thread before entering potentially blocking cleanup. This health check is not an audio-level meter and does not persist captured sound.

The worker's Stop first disables processed rendering and requests `.unmuted`, retrying transient restoration failures. It then stops/destroys the IOProc, destroys the aggregate and destroys the tap. The HUD displays stopping while that work remains in flight. HAL errors are reported; setting a failed row to 100% retries returned but incomplete cleanup, while an in-flight blocked call retains ownership without starting concurrent teardown. A replacement tap for that process is blocked until release is confirmed. If macOS refuses cleanup or a call remains blocked, quitting EndfieldCharge is the final recovery path. Private audio objects are process owned; unexpected termination and device-failure recovery still need real hardware validation.

No audio callback state is freed while HAL may still own its IOProc. A pathological teardown refusal may intentionally retain a small POD context until process exit to avoid a callback use-after-free.

## Verification performed

Automated tests use synthetic stereo buffers and injected HAL implementations only. They cover all four interleaved/planar pairings, first-buffer requested gain and zero, pending edits during blocked arming, independent gain, smoothing, nonfinite samples, frame mismatch, selected-process-only configuration, aggregate readiness/cancellation, startup rollback, format/stream identity races, reversed stereo rejection, unmute retries, cleanup failures, stale callbacks, device changes and process death. Simulated blocked startup and cleanup verify main-loop responsiveness, retained ownership and delayed cancellation. No real application audio is captured or muted by these automated tests.

The user-confirmed Bilibili slider and previous × Stop test supplies audible validation for one app on this Mac. The current return-to-100% behavior preserves the route at unity gain; this revised behavior has not received a separate listening test and is not a teardown test. Before general release, hardware checks still need two simultaneously playing apps, unchanged-app audibility, denied permission, pause/resume, unplug/default-output changes, sleep/wake, app exit, other supported devices, and latency/quality. Automated synthetic tests do not establish these results.

## Sources

The implementation was checked against the installed macOS 15.5 SDK's `CATapDescription.h`, `AudioHardwareTapping.h`, `AudioHardware.h` and `AudioHardwareBase.h`. Apple's [Core Audio tap sample](https://developer.apple.com/documentation/coreaudio/capturing-system-audio-with-core-audio-taps) describes public process capture and aggregate-device construction. [`mutedWhenTapped`](https://developer.apple.com/documentation/coreaudio/catapmutebehavior/mutedwhentapped) defines conditional original-playback suppression. These APIs supply the building blocks; they do not by themselves validate this app's processing route on every audio device.

## 中文

这是 **macOS 14.2 及以上的实验性独立进程音量功能**，兼容性限制保留在本文档中。音量页直接显示可调输出进程的滑块，通过滚轮或触控板连续滚动，不分页；空闲的可用输出进程仍保留，隐藏仅输入、本应用及不兼容条目。滑块低于 100% 时请求启动路由；已启用的滑块回到 100% 时采用单位增益，保留正在准备或运行的同一路由，不销毁重建 Tap 或聚合设备。进程仍可用时保留其条目；无需子菜单或 × 按钮。仅打开列表或把空闲滑块保留在 100% 不会捕获音频。每个进程有独立的 Tap、私有聚合设备与 0%–100% 音量；不会调整系统主音量，也不会自动捕获所有应用。可能需要允许“系统音频录制”权限。音频只在内存中处理，不写入文件或上传。

启动期间保持原始声音且不重复播放，滑块保存最新目标音量。只有收到格式正确、非静音的目标进程音频并再次验证设备后，才启用处理输出和条件静音；第一个处理样本即使用最新目标值，设为零时不会先输出全音量。已启用的路由在 100% 及隐藏浮层后仍继续运行；设备、格式或进程变更、睡眠、会话暂停或退出会结束路由，不会自动改接其他输出或重启。失败条目设为 100% 时会再次请求清理。HAL 操作在独立串行工作队列执行，超时只请求取消并保留资源，待系统调用返回后再完成清理，不会伪报已经停止。

首版仅支持标准左右双声道、Float32、采样率匹配且不带输入声道的内建或 USB 输出设备。不支持蓝牙、AirPlay、HDMI、虚拟／聚合设备、环绕声、同一音频设备对象带输入声道或应用同时输出到多个设备的情况。浏览器等应用可能有多个音频辅助进程，此功能仅控制用户明确选择的进程。

已完成模拟 PCM 与注入式 HAL 测试。早期两次经同意的真实测试在 HAL 启动阶段等待；加入后台处理和就绪检查后，界面保持响应，哔哩哔哩辅助进程路由成功启动。用户已确认：直接拖动滑块可听见音量降低，旧版 × 按钮能恢复正常播放。当前滑块回到 100% 会保留路由并使用单位增益，与旧版停止操作不同；这一新行为尚未单独进行听感复测。这仅验证了本机该应用的原有调节和恢复路径；多应用同时独立调节、其他设备、音质、延迟及异常恢复尚未验证。界面会区分正在停止与清理失败；若系统调用持续等待或拒绝清理且声音受影响，请退出 EndfieldCharge。
