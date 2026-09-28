import CoreAudio
import Darwin

/// POD storage shared with the HAL callback. Only the callback writes currentGain;
/// cross-thread fields live in separately allocated public atomic primitives.
/// OSAtomic remains public on 14.2; Swift Synchronization requires macOS 15.
struct PerAppAudioRenderState {
    let atomic: UnsafeMutablePointer<Int32>
    var currentGain: Float32
    var hasRenderedEnabled: Bool
}

enum PerAppAudioPCM {
    static let gainSlot = 0, modeSlot = 1, signalSlot = 2, faultSlot = 3, tickSlot = 4

    static func allocate() -> UnsafeMutablePointer<PerAppAudioRenderState> {
        let atomic = UnsafeMutablePointer<Int32>.allocate(capacity: 5)
        atomic.initialize(repeating: 0, count: 5)
        atomic[0] = Int32(bitPattern: Float32(1).bitPattern)
        let result = UnsafeMutablePointer<PerAppAudioRenderState>.allocate(capacity: 1)
        result.initialize(to: PerAppAudioRenderState(atomic: atomic, currentGain: 1, hasRenderedEnabled: false))
        return result
    }
    static func release(_ state: UnsafeMutablePointer<PerAppAudioRenderState>) {
        state.pointee.atomic.deinitialize(count: 5); state.pointee.atomic.deallocate()
        state.deinitialize(count: 1); state.deallocate()
    }
    static func load(_ state: UnsafeMutablePointer<PerAppAudioRenderState>, _ slot: Int) -> Int32 {
        OSAtomicAdd32Barrier(0, state.pointee.atomic.advanced(by: slot))
    }
    static func store(_ state: UnsafeMutablePointer<PerAppAudioRenderState>, _ slot: Int, _ value: Int32) {
        let pointer = state.pointee.atomic.advanced(by: slot)
        var previous = OSAtomicAdd32Barrier(0, pointer)
        while !OSAtomicCompareAndSwap32Barrier(previous, value, pointer) { previous = OSAtomicAdd32Barrier(0, pointer) }
    }
    static func setGain(_ gain: Double, state: UnsafeMutablePointer<PerAppAudioRenderState>) {
        guard gain.isFinite else { return }
        store(state, gainSlot, Int32(bitPattern: Float32(min(1, max(0, gain))).bitPattern))
    }

    /// Float32 stereo only, interleaved or planar. No allocation, locks,
    /// Foundation calls, dispatch, logging or reference-counted objects occur
    /// here. Aggregate input/output frame counts must already agree.
    @discardableResult static func render(input: UnsafePointer<AudioBufferList>, output: UnsafeMutablePointer<AudioBufferList>,
                                          state: UnsafeMutablePointer<PerAppAudioRenderState>) -> Bool {
        let source = UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: input))
        let destination = UnsafeMutableAudioBufferListPointer(output)
        for buffer in destination {
            if let data = buffer.mData { memset(data, 0, Int(buffer.mDataByteSize)) }
        }
        guard (source.count == 1 || source.count == 2), (destination.count == 1 || destination.count == 2),
              validStereo(source), validStereo(destination) else { store(state, faultSlot, 1); return false }
        let frames = Int(source[0].mDataByteSize / (source[0].mNumberChannels * 4))
        guard frames > 0, frames <= 16_384,
              Int(destination[0].mDataByteSize / (destination[0].mNumberChannels * 4)) == frames,
              (source.count == 1 || source[1].mDataByteSize == source[0].mDataByteSize),
              (destination.count == 1 || destination[1].mDataByteSize == destination[0].mDataByteSize) else {
            store(state, faultSlot, 1); return false
        }
        guard let inputFirst = source[0].mData?.assumingMemoryBound(to: Float32.self),
              let outputFirst = destination[0].mData?.assumingMemoryBound(to: Float32.self) else {
            store(state, faultSlot, 1); return false
        }
        let inputSecond = source.count == 2 ? source[1].mData?.assumingMemoryBound(to: Float32.self) : inputFirst
        let outputSecond = destination.count == 2 ? destination[1].mData?.assumingMemoryBound(to: Float32.self) : outputFirst
        guard let inputSecond, let outputSecond else { store(state, faultSlot, 1); return false }
        let inputStride = source.count == 1 ? 2 : 1
        let outputStride = destination.count == 1 ? 2 : 1
        let inputRightOffset = source.count == 1 ? 1 : 0
        let outputRightOffset = destination.count == 1 ? 1 : 0
        let enabled = load(state, modeSlot) == 1
        let target = Float32(bitPattern: UInt32(bitPattern: load(state, gainSlot)))
        guard target.isFinite, target >= 0, target <= 1 else { store(state, faultSlot, 1); return false }
        var gain = state.pointee.currentGain
        // The first forwarded buffer starts at the requested gain, including
        // zero. Only later active edits ramp; startup must never fade from an
        // accidental unity gain. Both fields are owned solely by this callback.
        if enabled, !state.pointee.hasRenderedEnabled { gain = target; state.pointee.hasRenderedEnabled = true }
        if !enabled { state.pointee.hasRenderedEnabled = false }
        let step = (target - gain) / Float32(max(1, min(frames, 256)))
        var signal = false
        for frame in 0..<frames {
            let left = inputFirst[frame * inputStride]
            let right = inputSecond[frame * inputStride + inputRightOffset]
            let cleanLeft = left.isFinite ? left : 0
            let cleanRight = right.isFinite ? right : 0
            if abs(cleanLeft) > 0.0000001 || abs(cleanRight) > 0.0000001 { signal = true }
            if enabled {
                if frame < 256 { gain += step }
                if frame + 1 >= min(frames, 256) { gain = target }
                outputFirst[frame * outputStride] = cleanLeft * gain
                outputSecond[frame * outputStride + outputRightOffset] = cleanRight * gain
            }
        }
        if enabled { state.pointee.currentGain = target }
        if signal { _ = OSAtomicCompareAndSwap32Barrier(0, 1, state.pointee.atomic.advanced(by: signalSlot)) }
        _ = OSAtomicAdd32Barrier(1, state.pointee.atomic.advanced(by: tickSlot))
        return true
    }
    private static func validStereo(_ buffers: UnsafeMutableAudioBufferListPointer) -> Bool {
        if buffers.count == 1 { return buffers[0].mNumberChannels == 2 && buffers[0].mDataByteSize % 8 == 0 }
        return buffers.count == 2 && buffers[0].mNumberChannels == 1 && buffers[1].mNumberChannels == 1
            && buffers[0].mDataByteSize % 4 == 0 && buffers[1].mDataByteSize % 4 == 0
    }
}

let perAppAudioIOProc: AudioDeviceIOProc = { _, _, input, _, output, _, client in
    guard let client else { return kAudioHardwareIllegalOperationError }
    let state = client.assumingMemoryBound(to: PerAppAudioRenderState.self)
    _ = PerAppAudioPCM.render(input: input, output: output, state: state)
    return noErr
}
