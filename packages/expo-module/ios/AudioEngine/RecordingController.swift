import AVFoundation

final class RecordingController {
  private let queue: DispatchQueue
  private let recordingMixer = AVAudioMixerNode()
  private var recordingMixerConnected = false
  private var recordingTapInstalled = false

  init(queue: DispatchQueue) {
    self.queue = queue
  }

  @discardableResult
  func prepareIfNeeded(
    runtime: EngineRuntime,
    sessionCoordinator: AudioSessionCoordinator,
    config: AudioEngineConfig,
    state: EngineState
  ) -> Bool {
    return ensureRecordingMixerConnected(
      engine: runtime.engine,
      sampleRate: sessionCoordinator.currentSampleRate(),
      channels: sessionCoordinator.inputChannelCount(),
      config: config,
      sessionCoordinator: sessionCoordinator,
      state: state
    )
  }

  @discardableResult
  func startRecording(
    config: [String: Any]?,
    runtime: EngineRuntime,
    sessionCoordinator: AudioSessionCoordinator,
    eventEmitter: EngineEventEmitter,
    state: EngineState
  ) -> Bool {
    guard !state.isRecording else { return true }

    state.lastRecordingError = nil

    let permission = sessionCoordinator.recordPermission()
    if permission != .granted {
      if permission == .undetermined {
        requestRecordPermission(sessionCoordinator: sessionCoordinator)
        state.lastRecordingError = "microphone permission not determined"
      } else {
        state.lastRecordingError = "microphone permission denied"
      }
      return false
    }

    let configured: Bool
    if state.backgroundPlaybackEnabled {
      configured = sessionCoordinator.configureBackground(with: state.lastConfig)
    } else {
      configured = sessionCoordinator.configureForeground(with: state.lastConfig)
    }

    if !configured {
      state.lastRecordingError = sessionCoordinator.lastErrorDescription() ?? "audio session configuration failed"
      return false
    }

    if !sessionCoordinator.isInputAvailable() {
      state.lastRecordingError = "audio input not available (category=\(sessionCoordinator.categoryRawValue()), route=\(sessionCoordinator.describeRoute()))"
      return false
    }

    guard runtime.startEngineIfNeeded(state: state, sessionCoordinator: sessionCoordinator, eventEmitter: eventEmitter) else {
      state.lastRecordingError = state.lastEngineError ?? "audio engine failed to start"
      return false
    }

    let sampleRate = sessionCoordinator.currentSampleRate()
    let channels = sessionCoordinator.inputChannelCount()
    if !ensureRecordingMixerConnected(
      engine: runtime.engine,
      sampleRate: sampleRate,
      channels: channels,
      config: state.lastConfig,
      sessionCoordinator: sessionCoordinator,
      state: state
    ) {
      return false
    }

    let tapFormat = recordingMixer.outputFormat(forBus: 0)
    guard tapFormat.sampleRate > 0, tapFormat.channelCount > 0 else {
      state.lastRecordingError = "input format invalid (sampleRate=\(tapFormat.sampleRate), channels=\(tapFormat.channelCount), sessionRate=\(sampleRate), sessionChannels=\(channels), route=\(sessionCoordinator.describeRoute()))"
      return false
    }

    let requestedFormat = config?["format"] as? String ?? "aac"
    let bitrate = resolveBitrate(config: config)
    let formatInfo = resolveRecordingFormat(requestedFormat: requestedFormat)
    let outputURL = resolveOutputURL(
      prefix: "recording",
      fileExtension: formatInfo.fileExtension,
      outputDir: config?["outputDir"] as? String
    )

    let settings: [String: Any]
    if formatInfo.format == "wav" {
      settings = [
        AVFormatIDKey: kAudioFormatLinearPCM,
        AVSampleRateKey: sampleRate,
        AVNumberOfChannelsKey: channels,
        AVLinearPCMBitDepthKey: 16,
        AVLinearPCMIsBigEndianKey: false,
        AVLinearPCMIsFloatKey: false
      ]
    } else {
      settings = [
        AVFormatIDKey: kAudioFormatMPEG4AAC,
        AVSampleRateKey: sampleRate,
        AVNumberOfChannelsKey: channels,
        AVEncoderBitRateKey: bitrate
      ]
    }

    do {
      let file = try AVAudioFile(forWriting: outputURL, settings: settings)

      let startTimeMs: Double
      let startTimeSamples: Int64
      let startTimeResolved: Bool

      if state.isPlaying {
        startTimeMs = runtime.currentPlaybackPositionMs(state: state)
        startTimeSamples = Int64(startTimeMs / 1000.0 * sampleRate)
        startTimeResolved = false
      } else {
        startTimeSamples = 0
        startTimeMs = 0.0
        startTimeResolved = true
      }

      state.recordingState = RecordingState(
        url: outputURL,
        file: file,
        format: formatInfo.format,
        sampleRate: sampleRate,
        channels: channels,
        startTimeMs: startTimeMs,
        startTimeSamples: startTimeSamples,
        startTimeResolved: startTimeResolved
      )

      state.isRecording = true
      return true
    } catch {
      state.lastRecordingError = "recording setup failed: \(error.localizedDescription)"
      state.recordingState = nil
      return false
    }
  }

  func stopRecording(state: EngineState) -> [String: Any] {
    guard let activeRecording = state.recordingState else {
      state.isRecording = false
      return [
        "uri": "",
        "duration": 0,
        "startTimeMs": 0,
        "sampleRate": 44100,
        "channels": 1,
        "format": "aac",
        "fileSize": 0
      ]
    }

    stopRecordingInternal(state: state)

    let durationMs = (Double(activeRecording.file.length) / activeRecording.sampleRate) * 1000.0
    let fileSize = resolveFileSize(url: activeRecording.url)

    return [
      "uri": activeRecording.url.absoluteString,
      "duration": durationMs,
      "startTimeMs": activeRecording.startTimeMs,
      "startTimeSamples": activeRecording.startTimeSamples,
      "sampleRate": activeRecording.sampleRate,
      "channels": activeRecording.channels,
      "format": activeRecording.format,
      "fileSize": fileSize
    ]
  }

  func stopRecordingInternal(state: EngineState) {
    state.recordingState = nil
    state.isRecording = false
  }

  func setRecordingVolume(_ volume: Double, state: EngineState) {
    state.recordingVolume = volume
  }

  func getLastRecordingError(state: EngineState) -> String? {
    return state.lastRecordingError
  }

  func getInputLevel(state: EngineState) -> Double {
    return state.inputLevel
  }

  func detach(runtime: EngineRuntime) {
    detachRecordingMixer(engine: runtime.engine)
  }

  @discardableResult
  private func ensureRecordingMixerConnected(
    engine: AVAudioEngine,
    sampleRate: Double,
    channels: Int,
    config: AudioEngineConfig,
    sessionCoordinator: AudioSessionCoordinator,
    state: EngineState
  ) -> Bool {
    if recordingMixerConnected {
      return true
    }

    if engine.isRunning {
      state.lastRecordingError = "recording mixer unavailable while engine is running"
      return false
    }

    if !sessionCoordinator.isInputAvailable() {
      state.lastRecordingError = "audio input not available (category=\(sessionCoordinator.categoryRawValue()), route=\(sessionCoordinator.describeRoute()))"
      return false
    }

    sessionCoordinator.selectPreferredInput()

    let inputFormat = engine.inputNode.inputFormat(forBus: 0)
    let inputSampleRate = inputFormat.sampleRate
    let inputChannels = Int(inputFormat.channelCount)

    guard inputSampleRate > 0, inputChannels > 0 else {
      state.lastRecordingError = "input hw format invalid (sampleRate=\(inputSampleRate), channels=\(inputChannels))"
      return false
    }

    let resolvedRate = sampleRate > 0 ? sampleRate : (config.sampleRate ?? inputSampleRate)
    let resolvedChannels = channels > 0 ? channels : inputChannels

    let format: AVAudioFormat
    if resolvedRate == inputSampleRate && resolvedChannels == inputChannels {
      format = inputFormat
    } else if let preferredFormat = AVAudioFormat(
      standardFormatWithSampleRate: resolvedRate,
      channels: AVAudioChannelCount(resolvedChannels)
    ) {
      format = preferredFormat
    } else {
      state.lastRecordingError = "recording format invalid (sampleRate=\(resolvedRate), channels=\(resolvedChannels))"
      return false
    }

    engine.attach(recordingMixer)
    engine.connect(engine.inputNode, to: recordingMixer, format: format)
    engine.connect(recordingMixer, to: engine.mainMixerNode, format: format)
    recordingMixer.volume = 0.0

    if !recordingTapInstalled {
      recordingMixer.installTap(onBus: 0, bufferSize: 1024, format: format) { [weak self] buffer, _ in
        guard let self = self else { return }
        self.queue.async {
          guard let activeRecording = state.recordingState else { return }
          self.applyRecordingGain(buffer: buffer, gain: state.recordingVolume)
          self.updateInputLevel(buffer: buffer, state: state)
          do {
            try activeRecording.file.write(from: buffer)
          } catch {
            state.lastRecordingError = "recording write failed: \(error.localizedDescription)"
          }
        }
      }
      recordingTapInstalled = true
    }

    recordingMixerConnected = true
    return true
  }

  private func detachRecordingMixer(engine: AVAudioEngine) {
    if !recordingMixerConnected {
      return
    }

    if recordingTapInstalled {
      recordingMixer.removeTap(onBus: 0)
      recordingTapInstalled = false
    }

    engine.disconnectNodeInput(recordingMixer)
    engine.disconnectNodeOutput(recordingMixer)
    engine.detach(recordingMixer)
    recordingMixerConnected = false
  }

  private func requestRecordPermission(sessionCoordinator: AudioSessionCoordinator) {
    let requestBlock = {
      sessionCoordinator.requestRecordPermission { _ in }
    }

    if Thread.isMainThread {
      requestBlock()
    } else {
      DispatchQueue.main.async(execute: requestBlock)
    }
  }

  private func resolveRecordingFormat(requestedFormat: String) -> (format: String, fileExtension: String) {
    if requestedFormat == "wav" {
      return ("wav", "wav")
    }
    if requestedFormat == "m4a" {
      return ("m4a", "m4a")
    }
    if requestedFormat == "mp3" {
      return ("aac", "m4a")
    }
    return ("aac", "m4a")
  }

  private func resolveBitrate(config: [String: Any]?) -> Int {
    if let bitrate = config?["bitrate"] as? Int {
      return bitrate
    }
    if let quality = config?["quality"] as? String {
      switch quality {
      case "low":
        return 64_000
      case "high":
        return 192_000
      default:
        return 128_000
      }
    }
    return 128_000
  }

  private func resolveOutputURL(prefix: String, fileExtension: String, outputDir: String?) -> URL {
    let baseURL: URL
    if let outputDir = outputDir {
      baseURL = URL(fileURLWithPath: outputDir, isDirectory: true)
    } else {
      baseURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("sezo-audio-output", isDirectory: true)
    }

    try? FileManager.default.createDirectory(at: baseURL, withIntermediateDirectories: true)
    let filename = "\(prefix)-\(UUID().uuidString).\(fileExtension)"
    return baseURL.appendingPathComponent(filename)
  }

  private func resolveFileSize(url: URL) -> Int {
    let path = url.path
    let attributes = try? FileManager.default.attributesOfItem(atPath: path)
    return attributes?[.size] as? Int ?? 0
  }

  private func applyRecordingGain(buffer: AVAudioPCMBuffer, gain: Double) {
    let resolvedGain = Float(gain)
    if resolvedGain == 1.0 {
      return
    }

    let frameLength = Int(buffer.frameLength)
    let channelCount = Int(buffer.format.channelCount)

    switch buffer.format.commonFormat {
    case .pcmFormatFloat32:
      guard let channelData = buffer.floatChannelData else { return }
      for channel in 0..<channelCount {
        let samples = channelData[channel]
        for i in 0..<frameLength {
          samples[i] *= resolvedGain
        }
      }
    case .pcmFormatInt16:
      guard let channelData = buffer.int16ChannelData else { return }
      for channel in 0..<channelCount {
        let samples = channelData[channel]
        for i in 0..<frameLength {
          let scaled = Float(samples[i]) * resolvedGain
          let clamped = max(Float(Int16.min), min(Float(Int16.max), scaled))
          samples[i] = Int16(clamped)
        }
      }
    default:
      break
    }
  }

  private func updateInputLevel(buffer: AVAudioPCMBuffer, state: EngineState) {
    guard let channelData = buffer.floatChannelData else { return }
    let frameLength = Int(buffer.frameLength)
    if frameLength == 0 {
      return
    }

    let samples = channelData[0]
    var sum: Float = 0.0
    for i in 0..<frameLength {
      let value = samples[i]
      sum += value * value
    }

    state.inputLevel = Double(sqrt(sum / Float(frameLength)))
  }
}
