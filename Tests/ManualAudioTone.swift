// Standalone, opt-in playback source for a consented per-app audio test.
// This file is intentionally NOT included in CoreTests or the application.
//
// Build from the repository root (compiling does not play audio):
//   mkdir -p build/tests build/module-cache
//   AUDIO_TEST_SDK="$(scripts/build.sh --print-sdk)"
//   xcrun swiftc -swift-version 5 -sdk "$AUDIO_TEST_SDK" \
//     -module-cache-path build/module-cache -framework AVFoundation \
//     Tests/ManualAudioTone.swift -o build/tests/EndfieldAudioTone
//
// Run ONLY after permission for the manual audio test:
//   build/tests/EndfieldAudioTone [seconds:1...120] [frequencyHz:220...880]
// Defaults: 120 seconds, 440 Hz, stereo, 0.01 peak (-40 dBFS per channel).
// A second explicitly authorized instance at 660 Hz can test independence.
// Each instance prints its PID: select exactly that PID in the audio HUD.
// The CLI may appear as "PID <number>" rather than a named application.
// Ctrl-C / SIGTERM stops playback; the deadline always bounds playback.
//
// No microphone, capture/tap, permission request, output-device reassignment,
// system volume write, network, or file I/O is used. Samples exist in memory.
// Device/engine configuration changes end the test instead of restarting it.

import AVFoundation
import Foundation
import Darwin

private enum ToneError: LocalizedError {
    case usage
    case unsupportedOutput
    case bufferAllocation

    var errorDescription: String? {
        switch self {
        case .usage:
            return "Usage: EndfieldAudioTone [seconds:1...120] [frequencyHz:220...880]"
        case .unsupportedOutput:
            return "The current output must provide a finite 8–192 kHz sample rate and at least two channels."
        case .bufferAllocation:
            return "Could not allocate the in-memory stereo tone buffer."
        }
    }
}

private final class ManualTone {
    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private var signals: [DispatchSourceSignal] = []
    private var configurationObserver: NSObjectProtocol?
    private var stopTimer: Timer?
    private var fadeTimer: Timer?
    private var isRunning = false
    private var isStopping = false

    func run(seconds: Int, frequency: Int) throws {
        let hardwareFormat = engine.outputNode.inputFormat(forBus: 0)
        let rate = hardwareFormat.sampleRate
        guard rate.isFinite, (8_000...192_000).contains(rate), hardwareFormat.channelCount >= 2,
              let format = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 2) else {
            throw ToneError.unsupportedOutput
        }
        let frameCount = AVAudioFrameCount(rate.rounded())
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: frameCount),
              let channels = buffer.floatChannelData else { throw ToneError.bufferAllocation }
        buffer.frameLength = frameCount
        // An integer number of cycles per loop keeps its boundary continuous,
        // even if the nominal hardware rate is not an exact integer.
        for frame in 0..<Int(frameCount) {
            let phase = 2 * Double.pi * Double(frequency) * Double(frame) / Double(frameCount)
            let sample = Float(sin(phase) * 0.01)
            channels[0][frame] = sample
            channels[1][frame] = sample
        }

        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: format)
        engine.mainMixerNode.outputVolume = 1 // This engine's mixer, never the system volume.
        player.scheduleBuffer(buffer, at: nil, options: .loops)
        engine.prepare()
        try engine.start()
        player.play()
        isRunning = true

        // Dispatch signal sources avoid performing AVFoundation work inside
        // an async POSIX signal handler. Keep them alive until cleanup.
        for number in [SIGINT, SIGTERM] {
            signal(number, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: number, queue: .main)
            source.setEventHandler { [weak self] in self?.stop(reason: "signal \(number)") }
            source.resume()
            signals.append(source)
        }
        configurationObserver = NotificationCenter.default.addObserver(
            forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main
        ) { [weak self] _ in self?.stop(reason: "audio configuration changed") }
        stopTimer = Timer.scheduledTimer(withTimeInterval: Double(seconds), repeats: false) { [weak self] _ in
            self?.stop(reason: "test deadline")
        }

        print("EndfieldAudioTone PID=\(getpid()) frequency=\(frequency)Hz peak=0.01 (-40dBFS) duration=\(seconds)s sampleRate=\(rate)")
        print("Playing a quiet synthetic stereo tone. Select only this PID. Ctrl-C stops playback.")
        fflush(stdout)
        while isRunning { _ = RunLoop.main.run(mode: .default, before: Date(timeIntervalSinceNow: 1)) }
        withExtendedLifetime(buffer) {} // Keep the loop's backing buffer alive through player.stop().
        cleanup()
    }

    private func stop(reason: String) {
        guard isRunning, !isStopping else { return }
        isStopping = true
        stopTimer?.invalidate()
        print("Stopping EndfieldAudioTone: \(reason).")
        fflush(stdout)
        // This touches only our player. A short fade avoids a discontinuity on
        // voluntary stops; engine/configuration failure still ends promptly.
        var step = 0
        fadeTimer = Timer.scheduledTimer(withTimeInterval: 0.01, repeats: true) { [weak self] timer in
            guard let self = self else { timer.invalidate(); return }
            step += 1
            self.player.volume = max(0, 1 - Float(step) / 10)
            if step >= 10 {
                timer.invalidate()
                self.player.stop()
                self.engine.stop()
                self.isRunning = false
            }
        }
    }

    private func cleanup() {
        stopTimer?.invalidate(); fadeTimer?.invalidate()
        if let observer = configurationObserver { NotificationCenter.default.removeObserver(observer) }
        configurationObserver = nil
        signals.forEach { $0.cancel() }
        signals.removeAll()
        player.stop()
        engine.stop()
    }

    deinit { cleanup() }
}

do {
    let arguments = Array(CommandLine.arguments.dropFirst())
    if arguments == ["--help"] {
        print(ToneError.usage.localizedDescription)
        print("Defaults: 120 seconds, 440 Hz, 0.01 peak. Playback only; no capture or system volume changes.")
    } else {
        guard arguments.count <= 2,
              let seconds = arguments.first.flatMap(Int.init) ?? (arguments.isEmpty ? 120 : nil),
              let frequency = arguments.count > 1 ? Int(arguments[1]) : 440,
              (1...120).contains(seconds), (220...880).contains(frequency) else { throw ToneError.usage }
        let tone = ManualTone()
        try withExtendedLifetime(tone) { try tone.run(seconds: seconds, frequency: frequency) }
    }
} catch {
    fputs("EndfieldAudioTone: \(error.localizedDescription)\n", stderr)
    exit(EXIT_FAILURE)
}
