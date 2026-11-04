export type PlaybackState = 'stopped' | 'playing' | 'paused' | 'recording';
export interface AudioEngineConfig {
    sampleRate?: number;
    bufferSize?: number;
    maxTracks?: number;
    enableProcessing?: boolean;
}
export interface AudioTrack {
    id: string;
    uri: string;
    type?: 'local' | 'remote';
    volume?: number;
    pan?: number;
    muted?: boolean;
    startTimeMs?: number;
}
export interface RecordingConfig {
    sampleRate?: number;
    channels?: number;
    format?: 'aac' | 'm4a' | 'mp3' | 'wav';
    bitrate?: number;
    quality?: 'low' | 'medium' | 'high';
    enableNoiseGate?: boolean;
    enableNormalization?: boolean;
}
export interface RecordingResult {
    uri: string;
    duration: number;
    startTimeMs: number;
    startTimeSamples?: number;
    sampleRate: number;
    channels: number;
    format: 'aac' | 'm4a' | 'mp3' | 'wav';
    bitrate?: number;
    fileSize: number;
}
export type RecordingStopReason = 'requested' | 'interrupted';
export interface InputMonitorConfig {
    /** Lowest / highest fundamental reported, Hz. Default 50 / 1500. */
    minHz?: number;
    maxHz?: number;
    /** Target interval between `inputPitch` events, ms. Default 25, minimum 10. */
    intervalMs?: number;
}
export interface InputPitchEvent {
    /** Fundamental frequency in Hz; 0 when no pitch was detected in the window. */
    hz: number;
    /** Detector confidence, 0..1 (0 when `hz` is 0). */
    confidence: number;
    /** RMS level of the analysed window, linear 0..1. */
    level: number;
    /** Transport position (ms) at the end of the analysed window; -1 when the transport has no position (nothing loaded or stopped). */
    positionMs: number;
    /** Wall clock, ms since epoch, when the window was analysed. */
    timestampMs: number;
}
export interface ExtractionConfig {
    format?: 'aac' | 'm4a' | 'mp3' | 'wav';
    bitrate?: number;
    includeEffects?: boolean;
    outputDir?: string;
}
export interface ExtractionResult {
    trackId?: string;
    uri: string;
    duration: number;
    format: 'aac' | 'm4a' | 'mp3' | 'wav';
    bitrate?: number;
    fileSize: number;
}
export interface MediaMetadata {
    title: string;
    artist?: string;
    album?: string;
    artwork?: string;
    logo?: string;
    playbackCard?: PlaybackCardOptions;
}
export interface PlaybackCardOptions {
    smallIcon?: string;
    accentColor?: string | number;
    showPrevious?: boolean;
    showNext?: boolean;
    showStop?: boolean;
    seekStepMs?: number;
}
export type AudioEngineEvent = 'playbackStateChange' | 'positionUpdate' | 'playbackComplete' | 'trackLoaded' | 'trackUnloaded' | 'recordingStarted' | 'recordingStopped' | 'extractionProgress' | 'extractionComplete' | 'engineStateChanged' | 'inputPitch' | 'debugLog' | 'error';
export type AudioEngineErrorSeverity = 'warning' | 'fatal';
export type AudioEngineErrorSource = 'engine' | 'session' | 'playback' | 'recording' | 'extraction' | 'focus' | 'system';
export interface AudioEngineError {
    code: string;
    message: string;
    details?: unknown;
    severity: AudioEngineErrorSeverity;
    recoverable: boolean;
    source: AudioEngineErrorSource;
    timestampMs: number;
    platform: 'ios' | 'android';
}
export interface AudioEngineEventMap {
    playbackStateChange: {
        state: string;
        positionMs: number;
        durationMs: number;
    };
    positionUpdate: {
        positionMs: number;
        durationMs: number;
    };
    playbackComplete: {
        positionMs: number;
        durationMs: number;
    };
    trackLoaded: {
        trackId: string;
    };
    trackUnloaded: {
        trackId: string;
    };
    recordingStarted: Record<string, never>;
    recordingStopped: {
        uri: string;
        duration: number;
        startTimeMs: number;
        startTimeSamples?: number;
        sampleRate: number;
        channels: number;
        format: 'aac' | 'm4a' | 'mp3' | 'wav';
        fileSize: number;
        /**
         * Why the take ended: `requested` for `stopRecording()`, `interrupted` when
         * the engine finalised it itself (iOS audio session interruption). Absent
         * from older module versions.
         */
        reason?: RecordingStopReason;
    };
    extractionProgress: Record<string, unknown>;
    extractionComplete: Record<string, unknown>;
    engineStateChanged: {
        reason: string;
        [key: string]: unknown;
    };
    /** One analysed microphone window while the input monitor runs (about every `intervalMs`). */
    inputPitch: InputPitchEvent;
    debugLog: {
        level: 'debug' | 'info' | 'warn' | 'error';
        message: string;
        context?: unknown;
    };
    error: AudioEngineError;
}
export interface AudioEngine {
    initialize(config: AudioEngineConfig): Promise<void>;
    release(): Promise<void>;
    loadTracks(tracks: AudioTrack[]): Promise<void>;
    unloadTrack(trackId: string): Promise<void>;
    unloadAllTracks(): Promise<void>;
    getLoadedTracks(): AudioTrack[];
    play(): void;
    pause(): void;
    stop(): void;
    seek(positionMs: number): void;
    isPlaying(): boolean;
    getCurrentPosition(): number;
    getDuration(): number;
    setTrackVolume(trackId: string, volume: number): void;
    setTrackMuted(trackId: string, muted: boolean): void;
    setTrackSolo(trackId: string, solo: boolean): void;
    setTrackPan(trackId: string, pan: number): void;
    /**
     * Sets the track's own pitch shift in semitones. It is an offset from the master
     * pitch: the track plays at `trackPitch + masterPitch` (see `setPitch`), on iOS and
     * Android. The composed value is clamped to the engine's range (±12 semitones on Android).
     */
    setTrackPitch(trackId: string, semitones: number): void;
    /** The track's own pitch from `setTrackPitch` (0 by default), without the master pitch. */
    getTrackPitch(trackId: string): number;
    /**
     * Sets the track's own playback rate. It multiplies the master speed: the track plays
     * at `trackSpeed * masterSpeed` (see `setSpeed`), on iOS and Android. The composed rate
     * is clamped to the engine's range (0.5x to 2x on Android).
     */
    setTrackSpeed(trackId: string, rate: number): void;
    /** The track's own rate from `setTrackSpeed` (1 by default), without the master speed. */
    getTrackSpeed(trackId: string): number;
    setMasterVolume(volume: number): void;
    getMasterVolume(): number;
    /**
     * Sets the master pitch shift in semitones. It is added to every track's own pitch
     * (`setTrackPitch`) and leaves those values unchanged; tracks loaded later get it too.
     */
    setPitch(semitones: number): void;
    /** The master pitch from `setPitch` (0 by default). */
    getPitch(): number;
    /**
     * Sets the master playback rate. It multiplies every track's own rate
     * (`setTrackSpeed`) and leaves those values unchanged; tracks loaded later get it too.
     */
    setSpeed(rate: number): void;
    /** The master rate from `setSpeed` (1 by default). */
    getSpeed(): number;
    setTempoAndPitch(tempo: number, pitch: number): void;
    startRecording(config?: RecordingConfig): Promise<void>;
    stopRecording(): Promise<RecordingResult>;
    isRecording(): boolean;
    setRecordingVolume(volume: number): void;
    extractTrack(trackId: string, config?: ExtractionConfig): Promise<ExtractionResult>;
    extractAllTracks(config?: ExtractionConfig): Promise<ExtractionResult[]>;
    cancelExtraction(jobId?: number): boolean;
    /**
     * Starts the input pitch monitor: the microphone's fundamental frequency is
     * reported as `inputPitch` events, independently of playback and recording.
     * The module never asks for the record permission; it rejects with
     * `INPUT_MONITOR_PERMISSION_DENIED` when it is not granted, and with
     * `INPUT_MONITOR_START_FAILED` when the input stream or the engine cannot
     * start. Calling it while running applies the new config.
     */
    startInputMonitor(config?: InputMonitorConfig): Promise<void>;
    /** Stops the input pitch monitor. Never stops a recording. */
    stopInputMonitor(): Promise<void>;
    isInputMonitoring(): boolean;
    /** Input RMS while monitoring or recording, linear 0..1. */
    getInputLevel(): number;
    getOutputLevel(): number;
    getTrackLevel(trackId: string): number;
    enableBackgroundPlayback(metadata: MediaMetadata): Promise<void>;
    updateNowPlayingInfo(metadata: Partial<MediaMetadata>): void;
    disableBackgroundPlayback(): Promise<void>;
    addListener<K extends keyof AudioEngineEventMap>(event: K, callback: (payload: AudioEngineEventMap[K]) => void): {
        remove: () => void;
    };
}
