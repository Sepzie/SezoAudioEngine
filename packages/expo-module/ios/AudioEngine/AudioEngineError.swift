import ExpoModulesCore

struct AudioEngineError: CodedError, CustomNSError {
  let code: String
  let description: String
  let details: [String: Any]?

  init(_ code: String, _ description: String, details: [String: Any]? = nil) {
    self.code = code
    self.description = description
    self.details = details
  }

  static var errorDomain: String {
    return "ExpoAudioEngine"
  }

  var errorCode: Int {
    return 0
  }

  var errorUserInfo: [String: Any] {
    var info: [String: Any] = [
      NSLocalizedDescriptionKey: description,
      "code": code
    ]
    if let details = details {
      info["details"] = details
    }
    return info
  }
}
