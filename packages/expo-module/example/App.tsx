import { useEffect, useState, useCallback, useRef } from 'react';
import {
  Animated,
  StyleSheet,
  Text,
  View,
  TouchableOpacity,
  ScrollView,
  Alert,
  Linking,
  Platform,
  Share,
  PermissionsAndroid,
} from 'react-native';
import { AudioEngineModule } from 'sezo-audio-engine';
import Slider from '@react-native-community/slider';
import { Asset } from 'expo-asset';
import * as FileSystem from 'expo-file-system';
import * as IntentLauncher from 'expo-intent-launcher';

interface Track {
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

interface ExtractionInfo {
  trackId?: string;
  uri: string;
  outputPath?: string;
  duration: number;
  format: string;
  fileSize: number;
  bitrate?: number;
}

interface RecordingInfo {
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

const theme = {
  colors: {
    background: '#0b0f10',
    surface: '#141b1f',
    surfaceAlt: '#1b252a',
    surfaceStrong: '#1f2b31',
    text: '#f5f7f9',
    textMuted: '#8a949c',
    accent: '#ffb454',
    accentStrong: '#ff8a3d',
    success: '#23d18b',
    danger: '#ff5d5d',
    border: '#253038',
    track: '#2a343a',
  },
  radius: {
    card: 18,
    pill: 999,
    button: 12,
  },
};

const getMimeType = (format: string) => {
  switch (format) {
    case 'aac':
      return 'audio/aac';
    case 'mp3':
      return 'audio/mpeg';
    case 'wav':
      return 'audio/wav';
    default:
      return 'audio/*';
  }
};

const getAndroidContentUri = async (uri: string) => {
  if (Platform.OS !== 'android') {
    return uri;
  }
  if (uri.startsWith('content://')) {
    return uri;
  }
  if (uri.startsWith('file://')) {
    return FileSystem.getContentUriAsync(uri);
  }
  return uri;
};

const isExtractionCancelledMessage = (message?: string) => {
  return typeof message === 'string' && message.toLowerCase().includes('cancel');
};

const isExtractionCancellationError = (error: any) => {
  if (error?.code === 'EXTRACTION_CANCELLED') {
    return true;
  }
  return (
    isExtractionCancelledMessage(error?.message) ||
    isExtractionCancelledMessage(error?.errorMessage)
  );
};

export default function App() {
  const [status, setStatus] = useState('Idle');
  const [isPlaying, setIsPlaying] = useState(false);
  const [position, setPosition] = useState(0);
  const [duration, setDuration] = useState(0);
  const [masterVolume, setMasterVolume] = useState(1.0);
  const [masterPitch, setMasterPitch] = useState(0.0);
  const [masterSpeed, setMasterSpeed] = useState(1.0);
  const [tracks, setTracks] = useState<Track[]>([]);
  const [isRecording, setIsRecording] = useState(false);
  const [recordingStatus, setRecordingStatus] = useState('Idle');
  const [recordingFormat, setRecordingFormat] = useState<'wav' | 'aac' | 'mp3'>('aac');
  const [recordingQuality, setRecordingQuality] = useState<'low' | 'medium' | 'high'>(
    'medium'
  );
  const [recordingChannels, setRecordingChannels] = useState<1 | 2>(1);
  const [recordingSampleRate, setRecordingSampleRate] = useState<44100 | 48000>(44100);
  const [recordingVolume, setRecordingVolume] = useState(1.0);
  const [recordingLevel, setRecordingLevel] = useState(0);
  const [recordingElapsed, setRecordingElapsed] = useState(0);
  const [lastRecording, setLastRecording] = useState<RecordingInfo | null>(null);
  const [extractionProgress, setExtractionProgress] = useState(0);
  const [extractionStatus, setExtractionStatus] = useState('Idle');
  const [extractionFormat, setExtractionFormat] = useState<'wav' | 'aac' | 'mp3'>('aac');
  const [extractionIncludeEffects, setExtractionIncludeEffects] = useState(true);
  const [lastExtraction, setLastExtraction] = useState<ExtractionInfo | null>(null);
  const [isExtracting, setIsExtracting] = useState(false);
  const [extractionJobId, setExtractionJobId] = useState<number | null>(null);
  const recordingStartRef = useRef<number | null>(null);
  const loadedRecordingUris = useRef(new Set<string>());
  const recordingCountRef = useRef(1);
  const engineAny = AudioEngineModule as Record<string, any>;
  const supportsTrackPitch = typeof engineAny.setTrackPitch === 'function';
  const supportsTrackSpeed = typeof engineAny.setTrackSpeed === 'function';
  const supportsRecording = typeof engineAny.startRecording === 'function';
  const recordingUnavailable = !supportsRecording || Platform.OS === 'ios';
  const introAnim = useState(() => new Animated.Value(0))[0];
  const controlsAnim = useState(() => new Animated.Value(0))[0];
  const tracksAnim = useState(() => new Animated.Value(0))[0];

  // Initialize engine
  useEffect(() => {
    let mounted = true;
    AudioEngineModule.initialize({ sampleRate: 44100, maxTracks: 4 })
      .then(() => {
        if (mounted) {
          setStatus('Ready');
          setRecordingStatus('Ready');
        }
      })
      .catch((error) => {
        if (mounted) {
          setStatus('Init failed');
          console.error('Init error:', error);
        }
      });

    return () => {
      mounted = false;
      AudioEngineModule.stop();
      AudioEngineModule.unloadAllTracks();
      AudioEngineModule.release().catch(() => {});
    };
  }, []);

  useEffect(() => {
    const progressSub = AudioEngineModule.addListener('extractionProgress', (event: any) => {
      const progress = typeof event?.progress === 'number' ? event.progress : 0;
      const jobId = typeof event?.jobId === 'number' ? event.jobId : null;
      const operation = event?.operation === 'mix' ? 'Mix' : 'Track';
      setExtractionProgress(progress);
      setExtractionStatus(`${operation} export ${Math.round(progress * 100)}%`);
      setIsExtracting(true);
      if (jobId !== null) {
        setExtractionJobId(jobId);
      }
    });

    const completeSub = AudioEngineModule.addListener('extractionComplete', (event: any) => {
      const success = event?.success !== false;
      const errorMessage =
        typeof event?.errorMessage === 'string' ? event.errorMessage : 'Unknown error';
      const cancelled = isExtractionCancelledMessage(errorMessage);
      setIsExtracting(false);
      setExtractionJobId(null);
      setExtractionProgress(success ? 1 : 0);
      if (!success) {
        setExtractionStatus(
          cancelled ? 'Export cancelled' : `Export failed: ${errorMessage}`
        );
        return;
      }
      setExtractionStatus('Export complete');
      if (event?.uri) {
        setLastExtraction({
          trackId: event?.trackId,
          uri: event?.uri,
          duration: event?.duration ?? 0,
          format: event?.format ?? extractionFormat,
          fileSize: event?.fileSize ?? 0,
          bitrate: event?.bitrate,
        });
      }
    });

    return () => {
      progressSub?.remove?.();
      completeSub?.remove?.();
    };
  }, [extractionFormat]);

  useEffect(() => {
    const recordingStartedSub = AudioEngineModule.addListener('recordingStarted', () => {
      setRecordingStatus('Recording');
      setIsRecording(true);
      recordingStartRef.current = Date.now();
    });

    const recordingStoppedSub = AudioEngineModule.addListener('recordingStopped', (event: any) => {
      if (event?.uri) {
        handleRecordingResult({
          uri: event.uri,
          duration: event?.duration ?? 0,
          startTimeMs: event?.startTimeMs ?? 0,
          startTimeSamples: event?.startTimeSamples,
          sampleRate: event?.sampleRate ?? 44100,
          channels: event?.channels ?? 1,
          format: event?.format ?? recordingFormat,
          fileSize: event?.fileSize ?? 0,
          bitrate: event?.bitrate,
        });
      }
      recordingStartRef.current = null;
      setRecordingElapsed(0);
      setRecordingLevel(0);
      setIsRecording(false);
      setRecordingStatus(event?.uri ? 'Recording saved' : 'Recording stopped');
    });

    return () => {
      recordingStartedSub?.remove?.();
      recordingStoppedSub?.remove?.();
    };
  }, [handleRecordingResult, recordingFormat]);

  useEffect(() => {
    Animated.stagger(120, [
      Animated.timing(introAnim, {
        toValue: 1,
        duration: 420,
        useNativeDriver: true,
      }),
      Animated.timing(controlsAnim, {
        toValue: 1,
        duration: 420,
        useNativeDriver: true,
      }),
      Animated.timing(tracksAnim, {
        toValue: 1,
        duration: 420,
        useNativeDriver: true,
      }),
    ]).start();
  }, [introAnim, controlsAnim, tracksAnim]);

  // Position update loop
  useEffect(() => {
    if (!isPlaying) return;

    const interval = setInterval(() => {
      try {
        const pos = AudioEngineModule.getCurrentPosition();
        const dur = AudioEngineModule.getDuration();
        setPosition(pos);
        setDuration(dur);

        // Auto-stop at end
        if (pos >= dur && dur > 0) {
          handleStop();
        }
      } catch (error) {
        console.error('Position update error:', error);
      }
    }, 100);

    return () => clearInterval(interval);
  }, [isPlaying]);

  useEffect(() => {
    if (!isRecording) {
      setRecordingElapsed(0);
      setRecordingLevel(0);
      recordingStartRef.current = null;
      return;
    }

    if (!recordingStartRef.current) {
      recordingStartRef.current = Date.now();
    }

    const interval = setInterval(() => {
      try {
        const level = AudioEngineModule.getInputLevel();
        const clamped = Math.max(0, Math.min(1, level));
        setRecordingLevel(clamped);
      } catch (error) {
        setRecordingLevel(0);
      }

      if (recordingStartRef.current) {
        setRecordingElapsed(Date.now() - recordingStartRef.current);
      }
    }, 100);

    return () => clearInterval(interval);
  }, [isRecording]);

  const requestRecordingPermission = useCallback(async () => {
    if (Platform.OS !== 'android') {
      return true;
    }

    try {
      const permission = PermissionsAndroid.PERMISSIONS.RECORD_AUDIO;
      const hasPermission = await PermissionsAndroid.check(permission);
      if (hasPermission) {
        return true;
      }

      const status = await PermissionsAndroid.request(permission, {
        title: 'Microphone access',
        message: 'Allow microphone access to record audio.',
        buttonPositive: 'Allow',
      });
      return status === PermissionsAndroid.RESULTS.GRANTED;
    } catch (error) {
      console.warn('[Recording] Permission request failed', error);
      return false;
    }
  }, []);

  const appendRecordingTrack = useCallback(
    async (recording: RecordingInfo) => {
      if (!recording?.uri) {
        return;
      }

      if (loadedRecordingUris.current.has(recording.uri)) {
        return;
      }

      if (tracks.length >= 4) {
        Alert.alert('Track limit reached', 'Unload a track to add this recording.');
        return;
      }

      const startTimeMs = typeof recording.startTimeMs === 'number' ? recording.startTimeMs : 0;
      const trackId = `recording_${Date.now()}`;
      const trackName = `Recording ${recordingCountRef.current++}`;

      loadedRecordingUris.current.add(recording.uri);

      try {
        await AudioEngineModule.loadTracks([
          {
            id: trackId,
            uri: recording.uri,
            volume: 1.0,
            pan: 0.0,
            muted: false,
            startTimeMs,
          },
        ]);

        setTracks((prev) => [
          ...prev,
          {
            id: trackId,
            name: trackName,
            uri: recording.uri,
            volume: 1.0,
            pan: 0.0,
            muted: false,
            solo: false,
            pitch: 0.0,
            speed: 1.0,
            startTimeMs,
          },
        ]);
        setDuration(AudioEngineModule.getDuration());
      } catch (error) {
        loadedRecordingUris.current.delete(recording.uri);
        console.warn('[Recording] Auto-load failed', error);
      }
    },
    [tracks.length]
  );

  const handleRecordingResult = useCallback(
    (recording: RecordingInfo | null) => {
      if (!recording?.uri) {
        return;
      }
      setLastRecording(recording);
      void appendRecordingTrack(recording);
    },
    [appendRecordingTrack]
  );

  const handleStartRecording = useCallback(async () => {
    if (!supportsRecording) {
      Alert.alert('Recording unavailable', 'Recording is not supported in this build.');
      return;
    }

    if (Platform.OS === 'ios') {
      Alert.alert('Recording unavailable', 'Recording is Android-only for now.');
      return;
    }

    const granted = await requestRecordingPermission();
    if (!granted) {
      setRecordingStatus('Permission denied');
      Alert.alert('Permission denied', 'Microphone access is required to record.');
      return;
    }

    try {
      setRecordingStatus('Starting...');
      await AudioEngineModule.startRecording({
        sampleRate: recordingSampleRate,
        channels: recordingChannels,
        format: recordingFormat,
        quality: recordingQuality,
      });
      AudioEngineModule.setRecordingVolume(recordingVolume);
      recordingStartRef.current = Date.now();
      setRecordingElapsed(0);
      setRecordingLevel(0);
      setIsRecording(true);
      setRecordingStatus('Recording');
    } catch (error: any) {
      console.error('Start recording error:', error);
      setIsRecording(false);
      setRecordingStatus('Start failed');
      Alert.alert('Recording failed', error?.message ?? 'Unable to start recording.');
    }
  }, [
    recordingChannels,
    recordingFormat,
    recordingQuality,
    recordingSampleRate,
    recordingVolume,
    requestRecordingPermission,
    supportsRecording,
  ]);

  const handleStopRecording = useCallback(async () => {
    try {
      setRecordingStatus('Stopping...');
      const result = await AudioEngineModule.stopRecording();
      handleRecordingResult(result as RecordingInfo);
      recordingStartRef.current = null;
      setRecordingElapsed(0);
      setRecordingLevel(0);
      setIsRecording(false);
      setRecordingStatus('Recording saved');
    } catch (error: any) {
      console.error('Stop recording error:', error);
      setRecordingStatus('Stop failed');
      Alert.alert('Recording failed', error?.message ?? 'Unable to stop recording.');
    }
  }, [handleRecordingResult]);

  const handleRecordingVolumeChange = useCallback((value: number) => {
    try {
      AudioEngineModule.setRecordingVolume(value);
      setRecordingVolume(value);
    } catch (error) {
      console.error('Recording volume error:', error);
    }
  }, []);

  const loadTracks = useCallback(async () => {
    try {
      setStatus('Loading tracks...');

      // Load audio assets
      const [asset1, asset2] = await Promise.all([
        Asset.loadAsync(require('./assets/track1.mp3')),
        Asset.loadAsync(require('./assets/track2.mp3')),
      ]);

      const track1Uri = asset1[0].localUri || asset1[0].uri;
      const track2Uri = asset2[0].localUri || asset2[0].uri;

      const newTracks: Track[] = [
        {
          id: 'track1',
          name: 'Track 1',
          uri: track1Uri,
          volume: 1.0,
          pan: 0.0,
          muted: false,
          solo: false,
          pitch: 0.0,
          speed: 1.0,
        },
        {
          id: 'track2',
          name: 'Track 2',
          uri: track2Uri,
          volume: 1.0,
          pan: 0.0,
          muted: false,
          solo: false,
          pitch: 0.0,
          speed: 1.0,
        },
      ];

      // Load tracks into engine
      await AudioEngineModule.loadTracks(
        newTracks.map((t) => ({
          id: t.id,
          uri: t.uri,
          volume: t.volume,
          pan: t.pan,
          muted: t.muted,
          startTimeMs: t.startTimeMs,
        }))
      );

      setTracks(newTracks);
      setDuration(AudioEngineModule.getDuration());
      setStatus('Tracks loaded');
    } catch (error: any) {
      setStatus('Load failed');
      Alert.alert('Error', 'Failed to load tracks: ' + error.message);
      console.error('Load error:', error);
    }
  }, []);

  const handlePlay = useCallback(() => {
    try {
      AudioEngineModule.play();
      setIsPlaying(true);
      setStatus('Playing');
    } catch (error) {
      console.error('Play error:', error);
    }
  }, []);

  const handlePause = useCallback(() => {
    try {
      AudioEngineModule.pause();
      setIsPlaying(false);
      setStatus('Paused');
    } catch (error) {
      console.error('Pause error:', error);
    }
  }, []);

  const handleStop = useCallback(() => {
    try {
      AudioEngineModule.stop();
      setIsPlaying(false);
      setPosition(0);
      setStatus('Stopped');
    } catch (error) {
      console.error('Stop error:', error);
    }
  }, []);

  const handleReset = useCallback(() => {
    try {
      AudioEngineModule.stop();
      AudioEngineModule.seek(0);
      AudioEngineModule.setMasterVolume(1.0);
      AudioEngineModule.setPitch(0.0);
      AudioEngineModule.setSpeed(1.0);

      tracks.forEach((track) => {
        AudioEngineModule.setTrackVolume(track.id, 1.0);
        AudioEngineModule.setTrackPan(track.id, 0.0);
        AudioEngineModule.setTrackMuted(track.id, false);
        AudioEngineModule.setTrackSolo(track.id, false);
        if (supportsTrackPitch) {
          engineAny.setTrackPitch(track.id, 0.0);
        }
        if (supportsTrackSpeed) {
          engineAny.setTrackSpeed(track.id, 1.0);
        }
      });

      setMasterVolume(1.0);
      setMasterPitch(0.0);
      setMasterSpeed(1.0);
      setTracks((prev) =>
        prev.map((track) => ({
          ...track,
          volume: 1.0,
          pan: 0.0,
          muted: false,
          solo: false,
          pitch: supportsTrackPitch ? 0.0 : track.pitch,
          speed: supportsTrackSpeed ? 1.0 : track.speed,
        }))
      );
      setIsPlaying(false);
      setPosition(0);
      setStatus('Reset');
    } catch (error) {
      console.error('Reset error:', error);
    }
  }, [engineAny, supportsTrackPitch, supportsTrackSpeed, tracks]);

  const handleExtractTrack = useCallback(async () => {
    const track = tracks[0];
    if (!track) {
      Alert.alert('No tracks', 'Load at least one track to export.');
      return;
    }

    try {
      setIsExtracting(true);
      setExtractionJobId(null);
      setExtractionProgress(0);
      setExtractionStatus(`Exporting ${track.name}...`);

      const result = await AudioEngineModule.extractTrack(track.id, {
        format: extractionFormat,
        includeEffects: extractionIncludeEffects,
      });
      setLastExtraction({
        ...result,
        outputPath: (result as any)?.outputPath,
      });
      setExtractionStatus(`Exported ${track.name}`);
      setExtractionProgress(1);
    } catch (error: any) {
      if (isExtractionCancellationError(error)) {
        setExtractionStatus('Export cancelled');
        setExtractionProgress(0);
      } else {
        setExtractionStatus('Export failed');
        Alert.alert('Export failed', error?.message ?? 'Unknown error');
      }
    } finally {
      setIsExtracting(false);
    }
  }, [extractionFormat, extractionIncludeEffects, tracks]);

  const handleExtractMix = useCallback(async () => {
    if (tracks.length === 0) {
      Alert.alert('No tracks', 'Load tracks to export a mix.');
      return;
    }

    try {
      setIsExtracting(true);
      setExtractionJobId(null);
      setExtractionProgress(0);
      setExtractionStatus('Exporting mix...');

      const results = await AudioEngineModule.extractAllTracks({
        format: extractionFormat,
        includeEffects: extractionIncludeEffects,
      });

      if (results && results.length > 0) {
        setLastExtraction({
          ...results[0],
          outputPath: (results[0] as any)?.outputPath,
        });
      }
      setExtractionStatus('Exported mix');
      setExtractionProgress(1);
    } catch (error: any) {
      if (isExtractionCancellationError(error)) {
        setExtractionStatus('Export cancelled');
        setExtractionProgress(0);
      } else {
        setExtractionStatus('Export failed');
        Alert.alert('Export failed', error?.message ?? 'Unknown error');
      }
    } finally {
      setIsExtracting(false);
    }
  }, [extractionFormat, extractionIncludeEffects, tracks]);

  const handleCancelExtraction = useCallback(() => {
    if (!isExtracting) {
      return;
    }

    try {
      const didCancel = AudioEngineModule.cancelExtraction(extractionJobId ?? undefined);
      if (didCancel) {
        setExtractionStatus('Cancelling export...');
      } else {
        setExtractionStatus('No active export');
      }
    } catch (error) {
      console.warn('[Extraction] Cancel export failed', error);
      setExtractionStatus('Cancel failed');
    }
  }, [extractionJobId, isExtracting]);

  const handleOpenExport = useCallback(async () => {
    if (!lastExtraction?.uri) {
      Alert.alert('No export', 'Run an export first.');
      return;
    }

    try {
      const openUri = await getAndroidContentUri(lastExtraction.uri);
      if (Platform.OS === 'android') {
        await IntentLauncher.startActivityAsync('android.intent.action.VIEW', {
          data: openUri,
          type: getMimeType(lastExtraction.format),
          flags: 1, // FLAG_GRANT_READ_URI_PERMISSION
        });
      } else {
        await Linking.openURL(openUri);
      }
    } catch (error) {
      console.warn('[Extraction] Open export failed', error);
      Alert.alert('Unable to open file', lastExtraction.uri);
    }
  }, [lastExtraction]);

  const handleShareExport = useCallback(async () => {
    if (!lastExtraction?.uri) {
      Alert.alert('No export', 'Run an export first.');
      return;
    }

    try {
      const shareUri = await getAndroidContentUri(lastExtraction.uri);
      if (Platform.OS === 'android') {
        await IntentLauncher.startActivityAsync('android.intent.action.SEND', {
          type: getMimeType(lastExtraction.format),
          flags: 1, // FLAG_GRANT_READ_URI_PERMISSION
          extra: {
            'android.intent.extra.STREAM': shareUri,
          },
        });
      } else {
        await Share.share({
          message: lastExtraction.uri,
          url: lastExtraction.uri,
        });
      }
    } catch (error) {
      console.warn('[Extraction] Share export failed', error);
    }
  }, [lastExtraction]);

  const handleOpenRecording = useCallback(async () => {
    if (!lastRecording?.uri) {
      Alert.alert('No recording', 'Make a recording first.');
      return;
    }

    try {
      const openUri = await getAndroidContentUri(lastRecording.uri);
      if (Platform.OS === 'android') {
        await IntentLauncher.startActivityAsync('android.intent.action.VIEW', {
          data: openUri,
          type: getMimeType(lastRecording.format),
          flags: 1, // FLAG_GRANT_READ_URI_PERMISSION
        });
      } else {
        await Linking.openURL(openUri);
      }
    } catch (error) {
      console.warn('[Recording] Open recording failed', error);
      Alert.alert('Unable to open file', lastRecording.uri);
    }
  }, [lastRecording]);

  const handleShareRecording = useCallback(async () => {
    if (!lastRecording?.uri) {
      Alert.alert('No recording', 'Make a recording first.');
      return;
    }

    try {
      const shareUri = await getAndroidContentUri(lastRecording.uri);
      if (Platform.OS === 'android') {
        await IntentLauncher.startActivityAsync('android.intent.action.SEND', {
          type: getMimeType(lastRecording.format),
          flags: 1, // FLAG_GRANT_READ_URI_PERMISSION
          extra: {
            'android.intent.extra.STREAM': shareUri,
          },
        });
      } else {
        await Share.share({
          message: lastRecording.uri,
          url: lastRecording.uri,
        });
      }
    } catch (error) {
      console.warn('[Recording] Share recording failed', error);
    }
  }, [lastRecording]);

  const handleMasterReset = useCallback(() => {
    try {
      AudioEngineModule.setMasterVolume(1.0);
      AudioEngineModule.setPitch(0.0);
      AudioEngineModule.setSpeed(1.0);
      setMasterVolume(1.0);
      setMasterPitch(0.0);
      setMasterSpeed(1.0);
      setTracks((prev) =>
        prev.map((track) => ({
          ...track,
          pitch: supportsTrackPitch ? 0.0 : track.pitch,
          speed: supportsTrackSpeed ? 1.0 : track.speed,
        }))
      );
    } catch (error) {
      console.error('Master reset error:', error);
    }
  }, [supportsTrackPitch, supportsTrackSpeed]);

  const handleSeek = useCallback((value: number) => {
    try {
      AudioEngineModule.seek(value);
      setPosition(value);
    } catch (error) {
      console.error('Seek error:', error);
    }
  }, []);

  const handleMasterVolumeChange = useCallback((value: number) => {
    try {
      AudioEngineModule.setMasterVolume(value);
      setMasterVolume(value);
    } catch (error) {
      console.error('Master volume error:', error);
    }
  }, []);

  const handleTrackVolumeChange = useCallback((trackId: string, value: number) => {
    try {
      AudioEngineModule.setTrackVolume(trackId, value);
      setTracks((prev) =>
        prev.map((t) => (t.id === trackId ? { ...t, volume: value } : t))
      );
    } catch (error) {
      console.error('Track volume error:', error);
    }
  }, []);

  const handleTrackPanChange = useCallback((trackId: string, value: number) => {
    try {
      AudioEngineModule.setTrackPan(trackId, value);
      setTracks((prev) =>
        prev.map((t) => (t.id === trackId ? { ...t, pan: value } : t))
      );
    } catch (error) {
      console.error('Track pan error:', error);
    }
  }, []);

  const handleTrackMuteToggle = useCallback((trackId: string) => {
    setTracks((prev) => {
      const track = prev.find((t) => t.id === trackId);
      if (!track) return prev;

      const newMuted = !track.muted;
      try {
        AudioEngineModule.setTrackMuted(trackId, newMuted);
      } catch (error) {
        console.error('Track mute error:', error);
      }

      return prev.map((t) => (t.id === trackId ? { ...t, muted: newMuted } : t));
    });
  }, []);

  const handleTrackSoloToggle = useCallback((trackId: string) => {
    setTracks((prev) => {
      const track = prev.find((t) => t.id === trackId);
      if (!track) return prev;

      const newSolo = !track.solo;
      try {
        AudioEngineModule.setTrackSolo(trackId, newSolo);
      } catch (error) {
        console.error('Track solo error:', error);
      }

      return prev.map((t) => (t.id === trackId ? { ...t, solo: newSolo } : t));
    });
  }, []);

  const handleMasterPitchChange = useCallback((value: number) => {
    try {
      AudioEngineModule.setPitch(value);
      setMasterPitch(value);
    } catch (error) {
      console.error('Master pitch error:', error);
    }
  }, []);

  const handleMasterSpeedChange = useCallback((value: number) => {
    try {
      AudioEngineModule.setSpeed(value);
      AudioEngineModule.setPitch(masterPitch);
      setMasterSpeed(value);
    } catch (error) {
      console.error('Master speed error:', error);
    }
  }, [masterPitch]);

  const handleTrackPitchChange = useCallback((trackId: string, value: number) => {
    try {
      if (!supportsTrackPitch) {
        return;
      }
      engineAny.setTrackPitch(trackId, value);
      setTracks((prev) =>
        prev.map((t) => (t.id === trackId ? { ...t, pitch: value } : t))
      );
    } catch (error) {
      console.error('Track pitch error:', error);
    }
  }, [engineAny, supportsTrackPitch]);

  const handleTrackSpeedChange = useCallback((trackId: string, value: number) => {
    try {
      if (!supportsTrackSpeed) {
        return;
      }
      engineAny.setTrackSpeed(trackId, value);
      if (supportsTrackPitch) {
        const currentPitch =
          tracks.find((track) => track.id === trackId)?.pitch ?? 0.0;
        engineAny.setTrackPitch(trackId, currentPitch);
      }
      setTracks((prev) =>
        prev.map((t) => (t.id === trackId ? { ...t, speed: value } : t))
      );
    } catch (error) {
      console.error('Track speed error:', error);
    }
  }, [engineAny, supportsTrackPitch, supportsTrackSpeed, tracks]);

  const formatTime = (ms: number) => {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    return `${minutes}:${seconds.toString().padStart(2, '0')}`;
  };

  const formatSeconds = (ms?: number) => {
    if (typeof ms !== 'number' || Number.isNaN(ms)) {
      return '0.00s';
    }
    return `${(Math.max(0, ms) / 1000).toFixed(2)}s`;
  };

  const heroStyle = {
    opacity: introAnim,
    transform: [
      {
        translateY: introAnim.interpolate({
          inputRange: [0, 1],
          outputRange: [16, 0],
        }),
      },
    ],
  };
  const controlsStyle = {
    opacity: controlsAnim,
    transform: [
      {
        translateY: controlsAnim.interpolate({
          inputRange: [0, 1],
          outputRange: [18, 0],
        }),
      },
    ],
  };
  const tracksStyle = {
    opacity: tracksAnim,
    transform: [
      {
        translateY: tracksAnim.interpolate({
          inputRange: [0, 1],
          outputRange: [20, 0],
        }),
      },
    ],
  };

  return (
    <View style={styles.root}>
      <View style={styles.background}>
        <View style={styles.glowTop} />
        <View style={styles.glowBottom} />
      </View>
      <ScrollView contentContainerStyle={styles.content} showsVerticalScrollIndicator={false}>
        <Animated.View style={[styles.heroCard, heroStyle]}>
          <View style={styles.heroTopRow}>
            <View>
              <Text style={styles.eyebrow}>Sezo Audio Engine</Text>
              <Text style={styles.title}>Phase 3 Playground</Text>
            </View>
            <View
              style={[
                styles.statusPill,
                status === 'Ready' && styles.statusPillReady,
                status === 'Playing' && styles.statusPillActive,
                status === 'Init failed' && styles.statusPillDanger,
              ]}
            >
              <Text style={styles.statusText}>{status}</Text>
            </View>
          </View>
          <Text style={styles.subtitle}>
            Playback, mixing, and live recording built for real-time audio apps.
          </Text>

          {tracks.length === 0 && (
            <TouchableOpacity style={styles.primaryButton} onPress={loadTracks}>
              <Text style={styles.primaryButtonText}>Load Tracks</Text>
            </TouchableOpacity>
          )}
        </Animated.View>

        {tracks.length > 0 && (
          <>
            <Animated.View style={[styles.card, controlsStyle]}>
              <View style={styles.sectionHeader}>
                <Text style={styles.sectionTitle}>Transport</Text>
                <View style={styles.timePill}>
                  <Text style={styles.timeText}>
                    {formatTime(position)} / {formatTime(duration)}
                  </Text>
                </View>
              </View>

              <View style={styles.transportRow}>
                <TouchableOpacity
                  style={[styles.controlButton, isPlaying && styles.controlButtonDisabled]}
                  onPress={handlePlay}
                  disabled={isPlaying}
                >
                  <Text style={styles.controlButtonText}>▶ Play</Text>
                </TouchableOpacity>

                <TouchableOpacity
                  style={[styles.controlButton, !isPlaying && styles.controlButtonDisabled]}
                  onPress={handlePause}
                  disabled={!isPlaying}
                >
                  <Text style={styles.controlButtonText}>⏸ Pause</Text>
                </TouchableOpacity>

                <TouchableOpacity style={styles.controlButton} onPress={handleStop}>
                  <Text style={styles.controlButtonText}>⏹ Stop</Text>
                </TouchableOpacity>

                <TouchableOpacity style={styles.resetButton} onPress={handleReset}>
                  <Text style={styles.resetButtonText}>Reset</Text>
                </TouchableOpacity>
              </View>

              <Slider
                style={styles.slider}
                minimumValue={0}
                maximumValue={duration}
                value={position}
                onSlidingComplete={handleSeek}
                minimumTrackTintColor={theme.colors.accent}
                maximumTrackTintColor={theme.colors.track}
                thumbTintColor={theme.colors.accentStrong}
              />
            </Animated.View>

            <Animated.View style={[styles.card, controlsStyle]}>
              <View style={styles.sectionHeader}>
                <Text style={styles.sectionTitle}>Master</Text>
                <View style={styles.sectionHeaderActions}>
                  <Text style={styles.sectionHint}>Global mix controls</Text>
                  <TouchableOpacity
                    style={styles.iconResetButton}
                    onPress={handleMasterReset}
                  >
                    <Text style={styles.iconResetText}>↺</Text>
                  </TouchableOpacity>
                </View>
              </View>

              <View style={styles.controlGroup}>
                <View style={styles.labelRow}>
                  <Text style={styles.label}>Master Volume</Text>
                  <View style={styles.valueRow}>
                    <Text style={styles.valuePill}>
                      {(masterVolume * 100).toFixed(0)}%
                    </Text>
                    <TouchableOpacity
                      style={styles.iconResetButton}
                      onPress={() => handleMasterVolumeChange(1.0)}
                    >
                      <Text style={styles.iconResetText}>↺</Text>
                    </TouchableOpacity>
                  </View>
                </View>
                <Slider
                  style={styles.slider}
                  minimumValue={0}
                  maximumValue={2.0}
                  value={masterVolume}
                  onValueChange={handleMasterVolumeChange}
                  minimumTrackTintColor={theme.colors.accent}
                  maximumTrackTintColor={theme.colors.track}
                  thumbTintColor={theme.colors.accentStrong}
                />
              </View>

              <View style={styles.controlGroup}>
                <View style={styles.labelRow}>
                  <Text style={styles.label}>Master Pitch</Text>
                  <View style={styles.valueRow}>
                    <Text style={styles.valuePill}>
                      {masterPitch > 0 ? '+' : ''}
                      {masterPitch.toFixed(1)} st
                    </Text>
                    <TouchableOpacity
                      style={styles.iconResetButton}
                      onPress={() => handleMasterPitchChange(0.0)}
                    >
                      <Text style={styles.iconResetText}>↺</Text>
                    </TouchableOpacity>
                  </View>
                </View>
                <Slider
                  style={styles.slider}
                  minimumValue={-12.0}
                  maximumValue={12.0}
                  value={masterPitch}
                  onValueChange={handleMasterPitchChange}
                  minimumTrackTintColor={theme.colors.accent}
                  maximumTrackTintColor={theme.colors.track}
                  thumbTintColor={theme.colors.accentStrong}
                />
              </View>

              <View style={styles.controlGroup}>
                <View style={styles.labelRow}>
                  <Text style={styles.label}>Master Speed</Text>
                  <View style={styles.valueRow}>
                    <Text style={styles.valuePill}>
                      {(masterSpeed * 100).toFixed(0)}%
                    </Text>
                    <TouchableOpacity
                      style={styles.iconResetButton}
                      onPress={() => handleMasterSpeedChange(1.0)}
                    >
                      <Text style={styles.iconResetText}>↺</Text>
                    </TouchableOpacity>
                  </View>
                </View>
                <Slider
                  style={styles.slider}
                  minimumValue={0.5}
                  maximumValue={2.0}
                  value={masterSpeed}
                  onValueChange={handleMasterSpeedChange}
                  minimumTrackTintColor={theme.colors.accent}
                  maximumTrackTintColor={theme.colors.track}
                  thumbTintColor={theme.colors.accentStrong}
                />
              </View>
            </Animated.View>

            <Animated.View style={[styles.card, controlsStyle]}>
              <View style={styles.sectionHeader}>
                <Text style={styles.sectionTitle}>Recording</Text>
                <View style={styles.sectionHeaderActions}>
                  <View
                    style={[
                      styles.statusPill,
                      isRecording && styles.recordingStatusPill,
                      recordingUnavailable && styles.statusPillDanger,
                    ]}
                  >
                    <Text style={styles.statusText}>
                      {recordingUnavailable
                        ? 'Unavailable'
                        : isRecording
                          ? 'Recording'
                          : recordingStatus}
                    </Text>
                  </View>
                </View>
              </View>

              {Platform.OS === 'ios' && (
                <Text style={styles.sectionHint}>
                  Recording is Android-only in this preview build.
                </Text>
              )}

              <View style={styles.formatRow}>
                {(['wav', 'aac', 'mp3'] as const).map((format) => (
                  <TouchableOpacity
                    key={format}
                    style={[
                      styles.formatButton,
                      recordingFormat === format && styles.formatButtonActive,
                      (isRecording || recordingUnavailable) && styles.formatButtonDisabled,
                    ]}
                    onPress={() => setRecordingFormat(format)}
                    disabled={isRecording || recordingUnavailable}
                  >
                    <Text
                      style={[
                        styles.formatButtonText,
                        recordingFormat === format && styles.formatButtonTextActive,
                      ]}
                    >
                      {format.toUpperCase()}
                    </Text>
                  </TouchableOpacity>
                ))}
              </View>

              <View style={styles.formatRow}>
                {(['low', 'medium', 'high'] as const).map((quality) => (
                  <TouchableOpacity
                    key={quality}
                    style={[
                      styles.formatButton,
                      recordingQuality === quality && styles.formatButtonActive,
                      (isRecording || recordingUnavailable) && styles.formatButtonDisabled,
                    ]}
                    onPress={() => setRecordingQuality(quality)}
                    disabled={isRecording || recordingUnavailable}
                  >
                    <Text
                      style={[
                        styles.formatButtonText,
                        recordingQuality === quality && styles.formatButtonTextActive,
                      ]}
                    >
                      {quality.toUpperCase()}
                    </Text>
                  </TouchableOpacity>
                ))}
              </View>

              <View style={styles.formatRow}>
                {[1, 2].map((channels) => (
                  <TouchableOpacity
                    key={channels}
                    style={[
                      styles.formatButton,
                      recordingChannels === channels && styles.formatButtonActive,
                      (isRecording || recordingUnavailable) && styles.formatButtonDisabled,
                    ]}
                    onPress={() => setRecordingChannels(channels as 1 | 2)}
                    disabled={isRecording || recordingUnavailable}
                  >
                    <Text
                      style={[
                        styles.formatButtonText,
                        recordingChannels === channels && styles.formatButtonTextActive,
                      ]}
                    >
                      {channels === 1 ? 'MONO' : 'STEREO'}
                    </Text>
                  </TouchableOpacity>
                ))}

                {[44100, 48000].map((sampleRate) => (
                  <TouchableOpacity
                    key={sampleRate}
                    style={[
                      styles.formatButton,
                      recordingSampleRate === sampleRate && styles.formatButtonActive,
                      (isRecording || recordingUnavailable) && styles.formatButtonDisabled,
                    ]}
                    onPress={() => setRecordingSampleRate(sampleRate as 44100 | 48000)}
                    disabled={isRecording || recordingUnavailable}
                  >
                    <Text
                      style={[
                        styles.formatButtonText,
                        recordingSampleRate === sampleRate && styles.formatButtonTextActive,
                      ]}
                    >
                      {sampleRate === 44100 ? '44.1K' : '48K'}
                    </Text>
                  </TouchableOpacity>
                ))}
              </View>

              <View style={styles.controlGroup}>
                <View style={styles.labelRow}>
                  <Text style={styles.label}>Input Gain</Text>
                  <View style={styles.valueRow}>
                    <Text style={styles.valuePill}>
                      {(recordingVolume * 100).toFixed(0)}%
                    </Text>
                    <TouchableOpacity
                      style={[
                        styles.iconResetButton,
                        recordingUnavailable && styles.iconResetButtonDisabled,
                      ]}
                      onPress={() => handleRecordingVolumeChange(1.0)}
                      disabled={recordingUnavailable}
                    >
                      <Text style={styles.iconResetText}>↺</Text>
                    </TouchableOpacity>
                  </View>
                </View>
                <Slider
                  style={styles.slider}
                  minimumValue={0}
                  maximumValue={2.0}
                  value={recordingVolume}
                  onValueChange={handleRecordingVolumeChange}
                  disabled={recordingUnavailable}
                  minimumTrackTintColor={theme.colors.accent}
                  maximumTrackTintColor={theme.colors.track}
                  thumbTintColor={theme.colors.accentStrong}
                />
              </View>

              <View style={styles.progressTrack}>
                <View
                  style={[
                    styles.levelFill,
                    { width: `${Math.round(recordingLevel * 100)}%` },
                  ]}
                />
              </View>

              <View style={styles.progressMeta}>
                <Text style={styles.sectionHint}>
                  {isRecording
                    ? `Recording ${formatTime(recordingElapsed)}`
                    : recordingUnavailable
                      ? 'Recording unavailable'
                      : recordingStatus}
                </Text>
                <Text style={styles.progressPercent}>
                  {Math.round(recordingLevel * 100)}%
                </Text>
              </View>

              <View style={styles.transportRow}>
                <TouchableOpacity
                  style={[
                    styles.controlButton,
                    styles.recordButton,
                    (isRecording || recordingUnavailable) && styles.controlButtonDisabled,
                  ]}
                  onPress={handleStartRecording}
                  disabled={isRecording || recordingUnavailable}
                >
                  <Text style={[styles.controlButtonText, styles.recordButtonText]}>
                    Start Recording
                  </Text>
                </TouchableOpacity>
                <TouchableOpacity
                  style={[
                    styles.controlButton,
                    styles.stopButton,
                    (!isRecording || recordingUnavailable) && styles.controlButtonDisabled,
                  ]}
                  onPress={handleStopRecording}
                  disabled={!isRecording || recordingUnavailable}
                >
                  <Text style={[styles.controlButtonText, styles.stopButtonText]}>
                    Stop Recording
                  </Text>
                </TouchableOpacity>
              </View>

              {lastRecording ? (
                <View style={styles.extractionDetails}>
                  <Text style={styles.detailLabel}>Last recording</Text>
                  <Text style={styles.detailValue}>
                    {lastRecording.format.toUpperCase()} •{' '}
                    {(lastRecording.fileSize / 1024).toFixed(1)} KB •{' '}
                    {formatTime(lastRecording.duration)} • Start{' '}
                    {formatSeconds(lastRecording.startTimeMs)}
                  </Text>
                  <Text style={styles.detailPath} numberOfLines={2}>
                    {lastRecording.uri}
                  </Text>
                  <View style={styles.exportActions}>
                    <TouchableOpacity
                      style={styles.controlButton}
                      onPress={handleOpenRecording}
                    >
                      <Text style={styles.controlButtonText}>Open File</Text>
                    </TouchableOpacity>
                    <TouchableOpacity
                      style={styles.resetButton}
                      onPress={handleShareRecording}
                    >
                      <Text style={styles.resetButtonText}>Share</Text>
                    </TouchableOpacity>
                  </View>
                </View>
              ) : (
                <Text style={styles.sectionHint}>No recordings yet</Text>
              )}
            </Animated.View>

            <Animated.View style={[styles.card, controlsStyle]}>
              <View style={styles.sectionHeader}>
                <Text style={styles.sectionTitle}>Extraction</Text>
                <Text style={styles.sectionHint}>Offline export</Text>
              </View>

              <View style={styles.formatRow}>
                {(['wav', 'aac', 'mp3'] as const).map((format) => (
                  <TouchableOpacity
                    key={format}
                    style={[
                      styles.formatButton,
                      extractionFormat === format && styles.formatButtonActive,
                      isExtracting && styles.formatButtonDisabled,
                    ]}
                    onPress={() => setExtractionFormat(format)}
                    disabled={isExtracting}
                  >
                    <Text
                      style={[
                        styles.formatButtonText,
                        extractionFormat === format && styles.formatButtonTextActive,
                      ]}
                    >
                      {format.toUpperCase()}
                    </Text>
                  </TouchableOpacity>
                ))}

                <TouchableOpacity
                  style={[
                    styles.effectsToggle,
                    extractionIncludeEffects && styles.effectsToggleActive,
                    isExtracting && styles.formatButtonDisabled,
                  ]}
                  onPress={() => setExtractionIncludeEffects((prev) => !prev)}
                  disabled={isExtracting}
                >
                  <Text style={styles.effectsToggleText}>
                    {extractionIncludeEffects ? 'Effects On' : 'Dry Export'}
                  </Text>
                </TouchableOpacity>
              </View>

              <View style={styles.transportRow}>
                <TouchableOpacity
                  style={[styles.controlButton, isExtracting && styles.controlButtonDisabled]}
                  onPress={handleExtractTrack}
                  disabled={isExtracting}
                >
                  <Text style={styles.controlButtonText}>Export Track</Text>
                </TouchableOpacity>
                <TouchableOpacity
                  style={[styles.controlButton, isExtracting && styles.controlButtonDisabled]}
                  onPress={handleExtractMix}
                  disabled={isExtracting}
                >
                  <Text style={styles.controlButtonText}>Export Mix</Text>
                </TouchableOpacity>
                <TouchableOpacity
                  style={[
                    styles.controlButton,
                    styles.cancelButton,
                    !isExtracting && styles.controlButtonDisabled,
                  ]}
                  onPress={handleCancelExtraction}
                  disabled={!isExtracting}
                >
                  <Text style={[styles.controlButtonText, styles.cancelButtonText]}>
                    Cancel Export
                  </Text>
                </TouchableOpacity>
              </View>

              <View style={styles.progressTrack}>
                <View
                  style={[
                    styles.progressFill,
                    { width: `${Math.round(extractionProgress * 100)}%` },
                  ]}
                />
              </View>

              <View style={styles.progressMeta}>
                <Text style={styles.sectionHint}>{extractionStatus}</Text>
                <Text style={styles.progressPercent}>
                  {Math.round(extractionProgress * 100)}%
                </Text>
              </View>

              {lastExtraction ? (
                <View style={styles.extractionDetails}>
                  <Text style={styles.detailLabel}>Last export</Text>
                  <Text style={styles.detailValue}>
                    {lastExtraction.format.toUpperCase()} •{' '}
                    {(lastExtraction.fileSize / 1024).toFixed(1)} KB •{' '}
                    {formatTime(lastExtraction.duration)}
                  </Text>
                  <Text style={styles.detailPath} numberOfLines={2}>
                    {lastExtraction.outputPath ?? lastExtraction.uri}
                  </Text>
                  <View style={styles.exportActions}>
                    <TouchableOpacity
                      style={styles.controlButton}
                      onPress={handleOpenExport}
                    >
                      <Text style={styles.controlButtonText}>Open File</Text>
                    </TouchableOpacity>
                    <TouchableOpacity
                      style={styles.resetButton}
                      onPress={handleShareExport}
                    >
                      <Text style={styles.resetButtonText}>Share</Text>
                    </TouchableOpacity>
                  </View>
                </View>
              ) : (
                <Text style={styles.sectionHint}>No exports yet</Text>
              )}
            </Animated.View>

            <Animated.View style={[styles.sectionTitleRow, tracksStyle]}>
              <Text style={styles.sectionTitle}>Tracks</Text>
              <Text style={styles.sectionHint}>{tracks.length} loaded</Text>
            </Animated.View>

            {tracks.map((track) => (
              <Animated.View key={track.id} style={[styles.trackCard, tracksStyle]}>
                <View style={styles.trackHeader}>
                  <View>
                    <Text style={styles.trackName}>{track.name}</Text>
                    <Text style={styles.trackMeta}>
                      Pitch {track.pitch > 0 ? '+' : ''}
                      {track.pitch.toFixed(1)} st | Speed {(track.speed * 100).toFixed(0)}%
                      {typeof track.startTimeMs === 'number'
                        ? ` | Start ${formatSeconds(track.startTimeMs)}`
                        : ''}
                    </Text>
                  </View>
                  <View style={styles.trackButtons}>
                    <TouchableOpacity
                      style={[styles.trackButton, track.muted && styles.trackButtonMuted]}
                      onPress={() => handleTrackMuteToggle(track.id)}
                    >
                      <Text style={styles.trackButtonText}>M</Text>
                    </TouchableOpacity>
                    <TouchableOpacity
                      style={[styles.trackButton, track.solo && styles.trackButtonSolo]}
                      onPress={() => handleTrackSoloToggle(track.id)}
                    >
                      <Text style={styles.trackButtonText}>S</Text>
                    </TouchableOpacity>
                  </View>
                </View>

                <View style={styles.controlGroup}>
                  <View style={styles.labelRow}>
                    <Text style={styles.label}>Volume</Text>
                    <View style={styles.valueRow}>
                      <Text style={styles.valuePill}>
                        {(track.volume * 100).toFixed(0)}%
                      </Text>
                      <TouchableOpacity
                        style={styles.iconResetButton}
                        onPress={() => handleTrackVolumeChange(track.id, 1.0)}
                      >
                        <Text style={styles.iconResetText}>↺</Text>
                      </TouchableOpacity>
                    </View>
                  </View>
                  <Slider
                    style={styles.slider}
                    minimumValue={0}
                    maximumValue={2.0}
                    value={track.volume}
                    onValueChange={(value: number) =>
                      handleTrackVolumeChange(track.id, value)
                    }
                    minimumTrackTintColor={theme.colors.accent}
                    maximumTrackTintColor={theme.colors.track}
                    thumbTintColor={theme.colors.accentStrong}
                  />
                </View>

                <View style={styles.controlGroup}>
                  <View style={styles.labelRow}>
                    <Text style={styles.label}>Pan</Text>
                    <View style={styles.valueRow}>
                      <Text style={styles.valuePill}>
                        {track.pan > 0 ? 'R' : track.pan < 0 ? 'L' : 'C'}{' '}
                        {Math.abs(track.pan * 100).toFixed(0)}
                      </Text>
                      <TouchableOpacity
                        style={styles.iconResetButton}
                        onPress={() => handleTrackPanChange(track.id, 0.0)}
                      >
                        <Text style={styles.iconResetText}>↺</Text>
                      </TouchableOpacity>
                    </View>
                  </View>
                  <Slider
                    style={styles.slider}
                    minimumValue={-1.0}
                    maximumValue={1.0}
                    value={track.pan}
                    onValueChange={(value: number) => handleTrackPanChange(track.id, value)}
                    minimumTrackTintColor={theme.colors.accent}
                    maximumTrackTintColor={theme.colors.track}
                    thumbTintColor={theme.colors.accentStrong}
                  />
                </View>

                <View style={styles.controlGroup}>
                  <View style={styles.labelRow}>
                    <Text style={styles.label}>Pitch</Text>
                    <View style={styles.valueRow}>
                      <Text style={styles.valuePill}>
                        {track.pitch > 0 ? '+' : ''}
                        {track.pitch.toFixed(1)} st
                      </Text>
                      <TouchableOpacity
                        style={[
                          styles.iconResetButton,
                          !supportsTrackPitch && styles.iconResetButtonDisabled,
                        ]}
                        onPress={() => handleTrackPitchChange(track.id, 0.0)}
                        disabled={!supportsTrackPitch}
                      >
                        <Text style={styles.iconResetText}>↺</Text>
                      </TouchableOpacity>
                    </View>
                  </View>
                  <Slider
                    style={styles.slider}
                    minimumValue={-12.0}
                    maximumValue={12.0}
                    value={track.pitch}
                    disabled={!supportsTrackPitch}
                    onValueChange={(value: number) => handleTrackPitchChange(track.id, value)}
                    minimumTrackTintColor={
                      supportsTrackPitch ? theme.colors.accent : theme.colors.track
                    }
                    maximumTrackTintColor={theme.colors.track}
                    thumbTintColor={
                      supportsTrackPitch ? theme.colors.accentStrong : theme.colors.border
                    }
                  />
                </View>

                <View style={styles.controlGroup}>
                  <View style={styles.labelRow}>
                    <Text style={styles.label}>Speed</Text>
                    <View style={styles.valueRow}>
                      <Text style={styles.valuePill}>
                        {(track.speed * 100).toFixed(0)}%
                      </Text>
                      <TouchableOpacity
                        style={[
                          styles.iconResetButton,
                          !supportsTrackSpeed && styles.iconResetButtonDisabled,
                        ]}
                        onPress={() => handleTrackSpeedChange(track.id, 1.0)}
                        disabled={!supportsTrackSpeed}
                      >
                        <Text style={styles.iconResetText}>↺</Text>
                      </TouchableOpacity>
                    </View>
                  </View>
                  <Slider
                    style={styles.slider}
                    minimumValue={0.5}
                    maximumValue={2.0}
                    value={track.speed}
                    disabled={!supportsTrackSpeed}
                    onValueChange={(value: number) => handleTrackSpeedChange(track.id, value)}
                    minimumTrackTintColor={
                      supportsTrackSpeed ? theme.colors.accent : theme.colors.track
                    }
                    maximumTrackTintColor={theme.colors.track}
                    thumbTintColor={
                      supportsTrackSpeed ? theme.colors.accentStrong : theme.colors.border
                    }
                  />
                </View>
              </Animated.View>
            ))}
          </>
        )}
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    backgroundColor: theme.colors.background,
  },
  background: {
    ...StyleSheet.absoluteFillObject,
  },
  glowTop: {
    position: 'absolute',
    width: 320,
    height: 320,
    borderRadius: 160,
    backgroundColor: 'rgba(255, 180, 84, 0.22)',
    top: -140,
    right: -80,
  },
  glowBottom: {
    position: 'absolute',
    width: 360,
    height: 360,
    borderRadius: 180,
    backgroundColor: 'rgba(35, 209, 139, 0.18)',
    bottom: -160,
    left: -120,
  },
  content: {
    padding: 20,
    paddingTop: 56,
    paddingBottom: 80,
  },
  heroCard: {
    backgroundColor: theme.colors.surface,
    borderRadius: theme.radius.card,
    padding: 20,
    borderWidth: 1,
    borderColor: theme.colors.border,
    shadowColor: '#000',
    shadowOpacity: 0.25,
    shadowRadius: 16,
    shadowOffset: { width: 0, height: 10 },
    elevation: 4,
  },
  heroTopRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    gap: 12,
  },
  eyebrow: {
    color: theme.colors.textMuted,
    textTransform: 'uppercase',
    letterSpacing: 2,
    fontSize: 12,
    fontWeight: '600',
    marginBottom: 6,
  },
  title: {
    fontSize: 28,
    fontWeight: '700',
    color: theme.colors.text,
    fontFamily: 'serif',
  },
  subtitle: {
    color: theme.colors.textMuted,
    fontSize: 14,
    marginTop: 12,
    marginBottom: 16,
  },
  statusPill: {
    paddingVertical: 6,
    paddingHorizontal: 12,
    borderRadius: theme.radius.pill,
    backgroundColor: theme.colors.surfaceAlt,
    borderWidth: 1,
    borderColor: theme.colors.border,
  },
  statusPillReady: {
    borderColor: 'rgba(35, 209, 139, 0.6)',
  },
  statusPillActive: {
    backgroundColor: 'rgba(35, 209, 139, 0.2)',
    borderColor: 'rgba(35, 209, 139, 0.7)',
  },
  recordingStatusPill: {
    backgroundColor: 'rgba(255, 93, 93, 0.18)',
    borderColor: 'rgba(255, 93, 93, 0.7)',
  },
  statusPillDanger: {
    backgroundColor: 'rgba(255, 93, 93, 0.18)',
    borderColor: 'rgba(255, 93, 93, 0.7)',
  },
  statusText: {
    color: theme.colors.text,
    fontSize: 12,
    fontWeight: '600',
  },
  primaryButton: {
    backgroundColor: theme.colors.accent,
    paddingVertical: 14,
    borderRadius: theme.radius.button,
    alignItems: 'center',
  },
  primaryButtonText: {
    color: '#1b1206',
    fontSize: 16,
    fontWeight: '700',
    letterSpacing: 0.3,
  },
  card: {
    backgroundColor: theme.colors.surface,
    borderRadius: theme.radius.card,
    padding: 18,
    borderWidth: 1,
    borderColor: theme.colors.border,
    marginTop: 18,
    shadowColor: '#000',
    shadowOpacity: 0.2,
    shadowRadius: 14,
    shadowOffset: { width: 0, height: 8 },
    elevation: 3,
  },
  sectionHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 12,
  },
  sectionHeaderActions: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  sectionTitle: {
    color: theme.colors.text,
    fontSize: 18,
    fontWeight: '700',
  },
  sectionTitleRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginTop: 26,
  },
  sectionHint: {
    color: theme.colors.textMuted,
    fontSize: 12,
  },
  timePill: {
    paddingHorizontal: 10,
    paddingVertical: 4,
    borderRadius: theme.radius.pill,
    backgroundColor: theme.colors.surfaceAlt,
  },
  timeText: {
    color: theme.colors.textMuted,
    fontSize: 12,
    fontFamily: 'monospace',
  },
  transportRow: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 10,
    marginBottom: 12,
  },
  formatRow: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 8,
    marginBottom: 12,
  },
  formatButton: {
    borderRadius: theme.radius.pill,
    borderWidth: 1,
    borderColor: theme.colors.border,
    paddingVertical: 6,
    paddingHorizontal: 12,
    backgroundColor: theme.colors.surfaceAlt,
  },
  formatButtonActive: {
    borderColor: theme.colors.accent,
    backgroundColor: 'rgba(255, 180, 84, 0.18)',
  },
  formatButtonDisabled: {
    opacity: 0.5,
  },
  formatButtonText: {
    color: theme.colors.textMuted,
    fontSize: 12,
    fontWeight: '700',
    letterSpacing: 0.4,
  },
  formatButtonTextActive: {
    color: theme.colors.accent,
  },
  effectsToggle: {
    borderRadius: theme.radius.pill,
    borderWidth: 1,
    borderColor: theme.colors.border,
    paddingVertical: 6,
    paddingHorizontal: 12,
    backgroundColor: theme.colors.surfaceAlt,
  },
  effectsToggleActive: {
    borderColor: 'rgba(35, 209, 139, 0.7)',
    backgroundColor: 'rgba(35, 209, 139, 0.2)',
  },
  effectsToggleText: {
    color: theme.colors.text,
    fontSize: 12,
    fontWeight: '600',
  },
  controlButton: {
    backgroundColor: theme.colors.surfaceStrong,
    paddingVertical: 10,
    paddingHorizontal: 16,
    borderRadius: theme.radius.button,
    borderWidth: 1,
    borderColor: theme.colors.border,
    minWidth: 96,
    alignItems: 'center',
  },
  controlButtonDisabled: {
    opacity: 0.5,
  },
  controlButtonText: {
    color: theme.colors.text,
    fontSize: 14,
    fontWeight: '600',
  },
  recordButton: {
    backgroundColor: 'rgba(255, 180, 84, 0.18)',
    borderColor: 'rgba(255, 180, 84, 0.6)',
  },
  recordButtonText: {
    color: theme.colors.accent,
  },
  stopButton: {
    backgroundColor: 'rgba(255, 93, 93, 0.18)',
    borderColor: 'rgba(255, 93, 93, 0.6)',
  },
  stopButtonText: {
    color: theme.colors.danger,
  },
  cancelButton: {
    backgroundColor: 'rgba(255, 93, 93, 0.15)',
    borderColor: 'rgba(255, 93, 93, 0.6)',
  },
  cancelButtonText: {
    color: theme.colors.danger,
  },
  progressTrack: {
    height: 8,
    borderRadius: 999,
    backgroundColor: theme.colors.track,
    overflow: 'hidden',
    marginBottom: 8,
  },
  progressFill: {
    height: '100%',
    borderRadius: 999,
    backgroundColor: theme.colors.accentStrong,
  },
  levelFill: {
    height: '100%',
    borderRadius: 999,
    backgroundColor: theme.colors.success,
  },
  progressMeta: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 10,
  },
  progressPercent: {
    color: theme.colors.text,
    fontSize: 12,
    fontWeight: '600',
  },
  extractionDetails: {
    paddingVertical: 10,
    paddingHorizontal: 12,
    borderRadius: theme.radius.button,
    backgroundColor: theme.colors.surfaceAlt,
    borderWidth: 1,
    borderColor: theme.colors.border,
  },
  detailLabel: {
    color: theme.colors.textMuted,
    fontSize: 11,
    textTransform: 'uppercase',
    letterSpacing: 1.2,
    marginBottom: 4,
  },
  detailValue: {
    color: theme.colors.text,
    fontSize: 13,
    fontWeight: '600',
  },
  detailPath: {
    color: theme.colors.textMuted,
    fontSize: 12,
    marginTop: 6,
    marginBottom: 8,
  },
  exportActions: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 10,
  },
  resetButton: {
    backgroundColor: 'rgba(255, 180, 84, 0.15)',
    paddingVertical: 10,
    paddingHorizontal: 16,
    borderRadius: theme.radius.button,
    borderWidth: 1,
    borderColor: 'rgba(255, 180, 84, 0.6)',
    minWidth: 96,
    alignItems: 'center',
  },
  resetButtonText: {
    color: theme.colors.accent,
    fontSize: 14,
    fontWeight: '600',
  },
  controlGroup: {
    marginVertical: 10,
  },
  labelRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 6,
  },
  valueRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  label: {
    color: theme.colors.text,
    fontSize: 14,
    fontWeight: '600',
  },
  valuePill: {
    color: theme.colors.textMuted,
    fontSize: 12,
    paddingHorizontal: 10,
    paddingVertical: 4,
    borderRadius: theme.radius.pill,
    backgroundColor: theme.colors.surfaceAlt,
    overflow: 'hidden',
  },
  iconResetButton: {
    width: 28,
    height: 28,
    borderRadius: 14,
    borderWidth: 1,
    borderColor: theme.colors.border,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: theme.colors.surfaceStrong,
  },
  iconResetButtonDisabled: {
    opacity: 0.4,
  },
  iconResetText: {
    color: theme.colors.text,
    fontSize: 14,
    fontWeight: '600',
  },
  slider: {
    width: '100%',
    height: 40,
  },
  trackCard: {
    backgroundColor: theme.colors.surface,
    borderRadius: theme.radius.card,
    padding: 16,
    borderWidth: 1,
    borderColor: theme.colors.border,
    marginTop: 14,
  },
  trackHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 12,
  },
  trackName: {
    color: theme.colors.text,
    fontSize: 17,
    fontWeight: '700',
  },
  trackMeta: {
    color: theme.colors.textMuted,
    fontSize: 12,
    marginTop: 4,
  },
  trackButtons: {
    flexDirection: 'row',
    gap: 8,
  },
  trackButton: {
    backgroundColor: theme.colors.surfaceStrong,
    width: 34,
    height: 34,
    borderRadius: 17,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 1,
    borderColor: theme.colors.border,
  },
  trackButtonMuted: {
    backgroundColor: 'rgba(255, 93, 93, 0.2)',
    borderColor: 'rgba(255, 93, 93, 0.8)',
  },
  trackButtonSolo: {
    backgroundColor: 'rgba(35, 209, 139, 0.2)',
    borderColor: 'rgba(35, 209, 139, 0.8)',
  },
  trackButtonText: {
    color: theme.colors.text,
    fontSize: 13,
    fontWeight: '700',
  },
});
