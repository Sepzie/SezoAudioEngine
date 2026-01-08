import { useEffect, useState, useCallback, useRef } from 'react';
import {
  Animated,
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
import CollapsibleSection from './components/CollapsibleSection';
import LabeledSlider from './components/LabeledSlider';
import TogglePillGroup from './components/TogglePillGroup';
import ProgressBar from './components/ProgressBar';
import TrackCard from './components/TrackCard';
import TestLabScreen from './TestLabScreen';
import { styles, theme } from './ui';
import { ExtractionInfo, RecordingInfo, Track } from './types';

type PlaybackState = 'stopped' | 'playing' | 'paused' | 'recording';
type PlaybackTestStatus = 'pending' | 'pass' | 'fail' | 'unexpected';
type PlaybackTestEntry = {
  id: number;
  status: PlaybackTestStatus;
  expected?: PlaybackState;
  actual?: PlaybackState;
  source: string;
  timestamp: number;
  positionMs?: number;
  durationMs?: number;
  message?: string;
};
type PlaybackExpectation = {
  id: number;
  expected: PlaybackState;
  source: string;
  timestamp: number;
};

const getMimeType = (format: string) => {
  switch (format) {
    case 'aac':
      return 'audio/aac';
    case 'm4a':
      return 'audio/mp4';
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

const isPlayableRecordingUri = (uri: string) => {
  const lower = uri.toLowerCase();
  if (Platform.OS === 'ios') {
    return (
      lower.endsWith('.wav') ||
      lower.endsWith('.mp3') ||
      lower.endsWith('.m4a') ||
      lower.endsWith('.aac')
    );
  }
  return lower.endsWith('.wav') || lower.endsWith('.mp3') || lower.endsWith('.m4a');
};

const wait = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));
const POSITION_POLL_MS = 250;
const RECORDING_POLL_MS = 200;
const PLAYBACK_EVENT_TIMEOUT_MS = 1500;
const AUDIO_FORMAT_OPTIONS = [
  { value: 'wav', label: 'WAV' },
  { value: 'aac', label: 'AAC' },
  { value: 'm4a', label: 'M4A' },
  { value: 'mp3', label: 'MP3' },
] as const;
const RECORDING_QUALITY_OPTIONS = [
  { value: 'low', label: 'LOW' },
  { value: 'medium', label: 'MEDIUM' },
  { value: 'high', label: 'HIGH' },
] as const;
const RECORDING_CHANNEL_OPTIONS = [
  { value: 1, label: 'MONO' },
  { value: 2, label: 'STEREO' },
] as const;
const RECORDING_SAMPLE_RATE_OPTIONS = [
  { value: 44100, label: '44.1K' },
  { value: 48000, label: '48K' },
] as const;
const BACKGROUND_OPTIONS = [
  { value: 1, label: 'ON' },
  { value: 0, label: 'OFF' },
] as const;

export default function App() {
  const [status, setStatus] = useState('Idle');
  const [engineReady, setEngineReady] = useState(false);
  const [activeScreen, setActiveScreen] = useState<'playground' | 'testLab'>('playground');
  const [isPlaying, setIsPlaying] = useState(false);
  const [position, setPosition] = useState(0);
  const [duration, setDuration] = useState(0);
  const [masterVolume, setMasterVolume] = useState(1.0);
  const [masterPitch, setMasterPitch] = useState(0.0);
  const [masterSpeed, setMasterSpeed] = useState(1.0);
  const [tracks, setTracks] = useState<Track[]>([]);
  const [isRecording, setIsRecording] = useState(false);
  const [recordingStatus, setRecordingStatus] = useState('Idle');
  const [recordingFormat, setRecordingFormat] = useState<'wav' | 'aac' | 'm4a' | 'mp3'>(
    'wav'
  );
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
  const [extractionFormat, setExtractionFormat] = useState<'wav' | 'aac' | 'm4a' | 'mp3'>(
    'aac'
  );
  const [extractionIncludeEffects, setExtractionIncludeEffects] = useState(true);
  const [lastExtraction, setLastExtraction] = useState<ExtractionInfo | null>(null);
  const [isExtracting, setIsExtracting] = useState(false);
  const [extractionJobId, setExtractionJobId] = useState<number | null>(null);
  const [backgroundEnabled, setBackgroundEnabled] = useState<0 | 1>(0);
  const [playbackTests, setPlaybackTests] = useState<PlaybackTestEntry[]>([]);
  const positionRef = useRef(0);
  const durationRef = useRef(0);
  const recordingLevelRef = useRef(0);
  const recordingElapsedRef = useRef(0);
  const recordingStartRef = useRef<number | null>(null);
  const pendingRecordingRef = useRef<RecordingInfo | null>(null);
  const loadedRecordingUris = useRef(new Set<string>());
  const recordingCountRef = useRef(1);
  const playbackStateRef = useRef<PlaybackState>('stopped');
  const playbackExpectationIdRef = useRef(1);
  const playbackExpectationsRef = useRef<PlaybackExpectation[]>([]);
  const playbackTimeoutsRef = useRef(new Map<number, ReturnType<typeof setTimeout>>());
  const engineAny = AudioEngineModule as Record<string, any>;
  const supportsTrackPitch = typeof engineAny.setTrackPitch === 'function';
  const supportsTrackSpeed = typeof engineAny.setTrackSpeed === 'function';
  const supportsRecording = typeof engineAny.startRecording === 'function';
  const recordingUnavailable = !supportsRecording;
  const canUseBackground = Platform.OS === 'ios';
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
          setEngineReady(true);
          setRecordingStatus('Ready');
        }
      })
      .catch((error) => {
        if (mounted) {
          setStatus('Init failed');
          setEngineReady(false);
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

  const handleOpenTestLab = useCallback(() => {
    setActiveScreen('testLab');
  }, []);

  const handleCloseTestLab = useCallback(() => {
    setActiveScreen('playground');
  }, []);

  const captureEngineSnapshot = useCallback(() => {
    return {
      tracks,
      masterVolume,
      masterPitch,
      masterSpeed,
    };
  }, [masterPitch, masterSpeed, masterVolume, tracks]);

  const restoreEngineSnapshot = useCallback(
    async (snapshot: {
      tracks: Track[];
      masterVolume: number;
      masterPitch: number;
      masterSpeed: number;
    }) => {
      try {
        AudioEngineModule.stop();
        AudioEngineModule.unloadAllTracks();
        if (snapshot.tracks.length > 0) {
          await AudioEngineModule.loadTracks(
            snapshot.tracks.map((track) => ({
              id: track.id,
              uri: track.uri,
              volume: track.volume,
              pan: track.pan,
              muted: track.muted,
              startTimeMs: track.startTimeMs,
            }))
          );

          snapshot.tracks.forEach((track) => {
            AudioEngineModule.setTrackVolume(track.id, track.volume);
            AudioEngineModule.setTrackPan(track.id, track.pan);
            AudioEngineModule.setTrackMuted(track.id, track.muted);
            AudioEngineModule.setTrackSolo(track.id, track.solo);
            if (supportsTrackPitch) {
              engineAny.setTrackPitch(track.id, track.pitch);
            }
            if (supportsTrackSpeed) {
              engineAny.setTrackSpeed(track.id, track.speed);
            }
          });
        }
        AudioEngineModule.setMasterVolume(snapshot.masterVolume);
        AudioEngineModule.setPitch(snapshot.masterPitch);
        AudioEngineModule.setSpeed(snapshot.masterSpeed);
        setDuration(AudioEngineModule.getDuration());
        setPosition(AudioEngineModule.getCurrentPosition());
        setIsPlaying(false);
      } catch (error) {
        console.warn('[TestLab] Restore snapshot failed', error);
      }
    },
    [engineAny, supportsTrackPitch, supportsTrackSpeed]
  );

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

  const appendPlaybackTest = useCallback((entry: PlaybackTestEntry) => {
    setPlaybackTests((prev) => {
      const next = [entry, ...prev];
      return next.length > 10 ? next.slice(0, 10) : next;
    });
  }, []);

  const updatePlaybackTestEntry = useCallback(
    (id: number, updates: Partial<PlaybackTestEntry>) => {
      setPlaybackTests((prev) =>
        prev.map((entry) => (entry.id === id ? { ...entry, ...updates } : entry))
      );
    },
    []
  );

  const clearPlaybackTests = useCallback(() => {
    playbackExpectationsRef.current = [];
    playbackTimeoutsRef.current.forEach((timeout) => clearTimeout(timeout));
    playbackTimeoutsRef.current.clear();
    setPlaybackTests([]);
  }, []);

  const queuePlaybackExpectation = useCallback(
    (expected: PlaybackState, source: string) => {
      if (playbackStateRef.current === expected) {
        return;
      }
      const id = playbackExpectationIdRef.current++;
      const timestamp = Date.now();
      const entry: PlaybackTestEntry = {
        id,
        status: 'pending',
        expected,
        source,
        timestamp,
      };
      appendPlaybackTest(entry);
      playbackExpectationsRef.current.push({
        id,
        expected,
        source,
        timestamp,
      });
      const timeout = setTimeout(() => {
        playbackExpectationsRef.current = playbackExpectationsRef.current.filter(
          (pending) => pending.id !== id
        );
        playbackTimeoutsRef.current.delete(id);
        updatePlaybackTestEntry(id, {
          status: 'fail',
          message: `No playbackStateChange within ${PLAYBACK_EVENT_TIMEOUT_MS}ms`,
        });
      }, PLAYBACK_EVENT_TIMEOUT_MS);
      playbackTimeoutsRef.current.set(id, timeout);
    },
    [appendPlaybackTest, updatePlaybackTestEntry]
  );

  useEffect(() => {
    const playbackSub = AudioEngineModule.addListener(
      'playbackStateChange',
      (event: any) => {
        const state =
          typeof event?.state === 'string'
            ? (event.state as PlaybackState)
            : undefined;
        if (!state) {
          return;
        }
        const positionMs =
          typeof event?.positionMs === 'number' ? event.positionMs : undefined;
        const durationMs =
          typeof event?.durationMs === 'number' ? event.durationMs : undefined;

        playbackStateRef.current = state;

        const expectation = playbackExpectationsRef.current.shift();
        if (!expectation) {
          const id = playbackExpectationIdRef.current++;
          appendPlaybackTest({
            id,
            status: 'unexpected',
            actual: state,
            source: 'native',
            timestamp: Date.now(),
            positionMs,
            durationMs,
            message: 'Unexpected playbackStateChange event',
          });
          return;
        }

        const timeout = playbackTimeoutsRef.current.get(expectation.id);
        if (timeout) {
          clearTimeout(timeout);
          playbackTimeoutsRef.current.delete(expectation.id);
        }

        const pass = state === expectation.expected;
        updatePlaybackTestEntry(expectation.id, {
          status: pass ? 'pass' : 'fail',
          actual: state,
          positionMs,
          durationMs,
          message: pass ? undefined : `Expected ${expectation.expected}, got ${state}`,
        });
      }
    );

    return () => {
      playbackSub?.remove?.();
    };
  }, [appendPlaybackTest, updatePlaybackTestEntry]);

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
        if (Math.abs(pos - positionRef.current) >= 20) {
          positionRef.current = pos;
          setPosition(pos);
        }
        if (Math.abs(dur - durationRef.current) >= 20) {
          durationRef.current = dur;
          setDuration(dur);
        }

        // Auto-stop at end
        if (pos >= dur && dur > 0) {
          handleStop();
        }
      } catch (error) {
        console.error('Position update error:', error);
      }
    }, POSITION_POLL_MS);

    return () => clearInterval(interval);
  }, [handleStop, isPlaying]);

  useEffect(() => {
    if (!canUseBackground || backgroundEnabled === 0) {
      return;
    }
    const title = tracks[0]?.name ?? 'Sezo Audio Engine';
    const artist = 'Sezo Audio Engine';
    try {
      AudioEngineModule.updateNowPlayingInfo({ title, artist });
    } catch (error) {
      console.warn('[Background] Now playing update failed', error);
    }
  }, [backgroundEnabled, canUseBackground, tracks]);

  useEffect(() => {
    if (!isRecording) {
      recordingElapsedRef.current = 0;
      recordingLevelRef.current = 0;
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
        if (Math.abs(clamped - recordingLevelRef.current) >= 0.02) {
          recordingLevelRef.current = clamped;
          setRecordingLevel(clamped);
        }
      } catch (error) {
        setRecordingLevel(0);
      }

      if (recordingStartRef.current) {
        const elapsed = Date.now() - recordingStartRef.current;
        if (elapsed - recordingElapsedRef.current >= RECORDING_POLL_MS) {
          recordingElapsedRef.current = elapsed;
          setRecordingElapsed(elapsed);
        }
      }
    }, RECORDING_POLL_MS);

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

      if (!isPlayableRecordingUri(recording.uri)) {
        setRecordingStatus('Saved (AAC playback unavailable)');
        console.warn('[Recording] Auto-load skipped (unsupported format)', recording.uri);
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
        let ready = false;
        for (let attempt = 0; attempt < 5; attempt += 1) {
          const info = await FileSystem.getInfoAsync(recording.uri);
          if (info.exists && typeof info.size === 'number' && info.size > 44) {
            ready = true;
            break;
          }
          await wait(150);
        }

        if (!ready) {
          setRecordingStatus('Saved (file not ready)');
          throw new Error('Recording file not ready');
        }

        let loaded = false;
        for (let attempt = 0; attempt < 2; attempt += 1) {
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
            loaded = true;
            break;
          } catch (error) {
            if (attempt < 1) {
              await wait(200);
            } else {
              throw error;
            }
          }
        }

        if (!loaded) {
          throw new Error('Recording track failed to load');
        }

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
        setRecordingStatus('Auto-load failed');
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
      if (isPlaying) {
        pendingRecordingRef.current = recording;
        setRecordingStatus('Saved (load after stop)');
        return;
      }
      void appendRecordingTrack(recording);
    },
    [appendRecordingTrack, isPlaying]
  );

  useEffect(() => {
    if (isPlaying) {
      return;
    }
    const pending = pendingRecordingRef.current;
    if (!pending) {
      return;
    }
    pendingRecordingRef.current = null;
    void appendRecordingTrack(pending);
  }, [appendRecordingTrack, isPlaying]);

  const handleStartRecording = useCallback(async () => {
    if (!supportsRecording) {
      Alert.alert('Recording unavailable', 'Recording is not supported in this build.');
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
      const effectiveFormat =
        Platform.OS === 'ios' && recordingFormat === 'mp3' ? 'aac' : recordingFormat;
      if (Platform.OS === 'ios' && recordingFormat === 'mp3') {
        Alert.alert('MP3 not supported on iOS', 'Recording will be saved as AAC (m4a).');
      }
      await AudioEngineModule.startRecording({
        sampleRate: recordingSampleRate,
        channels: recordingChannels,
        format: effectiveFormat,
        quality: recordingQuality,
      });
      AudioEngineModule.setRecordingVolume(recordingVolume);
      recordingStartRef.current = Date.now();
      setRecordingElapsed(0);
      setRecordingLevel(0);
      const isNativeRecording = AudioEngineModule.isRecording();
      setIsRecording(isNativeRecording);
      setRecordingStatus(isNativeRecording ? 'Recording' : 'Start failed');
      if (!isNativeRecording) {
        recordingStartRef.current = null;
        setRecordingElapsed(0);
        setRecordingLevel(0);
        Alert.alert('Recording failed', 'Recording did not start.');
      }
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
      const isNativeRecording = AudioEngineModule.isRecording();
      if (!isNativeRecording) {
        setIsRecording(false);
        setRecordingStatus('Not recording');
        return;
      }
      const result = await AudioEngineModule.stopRecording();
      handleRecordingResult(result as RecordingInfo);
      recordingStartRef.current = null;
      setRecordingElapsed(0);
      setRecordingLevel(0);
      setIsRecording(false);
      setRecordingStatus('Recording saved');
    } catch (error: any) {
      console.error('Stop recording error:', error);
      setIsRecording(false);
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

  const handleBackgroundToggle = useCallback(
    (value: 0 | 1) => {
      setBackgroundEnabled(value);
      if (!canUseBackground) {
        return;
      }
      try {
        if (value === 1) {
          const title = tracks[0]?.name ?? 'Sezo Audio Engine';
          const artist = 'Sezo Audio Engine';
          AudioEngineModule.enableBackgroundPlayback({ title, artist });
        } else {
          AudioEngineModule.disableBackgroundPlayback();
        }
      } catch (error) {
        console.warn('[Background] Toggle failed', error);
      }
    },
    [canUseBackground, tracks]
  );

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
      queuePlaybackExpectation('playing', 'play');
      setIsPlaying(true);
      setStatus('Playing');
    } catch (error) {
      console.error('Play error:', error);
    }
  }, [queuePlaybackExpectation]);

  const handlePause = useCallback(() => {
    try {
      AudioEngineModule.pause();
      queuePlaybackExpectation('paused', 'pause');
      setIsPlaying(false);
      setStatus('Paused');
    } catch (error) {
      console.error('Pause error:', error);
    }
  }, [queuePlaybackExpectation]);

  const handleStop = useCallback(() => {
    try {
      AudioEngineModule.stop();
      queuePlaybackExpectation('stopped', 'stop');
      setIsPlaying(false);
      setPosition(0);
      setStatus('Stopped');
    } catch (error) {
      console.error('Stop error:', error);
    }
  }, [queuePlaybackExpectation]);

  const handleReset = useCallback(() => {
    try {
      AudioEngineModule.stop();
      queuePlaybackExpectation('stopped', 'reset');
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
  }, [engineAny, queuePlaybackExpectation, supportsTrackPitch, supportsTrackSpeed, tracks]);

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
        return;
      }
      await Share.share({
        message: lastExtraction.uri,
        url: lastExtraction.uri,
      });
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
        return;
      }
      await Share.share({
        message: lastRecording.uri,
        url: lastRecording.uri,
      });
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

  const formatClock = (timestamp: number) => {
    const date = new Date(timestamp);
    const hours = date.getHours().toString().padStart(2, '0');
    const minutes = date.getMinutes().toString().padStart(2, '0');
    const seconds = date.getSeconds().toString().padStart(2, '0');
    return `${hours}:${minutes}:${seconds}`;
  };

  const playbackPendingCount = playbackTests.filter(
    (entry) => entry.status === 'pending'
  ).length;
  const playbackFailCount = playbackTests.filter(
    (entry) => entry.status === 'fail'
  ).length;

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
      {activeScreen === 'testLab' ? (
        <TestLabScreen
          engineReady={engineReady}
          onBack={handleCloseTestLab}
          captureSnapshot={captureEngineSnapshot}
          restoreSnapshot={restoreEngineSnapshot}
        />
      ) : (
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

            <View style={styles.heroActions}>
              <TouchableOpacity style={styles.resetButton} onPress={handleOpenTestLab}>
                <Text style={styles.resetButtonText}>Test Lab</Text>
              </TouchableOpacity>
              {tracks.length === 0 && (
                <TouchableOpacity style={styles.primaryButton} onPress={loadTracks}>
                  <Text style={styles.primaryButtonText}>Load Tracks</Text>
                </TouchableOpacity>
              )}
            </View>
          </Animated.View>

          {tracks.length > 0 && (
            <>
              <CollapsibleSection
                title="Transport"
                right={
                  <View style={styles.timePill}>
                    <Text style={styles.timeText}>
                      {formatTime(position)} / {formatTime(duration)}
                    </Text>
                  </View>
                }
                containerStyle={controlsStyle}
              >
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
            </CollapsibleSection>

            <CollapsibleSection
              title="Playback Event Tests"
              right={
                <Text style={styles.sectionHint}>
                  Pending {playbackPendingCount} • Failed {playbackFailCount}
                </Text>
              }
              containerStyle={controlsStyle}
            >
              <Text style={styles.sectionHint}>
                Asserts playbackStateChange events for play, pause, and stop.
              </Text>

              <View style={styles.transportRow}>
                <TouchableOpacity style={styles.resetButton} onPress={clearPlaybackTests}>
                  <Text style={styles.resetButtonText}>Clear Log</Text>
                </TouchableOpacity>
              </View>

              {playbackTests.length === 0 ? (
                <Text style={styles.sectionHint}>No playback state events yet.</Text>
              ) : (
                playbackTests.map((entry) => {
                  const statusStyle =
                    entry.status === 'pass'
                      ? styles.testEntryPass
                      : entry.status === 'fail'
                        ? styles.testEntryFail
                        : entry.status === 'pending'
                          ? styles.testEntryPending
                          : styles.testEntryUnexpected;
                  const title =
                    entry.status === 'unexpected'
                      ? `Unexpected ${entry.actual ?? 'state'}`
                      : `Expected ${entry.expected ?? 'state'}`;
                  const metaParts = [
                    entry.actual ? `Actual ${entry.actual}` : 'Awaiting event',
                    formatClock(entry.timestamp),
                    entry.source,
                  ];
                  if (typeof entry.positionMs === 'number') {
                    metaParts.push(`pos ${formatSeconds(entry.positionMs)}`);
                  }
                  if (typeof entry.durationMs === 'number') {
                    metaParts.push(`dur ${formatSeconds(entry.durationMs)}`);
                  }

                  return (
                    <View key={entry.id} style={[styles.testEntry, statusStyle]}>
                      <Text style={styles.testEntryTitle}>{title}</Text>
                      <Text style={styles.testEntryMeta}>{metaParts.join(' • ')}</Text>
                      {entry.message ? (
                        <Text style={styles.testEntryMeta}>{entry.message}</Text>
                      ) : null}
                    </View>
                  );
                })
              )}
            </CollapsibleSection>

            <CollapsibleSection
              title="Master"
              right={
                <>
                  <Text style={styles.sectionHint}>Global mix controls</Text>
                  <TouchableOpacity
                    style={styles.iconResetButton}
                    onPress={handleMasterReset}
                  >
                    <Text style={styles.iconResetText}>↺</Text>
                  </TouchableOpacity>
                </>
              }
              containerStyle={controlsStyle}
            >
              <LabeledSlider
                label="Master Volume"
                value={masterVolume}
                minimumValue={0}
                maximumValue={2.0}
                formatValue={(value) => `${(value * 100).toFixed(0)}%`}
                onValueChange={handleMasterVolumeChange}
                onReset={() => handleMasterVolumeChange(1.0)}
                minimumTrackTintColor={theme.colors.accent}
                maximumTrackTintColor={theme.colors.track}
                thumbTintColor={theme.colors.accentStrong}
              />

              <LabeledSlider
                label="Master Pitch"
                value={masterPitch}
                minimumValue={-12.0}
                maximumValue={12.0}
                formatValue={(value) => `${value > 0 ? '+' : ''}${value.toFixed(1)} st`}
                onValueChange={handleMasterPitchChange}
                onReset={() => handleMasterPitchChange(0.0)}
                minimumTrackTintColor={theme.colors.accent}
                maximumTrackTintColor={theme.colors.track}
                thumbTintColor={theme.colors.accentStrong}
              />

              <LabeledSlider
                label="Master Speed"
                value={masterSpeed}
                minimumValue={0.5}
                maximumValue={2.0}
                formatValue={(value) => `${(value * 100).toFixed(0)}%`}
                onValueChange={handleMasterSpeedChange}
                onReset={() => handleMasterSpeedChange(1.0)}
                minimumTrackTintColor={theme.colors.accent}
                maximumTrackTintColor={theme.colors.track}
                thumbTintColor={theme.colors.accentStrong}
              />
            </CollapsibleSection>

            <CollapsibleSection
              title="Background Playback"
              right={<Text style={styles.sectionHint}>Lock screen controls</Text>}
              containerStyle={controlsStyle}
            >
              {!canUseBackground && (
                <Text style={styles.sectionHint}>
                  Background playback is iOS-only for now.
                </Text>
              )}

              <TogglePillGroup
                value={backgroundEnabled}
                onChange={(value) => handleBackgroundToggle(value as 0 | 1)}
                disabled={!canUseBackground}
                options={BACKGROUND_OPTIONS}
              />
            </CollapsibleSection>

            <CollapsibleSection
              title="Recording"
              right={
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
              }
              containerStyle={controlsStyle}
            >
              <TogglePillGroup
                value={recordingFormat}
                onChange={setRecordingFormat}
                disabled={isRecording || recordingUnavailable}
                options={AUDIO_FORMAT_OPTIONS}
              />

              {recordingFormat === 'aac' && Platform.OS === 'android' && (
                <Text style={styles.sectionHint}>
                  AAC recordings cannot be auto-loaded yet. Use M4A/MP3/WAV for timeline placement.
                </Text>
              )}

              {recordingFormat === 'mp3' && Platform.OS === 'ios' && (
                <Text style={styles.sectionHint}>
                  iOS saves MP3 recordings as AAC (m4a).
                </Text>
              )}

              <TogglePillGroup
                value={recordingQuality}
                onChange={setRecordingQuality}
                disabled={isRecording || recordingUnavailable}
                options={RECORDING_QUALITY_OPTIONS}
              />

              <TogglePillGroup
                value={recordingChannels}
                onChange={(value) => setRecordingChannels(value as 1 | 2)}
                disabled={isRecording || recordingUnavailable}
                options={RECORDING_CHANNEL_OPTIONS}
              />

              <TogglePillGroup
                value={recordingSampleRate}
                onChange={(value) => setRecordingSampleRate(value as 44100 | 48000)}
                disabled={isRecording || recordingUnavailable}
                options={RECORDING_SAMPLE_RATE_OPTIONS}
              />

              <LabeledSlider
                label="Input Gain"
                value={recordingVolume}
                minimumValue={0}
                maximumValue={2.0}
                formatValue={(value) => `${(value * 100).toFixed(0)}%`}
                onValueChange={handleRecordingVolumeChange}
                disabled={recordingUnavailable}
                resetDisabled={recordingUnavailable}
                onReset={() => handleRecordingVolumeChange(1.0)}
                minimumTrackTintColor={theme.colors.accent}
                maximumTrackTintColor={theme.colors.track}
                thumbTintColor={theme.colors.accentStrong}
              />

              <ProgressBar progress={recordingLevel} fillStyle={styles.levelFill} />

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
            </CollapsibleSection>

            <CollapsibleSection
              title="Extraction"
              right={<Text style={styles.sectionHint}>Offline export</Text>}
              containerStyle={controlsStyle}
            >
              <TogglePillGroup
                value={extractionFormat}
                onChange={setExtractionFormat}
                disabled={isExtracting}
                options={AUDIO_FORMAT_OPTIONS}
              />

              <View style={styles.formatRow}>
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

              <ProgressBar progress={extractionProgress} />

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
            </CollapsibleSection>

            <CollapsibleSection
              title="Tracks"
              right={<Text style={styles.sectionHint}>{tracks.length} loaded</Text>}
              variant="plain"
              containerStyle={tracksStyle}
            >
              {tracks.map((track) => (
                <TrackCard
                  key={track.id}
                  track={track}
                  supportsTrackPitch={supportsTrackPitch}
                  supportsTrackSpeed={supportsTrackSpeed}
                  onToggleMute={handleTrackMuteToggle}
                  onToggleSolo={handleTrackSoloToggle}
                  onVolumeChange={handleTrackVolumeChange}
                  onPanChange={handleTrackPanChange}
                  onPitchChange={handleTrackPitchChange}
                  onSpeedChange={handleTrackSpeedChange}
                  formatStartTime={formatSeconds}
                />
              ))}
            </CollapsibleSection>
          </>
        )}
      </ScrollView>
      )}
    </View>
  );
}
