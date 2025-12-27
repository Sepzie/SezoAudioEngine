import { requireNativeModule } from 'expo-modules-core';
import type { AudioEngine } from './AudioEngineModule.types';

const NativeAudioEngineModule = requireNativeModule('ExpoAudioEngineModule');

export const AudioEngineModule: AudioEngine = {
  initialize: (config) => NativeAudioEngineModule.initialize(config),
  release: () => NativeAudioEngineModule.release(),

  loadTracks: (tracks) => NativeAudioEngineModule.loadTracks(tracks),
  unloadTrack: (trackId) => NativeAudioEngineModule.unloadTrack(trackId),
  unloadAllTracks: () => NativeAudioEngineModule.unloadAllTracks(),
  getLoadedTracks: () => NativeAudioEngineModule.getLoadedTracks(),

  play: () => NativeAudioEngineModule.play(),
  pause: () => NativeAudioEngineModule.pause(),
  stop: () => NativeAudioEngineModule.stop(),
  seek: (positionMs) => NativeAudioEngineModule.seek(positionMs),
  isPlaying: () => NativeAudioEngineModule.isPlaying(),
  getCurrentPosition: () => NativeAudioEngineModule.getCurrentPosition(),
  getDuration: () => NativeAudioEngineModule.getDuration(),

  setTrackVolume: (trackId, volume) => NativeAudioEngineModule.setTrackVolume(trackId, volume),
  setTrackMuted: (trackId, muted) => NativeAudioEngineModule.setTrackMuted(trackId, muted),
  setTrackSolo: (trackId, solo) => NativeAudioEngineModule.setTrackSolo(trackId, solo),
  setTrackPan: (trackId, pan) => NativeAudioEngineModule.setTrackPan(trackId, pan),

  setMasterVolume: (volume) => NativeAudioEngineModule.setMasterVolume(volume),
  getMasterVolume: () => NativeAudioEngineModule.getMasterVolume(),

  setPitch: (semitones) => NativeAudioEngineModule.setPitch(semitones),
  getPitch: () => NativeAudioEngineModule.getPitch(),
  setSpeed: (rate) => NativeAudioEngineModule.setSpeed(rate),
  getSpeed: () => NativeAudioEngineModule.getSpeed(),
  setTempoAndPitch: (tempo, pitch) => NativeAudioEngineModule.setTempoAndPitch(tempo, pitch),

  startRecording: (config) => NativeAudioEngineModule.startRecording(config),
  stopRecording: () => NativeAudioEngineModule.stopRecording(),
  isRecording: () => NativeAudioEngineModule.isRecording(),
  setRecordingVolume: (volume) => NativeAudioEngineModule.setRecordingVolume(volume),

  extractTrack: (trackId, config) => NativeAudioEngineModule.extractTrack(trackId, config),
  extractAllTracks: (config) => NativeAudioEngineModule.extractAllTracks(config),

  getInputLevel: () => NativeAudioEngineModule.getInputLevel(),
  getOutputLevel: () => NativeAudioEngineModule.getOutputLevel(),
  getTrackLevel: (trackId) => NativeAudioEngineModule.getTrackLevel(trackId),

  enableBackgroundPlayback: (metadata) => NativeAudioEngineModule.enableBackgroundPlayback(metadata),
  updateNowPlayingInfo: (metadata) => NativeAudioEngineModule.updateNowPlayingInfo(metadata),
  disableBackgroundPlayback: () => NativeAudioEngineModule.disableBackgroundPlayback(),

  addListener: (event, callback) => NativeAudioEngineModule.addListener(event, callback)
};
