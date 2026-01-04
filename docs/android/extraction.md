# Extraction

Use extraction to render a track or the full mix to a file with effects applied.

## Extract a Single Track

```kotlin
val result = engine.extractTrack(
  trackId = "backing",
  outputPath = "/sdcard/Music/backing_fx.wav",
  format = "wav",
  includeEffects = true
)
```

## Extract All Tracks (Mixdown)

```kotlin
val result = engine.extractAllTracks(
  outputPath = "/sdcard/Music/mixdown.aac",
  format = "aac",
  bitrate = 128000
)
```

## Async Extraction

For long exports, use the async APIs and track progress:

- `startExtractTrack(...)` returns a job ID
- `startExtractAllTracks(...)` returns a job ID
- `cancelExtraction(jobId)` cancels the job

Progress and completion callbacks are exposed from the Kotlin wrapper.
