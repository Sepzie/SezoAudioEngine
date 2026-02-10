import ExpoModulesCore
import Foundation

public class ExpoAudioEngineModule: Module {
  private let engine = AudioEngineFacade()

  public func definition() -> ModuleDefinition {
    Name("ExpoAudioEngineModule")
    OnCreate {
      emitDebugLog(level: "info", message: "ExpoAudioEngineModule created")
      engine.onPlaybackStateChange = { [weak self] state, positionMs, durationMs in
        DispatchQueue.main.async {
          self?.sendEvent(
            "playbackStateChange",
            [
              "state": state,
              "positionMs": positionMs,
              "durationMs": durationMs
            ]
          )
          self?.sendEvent(
            "positionUpdate",
            [
              "positionMs": positionMs,
              "durationMs": durationMs
            ]
          )
        }
      }
      engine.onPlaybackComplete = { [weak self] positionMs, durationMs in
        DispatchQueue.main.async {
          self?.sendEvent(
            "playbackComplete",
            [
              "positionMs": positionMs,
              "durationMs": durationMs
            ]
          )
        }
      }
      engine.onError = { [weak self] code, message, details in
        DispatchQueue.main.async {
          self?.emitEngineError(code: code, message: message, details: details)
        }
      }
    }
    OnDestroy {
      engine.onPlaybackStateChange = nil
      engine.onPlaybackComplete = nil
      engine.onError = nil
      emitDebugLog(level: "info", message: "ExpoAudioEngineModule destroyed")
    }

    AsyncFunction("initialize") { (config: [String: Any]) in
      engine.initialize(config: config)
      emitEngineStateChanged(reason: "initialized")
      emitDebugLog(level: "info", message: "Audio engine initialized", context: ["config": config])
    }
    AsyncFunction("release") {
      engine.releaseResources()
      emitEngineStateChanged(reason: "released")
      emitDebugLog(level: "info", message: "Audio engine released")
    }
    AsyncFunction("loadTracks") { (tracks: [[String: Any]]) throws in
      if let failure = engine.loadTracks(tracks) {
        let failedCount = failure["failedCount"] as? Int ?? 0
        let message = failedCount > 0
          ? "Failed to load \(failedCount) track(s)."
          : "Failed to load tracks."
        emitEngineError(code: "TRACK_LOAD_FAILED", message: message, details: failure, source: "engine")
        throw AudioEngineError("TRACK_LOAD_FAILED", message, details: failure)
      }
      for track in tracks {
        if let trackId = track["id"] as? String {
          sendEvent("trackLoaded", ["trackId": trackId])
        }
      }
    }
    AsyncFunction("unloadTrack") { (trackId: String) in
      engine.unloadTrack(trackId)
      sendEvent("trackUnloaded", ["trackId": trackId])
    }
    AsyncFunction("unloadAllTracks") {
      let loadedTrackIds = engine.getLoadedTracks().compactMap { $0["id"] as? String }
      engine.unloadAllTracks()
      for trackId in loadedTrackIds {
        sendEvent("trackUnloaded", ["trackId": trackId])
      }
    }
    Function("getLoadedTracks") {
      return engine.getLoadedTracks()
    }

    Function("play") {
      engine.play()
    }
    Function("pause") {
      engine.pause()
    }
    Function("stop") {
      engine.stop()
    }
    Function("seek") { (positionMs: Double) in
      engine.seek(positionMs: positionMs)
      sendEvent(
        "positionUpdate",
        [
          "positionMs": engine.getCurrentPosition(),
          "durationMs": engine.getDuration()
        ]
      )
    }
    Function("isPlaying") {
      return engine.isPlaying()
    }
    Function("getCurrentPosition") {
      return engine.getCurrentPosition()
    }
    Function("getDuration") {
      return engine.getDuration()
    }

    Function("setTrackVolume") { (trackId: String, volume: Double) in
      engine.setTrackVolume(trackId: trackId, volume: volume)
    }
    Function("setTrackMuted") { (trackId: String, muted: Bool) in
      engine.setTrackMuted(trackId: trackId, muted: muted)
    }
    Function("setTrackSolo") { (trackId: String, solo: Bool) in
      engine.setTrackSolo(trackId: trackId, solo: solo)
    }
    Function("setTrackPan") { (trackId: String, pan: Double) in
      engine.setTrackPan(trackId: trackId, pan: pan)
    }
    Function("setTrackPitch") { (trackId: String, semitones: Double) in
      engine.setTrackPitch(trackId: trackId, semitones: semitones)
    }
    Function("getTrackPitch") { (trackId: String) in
      return engine.getTrackPitch(trackId: trackId)
    }
    Function("setTrackSpeed") { (trackId: String, rate: Double) in
      engine.setTrackSpeed(trackId: trackId, rate: rate)
    }
    Function("getTrackSpeed") { (trackId: String) in
      return engine.getTrackSpeed(trackId: trackId)
    }

    Function("setMasterVolume") { (volume: Double) in
      engine.setMasterVolume(volume)
    }
    Function("getMasterVolume") {
      return engine.getMasterVolume()
    }

    Function("setPitch") { (semitones: Double) in
      engine.setPitch(semitones)
    }
    Function("getPitch") {
      return engine.getPitch()
    }
    Function("setSpeed") { (rate: Double) in
      engine.setSpeed(rate)
    }
    Function("getSpeed") {
      return engine.getSpeed()
    }
    Function("setTempoAndPitch") { (tempo: Double, pitch: Double) in
      engine.setTempoAndPitch(tempo: tempo, pitch: pitch)
    }

    AsyncFunction("startRecording") { (config: [String: Any]?) throws in
      let didStart = engine.startRecording(config: config)
      if !didStart {
        let detail = engine.getLastRecordingError() ?? "audio input format is invalid or the audio session is not ready"
        let message = "Failed to start recording. \(detail)."
        emitEngineError(
          code: "RECORDING_START_FAILED",
          message: message,
          details: ["detail": detail],
          source: "recording"
        )
        throw AudioEngineError("RECORDING_START_FAILED", message, details: ["detail": detail])
      }
      sendEvent("recordingStarted", [String: Any]())
      emitEngineStateChanged(reason: "recordingStarted")
    }
    AsyncFunction("stopRecording") {
      let result = engine.stopRecording()
      sendEvent("recordingStopped", result)
      emitEngineStateChanged(reason: "recordingStopped")
      return result
    }
    Function("isRecording") {
      return engine.isRecording()
    }
    Function("setRecordingVolume") { (volume: Double) in
      engine.setRecordingVolume(volume)
    }

    AsyncFunction("extractTrack") { (trackId: String, config: [String: Any]?) throws in
      do {
        let result = try engine.extractTrack(trackId: trackId, config: config)
        sendEvent("extractionProgress", ["trackId": trackId, "progress": 1.0, "operation": "track"])
        var completionPayload: [String: Any] = [
          "success": true,
          "trackId": trackId
        ]
        for (key, value) in result {
          completionPayload[key] = value
        }
        sendEvent("extractionComplete", completionPayload)
        return result
      } catch let error as AudioEngineError {
        emitEngineError(
          code: error.code,
          message: error.description,
          details: error.details,
          source: "extraction"
        )
        sendEvent(
          "extractionComplete",
          [
            "success": false,
            "trackId": trackId,
            "errorMessage": error.description
          ]
        )
        throw error
      }
    }

    AsyncFunction("extractAllTracks") { (config: [String: Any]?) throws in
      do {
        let results = try engine.extractAllTracks(config: config)
        sendEvent("extractionProgress", ["progress": 1.0, "operation": "mix"])
        sendEvent(
          "extractionComplete",
          [
            "success": true,
            "operation": "mix",
            "count": results.count
          ]
        )
        return results
      } catch let error as AudioEngineError {
        emitEngineError(
          code: error.code,
          message: error.description,
          details: error.details,
          source: "extraction"
        )
        sendEvent(
          "extractionComplete",
          [
            "success": false,
            "operation": "mix",
            "errorMessage": error.description
          ]
        )
        throw error
      }
    }

    Function("cancelExtraction") { (jobId: Double?) in
      return engine.cancelExtraction(jobId: jobId)
    }

    Function("getInputLevel") {
      return engine.getInputLevel()
    }
    Function("getOutputLevel") {
      return engine.getOutputLevel()
    }
    Function("getTrackLevel") { (trackId: String) in
      return engine.getTrackLevel(trackId: trackId)
    }

    AsyncFunction("enableBackgroundPlayback") { (metadata: [String: Any]) in
      engine.enableBackgroundPlayback(metadata: metadata)
      emitEngineStateChanged(reason: "backgroundPlaybackEnabled")
    }
    Function("updateNowPlayingInfo") { (metadata: [String: Any]) in
      engine.updateNowPlayingInfo(metadata: metadata)
    }
    AsyncFunction("disableBackgroundPlayback") {
      engine.disableBackgroundPlayback()
      emitEngineStateChanged(reason: "backgroundPlaybackDisabled")
    }

    Events(
      "playbackStateChange",
      "positionUpdate",
      "playbackComplete",
      "trackLoaded",
      "trackUnloaded",
      "recordingStarted",
      "recordingStopped",
      "extractionProgress",
      "extractionComplete",
      "engineStateChanged",
      "error",
      "debugLog"
    )
  }

  private func emitEngineStateChanged(reason: String, payload: [String: Any] = [:]) {
    var statePayload = payload
    statePayload["reason"] = reason
    sendEvent("engineStateChanged", statePayload)
  }

  private func emitDebugLog(level: String, message: String, context: [String: Any]? = nil) {
    var payload: [String: Any] = [
      "level": level,
      "message": message
    ]
    if let context = context {
      payload["context"] = context
    }
    sendEvent("debugLog", payload)
  }

  private func emitEngineError(
    code: String,
    message: String,
    details: [String: Any]? = nil,
    source: String? = nil
  ) {
    let sourceValue = source ?? resolveErrorSource(code: code)
    let classification = classifyError(code: code)
    var payload: [String: Any] = [
      "code": code,
      "message": message,
      "severity": classification.severity,
      "recoverable": classification.recoverable,
      "source": sourceValue,
      "timestampMs": Int(Date().timeIntervalSince1970 * 1000.0),
      "platform": "ios"
    ]
    if let details = details {
      payload["details"] = details
    }
    sendEvent("error", payload)
    emitDebugLog(level: classification.severity == "fatal" ? "error" : "warn", message: message, context: payload)
  }

  private func resolveErrorSource(code: String) -> String {
    if code == "AUDIO_SESSION_FAILED" {
      return "session"
    }
    if code == "AUDIO_FOCUS_DENIED" {
      return "focus"
    }
    if code.hasPrefix("PLAYBACK_") || code == "NO_TRACKS_LOADED" {
      return "playback"
    }
    if code.hasPrefix("RECORDING_") {
      return "recording"
    }
    if code.hasPrefix("EXTRACTION_") {
      return "extraction"
    }
    if code.hasPrefix("ENGINE_") || code.hasPrefix("TRACK_") || code == "STREAM_DISCONNECTED" {
      return "engine"
    }
    return "system"
  }

  private func classifyError(code: String) -> (severity: String, recoverable: Bool) {
    let fatalCodes: Set<String> = [
      "ENGINE_INIT_FAILED",
      "ENGINE_START_FAILED",
      "AUDIO_SESSION_FAILED",
      "STREAM_DISCONNECTED"
    ]
    if fatalCodes.contains(code) {
      return ("fatal", false)
    }
    return ("warning", true)
  }
}
