# Input Pitch Monitor

The input pitch monitor reports the fundamental frequency of the microphone
while the app plays, records, or does neither. It keeps the input open without
writing a file and emits a small `inputPitch` event about every 25 ms.

```ts
import { AudioEngineModule } from 'sezo-audio-engine';

await AudioEngineModule.initialize({});
// Ask for the microphone permission first (for example with expo-audio or
// PermissionsAndroid). The module never requests it.

const sub = AudioEngineModule.addListener('inputPitch', (e) => {
  if (e.hz > 0 && e.confidence > 0.8) {
    console.log(`${e.hz.toFixed(1)} Hz at ${e.positionMs} ms`);
  }
});

await AudioEngineModule.startInputMonitor({ minHz: 70, maxHz: 1200 });
// ... later
await AudioEngineModule.stopInputMonitor();
sub.remove();
```

## API

- `startInputMonitor(config?: InputMonitorConfig): Promise<void>` starts the
  monitor. Calling it while it runs applies the new config.
- `stopInputMonitor(): Promise<void>` stops it. It never stops a recording.
- `isInputMonitoring(): boolean`
- `getInputLevel(): number` returns the RMS of the latest analysed window while
  the monitor runs (and the input level while recording, as before).

```ts
interface InputMonitorConfig {
  minHz?: number;     // default 50
  maxHz?: number;     // default 1500
  intervalMs?: number; // default 25, minimum 10
}

interface InputPitchEvent {
  hz: number;          // 0 when no pitch was detected in the window
  confidence: number;  // 0..1, 0 when hz is 0
  level: number;       // RMS of the analysed window, linear 0..1
  positionMs: number;  // transport position, -1 when nothing is loaded or the transport is stopped
  timestampMs: number; // wall clock, ms since the epoch
}
```

`minHz` is raised to 32 Hz and `maxHz` capped at 4 kHz if they are outside the
detector's range; a range with `minHz >= maxHz` is rejected.

### Errors

| code | when |
| --- | --- |
| `INPUT_MONITOR_PERMISSION_DENIED` | The record permission is not granted. Request it and retry. |
| `INPUT_MONITOR_START_FAILED` | The input stream, the audio session or the engine could not start, or the config is invalid; the message says why. |

Both are also emitted as `error` events with `source: 'recording'`,
`severity: 'warning'`.

## Behaviour

- **Independent of playback and recording.** The monitor runs with nothing
  loaded, with tracks loaded and stopped, while playing and while recording.
  Starting or stopping a recording does not interrupt it; stopping it does not
  stop a recording.
- **One input stream.** On Android the monitor and `startRecording()` share one
  Oboe input stream: whichever starts first opens it (the monitor opens it mono
  at the engine sample rate; a recording started afterwards uses that format and
  reports it in its result), and it closes when neither uses it. On iOS the
  monitor reads the same input tap as the recording; it starts the audio engine
  if nothing is playing, and pausing or stopping playback leaves the engine
  running while the monitor is on.
- **Background.** When the app goes to the background and no recording is
  running, the module stops the monitor and emits `engineStateChanged` with
  `reason: 'inputMonitorStopped'` and `cause: 'background'`. Restart it when the
  app returns to the foreground. On iOS an audio session interruption (a call,
  Siri) stops it as well, with `cause: 'interrupted'`. `release()` stops it.
- **Rate and coalescing.** One event per analysed window, about 40 per second at
  the default interval. Events are dropped, not queued, when more than a few are
  waiting to be delivered. On iOS the input tap hands over audio in blocks (up
  to about 100 ms), so events can arrive a few at a time; `timestampMs` and
  `positionMs` are taken when each window is analysed.
- **Position.** `positionMs` is the transport position when the window was
  analysed, without input latency compensation.

## Detector

YIN (de Cheveigné & Kawahara, 2002) with the cumulative mean normalised
difference, an absolute threshold of 0.15, parabolic interpolation of the
minimum and confidence `1 - d'(τ)`. The input is mixed to mono, low-passed
(63-tap windowed sinc at 7 kHz) and resampled to 16 kHz; each analysis window
is the newest 1024 samples (64 ms), analysed every `intervalMs`. Windows whose
RMS is under -50 dBFS report `hz: 0`.

The same algorithm runs on both platforms: C++ on Android
(`packages/android-engine/.../cpp/analysis/PitchDetector.cpp`) and a Swift port
on iOS (`packages/expo-module/ios/AudioEngine/InputPitchAnalyzer.swift`). Analysis
never runs on the audio callback: Android copies the input into a ring buffer on
the Oboe callback and analyses it on a worker thread; iOS analyses on the
engine's serial state queue.

Measured on the reference signals (host tests `yarn test:cpp:host`, Swift check
`yarn test:swift:pitch`, 48 kHz and 44.1 kHz input):

| signal | result |
| --- | --- |
| 220 Hz sine, -20 dBFS | every window within 0.17 %, confidence > 0.99 |
| 1 kHz sine, -20 dBFS | every window within 0.19 %, confidence > 0.99 |
| A440 + 2nd/3rd harmonics at -6/-12 dB | 440 Hz, within 0.04 %, no octave error |
| silence, white noise at -40 dBFS | `hz: 0` in every window |
| linear glide 200 → 400 Hz over 2 s | within 0.6 % of the glide at the window centre |

Note names, cents, scoring, octave folding and smoothing are left to the app.
