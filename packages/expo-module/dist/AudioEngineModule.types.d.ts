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
    format?: 'aac' | 'mp3' | 'wav';
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
    format: 'aac' | 'mp3' | 'wav';
    bitrate?: number;
    fileSize: number;
}
export interface ExtractionConfig {
    format?: 'aac' | 'mp3' | 'wav';
    bitrate?: number;
    includeEffects?: boolean;
    outputDir?: string;
}
export interface ExtractionResult {
    trackId?: string;
    uri: string;
    duration: number;
    format: 'aac' | 'mp3' | 'wav';
    bitrate?: number;
    fileSize: number;
}
export interface MediaMetadata {
    title: string;
    artist?: string;
    album?: string;
    artwork?: string;
}
export type AudioEngineEvent = 'playbackStateChange' | 'positionUpdate' | 'playbackComplete' | 'trackLoaded' | 'trackUnloaded' | 'recordingStarted' | 'recordingStopped' | 'extractionProgress' | 'extractionComplete' | 'error';
export interface AudioEngineError {
    code: string;
    message: string;
    details?: unknown;
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
    setTrackPitch(trackId: string, semitones: number): void;
    getTrackPitch(trackId: string): number;
    setTrackSpeed(trackId: string, rate: number): void;
    getTrackSpeed(trackId: string): number;
    setMasterVolume(volume: number): void;
    getMasterVolume(): number;
    setPitch(semitones: number): void;
    getPitch(): number;
    setSpeed(rate: number): void;
    getSpeed(): number;
    setTempoAndPitch(tempo: number, pitch: number): void;
    startRecording(config?: RecordingConfig): Promise<void>;
    stopRecording(): Promise<RecordingResult>;
    isRecording(): boolean;
    setRecordingVolume(volume: number): void;
    extractTrack(trackId: string, config?: ExtractionConfig): Promise<ExtractionResult>;
    extractAllTracks(config?: ExtractionConfig): Promise<ExtractionResult[]>;
    cancelExtraction(jobId?: number): boolean;
    getInputLevel(): number;
    getOutputLevel(): number;
    getTrackLevel(trackId: string): number;
    enableBackgroundPlayback(metadata: MediaMetadata): Promise<void>;
    updateNowPlayingInfo(metadata: Partial<MediaMetadata>): void;
    disableBackgroundPlayback(): Promise<void>;
    addListener(event: AudioEngineEvent, callback: Function): {
        remove: () => void;
    };
}
