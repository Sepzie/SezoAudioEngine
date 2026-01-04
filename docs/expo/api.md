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

## Background Playback (Planned)

- `enableBackgroundPlayback(metadata: MediaMetadata): Promise<void>`
- `updateNowPlayingInfo(metadata: Partial<MediaMetadata>): void`
- `disableBackgroundPlayback(): Promise<void>`
