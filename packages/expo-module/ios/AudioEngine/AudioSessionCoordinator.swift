import AVFoundation

final class AudioSessionCoordinator {
  private let session = AVAudioSession.sharedInstance()
  private var lastError: String?
  private var interruptionObserver: NSObjectProtocol?
  private var routeChangeObserver: NSObjectProtocol?

  var onInterruptionBegan: (() -> Void)?
  var onInterruptionEnded: ((Bool) -> Void)?
  var onRouteChanged: ((AVAudioSession.RouteChangeReason) -> Void)?

  init() {
    registerObservers()
  }

  deinit {
    unregisterObservers()
  }

  @discardableResult
  func configureForeground(with config: AudioEngineConfig) -> Bool {
    return configure(
      category: .playAndRecord,
      mode: .default,
      options: [.defaultToSpeaker, .allowBluetoothHFP],
      config: config
    )
  }

  @discardableResult
  func configureBackground(with config: AudioEngineConfig) -> Bool {
    return configure(
      category: .playAndRecord,
      mode: .default,
      options: [.defaultToSpeaker, .allowBluetoothHFP, .allowAirPlay],
      config: config
    )
  }

  func deactivate() {
    try? session.setActive(false, options: .notifyOthersOnDeactivation)
  }

  func lastErrorDescription() -> String? {
    return lastError
  }

  func recordPermission() -> AVAudioSession.RecordPermission {
    return session.recordPermission
  }

  func requestRecordPermission(_ completion: @escaping (Bool) -> Void) {
    session.requestRecordPermission(completion)
  }

  func isInputAvailable() -> Bool {
    return session.isInputAvailable
  }

  func currentSampleRate() -> Double {
    return session.sampleRate
  }

  func inputChannelCount() -> Int {
    return max(1, session.inputNumberOfChannels)
  }

  func categoryRawValue() -> String {
    return session.category.rawValue
  }

  func selectPreferredInput() {
    guard let inputs = session.availableInputs, !inputs.isEmpty else { return }
    if let builtIn = inputs.first(where: { $0.portType == .builtInMic }) {
      try? session.setPreferredInput(builtIn)
    }
  }

  func describeRoute() -> String {
    let inputs = session.currentRoute.inputs.map { "\($0.portType.rawValue):\($0.portName)" }
    let outputs = session.currentRoute.outputs.map { "\($0.portType.rawValue):\($0.portName)" }
    return "inputs[\(inputs.joined(separator: ","))], outputs[\(outputs.joined(separator: ","))]"
  }

  private func registerObservers() {
    let center = NotificationCenter.default
    interruptionObserver = center.addObserver(
      forName: AVAudioSession.interruptionNotification,
      object: session,
      queue: nil
    ) { [weak self] notification in
      self?.handleInterruption(notification)
    }

    routeChangeObserver = center.addObserver(
      forName: AVAudioSession.routeChangeNotification,
      object: session,
      queue: nil
    ) { [weak self] notification in
      self?.handleRouteChange(notification)
    }
  }

  private func unregisterObservers() {
    let center = NotificationCenter.default
    if let observer = interruptionObserver {
      center.removeObserver(observer)
    }
    if let observer = routeChangeObserver {
      center.removeObserver(observer)
    }
    interruptionObserver = nil
    routeChangeObserver = nil
  }

  @discardableResult
  private func configure(
    category: AVAudioSession.Category,
    mode: AVAudioSession.Mode,
    options: AVAudioSession.CategoryOptions,
    config: AudioEngineConfig
  ) -> Bool {
    lastError = nil

    do {
      try session.setCategory(category, mode: mode, options: options)
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

  private func handleInterruption(_ notification: Notification) {
    guard
      let rawType = notification.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
      let type = AVAudioSession.InterruptionType(rawValue: rawType)
    else {
      return
    }

    switch type {
    case .began:
      onInterruptionBegan?()
    case .ended:
      let optionsRaw = notification.userInfo?[AVAudioSessionInterruptionOptionKey] as? UInt ?? 0
      let options = AVAudioSession.InterruptionOptions(rawValue: optionsRaw)
      onInterruptionEnded?(options.contains(.shouldResume))
    @unknown default:
      break
    }
  }

  private func handleRouteChange(_ notification: Notification) {
    guard
      let rawReason = notification.userInfo?[AVAudioSessionRouteChangeReasonKey] as? UInt,
      let reason = AVAudioSession.RouteChangeReason(rawValue: rawReason)
    else {
      return
    }
    onRouteChanged?(reason)
  }
}
