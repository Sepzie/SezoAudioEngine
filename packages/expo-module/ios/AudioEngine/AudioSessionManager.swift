import AVFoundation

final class AudioSessionManager {
  private let session = AVAudioSession.sharedInstance()

  func configure(with config: AudioEngineConfig) {
    do {
      try session.setCategory(
        .playAndRecord,
        mode: .default,
        options: [.defaultToSpeaker, .allowBluetooth]
      )
    } catch {
      return
    }

    if let sampleRate = config.sampleRate {
      try? session.setPreferredSampleRate(sampleRate)
    }

    if let bufferSize = config.bufferSize {
      let sampleRate = config.sampleRate ?? session.sampleRate
      if sampleRate > 0 {
        let duration = bufferSize / sampleRate
        try? session.setPreferredIOBufferDuration(duration)
      }
    }

    try? session.setActive(true)
  }

  func deactivate() {
    try? session.setActive(false, options: .notifyOthersOnDeactivation)
  }
}
