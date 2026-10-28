# Recording

The Android engine can record microphone input while playback runs.

## Start Recording

```kotlin
val outputPath = "/sdcard/Music/recording.aac"
val ok = engine.startRecording(
  outputPath = outputPath,
  sampleRate = 44100,
  channels = 1,
  format = "aac",
  bitrate = 128000,
  bitsPerSample = 16
)
```

## Stop Recording

```kotlin
val result = engine.stopRecording()
if (!result.success) {
  throw IllegalStateException(result.errorMessage ?: "Recording failed")
}
// result.outputPath is complete and playable as soon as stopRecording() returns.
// result.sampleRate / result.channels are what the input stream delivered
// (they can differ from what was requested); result.durationMs derives from them.
```

Encoding runs while the take is captured: the microphone callback fills a
ring buffer (about two seconds), and a worker thread drains it into the
encoder as data arrives. `stopRecording()` only has to encode what is still
in the ring and finalise the container, so it returns in milliseconds whether
the take lasted ten seconds or ten minutes, and memory never holds the whole
recording.

## Formats

- `aac` (MediaCodec) is the recommended default.
- `mp3` requires LAME support to be enabled.
- `wav` is lossless but larger on disk.
