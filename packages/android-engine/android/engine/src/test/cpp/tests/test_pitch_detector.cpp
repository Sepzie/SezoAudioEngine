// Quality bar for the input pitch monitor's detector (YIN at 16 kHz behind a
// low-pass + resampler), run end to end through InputPitchAnalyzer at the
// capture rates devices actually deliver, plus the ring-buffer plumbing the
// Android worker uses.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#include "analysis/InputPitchMonitor.h"
#include "analysis/PitchDetector.h"
#include "core/CircularBuffer.h"

namespace sezo {
namespace analysis {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Window {
  PitchEstimate estimate;
  int64_t end_frame = 0;
};

double DbfsToRmsAmplitude(double dbfs) {
  // Peak amplitude of a sine whose RMS is `dbfs`.
  return std::pow(10.0, dbfs / 20.0) * std::sqrt(2.0);
}

std::vector<float> Sine(double hz, double seconds, int32_t rate, double peak) {
  const size_t frames = static_cast<size_t>(seconds * rate);
  std::vector<float> out(frames);
  for (size_t i = 0; i < frames; ++i) {
    out[i] = static_cast<float>(peak * std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
  }
  return out;
}

std::vector<Window> Analyze(const std::vector<float>& mono,
                            int32_t rate,
                            int32_t channels = 1,
                            InputMonitorConfig config = InputMonitorConfig{},
                            size_t callback_frames = 480) {
  EXPECT_TRUE(NormalizeInputMonitorConfig(&config));
  InputPitchAnalyzer analyzer(rate, channels, config);
  std::vector<Window> windows;
  const auto on_window = [&windows](const PitchEstimate& estimate, int64_t end_frame) {
    windows.push_back({estimate, end_frame});
  };
  // Interleave: every channel carries the same signal.
  std::vector<float> interleaved(mono.size() * static_cast<size_t>(channels));
  for (size_t i = 0; i < mono.size(); ++i) {
    for (int32_t c = 0; c < channels; ++c) {
      interleaved[i * static_cast<size_t>(channels) + static_cast<size_t>(c)] = mono[i];
    }
  }
  for (size_t offset = 0; offset < mono.size(); offset += callback_frames) {
    const size_t frames = std::min(callback_frames, mono.size() - offset);
    analyzer.Process(interleaved.data() + offset * static_cast<size_t>(channels), frames, on_window);
  }
  return windows;
}

void ExpectSteadyPitch(const std::vector<Window>& windows, double expected_hz,
                       double tolerance, float min_confidence) {
  ASSERT_GT(windows.size(), 10u);
  for (size_t i = 0; i < windows.size(); ++i) {
    const auto& w = windows[i].estimate;
    EXPECT_NEAR(w.hz, expected_hz, expected_hz * tolerance) << "window " << i;
    EXPECT_GT(w.confidence, min_confidence) << "window " << i;
  }
}

TEST(PitchDetectorTest, Sine220HzAtMinus20Dbfs) {
  for (const int32_t rate : {48000, 44100}) {
    SCOPED_TRACE(rate);
    const auto windows = Analyze(Sine(220.0, 2.0, rate, DbfsToRmsAmplitude(-20.0)), rate);
    ExpectSteadyPitch(windows, 220.0, 0.01, 0.8f);
    // Level is the window RMS: -20 dBFS = 0.1.
    EXPECT_NEAR(windows.back().estimate.level, 0.1, 0.005);
  }
}

TEST(PitchDetectorTest, Sine1kHz) {
  for (const int32_t rate : {48000, 44100}) {
    SCOPED_TRACE(rate);
    const auto windows = Analyze(Sine(1000.0, 2.0, rate, DbfsToRmsAmplitude(-20.0)), rate);
    ExpectSteadyPitch(windows, 1000.0, 0.01, 0.8f);
  }
}

TEST(PitchDetectorTest, StereoInputIsMixedToMono) {
  const auto windows =
      Analyze(Sine(220.0, 1.0, 48000, DbfsToRmsAmplitude(-20.0)), 48000, /*channels=*/2);
  ExpectSteadyPitch(windows, 220.0, 0.01, 0.8f);
}

TEST(PitchDetectorTest, A440WithHarmonicsHasNoOctaveError) {
  const int32_t rate = 48000;
  const double peak = DbfsToRmsAmplitude(-20.0);
  const double h2 = std::pow(10.0, -6.0 / 20.0);
  const double h3 = std::pow(10.0, -12.0 / 20.0);
  std::vector<float> signal(static_cast<size_t>(2.0 * rate));
  for (size_t i = 0; i < signal.size(); ++i) {
    const double t = static_cast<double>(i) / rate;
    signal[i] = static_cast<float>(peak * (std::sin(2.0 * kPi * 440.0 * t) +
                                           h2 * std::sin(2.0 * kPi * 880.0 * t) +
                                           h3 * std::sin(2.0 * kPi * 1320.0 * t)));
  }
  const auto windows = Analyze(signal, rate);
  ExpectSteadyPitch(windows, 440.0, 0.01, 0.5f);
}

TEST(PitchDetectorTest, SilenceReportsNoPitch) {
  const std::vector<float> silence(48000 * 2, 0.0f);
  const auto windows = Analyze(silence, 48000);
  ASSERT_GT(windows.size(), 10u);
  for (const auto& w : windows) {
    EXPECT_EQ(w.estimate.hz, 0.0f);
    EXPECT_EQ(w.estimate.confidence, 0.0f);
    EXPECT_EQ(w.estimate.level, 0.0f);
  }
}

TEST(PitchDetectorTest, QuietSineUnderTheGateReportsNoPitch) {
  // -60 dBFS is under the -50 dBFS gate.
  const auto windows = Analyze(Sine(220.0, 1.0, 48000, DbfsToRmsAmplitude(-60.0)), 48000);
  ASSERT_GT(windows.size(), 5u);
  for (const auto& w : windows) {
    EXPECT_EQ(w.estimate.hz, 0.0f);
    EXPECT_GT(w.estimate.level, 0.0f);
  }
}

TEST(PitchDetectorTest, WhiteNoiseAtMinus40DbfsReportsNoPitch) {
  std::mt19937 rng(1234);
  // RMS 0.01 (-40 dBFS).
  std::normal_distribution<float> noise(0.0f, 0.01f);
  std::vector<float> signal(48000 * 2);
  for (auto& s : signal) {
    s = noise(rng);
  }
  const auto windows = Analyze(signal, 48000);
  ASSERT_GT(windows.size(), 10u);
  for (size_t i = 0; i < windows.size(); ++i) {
    const auto& w = windows[i].estimate;
    // Above the silence gate, so this exercises the YIN threshold, not the gate.
    EXPECT_GT(w.level, std::pow(10.0f, kSilenceThresholdDbfs / 20.0f)) << "window " << i;
    EXPECT_EQ(w.hz, 0.0f) << "window " << i;
    EXPECT_EQ(w.confidence, 0.0f) << "window " << i;
  }
}

TEST(PitchDetectorTest, LinearGlideIsTrackedWithinTwoPercent) {
  const int32_t rate = 48000;
  const double seconds = 2.0;
  const double f0 = 200.0;
  const double f1 = 400.0;
  const double slope = (f1 - f0) / seconds;
  const double peak = DbfsToRmsAmplitude(-20.0);
  std::vector<float> signal(static_cast<size_t>(seconds * rate));
  for (size_t i = 0; i < signal.size(); ++i) {
    const double t = static_cast<double>(i) / rate;
    signal[i] = static_cast<float>(peak * std::sin(2.0 * kPi * (f0 * t + 0.5 * slope * t * t)));
  }
  const auto windows = Analyze(signal, rate);
  ASSERT_GT(windows.size(), 50u);
  // Window length in input frames.
  const double window_frames = static_cast<double>(kAnalysisWindowSize) * rate / kAnalysisSampleRate;
  for (size_t i = 0; i < windows.size(); ++i) {
    const double centre_s = (static_cast<double>(windows[i].end_frame) - window_frames / 2.0) / rate;
    const double expected = f0 + slope * centre_s;
    EXPECT_NEAR(windows[i].estimate.hz, expected, expected * 0.02)
        << "window " << i << " centre " << centre_s << " s";
  }
}

TEST(PitchDetectorTest, ReportedRangeFollowsConfig) {
  InputMonitorConfig config;
  config.min_hz = 300.0f;
  config.max_hz = 1500.0f;
  // 220 Hz is below min_hz: no pitch is reported for it.
  const auto windows = Analyze(Sine(220.0, 1.0, 48000, DbfsToRmsAmplitude(-20.0)), 48000, 1, config);
  ASSERT_GT(windows.size(), 5u);
  for (const auto& w : windows) {
    EXPECT_TRUE(w.estimate.hz == 0.0f || w.estimate.hz >= 300.0f) << w.estimate.hz;
    EXPECT_GT(std::fabs(w.estimate.hz - 220.0f), 220.0f * 0.05f);
  }
}

TEST(PitchDetectorTest, ConfigIsNormalized) {
  InputMonitorConfig config;
  config.interval_ms = 2;
  config.min_hz = 10.0f;
  config.max_hz = 20000.0f;
  ASSERT_TRUE(NormalizeInputMonitorConfig(&config));
  EXPECT_EQ(config.interval_ms, kMinIntervalMs);
  EXPECT_GE(config.min_hz, 32.0f);
  EXPECT_LE(config.max_hz, 4000.0f);

  InputMonitorConfig inverted;
  inverted.min_hz = 800.0f;
  inverted.max_hz = 400.0f;
  EXPECT_FALSE(NormalizeInputMonitorConfig(&inverted));

  InputMonitorConfig nan_config;
  nan_config.min_hz = std::nanf("");
  EXPECT_FALSE(NormalizeInputMonitorConfig(&nan_config));
}

TEST(DecimatorTest, OutputCountFollowsTheRateRatio) {
  for (const int32_t rate : {48000, 44100, 16000, 96000}) {
    SCOPED_TRACE(rate);
    Decimator decimator(rate);
    std::vector<float> in(rate, 0.0f);  // one second
    std::vector<float> out(decimator.MaxOutputFor(in.size()));
    const size_t produced = decimator.Process(in.data(), in.size(), out.data());
    // One second in, 16000 out (the last sample waits for one more input sample).
    EXPECT_NEAR(static_cast<double>(produced), 16000.0, 1.0);
  }
}

// Ring buffer plumbing: an Oboe-like producer writes 480-frame callbacks at
// 48 kHz into a CircularBuffer, the monitor's Pump() drains it. One window per
// 25 ms (1200 input frames), none skipped, none repeated.
TEST(InputPitchMonitorTest, RingBufferPlumbingProducesOneWindowPer25Ms) {
  const int32_t rate = 48000;
  const size_t callback_frames = 480;
  core::CircularBuffer ring(static_cast<size_t>(rate) * 2);
  std::vector<InputPitchEvent> events;

  InputPitchMonitor monitor(
      rate, 1, InputMonitorConfig{},
      [&ring](float* dst, size_t max_frames) { return ring.Read(dst, max_frames); },
      [] { return 1234.0; },
      [&events](const InputPitchEvent& event) { events.push_back(event); });

  const auto tone = Sine(220.0, 3.0, rate, DbfsToRmsAmplitude(-20.0));
  size_t fed = 0;
  for (size_t offset = 0; offset + callback_frames <= tone.size(); offset += callback_frames) {
    ASSERT_EQ(ring.Write(tone.data() + offset, callback_frames), callback_frames);
    fed += callback_frames;
    // Drain every other callback, like a worker that wakes up every ~10 ms.
    if ((offset / callback_frames) % 2 == 1) {
      monitor.Pump();
    }
  }
  monitor.Pump();
  EXPECT_EQ(ring.Available(), 0u);

  // First window when 1024 samples at 16 kHz (3072 input frames) are in, then every 1200 frames.
  const size_t decimated = fed / 3;
  ASSERT_GE(decimated, kAnalysisWindowSize);
  const size_t expected_windows = (decimated - kAnalysisWindowSize) / 400 + 1;
  ASSERT_EQ(events.size(), expected_windows);
  EXPECT_EQ(events.front().window_end_frame, 3072);
  for (size_t i = 1; i < events.size(); ++i) {
    EXPECT_EQ(events[i].window_end_frame - events[i - 1].window_end_frame, 1200) << "window " << i;
  }
  for (const auto& event : events) {
    EXPECT_NEAR(event.hz, 220.0f, 2.2f);
    EXPECT_DOUBLE_EQ(event.position_ms, 1234.0);
    EXPECT_GT(event.timestamp_ms, 0);
  }
  EXPECT_NEAR(monitor.GetLevel(), 0.1f, 0.005f);
}

TEST(InputPitchMonitorTest, HopSpacingAt44100IsTwentyFiveMilliseconds) {
  const int32_t rate = 44100;
  core::CircularBuffer ring(static_cast<size_t>(rate) * 2);
  std::vector<InputPitchEvent> events;
  InputPitchMonitor monitor(
      rate, 1, InputMonitorConfig{},
      [&ring](float* dst, size_t max_frames) { return ring.Read(dst, max_frames); },
      nullptr,
      [&events](const InputPitchEvent& event) { events.push_back(event); });

  const auto tone = Sine(440.0, 2.0, rate, DbfsToRmsAmplitude(-20.0));
  for (size_t offset = 0; offset + 441 <= tone.size(); offset += 441) {
    ring.Write(tone.data() + offset, 441);
    monitor.Pump();
  }
  ASSERT_GT(events.size(), 50u);
  for (size_t i = 1; i < events.size(); ++i) {
    const int64_t step = events[i].window_end_frame - events[i - 1].window_end_frame;
    // 25 ms at 44.1 kHz is 1102.5 frames.
    EXPECT_TRUE(step == 1102 || step == 1103) << "window " << i << " step " << step;
    EXPECT_DOUBLE_EQ(events[i].position_ms, -1.0);
  }
}

TEST(InputPitchMonitorTest, WorkerThreadDeliversEventsAndStopsCleanly) {
  const int32_t rate = 48000;
  core::CircularBuffer ring(static_cast<size_t>(rate) * 2);
  std::atomic<int> count{0};
  InputPitchMonitor monitor(
      rate, 1, InputMonitorConfig{},
      [&ring](float* dst, size_t max_frames) { return ring.Read(dst, max_frames); },
      nullptr,
      [&count](const InputPitchEvent&) { count.fetch_add(1); });
  monitor.Start();
  EXPECT_TRUE(monitor.IsRunning());
  const auto tone = Sine(220.0, 1.0, rate, DbfsToRmsAmplitude(-20.0));
  ring.Write(tone.data(), tone.size());
  for (int i = 0; i < 200 && count.load() < 30; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  monitor.Stop();
  EXPECT_FALSE(monitor.IsRunning());
  const int after_stop = count.load();
  EXPECT_GE(after_stop, 30);
  ring.Write(tone.data(), tone.size());
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  EXPECT_EQ(count.load(), after_stop);
}

}  // namespace
}  // namespace analysis
}  // namespace sezo
