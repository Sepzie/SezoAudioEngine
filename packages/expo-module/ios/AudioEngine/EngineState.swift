import AVFoundation

enum EnginePlaybackState: String {
  case stopped
  case playing
  case paused
  case recording
}

struct RecordingState {
  let url: URL
  let file: AVAudioFile
  let format: String
  let sampleRate: Double
  let channels: Int
  var startTimeMs: Double
  var startTimeSamples: Int64
  var startTimeResolved: Bool
}

final class EngineState {
  var tracks: [String: AudioTrack] = [:]

  var masterVolume: Double = 1.0
  var globalPitch: Double = 0.0
  var globalSpeed: Double = 1.0

  var isPlaying: Bool = false
  var isRecording: Bool = false
  var currentPositionMs: Double = 0.0
  var durationMs: Double = 0.0

  var isInitialized: Bool = false
  var playbackStartHostTime: UInt64?
  var playbackStartPositionMs: Double = 0.0
  var activePlaybackCount: Int = 0
  var playbackToken = UUID()

  var recordingVolume: Double = 1.0
  var recordingState: RecordingState?
  var lastRecordingError: String?
  var inputLevel: Double = 0.0

  var lastEngineError: String?

  var backgroundPlaybackEnabled: Bool = false
  var nowPlayingMetadata: [String: Any] = [:]

  var lastConfig = AudioEngineConfig(dictionary: [:])
}
