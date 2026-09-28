# FineTune reference review

Reviewed on 2026-09-27 at commit
[`2285279d36d3f8115c1c2d4aecd904f1bdf96a51`](https://github.com/ronitsingh10/FineTune/tree/2285279d36d3f8115c1c2d4aecd904f1bdf96a51).
FineTune is [GPL-3.0 licensed][license]. This was a read-only architectural comparison: no FineTune implementation was incorporated into EndfieldCharge, and FineTune was not executed.

The comparison does **not** establish the cause of our observed `AudioDeviceStart` stall.

- **Startup readiness:** FineTune checks the aggregate's `kAudioDevicePropertyDeviceIsAlive` before creating its IOProc, allowing up to two seconds. Its [readiness helper][readiness] services the default CFRunLoop in short intervals while waiting. EndfieldCharge now independently checks liveness with a temporary listener and bounded, cancellable worker waits before validating channels and formats. This is not yet a proven fix for the observed stall.
- **Aggregate configuration:** FineTune supplies both main-device and clock-device UIDs, normally uses a stacked aggregate for a plain physical output, and leaves subdevice channel counts implicit. The earlier EndfieldCharge configuration used a non-stacked aggregate and explicit zero input/two output channel counts. It now uses a stacked aggregate with live channel metadata, retaining strict validation. Both create private aggregates. EndfieldCharge keeps MainSubDeviceKey alone: the SDK defines ClockDeviceKey as a separate clock-device UID. These [configuration differences][aggregate] informed isolated checks, not copied implementation.
- **Threading and tap setup:** FineTune's controller is `@MainActor`; its [activation path][activation] calls `AudioDeviceStart` synchronously and registers a block IOProc. It prefers a device/stream-specific tap with a stereo-mixdown fallback. EndfieldCharge uses a worker, a C IOProc and stereo mixdown. Keeping our worker protects UI responsiveness; FineTune does not demonstrate that queue choice resolves the stall.
- **Buffer layout:** FineTune's [callback mapping][buffers] handles trailing tap buffers and varying output layouts. Our renderer deliberately accepts validated Float32 stereo buffers only. Broader buffer support would improve device coverage, but does not explain a stall before rendering starts.

FineTune sets `kAudioAggregateDeviceTapAutoStartKey` to true. **Do not treat that as a startup fix:** Apple's macOS 15.5 SDK `AudioHardware.h` documentation for this [public key][autostart] says a nonzero value makes aggregate startup wait for the first tapped audio. EndfieldCharge keeps it false. FineTune also initially uses `mutedWhenTapped`; our route stays unmuted until valid audio is observed.

FineTune optionally enables [private TCC permission APIs][permission] under `ENABLE_TCC_SPI`, dynamically loading `TCCAccessPreflight` and `TCCAccessRequest`. Those private APIs are omitted from EndfieldCharge; they are not a public Core Audio permission contract.

中文说明：本次仅参考架构，未复制或引入 FineTune 的 GPL-3.0 实现。尚未确认启动卡顿的原因；等待设备就绪只是待验证方向。保留后台启动、取消机制及公开 API，不采用私有权限接口。

[license]: https://github.com/ronitsingh10/FineTune/blob/2285279d36d3f8115c1c2d4aecd904f1bdf96a51/LICENSE
[readiness]: https://github.com/ronitsingh10/FineTune/blob/2285279d36d3f8115c1c2d4aecd904f1bdf96a51/FineTune/Audio/Extensions/AudioObjectID%2BReadiness.swift#L23-L42
[aggregate]: https://github.com/ronitsingh10/FineTune/blob/2285279d36d3f8115c1c2d4aecd904f1bdf96a51/FineTune/Audio/Engine/ProcessTapController.swift#L355-L412
[activation]: https://github.com/ronitsingh10/FineTune/blob/2285279d36d3f8115c1c2d4aecd904f1bdf96a51/FineTune/Audio/Engine/ProcessTapController.swift#L530-L703
[buffers]: https://github.com/ronitsingh10/FineTune/blob/2285279d36d3f8115c1c2d4aecd904f1bdf96a51/FineTune/Audio/Engine/ProcessTapController.swift#L1317-L1377
[autostart]: https://developer.apple.com/documentation/coreaudio/kaudioaggregatedevicetapautostartkey
[permission]: https://github.com/ronitsingh10/FineTune/blob/2285279d36d3f8115c1c2d4aecd904f1bdf96a51/FineTune/Audio/Permission/AudioRecordingPermission.swift#L76-L117
