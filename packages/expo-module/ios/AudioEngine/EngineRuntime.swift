import AVFoundation

final class EngineRuntime {
  let engine = AVAudioEngine()

  var onPlaybackCompleted: (() -> Void)?

  private let queue: DispatchQueue

  init(queue: DispatchQueue) {
    self.queue = queue
  }

  func prepare(masterVolume: Double) {
    engine.mainMixerNode.outputVolume = Float(masterVolume)
    engine.prepare()
  }

  func setMasterVolume(_ volume: Double) {
    engine.mainMixerNode.outputVolume = Float(volume)
  }

  func recalculateDuration(state: EngineState) {
    state.durationMs = state.tracks.values.reduce(0.0) { current, track in
      max(current, track.startTimeMs + track.durationMs)
    }
  }

  func attachTrack(_ track: AudioTrack, state: EngineState) {
    guard !track.isAttached else { return }
    engine.attach(track.playerNode)
    engine.attach(track.timePitch)
    engine.connect(track.playerNode, to: track.timePitch, format: track.file.processingFormat)
    engine.connect(track.timePitch, to: engine.mainMixerNode, format: track.file.processingFormat)
    track.isAttached = true
    applyMixing(for: track, soloActive: isSoloActive(state: state))
    applyPitchSpeed(for: track, state: state)
  }

  func detachTrack(_ track: AudioTrack) {
    guard track.isAttached else { return }
    engine.detach(track.playerNode)
    engine.detach(track.timePitch)
    track.isAttached = false
  }

  func detachAllTracks(state: EngineState) {
    for track in state.tracks.values {
      detachTrack(track)
    }
  }

  func stopAllPlayers(state: EngineState) {
    for track in state.tracks.values {
      track.playerNode.stop()
    }
  }

  @discardableResult
  func startEngineIfNeeded(
    state: EngineState,
    sessionCoordinator: AudioSessionCoordinator,
    eventEmitter: EngineEventEmitter
  ) -> Bool {
    if !state.isInitialized {
      state.lastEngineError = "audio engine not initialized"
      eventEmitter.emitError(
        code: "ENGINE_NOT_INITIALIZED",
        message: state.lastEngineError ?? "Audio engine not initialized"
      )
      return false
    }

    if !engine.isRunning {
      do {
        try engine.start()
        state.lastEngineError = nil
      } catch {
        state.lastEngineError = error.localizedDescription
        eventEmitter.emitError(
          code: "ENGINE_START_FAILED",
          message: "Failed to start audio engine. \(error.localizedDescription)",
          details: [
            "nativeError": error.localizedDescription,
            "category": sessionCoordinator.categoryRawValue(),
            "sampleRate": sessionCoordinator.currentSampleRate(),
            "inputAvailable": sessionCoordinator.isInputAvailable(),
            "inputChannels": sessionCoordinator.inputChannelCount(),
            "route": sessionCoordinator.describeRoute()
          ]
        )
        return false
      }
    }

    return engine.isRunning
  }

  func stopEngineIfRunning() {
    if engine.isRunning {
      engine.stop()
    }
  }

  func applyMixingForAllTracks(state: EngineState) {
    let soloActive = isSoloActive(state: state)
    for track in state.tracks.values {
      applyMixing(for: track, soloActive: soloActive)
    }
  }

  func applyPitchSpeedForAllTracks(state: EngineState) {
    for track in state.tracks.values {
      applyPitchSpeed(for: track, state: state)
    }
  }

  func currentPlaybackPositionMs(state: EngineState) -> Double {
    guard let startHostTime = state.playbackStartHostTime else {
      return state.currentPositionMs
    }

    let nowSeconds = AVAudioTime.seconds(forHostTime: mach_absolute_time())
    let startSeconds = AVAudioTime.seconds(forHostTime: startHostTime)
    let elapsedMs = max(0.0, (nowSeconds - startSeconds) * 1000.0)
    let position = state.playbackStartPositionMs + elapsedMs
    return min(position, state.durationMs)
  }

  func schedulePlayback(at positionMs: Double, state: EngineState) {
    stopAllPlayers(state: state)
    state.playbackToken = UUID()
    state.activePlaybackCount = 0

    let baseHostTime = mach_absolute_time() + AVAudioTime.hostTime(forSeconds: 0.05)
    let token = state.playbackToken

    for track in state.tracks.values {
      scheduleTrack(
        track,
        baseHostTime: baseHostTime,
        positionMs: positionMs,
        token: token,
        state: state
      )
    }

    state.playbackStartHostTime = baseHostTime
    state.playbackStartPositionMs = positionMs
  }

  func scheduleTrackForManualRendering(
    _ track: AudioTrack,
    positionMs: Double,
    sampleRate: Double
  ) -> Bool {
    let localStartMs = positionMs - track.startTimeMs
    if localStartMs >= track.durationMs {
      return false
    }

    let fileOffsetMs = max(0.0, localStartMs)
    let delayMs = max(0.0, -localStartMs)
    let trackSampleRate = track.file.processingFormat.sampleRate
    if trackSampleRate <= 0 {
      return false
    }

    let startFrame = AVAudioFramePosition((fileOffsetMs / 1000.0) * trackSampleRate)
    let framesRemaining = track.file.length - startFrame
    if framesRemaining <= 0 {
      return false
    }

    let frameCount = AVAudioFrameCount(framesRemaining)
    let startSample = AVAudioFramePosition((delayMs / 1000.0) * sampleRate)
    let startTime = AVAudioTime(sampleTime: startSample, atRate: sampleRate)

    track.playerNode.scheduleSegment(
      track.file,
      startingFrame: startFrame,
      frameCount: frameCount,
      at: startTime,
      completionHandler: nil
    )
    return true
  }

  private func scheduleTrack(
    _ track: AudioTrack,
    baseHostTime: UInt64,
    positionMs: Double,
    token: UUID,
    state: EngineState
  ) {
    let localStartMs = positionMs - track.startTimeMs
    if localStartMs >= track.durationMs {
      return
    }

    let fileOffsetMs = max(0.0, localStartMs)
    let delayMs = max(0.0, -localStartMs)
    let sampleRate = track.file.processingFormat.sampleRate
    if sampleRate <= 0 {
      return
    }

    let startFrame = AVAudioFramePosition((fileOffsetMs / 1000.0) * sampleRate)
    let framesRemaining = track.file.length - startFrame
    if framesRemaining <= 0 {
      return
    }

    let frameCount = AVAudioFrameCount(framesRemaining)
    state.activePlaybackCount += 1

    track.playerNode.scheduleSegment(
      track.file,
      startingFrame: startFrame,
      frameCount: frameCount,
      at: nil
    ) { [weak self] in
      guard let self = self else { return }
      self.queue.async {
        if state.playbackToken != token {
          return
        }
        state.activePlaybackCount -= 1
        if state.activePlaybackCount <= 0 {
          self.onPlaybackCompleted?()
        }
      }
    }

    let hostTimeDelay = AVAudioTime.hostTime(forSeconds: delayMs / 1000.0)
    let hostTime = baseHostTime + hostTimeDelay
    track.playerNode.play(at: AVAudioTime(hostTime: hostTime))
  }

  private func applyMixing(for track: AudioTrack, soloActive: Bool) {
    let shouldMute = track.muted || (soloActive && !track.solo)
    let volume = shouldMute ? 0.0 : track.volume
    track.playerNode.volume = Float(volume)
    track.playerNode.pan = Float(track.pan)
  }

  private func applyPitchSpeed(for track: AudioTrack, state: EngineState) {
    let combinedPitch = track.pitch + state.globalPitch
    let combinedSpeed = track.speed * state.globalSpeed
    track.timePitch.pitch = Float(combinedPitch * 100.0)
    track.timePitch.rate = Float(combinedSpeed)
  }

  private func isSoloActive(state: EngineState) -> Bool {
    return state.tracks.values.contains { $0.solo }
  }
}
