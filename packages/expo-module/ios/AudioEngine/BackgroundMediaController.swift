import AVFoundation
import MediaPlayer
import UIKit

final class BackgroundMediaController {
  var onPlayRequested: (() -> Void)?
  var onPauseRequested: (() -> Void)?
  var onToggleRequested: (() -> Void)?
  var onSeekRequested: ((Double) -> Void)?
  var onSkipRequested: ((Double) -> Void)?

  private var playCommandTarget: Any?
  private var pauseCommandTarget: Any?
  private var toggleCommandTarget: Any?
  private var changePlaybackPositionCommandTarget: Any?
  private var skipForwardCommandTarget: Any?
  private var skipBackwardCommandTarget: Any?

  func enable(metadata: [String: Any], state: EngineState) {
    state.backgroundPlaybackEnabled = true
    state.nowPlayingMetadata.merge(metadata) { _, new in new }
    setRemoteControlEventsEnabled(true)
    configureRemoteCommandsIfNeeded()
    updateNowPlayingInfo(
      state: state,
      positionMs: state.isPlaying ? currentPlaybackPositionMs(state: state) : state.currentPositionMs,
      durationMs: state.durationMs,
      isPlaying: state.isPlaying
    )
  }

  func updateMetadata(_ metadata: [String: Any], state: EngineState) {
    state.nowPlayingMetadata.merge(metadata) { _, new in new }
    updateNowPlayingInfo(
      state: state,
      positionMs: state.isPlaying ? currentPlaybackPositionMs(state: state) : state.currentPositionMs,
      durationMs: state.durationMs,
      isPlaying: state.isPlaying
    )
  }

  func updateNowPlayingInfo(state: EngineState, positionMs: Double, durationMs: Double, isPlaying: Bool) {
    guard state.backgroundPlaybackEnabled else { return }

    var info: [String: Any] = [:]
    if let title = state.nowPlayingMetadata["title"] as? String {
      info[MPMediaItemPropertyTitle] = title
    }
    if let artist = state.nowPlayingMetadata["artist"] as? String {
      info[MPMediaItemPropertyArtist] = artist
    }
    if let album = state.nowPlayingMetadata["album"] as? String {
      info[MPMediaItemPropertyAlbumTitle] = album
    }
    if let artwork = resolveArtwork(metadata: state.nowPlayingMetadata) {
      info[MPMediaItemPropertyArtwork] = artwork
    }

    info[MPMediaItemPropertyPlaybackDuration] = durationMs / 1000.0
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = positionMs / 1000.0
    info[MPNowPlayingInfoPropertyPlaybackRate] = isPlaying ? 1.0 : 0.0

    MPNowPlayingInfoCenter.default().nowPlayingInfo = info
    if #available(iOS 13.0, *) {
      if isPlaying {
        MPNowPlayingInfoCenter.default().playbackState = .playing
      } else if positionMs > 0 {
        MPNowPlayingInfoCenter.default().playbackState = .paused
      } else {
        MPNowPlayingInfoCenter.default().playbackState = .stopped
      }
    }

    updateRemoteCommandStates(state: state)
  }

  func updateRemoteCommandStates(state: EngineState) {
    let commandCenter = MPRemoteCommandCenter.shared()
    let hasTracks = !state.tracks.isEmpty
    let hasSeekableDuration = state.durationMs > 0
    let seekStepSeconds = max(1.0, resolveSeekStepMs(state: state) / 1000.0)

    commandCenter.playCommand.isEnabled = state.backgroundPlaybackEnabled && hasTracks && !state.isPlaying
    commandCenter.pauseCommand.isEnabled = state.backgroundPlaybackEnabled && hasTracks && state.isPlaying
    commandCenter.togglePlayPauseCommand.isEnabled = state.backgroundPlaybackEnabled && hasTracks
    commandCenter.changePlaybackPositionCommand.isEnabled = state.backgroundPlaybackEnabled && hasTracks && hasSeekableDuration
    commandCenter.skipForwardCommand.preferredIntervals = [NSNumber(value: seekStepSeconds)]
    commandCenter.skipBackwardCommand.preferredIntervals = [NSNumber(value: seekStepSeconds)]
    commandCenter.skipForwardCommand.isEnabled = state.backgroundPlaybackEnabled && hasTracks && hasSeekableDuration
    commandCenter.skipBackwardCommand.isEnabled = state.backgroundPlaybackEnabled && hasTracks && hasSeekableDuration
  }

  func disable(state: EngineState) {
    state.backgroundPlaybackEnabled = false
    state.nowPlayingMetadata.removeAll()

    removeRemoteCommands()
    MPNowPlayingInfoCenter.default().nowPlayingInfo = nil
    if #available(iOS 13.0, *) {
      MPNowPlayingInfoCenter.default().playbackState = .stopped
    }
    setRemoteControlEventsEnabled(false)
  }

  private func configureRemoteCommandsIfNeeded() {
    guard playCommandTarget == nil else { return }
    let commandCenter = MPRemoteCommandCenter.shared()

    playCommandTarget = commandCenter.playCommand.addTarget { [weak self] _ in
      self?.onPlayRequested?()
      return .success
    }
    pauseCommandTarget = commandCenter.pauseCommand.addTarget { [weak self] _ in
      self?.onPauseRequested?()
      return .success
    }
    toggleCommandTarget = commandCenter.togglePlayPauseCommand.addTarget { [weak self] _ in
      self?.onToggleRequested?()
      return .success
    }
    changePlaybackPositionCommandTarget = commandCenter.changePlaybackPositionCommand.addTarget { [weak self] event in
      guard let positionEvent = event as? MPChangePlaybackPositionCommandEvent else {
        return .commandFailed
      }
      self?.onSeekRequested?(max(0.0, positionEvent.positionTime * 1000.0))
      return .success
    }
    skipForwardCommandTarget = commandCenter.skipForwardCommand.addTarget { [weak self] event in
      let seconds = (event as? MPSkipIntervalCommandEvent)?.interval ?? 15.0
      self?.onSkipRequested?(max(0.0, seconds) * 1000.0)
      return .success
    }
    skipBackwardCommandTarget = commandCenter.skipBackwardCommand.addTarget { [weak self] event in
      let seconds = (event as? MPSkipIntervalCommandEvent)?.interval ?? 15.0
      self?.onSkipRequested?(-max(0.0, seconds) * 1000.0)
      return .success
    }
  }

  private func removeRemoteCommands() {
    let commandCenter = MPRemoteCommandCenter.shared()

    if let target = playCommandTarget {
      commandCenter.playCommand.removeTarget(target)
    }
    if let target = pauseCommandTarget {
      commandCenter.pauseCommand.removeTarget(target)
    }
    if let target = toggleCommandTarget {
      commandCenter.togglePlayPauseCommand.removeTarget(target)
    }
    if let target = changePlaybackPositionCommandTarget {
      commandCenter.changePlaybackPositionCommand.removeTarget(target)
    }
    if let target = skipForwardCommandTarget {
      commandCenter.skipForwardCommand.removeTarget(target)
    }
    if let target = skipBackwardCommandTarget {
      commandCenter.skipBackwardCommand.removeTarget(target)
    }

    playCommandTarget = nil
    pauseCommandTarget = nil
    toggleCommandTarget = nil
    changePlaybackPositionCommandTarget = nil
    skipForwardCommandTarget = nil
    skipBackwardCommandTarget = nil
  }

  private func resolveSeekStepMs(state: EngineState) -> Double {
    guard let playbackCard = state.nowPlayingMetadata["playbackCard"] as? [String: Any] else {
      return 15000.0
    }
    if let seekStepMs = playbackCard["seekStepMs"] as? NSNumber {
      return max(1000.0, seekStepMs.doubleValue)
    }
    return 15000.0
  }

  private func currentPlaybackPositionMs(state: EngineState) -> Double {
    guard let startHostTime = state.playbackStartHostTime else {
      return state.currentPositionMs
    }

    let nowSeconds = AVAudioTime.seconds(forHostTime: mach_absolute_time())
    let startSeconds = AVAudioTime.seconds(forHostTime: startHostTime)
    let elapsedMs = max(0.0, (nowSeconds - startSeconds) * 1000.0)
    let position = state.playbackStartPositionMs + elapsedMs
    return min(position, state.durationMs)
  }

  private func setRemoteControlEventsEnabled(_ enabled: Bool) {
    let handler = {
      if enabled {
        UIApplication.shared.beginReceivingRemoteControlEvents()
      } else {
        UIApplication.shared.endReceivingRemoteControlEvents()
      }
    }

    if Thread.isMainThread {
      handler()
    } else {
      DispatchQueue.main.async(execute: handler)
    }
  }

  private func resolveArtwork(metadata: [String: Any]) -> MPMediaItemArtwork? {
    guard let artworkValue = metadata["artwork"] as? String else {
      return nil
    }

    let url: URL?
    if artworkValue.hasPrefix("file://") {
      url = URL(string: artworkValue)
    } else if artworkValue.hasPrefix("/") {
      url = URL(fileURLWithPath: artworkValue)
    } else {
      url = nil
    }

    guard let imageURL = url,
          let data = try? Data(contentsOf: imageURL),
          let image = UIImage(data: data) else {
      return nil
    }

    return MPMediaItemArtwork(boundsSize: image.size) { _ in image }
  }
}
