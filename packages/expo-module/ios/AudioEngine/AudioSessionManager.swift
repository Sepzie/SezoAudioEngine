import AVFoundation

/// Backward-compatibility shim for stale Pod project references.
/// New architecture should use `AudioSessionCoordinator` directly.
final class AudioSessionManager {
  private let coordinator = AudioSessionCoordinator()

  @discardableResult
  func configure(with config: AudioEngineConfig) -> Bool {
    return coordinator.configureForeground(with: config)
  }

  @discardableResult
  func enableBackgroundPlayback(with config: AudioEngineConfig) -> Bool {
    return coordinator.configureBackground(with: config)
  }

  func lastErrorDescription() -> String? {
    return coordinator.lastErrorDescription()
  }

  func deactivate() {
    coordinator.deactivate()
  }
}
