import Accelerate
import Foundation

// Fundamental frequency detection for the input pitch monitor.
//
// Swift port of packages/android-engine/android/engine/src/main/cpp/analysis/
// PitchDetector.{h,cpp}. Keep the two line-for-line comparable: same constants,
// same steps, same order. The C++ side carries the quality-bar tests
// (`yarn test:cpp:host`); a change here must be mirrored there and the other way
// round.
//
// YIN (de Cheveigne & Kawahara 2002): difference function, cumulative mean
// normalised difference d'(tau), absolute threshold, parabolic interpolation of
// the minimum, confidence 1 - d'(tau). The input is mixed to mono and resampled
// to 16 kHz (windowed-sinc low-pass, then linear interpolation at the output
// instants); each analysis window is the newest 1024 samples (64 ms), and a new
// window is analysed every `intervalMs`.
//
// Nothing here allocates after construction, and nothing here runs in the
// AVAudioEngine tap closure: the recording controller calls it on its serial queue.
//
// The two inner sums (the difference function and the low-pass dot product)
// use vDSP instead of the C++ loops: the same math, but Accelerate is optimised
// in a Debug build of the consumer, where plain Swift loops ran slower than real
// time. Accumulation order differs, so results match the C++ to float rounding.

enum InputPitchConstants {
  /// Sample rate the detector runs at.
  static let analysisSampleRate: Int = 16000
  /// Analysis window, samples at analysisSampleRate (64 ms).
  static let analysisWindowSize: Int = 1024
  /// YIN absolute threshold on d'(tau).
  static let yinThreshold: Float = 0.15
  /// Windows whose RMS is below this level (dBFS) report no pitch.
  static let silenceThresholdDbfs: Float = -50.0
  /// Low-pass cutoff applied before resampling to 16 kHz, Hz.
  static let decimatorCutoffHz: Float = 7000.0
  /// Low-pass length, taps (odd, so the filter has an integer delay).
  static let decimatorTaps: Int = 63

  static let defaultMinHz: Float = 50.0
  static let defaultMaxHz: Float = 1500.0
  static let defaultIntervalMs: Int = 25
  static let minIntervalMs: Int = 10

  // Lags are capped at half the window so the difference function always
  // integrates over at least half of it.
  static let maxLag: Int = analysisWindowSize / 2
  // Smallest lag searched: a period of two samples is the Nyquist limit.
  static let minLag: Int = 2
  // Range the config is clamped to (lag 512 at 16 kHz is 31.25 Hz; lag 4 is 4 kHz).
  static let lowestMinHz: Float = 32.0
  static let highestMaxHz: Float = 4000.0
}

struct InputMonitorConfig {
  /// Lowest fundamental reported, Hz.
  var minHz: Float = InputPitchConstants.defaultMinHz
  /// Highest fundamental reported, Hz.
  var maxHz: Float = InputPitchConstants.defaultMaxHz
  /// Target interval between analysed windows, ms (minimum 10).
  var intervalMs: Int = InputPitchConstants.defaultIntervalMs

  /// Clamps the config to what the detector supports: interval >= 10 ms,
  /// 32 Hz <= minHz < maxHz <= 4 kHz. Returns false when the values cannot form a
  /// usable range (minHz >= maxHz, non-positive or non-finite values).
  mutating func normalize() -> Bool {
    if intervalMs < InputPitchConstants.minIntervalMs {
      intervalMs = InputPitchConstants.minIntervalMs
    }
    let minOk = minHz.isFinite && minHz > 0.0 && minHz < 1.0e6
    let maxOk = maxHz.isFinite && maxHz > 0.0 && maxHz < 1.0e6
    if !minOk || !maxOk {
      return false
    }
    minHz = max(minHz, InputPitchConstants.lowestMinHz)
    maxHz = min(maxHz, InputPitchConstants.highestMaxHz)
    return minHz < maxHz
  }
}

struct PitchEstimate {
  /// Fundamental, Hz; 0 when no pitch was found in the window.
  var hz: Float = 0.0
  /// 1 - d'(tau) at the chosen lag, 0..1; 0 when hz is 0.
  var confidence: Float = 0.0
  /// RMS of the analysed window, linear.
  var level: Float = 0.0
}

/// YIN over one window of analysisWindowSize samples at analysisSampleRate.
final class PitchDetector {
  private let minHz: Float
  private let maxHz: Float
  let tauMin: Int
  let tauMax: Int
  private let integrationSize: Int
  private var difference: [Float]
  private var cmnd: [Float]
  private let silenceThreshold: Float

  init(minHz: Float, maxHz: Float) {
    self.minHz = minHz
    self.maxHz = maxHz
    let rate = Float(InputPitchConstants.analysisSampleRate)
    // Shortest period searched (highest frequency).
    var lowLag = max(InputPitchConstants.minLag, Int((rate / maxHz).rounded(.down)))
    // Longest period searched, plus one lag so the minimum can be interpolated.
    let highLag = min(InputPitchConstants.maxLag, Int((rate / minHz).rounded(.up)) + 1)
    lowLag = min(lowLag, highLag - 1)
    tauMin = lowLag
    tauMax = highLag
    integrationSize = InputPitchConstants.analysisWindowSize - highLag
    difference = [Float](repeating: 0.0, count: highLag + 1)
    cmnd = [Float](repeating: 1.0, count: highLag + 1)
    silenceThreshold = powf(10.0, InputPitchConstants.silenceThresholdDbfs / 20.0)
  }

  func analyze(_ window: UnsafePointer<Float>) -> PitchEstimate {
    var result = PitchEstimate()
    let windowSize = InputPitchConstants.analysisWindowSize

    // 1. Level of the whole window; quiet windows report no pitch.
    var energy: Float = 0.0
    vDSP_svesq(window, 1, &energy, vDSP_Length(windowSize))
    result.level = (energy / Float(windowSize)).squareRoot()
    if result.level < silenceThreshold {
      return result
    }

    difference.withUnsafeMutableBufferPointer { differenceBuffer in
      cmnd.withUnsafeMutableBufferPointer { cmndBuffer in
        // 2. Difference function d(tau).
        // sum over j < integrationSize of (x[j] - x[j + tau])^2
        let length = vDSP_Length(integrationSize)
        for tau in 1...tauMax {
          var sum: Float = 0.0
          vDSP_distancesq(window, 1, window + tau, 1, &sum, length)
          differenceBuffer[tau] = sum
        }

        // 3. Cumulative mean normalised difference d'(tau).
        cmndBuffer[0] = 1.0
        var running: Float = 0.0
        for tau in 1...tauMax {
          running += differenceBuffer[tau]
          cmndBuffer[tau] = running > 0.0
            ? differenceBuffer[tau] * Float(tau) / running
            : 1.0
        }
      }
    }

    // 4. Absolute threshold: first dip under the threshold, then down to its minimum.
    var tau = 0
    var candidate = tauMin
    while candidate < tauMax {
      if cmnd[candidate] < InputPitchConstants.yinThreshold {
        while candidate + 1 < tauMax && cmnd[candidate + 1] < cmnd[candidate] {
          candidate += 1
        }
        tau = candidate
        break
      }
      candidate += 1
    }
    if tau == 0 {
      return result
    }

    // 5. Parabolic interpolation of the minimum.
    var refined = Float(tau)
    let s0 = cmnd[tau - 1]
    let s1 = cmnd[tau]
    let s2 = cmnd[tau + 1]
    let denominator = s0 + s2 - 2.0 * s1
    if abs(denominator) > 1e-12 {
      let shift = 0.5 * (s0 - s2) / denominator
      if abs(shift) < 1.0 {
        refined += shift
      }
    }

    // 6. Frequency and confidence.
    let hz = Float(InputPitchConstants.analysisSampleRate) / refined
    if hz < minHz || hz > maxHz {
      return result
    }
    result.hz = hz
    result.confidence = min(max(1.0 - cmnd[tau], 0.0), 1.0)
    return result
  }
}

/// Mono stream at any input rate -> analysisSampleRate. Output sample k sits at
/// input position k * inputRate / 16000 (exact integer arithmetic, so it does not
/// drift over long sessions).
final class PitchDecimator {
  private let inputRate: Int
  private let filterEnabled: Bool
  private var taps: [Float] = []
  // Delay line stored twice so the dot product never wraps.
  private var history: [Float] = []
  private var historyPos = 0
  private var inputCount: Int64 = 0
  private(set) var outputCount: Int64 = 0
  private var previous: Float = 0.0

  init(inputRate: Int) {
    self.inputRate = max(1, inputRate)
    filterEnabled = inputRate > InputPitchConstants.analysisSampleRate
    if !filterEnabled {
      return
    }
    // Windowed-sinc low-pass (Blackman), unity gain at DC.
    let tapCount = InputPitchConstants.decimatorTaps
    let cutoff = Double(InputPitchConstants.decimatorCutoffHz) / Double(self.inputRate)
    let middle = Double(tapCount - 1) / 2.0
    var raw = [Double](repeating: 0.0, count: tapCount)
    var sum = 0.0
    for n in 0..<tapCount {
      let x = Double(n) - middle
      let sinc = x == 0.0
        ? 2.0 * cutoff
        : sin(2.0 * Double.pi * cutoff * x) / (Double.pi * x)
      let phase = 2.0 * Double.pi * Double(n) / Double(tapCount - 1)
      let blackman = 0.42 - 0.5 * cos(phase) + 0.08 * cos(2.0 * phase)
      let tap = sinc * blackman
      raw[n] = Double(Float(tap))
      sum += tap
    }
    taps = raw.map { Float($0 / sum) }
    history = [Float](repeating: 0.0, count: tapCount * 2)
  }

  /// Upper bound of output samples produced from `count` input samples.
  func maxOutput(for count: Int) -> Int {
    return count * InputPitchConstants.analysisSampleRate / inputRate + 2
  }

  private func filter(_ sample: Float) -> Float {
    let tapCount = InputPitchConstants.decimatorTaps
    history[historyPos] = sample
    history[historyPos + tapCount] = sample
    historyPos = (historyPos + 1) % tapCount
    // history[historyPos ..< historyPos + tapCount] is oldest -> newest.
    var acc: Float = 0.0
    let start = historyPos
    history.withUnsafeBufferPointer { line in
      taps.withUnsafeBufferPointer { coefficients in
        vDSP_dotpr(coefficients.baseAddress!, 1, line.baseAddress! + start, 1, &acc, vDSP_Length(tapCount))
      }
    }
    return acc
  }

  /// Consumes `count` mono samples and writes the resampled ones to `out`, which
  /// must hold at least maxOutput(for: count) samples. Returns samples written.
  func process(_ input: UnsafePointer<Float>, count: Int, into out: UnsafeMutablePointer<Float>) -> Int {
    var written = 0
    let rate = Int64(inputRate)
    let target = Int64(InputPitchConstants.analysisSampleRate)
    for i in 0..<count {
      let current = filterEnabled ? filter(input[i]) : input[i]
      let n = inputCount
      if n >= 1 {
        // Emit every output instant that lies in [n - 1, n).
        while true {
          let position = outputCount * rate
          let index = position / target
          if index > n - 1 {
            break
          }
          let fraction = Float(position - index * target) / Float(target)
          out[written] = previous + fraction * (current - previous)
          written += 1
          outputCount += 1
        }
      }
      previous = current
      inputCount += 1
    }
    return written
  }
}

/// Frames the stream into hop-spaced windows and runs the detector on each.
/// Feed it input as it arrives; it calls `onWindow` once per analysed window, in
/// order, with the input frame index (counted from the first frame fed) at the
/// end of that window.
final class InputPitchAnalyzer {
  typealias WindowCallback = (PitchEstimate, Int64) -> Void

  /// Frames converted per internal step.
  static let chunkFrames = 512

  let inputRate: Int
  let channels: Int
  let hopSize: Int
  let config: InputMonitorConfig
  private let decimator: PitchDecimator
  private let detector: PitchDetector
  private var mono: [Float]
  private var decimated: [Float]
  // Newest analysisWindowSize decimated samples, circular.
  private var ring: [Float]
  private var ringPos = 0
  private var window: [Float]
  private var decimatedCount: Int64 = 0
  private var sinceLastWindow = 0
  private var firstWindowDone = false

  init(inputRate: Int, channels: Int, config: InputMonitorConfig) {
    self.inputRate = max(1, inputRate)
    self.channels = max(1, channels)
    self.config = config
    hopSize = max(config.intervalMs, InputPitchConstants.minIntervalMs) *
      InputPitchConstants.analysisSampleRate / 1000
    decimator = PitchDecimator(inputRate: self.inputRate)
    detector = PitchDetector(minHz: config.minHz, maxHz: config.maxHz)
    mono = [Float](repeating: 0.0, count: InputPitchAnalyzer.chunkFrames)
    decimated = [Float](repeating: 0.0, count: decimator.maxOutput(for: InputPitchAnalyzer.chunkFrames))
    ring = [Float](repeating: 0.0, count: InputPitchConstants.analysisWindowSize)
    window = [Float](repeating: 0.0, count: InputPitchConstants.analysisWindowSize)
  }

  /// Non-interleaved input, one pointer per channel (AVAudioPCMBuffer.floatChannelData).
  /// The C++ analyzer takes interleaved frames; the mono mix is the same.
  func process(channelData: UnsafePointer<UnsafeMutablePointer<Float>>, frames: Int, onWindow: WindowCallback) {
    let scale = 1.0 / Float(channels)
    var offset = 0
    while offset < frames {
      let chunk = min(InputPitchAnalyzer.chunkFrames, frames - offset)
      // Mono mix.
      for i in 0..<chunk {
        var sum: Float = 0.0
        for c in 0..<channels {
          sum += channelData[c][offset + i]
        }
        mono[i] = sum * scale
      }
      // Low-pass + resample to 16 kHz.
      let produced = mono.withUnsafeBufferPointer { monoBuffer in
        decimated.withUnsafeMutableBufferPointer { decimatedBuffer in
          decimator.process(monoBuffer.baseAddress!, count: chunk, into: decimatedBuffer.baseAddress!)
        }
      }
      for i in 0..<produced {
        push(decimated[i], onWindow: onWindow)
      }
      offset += chunk
    }
  }

  private func push(_ sample: Float, onWindow: WindowCallback) {
    let windowSize = InputPitchConstants.analysisWindowSize
    ring[ringPos] = sample
    ringPos = (ringPos + 1) % windowSize
    decimatedCount += 1

    var due = false
    if !firstWindowDone {
      due = decimatedCount >= Int64(windowSize)
    } else {
      sinceLastWindow += 1
      due = sinceLastWindow >= hopSize
    }
    if !due {
      return
    }
    firstWindowDone = true
    sinceLastWindow = 0

    // ring[ringPos] is the oldest sample.
    let tail = windowSize - ringPos
    for i in 0..<tail {
      window[i] = ring[ringPos + i]
    }
    for i in 0..<ringPos {
      window[tail + i] = ring[i]
    }

    let estimate = window.withUnsafeBufferPointer { detector.analyze($0.baseAddress!) }
    let windowEndFrame = decimatedCount * Int64(inputRate) / Int64(InputPitchConstants.analysisSampleRate)
    onWindow(estimate, windowEndFrame)
  }
}
