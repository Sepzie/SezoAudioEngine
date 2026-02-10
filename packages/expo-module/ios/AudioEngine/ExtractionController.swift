import AVFoundation

final class ExtractionController {
  func extractTrack(
    trackId: String,
    config: [String: Any]?,
    runtime: EngineRuntime,
    state: EngineState,
    ensureInitialized: () -> Void
  ) throws -> [String: Any] {
    guard let track = state.tracks[trackId] else {
      throw AudioEngineError(
        "TRACK_NOT_FOUND",
        "Track not found: \(trackId).",
        details: ["trackId": trackId]
      )
    }

    let durationMs = track.startTimeMs + track.durationMs
    return try renderOffline(
      tracksToRender: [track],
      totalDurationMs: durationMs,
      config: config,
      trackId: trackId,
      runtime: runtime,
      state: state,
      ensureInitialized: ensureInitialized
    )
  }

  func extractAllTracks(
    config: [String: Any]?,
    runtime: EngineRuntime,
    state: EngineState,
    ensureInitialized: () -> Void
  ) throws -> [[String: Any]] {
    if state.tracks.isEmpty {
      throw AudioEngineError(
        "NO_TRACKS_LOADED",
        "Cannot extract without loaded tracks."
      )
    }

    var results: [[String: Any]] = []
    for track in state.tracks.values {
      let durationMs = track.startTimeMs + track.durationMs
      let result = try renderOffline(
        tracksToRender: [track],
        totalDurationMs: durationMs,
        config: config,
        trackId: track.id,
        runtime: runtime,
        state: state,
        ensureInitialized: ensureInitialized
      )
      results.append(result)
    }

    return results
  }

  private func renderOffline(
    tracksToRender: [AudioTrack],
    totalDurationMs: Double,
    config: [String: Any]?,
    trackId: String,
    runtime: EngineRuntime,
    state: EngineState,
    ensureInitialized: () -> Void
  ) throws -> [String: Any] {
    ensureInitialized()
    runtime.stopEngineIfRunning()
    runtime.stopAllPlayers(state: state)

    let engine = runtime.engine
    let renderFormat = engine.mainMixerNode.outputFormat(forBus: 0)
    let formatInfo = resolveRecordingFormat(requestedFormat: config?["format"] as? String ?? "aac")
    let outputURL = resolveOutputURL(
      prefix: "extract-\(trackId)",
      fileExtension: formatInfo.fileExtension,
      outputDir: config?["outputDir"] as? String
    )
    let bitrate = resolveBitrate(config: config)

    let settings: [String: Any]
    if formatInfo.format == "wav" {
      settings = [
        AVFormatIDKey: kAudioFormatLinearPCM,
        AVSampleRateKey: renderFormat.sampleRate,
        AVNumberOfChannelsKey: renderFormat.channelCount,
        AVLinearPCMBitDepthKey: 16,
        AVLinearPCMIsBigEndianKey: false,
        AVLinearPCMIsFloatKey: false
      ]
    } else {
      settings = [
        AVFormatIDKey: kAudioFormatMPEG4AAC,
        AVSampleRateKey: renderFormat.sampleRate,
        AVNumberOfChannelsKey: renderFormat.channelCount,
        AVEncoderBitRateKey: bitrate
      ]
    }

    let includeEffects = config?["includeEffects"] as? Bool ?? true
    let originalState = snapshotTrackState(state: state)
    applyExtractionMixOverrides(
      tracksToRender: tracksToRender,
      includeEffects: includeEffects,
      runtime: runtime,
      state: state
    )

    let errorContext: [String: Any] = [
      "trackId": trackId,
      "format": formatInfo.format,
      "outputUri": outputURL.absoluteString
    ]

    do {
      try engine.enableManualRenderingMode(
        .offline,
        format: renderFormat,
        maximumFrameCount: 4096
      )

      var scheduledTracks: [AudioTrack] = []
      for track in tracksToRender {
        if runtime.scheduleTrackForManualRendering(
          track,
          positionMs: 0.0,
          sampleRate: renderFormat.sampleRate
        ) {
          scheduledTracks.append(track)
        }
      }

      try engine.start()
      for track in scheduledTracks {
        track.playerNode.play()
      }

      let outputFile = try AVAudioFile(forWriting: outputURL, settings: settings)
      let totalFrames = AVAudioFramePosition((totalDurationMs / 1000.0) * renderFormat.sampleRate)

      while engine.manualRenderingSampleTime < totalFrames {
        let remaining = totalFrames - engine.manualRenderingSampleTime
        let frameCount = AVAudioFrameCount(min(Int(4096), Int(remaining)))
        guard let buffer = AVAudioPCMBuffer(
          pcmFormat: renderFormat,
          frameCapacity: frameCount
        ) else {
          throw AudioEngineError(
            "EXTRACTION_FAILED",
            "Failed to allocate render buffer.",
            details: errorContext
          )
        }

        let status = try engine.renderOffline(frameCount, to: buffer)
        switch status {
        case .success:
          try outputFile.write(from: buffer)
        case .insufficientDataFromInputNode:
          continue
        case .cannotDoInCurrentContext:
          continue
        case .error:
          throw AudioEngineError(
            "EXTRACTION_FAILED",
            "Offline render failed.",
            details: errorContext
          )
        @unknown default:
          throw AudioEngineError(
            "EXTRACTION_FAILED",
            "Offline render failed with unknown status.",
            details: errorContext
          )
        }
      }

      engine.stop()
      engine.disableManualRenderingMode()
    } catch {
      if engine.isInManualRenderingMode {
        engine.disableManualRenderingMode()
      }
      restoreTrackState(originalState, runtime: runtime, state: state)
      if let codedError = error as? AudioEngineError {
        throw codedError
      }

      var details = errorContext
      details["nativeError"] = error.localizedDescription
      throw AudioEngineError(
        "EXTRACTION_FAILED",
        "Failed to extract track \(trackId). \(error.localizedDescription)",
        details: details
      )
    }

    restoreTrackState(originalState, runtime: runtime, state: state)
    let fileSize = resolveFileSize(url: outputURL)

    return [
      "trackId": trackId,
      "uri": outputURL.absoluteString,
      "duration": totalDurationMs,
      "format": formatInfo.format,
      "bitrate": bitrate,
      "fileSize": fileSize
    ]
  }

  private func snapshotTrackState(state: EngineState) -> [String: (Double, Double, Bool, Bool, Double, Double)] {
    var snapshot: [String: (Double, Double, Bool, Bool, Double, Double)] = [:]
    for (trackId, track) in state.tracks {
      snapshot[trackId] = (track.volume, track.pan, track.muted, track.solo, track.pitch, track.speed)
    }
    return snapshot
  }

  private func restoreTrackState(
    _ snapshot: [String: (Double, Double, Bool, Bool, Double, Double)],
    runtime: EngineRuntime,
    state: EngineState
  ) {
    for (trackId, values) in snapshot {
      guard let track = state.tracks[trackId] else { continue }
      track.volume = values.0
      track.pan = values.1
      track.muted = values.2
      track.solo = values.3
      track.pitch = values.4
      track.speed = values.5
    }
    runtime.applyMixingForAllTracks(state: state)
    runtime.applyPitchSpeedForAllTracks(state: state)
  }

  private func applyExtractionMixOverrides(
    tracksToRender: [AudioTrack],
    includeEffects: Bool,
    runtime: EngineRuntime,
    state: EngineState
  ) {
    let allowedIds = Set(tracksToRender.map { $0.id })
    for track in state.tracks.values {
      if !allowedIds.contains(track.id) {
        track.volume = 0.0
        track.muted = true
      } else {
        track.muted = false
      }

      if !includeEffects {
        track.pitch = 0.0
        track.speed = 1.0
      }
    }

    runtime.applyMixingForAllTracks(state: state)
    runtime.applyPitchSpeedForAllTracks(state: state)
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
}
