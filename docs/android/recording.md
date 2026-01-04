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
```

## Formats

- `aac` (MediaCodec) is the recommended default.
- `mp3` requires LAME support to be enabled.
- `wav` is lossless but larger on disk.
