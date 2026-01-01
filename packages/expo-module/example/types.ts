export interface Track {
  id: string;
  name: string;
  uri: string;
  volume: number;
  pan: number;
  muted: boolean;
  solo: boolean;
  pitch: number;
  speed: number;
  startTimeMs?: number;
}

export interface ExtractionInfo {
  trackId?: string;
  uri: string;
  outputPath?: string;
  duration: number;
  format: string;
  fileSize: number;
  bitrate?: number;
}

export interface RecordingInfo {
  uri: string;
  duration: number;
  startTimeMs?: number;
  startTimeSamples?: number;
  sampleRate: number;
  channels: number;
  format: string;
  fileSize: number;
  bitrate?: number;
}
