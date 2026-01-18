import AVFoundation

final class AudioTrack {
  let id: String
  let uri: String
  let url: URL
  let file: AVAudioFile
  let playerNode: AVAudioPlayerNode
  let timePitch: AVAudioUnitTimePitch
  let durationMs: Double
  var startTimeMs: Double
  var volume: Double
  var pan: Double
  var muted: Bool
  var solo: Bool
  var pitch: Double
  var speed: Double
  var isAttached: Bool

  init?(input: [String: Any]) {
    guard let id = input["id"] as? String,
          let uri = input["uri"] as? String,
          let url = AudioTrack.resolveURL(uri: uri) else {
      return nil
    }

    do {
      let file = try AVAudioFile(forReading: url)
      let sampleRate = file.processingFormat.sampleRate
      let durationMs = sampleRate > 0 ? (Double(file.length) / sampleRate) * 1000.0 : 0.0

      self.id = id
      self.uri = uri
      self.url = url
      self.file = file
      self.durationMs = durationMs
      self.startTimeMs = input["startTimeMs"] as? Double ?? 0.0
      self.volume = input["volume"] as? Double ?? 1.0
      self.pan = input["pan"] as? Double ?? 0.0
      self.muted = input["muted"] as? Bool ?? false
      self.solo = input["solo"] as? Bool ?? false
      self.pitch = 0.0
      self.speed = 1.0
      self.playerNode = AVAudioPlayerNode()
      self.timePitch = AVAudioUnitTimePitch()
      self.isAttached = false

      // DEBUG: Log track loading details
      print("🎵 [TRACK LOAD DEBUG] Track '\(id)' loaded:")
      print("  📍 startTimeMs: \(self.startTimeMs)ms")
      print("  ⏱️  duration: \(durationMs)ms")
      print("  📄 uri: \(uri)")
    } catch {
      return nil
    }
  }

  func asDictionary() -> [String: Any] {
    return [
      "id": id,
      "uri": uri,
      "volume": volume,
      "pan": pan,
      "muted": muted,
      "startTimeMs": startTimeMs
    ]
  }

  private static func resolveURL(uri: String) -> URL? {
    if uri.hasPrefix("file://") {
      return URL(string: uri)
    }
    if uri.hasPrefix("/") {
      return URL(fileURLWithPath: uri)
    }
    return URL(string: uri)
  }
}
