import { requireNativeModule } from 'expo-modules-core';
console.log('[AudioEngineModule.ts] Loading NativeAudioEngineModule...');
const NativeAudioEngineModule = requireNativeModule('ExpoAudioEngineModule');
console.log('[AudioEngineModule.ts] NativeAudioEngineModule loaded:', NativeAudioEngineModule);
console.log('[AudioEngineModule.ts] extractTrack function exists?', typeof NativeAudioEngineModule.extractTrack);
console.log('[AudioEngineModule.ts] extractAllTracks function exists?', typeof NativeAudioEngineModule.extractAllTracks);
export const AudioEngineModule = {
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
    setTrackPitch: (trackId, semitones) => NativeAudioEngineModule.setTrackPitch(trackId, semitones),
    getTrackPitch: (trackId) => NativeAudioEngineModule.getTrackPitch(trackId),
    setTrackSpeed: (trackId, rate) => NativeAudioEngineModule.setTrackSpeed(trackId, rate),
    getTrackSpeed: (trackId) => NativeAudioEngineModule.getTrackSpeed(trackId),
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
    extractTrack: (trackId, config) => {
        console.log('[AudioEngineModule.ts] extractTrack called', { trackId, config });
        const result = NativeAudioEngineModule.extractTrack(trackId, config);
        console.log('[AudioEngineModule.ts] extractTrack native call completed');
        return result;
    },
    extractAllTracks: (config) => {
        console.log('[AudioEngineModule.ts] extractAllTracks called', { config });
        const result = NativeAudioEngineModule.extractAllTracks(config);
        console.log('[AudioEngineModule.ts] extractAllTracks native call completed');
        return result;
    },
    getInputLevel: () => NativeAudioEngineModule.getInputLevel(),
    getOutputLevel: () => NativeAudioEngineModule.getOutputLevel(),
    getTrackLevel: (trackId) => NativeAudioEngineModule.getTrackLevel(trackId),
    enableBackgroundPlayback: (metadata) => NativeAudioEngineModule.enableBackgroundPlayback(metadata),
    updateNowPlayingInfo: (metadata) => NativeAudioEngineModule.updateNowPlayingInfo(metadata),
    disableBackgroundPlayback: () => NativeAudioEngineModule.disableBackgroundPlayback(),
    addListener: (event, callback) => NativeAudioEngineModule.addListener(event, callback)
};
