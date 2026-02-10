# Expo Module API

The API is defined in `packages/expo-module/src/AudioEngineModule.types.ts`.

## Core

- `initialize(config: AudioEngineConfig): Promise<void>`
- `release(): Promise<void>`
- `loadTracks(tracks: AudioTrack[]): Promise<void>`
- `unloadTrack(trackId: string): Promise<void>`
- `unloadAllTracks(): Promise<void>`
- `getLoadedTracks(): AudioTrack[]`

## Playback

- `play(): void`
- `pause(): void`
- `stop(): void`
- `seek(positionMs: number): void`
- `isPlaying(): boolean`
- `getCurrentPosition(): number`
- `getDuration(): number`

## Track Controls

- `setTrackVolume(trackId: string, volume: number): void`
- `setTrackMuted(trackId: string, muted: boolean): void`
- `setTrackSolo(trackId: string, solo: boolean): void`
- `setTrackPan(trackId: string, pan: number): void`
- `setTrackPitch(trackId: string, semitones: number): void`
- `getTrackPitch(trackId: string): number`
- `setTrackSpeed(trackId: string, rate: number): void`
- `getTrackSpeed(trackId: string): number`

## Master Controls

- `setMasterVolume(volume: number): void`
- `getMasterVolume(): number`
- `setPitch(semitones: number): void`
- `getPitch(): number`
- `setSpeed(rate: number): void`
- `getSpeed(): number`
- `setTempoAndPitch(tempo: number, pitch: number): void`

## Recording

- `startRecording(config?: RecordingConfig): Promise<void>`
- `stopRecording(): Promise<RecordingResult>`
- `isRecording(): boolean`
- `setRecordingVolume(volume: number): void`

## Extraction

- `extractTrack(trackId: string, config?: ExtractionConfig): Promise<ExtractionResult>`
- `extractAllTracks(config?: ExtractionConfig): Promise<ExtractionResult[]>`
- `cancelExtraction(jobId?: number): boolean`

## Meters

- `getInputLevel(): number`
- `getOutputLevel(): number`
- `getTrackLevel(trackId: string): number`

## Background Playback

- `enableBackgroundPlayback(metadata: MediaMetadata): Promise<void>`
- `updateNowPlayingInfo(metadata: Partial<MediaMetadata>): void`
- `disableBackgroundPlayback(): Promise<void>`

## Events

- `playbackStateChange`
- `positionUpdate`
- `playbackComplete`
- `trackLoaded`
- `trackUnloaded`
- `recordingStarted`
- `recordingStopped`
- `extractionProgress`
- `extractionComplete`
- `engineStateChanged`
- `debugLog`
- `error`

Listener signature:

```ts
addListener<K extends keyof AudioEngineEventMap>(
  event: K,
  callback: (payload: AudioEngineEventMap[K]) => void
): { remove: () => void };
```

## Error Payload

`error` events use a canonical shape on both platforms:

```ts
{
  code: string;
  message: string;
  details?: unknown;
  severity: 'warning' | 'fatal';
  recoverable: boolean;
  source: 'engine' | 'session' | 'playback' | 'recording' | 'extraction' | 'focus' | 'system';
  timestampMs: number;
  platform: 'ios' | 'android';
}
```

## Error Code Policy

| code | source | severity | recoverable | expected app behavior |
| --- | --- | --- | --- | --- |
| `ENGINE_INIT_FAILED` | `engine` | `fatal` | `false` | Show blocking error and disable engine-dependent controls. |
| `ENGINE_START_FAILED` | `engine` | `fatal` | `false` | Show blocking playback/recording error; require user retry or restart flow. |
| `STREAM_DISCONNECTED` | `engine` | `fatal` | `false` | Show blocking error and prompt user retry/reopen audio session. |
| `AUDIO_SESSION_FAILED` | `session` | `fatal` | `false` | Show blocking error and stop active media actions. |
| `AUDIO_FOCUS_DENIED` | `focus` | `warning` | `true` | Log and show lightweight non-blocking notice; allow retry. |
| `PLAYBACK_START_FAILED` | `playback` | `warning` | `true` | Log and show inline playback failure state. |
| `NO_TRACKS_LOADED` | `playback` | `warning` | `true` | Log only or show inline validation message. |
| `TRACK_LOAD_FAILED` | `engine` | `warning` | `true` | Show per-track failure state; keep app usable. |
| `TRACK_UNLOAD_FAILED` | `engine` | `warning` | `true` | Log only unless unload is required for current flow. |
| `RECORDING_START_FAILED` | `recording` | `warning` | `true` | Show recording-blocking message with retry action. |
| `RECORDING_STOP_FAILED` | `recording` | `warning` | `true` | Show stop/save failure; keep session recoverable. |
| `EXTRACTION_FAILED` | `extraction` | `warning` | `true` | Show export failure state and allow retry. |
| `EXTRACTION_CANCELLED` | `extraction` | `warning` | `true` | Log only or show non-blocking cancelled status. |
| `UNSUPPORTED_URI` | `system` | `warning` | `true` | Show actionable validation message for URI format. |

`MediaMetadata` supports:

- `title`, `artist`, `album`, `artwork`
- `logo` (app/brand logo resource name or local file path)
- `playbackCard` (Android notification card options):
  - `smallIcon`
  - `accentColor`
  - `showPrevious`
  - `showNext`
  - `showStop`
  - `seekStepMs`
