struct AudioEngineConfig {
  let sampleRate: Double?
  let bufferSize: Double?
  let maxTracks: Int?
  let enableProcessing: Bool?

  init(dictionary: [String: Any]) {
    sampleRate = dictionary["sampleRate"] as? Double
    bufferSize = dictionary["bufferSize"] as? Double
    maxTracks = dictionary["maxTracks"] as? Int
    enableProcessing = dictionary["enableProcessing"] as? Bool
  }
}
