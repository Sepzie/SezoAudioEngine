# Android Engine API

## Lifecycle

- `initialize(sampleRate: Int = 44100, maxTracks: Int = 8): Boolean`
- `release()`
- `destroy()`

## Track Management

- `loadTrack(trackId: String, filePath: String, startTimeMs: Double = 0.0): Boolean`
- `unloadTrack(trackId: String): Boolean`
- `unloadAllTracks()`

## Playback

- `play()`
- `pause()`
- `stop()`
- `seek(positionMs: Double)`
- `isPlaying(): Boolean`
- `getCurrentPosition(): Double`
- `getDuration(): Double`

## Per-Track Controls

- `setTrackVolume(trackId: String, volume: Float)`
- `setTrackMuted(trackId: String, muted: Boolean)`
- `setTrackSolo(trackId: String, solo: Boolean)`
- `setTrackPan(trackId: String, pan: Float)`
- `setTrackPitch(trackId: String, semitones: Float)`
- `getTrackPitch(trackId: String): Float`
- `setTrackSpeed(trackId: String, rate: Float)`
- `getTrackSpeed(trackId: String): Float`

## Master Controls

- `setMasterVolume(volume: Float)`
- `getMasterVolume(): Float`
- `setPitch(semitones: Float)`
- `getPitch(): Float`
- `setSpeed(rate: Float)`
- `getSpeed(): Float`

## Recording

- `startRecording(outputPath: String, sampleRate: Int = 44100, channels: Int = 1, format: String = "aac", bitrate: Int = 128000, bitsPerSample: Int = 16): Boolean`
- `stopRecording(): RecordingResult`
- `isRecording(): Boolean`
- `setRecordingVolume(volume: Float)`

## Extraction

- `extractTrack(trackId: String, outputPath: String, format: String = "wav", bitrate: Int = 128000, bitsPerSample: Int = 16, includeEffects: Boolean = true): ExtractionResult`
- `startExtractTrack(trackId: String, outputPath: String, format: String = "wav", bitrate: Int = 128000, bitsPerSample: Int = 16, includeEffects: Boolean = true): Long`
- `extractAllTracks(outputPath: String, format: String = "wav", bitrate: Int = 128000, bitsPerSample: Int = 16, includeEffects: Boolean = true): ExtractionResult`
- `startExtractAllTracks(outputPath: String, format: String = "wav", bitrate: Int = 128000, bitsPerSample: Int = 16, includeEffects: Boolean = true): Long`
- `cancelExtraction(jobId: Long): Boolean`

## Meters

- `getInputLevel(): Float`
