// Quality bar for the Swift port of the input pitch detector
// (ios/AudioEngine/InputPitchAnalyzer.swift), mirroring the C++ host tests in
// packages/android-engine/android/engine/src/test/cpp/tests/test_pitch_detector.cpp.
// Runs on macOS without a simulator: `yarn test:swift:pitch` from the repo root.

import Foundation

var failures = 0

func check(_ condition: Bool, _ message: @autoclosure () -> String) {
  if !condition {
    failures += 1
    print("FAIL: \(message())")
  }
}

struct Window {
  let estimate: PitchEstimate
  let endFrame: Int64
}

func peak(forDbfs dbfs: Double) -> Double {
  // Peak amplitude of a sine whose RMS is `dbfs`.
  return pow(10.0, dbfs / 20.0) * 2.0.squareRoot()
}

func analyze(_ mono: [Float], rate: Int, channels: Int = 1,
             config: InputMonitorConfig = InputMonitorConfig(), callbackFrames: Int = 480) -> [Window] {
  var normalized = config
  check(normalized.normalize(), "config normalizes")
  let analyzer = InputPitchAnalyzer(inputRate: rate, channels: channels, config: normalized)
  var windows: [Window] = []
  // Non-interleaved, like AVAudioPCMBuffer: every channel carries the same signal.
  let channelBuffers = (0..<channels).map { _ in UnsafeMutablePointer<Float>.allocate(capacity: callbackFrames) }
  let channelPointers = UnsafeMutablePointer<UnsafeMutablePointer<Float>>.allocate(capacity: channels)
  for c in 0..<channels { channelPointers[c] = channelBuffers[c] }
  defer {
    channelBuffers.forEach { $0.deallocate() }
    channelPointers.deallocate()
  }
  var offset = 0
  while offset < mono.count {
    let frames = min(callbackFrames, mono.count - offset)
    for c in 0..<channels {
      for i in 0..<frames { channelBuffers[c][i] = mono[offset + i] }
    }
    analyzer.process(channelData: UnsafePointer(channelPointers), frames: frames) { estimate, end in
      windows.append(Window(estimate: estimate, endFrame: end))
    }
    offset += frames
  }
  return windows
}

func sine(_ hz: Double, seconds: Double, rate: Int, amplitude: Double) -> [Float] {
  let frames = Int(seconds * Double(rate))
  return (0..<frames).map { Float(amplitude * sin(2.0 * Double.pi * hz * Double($0) / Double(rate))) }
}

func expectSteady(_ name: String, _ windows: [Window], hz: Double, tolerance: Double, minConfidence: Float) {
  check(windows.count > 10, "\(name): \(windows.count) windows")
  var maxError = 0.0
  for (i, w) in windows.enumerated() {
    let error = abs(Double(w.estimate.hz) - hz) / hz
    maxError = max(maxError, error)
    check(error <= tolerance, "\(name): window \(i) hz \(w.estimate.hz), expected \(hz)")
    check(w.estimate.confidence > minConfidence, "\(name): window \(i) confidence \(w.estimate.confidence)")
  }
  print(String(format: "%@: %d windows, max error %.3f%%", name, windows.count, maxError * 100))
}

let a20 = peak(forDbfs: -20.0)

for rate in [48000, 44100] {
  expectSteady("220 Hz @\(rate)", analyze(sine(220, seconds: 2, rate: rate, amplitude: a20), rate: rate),
               hz: 220, tolerance: 0.01, minConfidence: 0.8)
  expectSteady("1 kHz @\(rate)", analyze(sine(1000, seconds: 2, rate: rate, amplitude: a20), rate: rate),
               hz: 1000, tolerance: 0.01, minConfidence: 0.8)
}

expectSteady("220 Hz stereo", analyze(sine(220, seconds: 1, rate: 48000, amplitude: a20), rate: 48000, channels: 2),
             hz: 220, tolerance: 0.01, minConfidence: 0.8)

do {
  let rate = 48000
  let h2 = pow(10.0, -6.0 / 20.0)
  let h3 = pow(10.0, -12.0 / 20.0)
  let signal: [Float] = (0..<(2 * rate)).map {
    let t = Double($0) / Double(rate)
    return Float(a20 * (sin(2 * Double.pi * 440 * t) + h2 * sin(2 * Double.pi * 880 * t) + h3 * sin(2 * Double.pi * 1320 * t)))
  }
  expectSteady("A440 + harmonics", analyze(signal, rate: rate), hz: 440, tolerance: 0.01, minConfidence: 0.5)
}

do {
  let windows = analyze([Float](repeating: 0, count: 96000), rate: 48000)
  check(windows.count > 10, "silence windows")
  for w in windows { check(w.estimate.hz == 0 && w.estimate.confidence == 0, "silence hz \(w.estimate.hz)") }
  print("silence: \(windows.count) windows, all hz 0")
}

do {
  // Gaussian noise, RMS 0.01 (-40 dBFS), deterministic LCG + Box-Muller.
  var state: UInt64 = 1234
  func uniform() -> Double {
    state = state &* 6364136223846793005 &+ 1442695040888963407
    return (Double(state >> 11) + 0.5) / Double(1 << 53)
  }
  let signal: [Float] = (0..<96000).map { _ in
    Float(0.01 * (-2.0 * log(uniform())).squareRoot() * cos(2.0 * Double.pi * uniform()))
  }
  let windows = analyze(signal, rate: 48000)
  let gate = powf(10.0, InputPitchConstants.silenceThresholdDbfs / 20.0)
  for (i, w) in windows.enumerated() {
    check(w.estimate.level > gate, "noise window \(i) under the gate (level \(w.estimate.level))")
    check(w.estimate.hz == 0, "noise window \(i) hz \(w.estimate.hz)")
  }
  print("white noise -40 dBFS: \(windows.count) windows, all hz 0")
}

do {
  let rate = 48000
  let slope = 100.0
  let signal: [Float] = (0..<(2 * rate)).map {
    let t = Double($0) / Double(rate)
    return Float(a20 * sin(2 * Double.pi * (200 * t + 0.5 * slope * t * t)))
  }
  let windows = analyze(signal, rate: rate)
  let windowFrames = Double(InputPitchConstants.analysisWindowSize) * Double(rate) / Double(InputPitchConstants.analysisSampleRate)
  var maxError = 0.0
  for (i, w) in windows.enumerated() {
    let centre = (Double(w.endFrame) - windowFrames / 2) / Double(rate)
    let expected = 200 + slope * centre
    let error = abs(Double(w.estimate.hz) - expected) / expected
    maxError = max(maxError, error)
    check(error <= 0.02, "glide window \(i): \(w.estimate.hz) vs \(expected)")
  }
  print(String(format: "glide 200-400 Hz / 2 s: %d windows, max error %.3f%%", windows.count, maxError * 100))
}

do {
  // Framing: 480-frame callbacks at 48 kHz -> one window per 1200 input frames.
  let windows = analyze(sine(220, seconds: 3, rate: 48000, amplitude: a20), rate: 48000)
  check(windows.first?.endFrame == 3072, "first window ends at frame 3072")
  for i in 1..<windows.count {
    check(windows[i].endFrame - windows[i - 1].endFrame == 1200, "window \(i) spacing")
  }
  print("framing: \(windows.count) windows, 1200 frames apart")
}

if failures > 0 {
  print("\(failures) check(s) failed")
  exit(1)
}
print("all checks passed")
