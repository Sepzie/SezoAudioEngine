import AVFoundation

final class AudioSessionManager {
  private let session = AVAudioSession.sharedInstance()
  private var lastError: String?

  @discardableResult
  func configure(with config: AudioEngineConfig) -> Bool {
    lastError = nil
    do {
      try session.setCategory(
        .playAndRecord,
        mode: .default,
        options: [.defaultToSpeaker, .allowBluetooth]
      )
    } catch {
      lastError = "setCategory failed: \(error.localizedDescription)"
      return false
    }

    if let sampleRate = config.sampleRate {
      do {
        try session.setPreferredSampleRate(sampleRate)
      } catch {
        lastError = "setPreferredSampleRate failed: \(error.localizedDescription)"
      }
    }

    if let bufferSize = config.bufferSize {
      let sampleRate = config.sampleRate ?? session.sampleRate
      if sampleRate > 0 {
        let duration = bufferSize / sampleRate
        do {
          try session.setPreferredIOBufferDuration(duration)
        } catch {
          lastError = "setPreferredIOBufferDuration failed: \(error.localizedDescription)"
        }
      }
    }

    do {
      try session.setActive(true)
    } catch {
      lastError = "setActive failed: \(error.localizedDescription)"
      return false
    }
    return true
  }

  @discardableResult
  func enableBackgroundPlayback(with config: AudioEngineConfig) -> Bool {
    lastError = nil
    do {
      try session.setCategory(
        .playback,
        mode: .default,
        options: [.allowBluetooth, .allowAirPlay]
      )
    } catch {
      lastError = "setCategory failed: \(error.localizedDescription)"
      return false
    }

    if let sampleRate = config.sampleRate {
      do {
        try session.setPreferredSampleRate(sampleRate)
      } catch {
        lastError = "setPreferredSampleRate failed: \(error.localizedDescription)"
      }
    }

    if let bufferSize = config.bufferSize {
      let sampleRate = config.sampleRate ?? session.sampleRate
      if sampleRate > 0 {
        let duration = bufferSize / sampleRate
        do {
          try session.setPreferredIOBufferDuration(duration)
        } catch {
          lastError = "setPreferredIOBufferDuration failed: \(error.localizedDescription)"
        }
      }
    }

    do {
      try session.setActive(true)
    } catch {
      lastError = "setActive failed: \(error.localizedDescription)"
      return false
    }
    return true
  }

  func lastErrorDescription() -> String? {
    return lastError
  }

  func deactivate() {
    try? session.setActive(false, options: .notifyOthersOnDeactivation)
  }
}
