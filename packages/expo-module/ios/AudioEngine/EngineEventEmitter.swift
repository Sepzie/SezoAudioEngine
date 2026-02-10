final class EngineEventEmitter {
  var onPlaybackStateChange: ((String, Double, Double) -> Void)?
  var onPlaybackComplete: ((Double, Double) -> Void)?
  var onError: ((String, String, [String: Any]?) -> Void)?

  func emitPlaybackState(_ state: EnginePlaybackState, positionMs: Double, durationMs: Double) {
    onPlaybackStateChange?(state.rawValue, positionMs, durationMs)
  }

  func emitPlaybackComplete(positionMs: Double, durationMs: Double) {
    onPlaybackComplete?(positionMs, durationMs)
  }

  func emitError(code: String, message: String, details: [String: Any]? = nil) {
    onError?(code, message, details)
  }
}
