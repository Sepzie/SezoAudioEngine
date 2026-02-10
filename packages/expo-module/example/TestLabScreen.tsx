import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  Alert,
  NativeModules,
  PermissionsAndroid,
  Platform,
  ScrollView,
  Share,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import { AudioEngineModule } from 'sezo-audio-engine';
import { Asset } from 'expo-asset';
import * as FileSystem from 'expo-file-system';
import * as IntentLauncher from 'expo-intent-launcher';
import CollapsibleSection from './components/CollapsibleSection';
import ProgressBar from './components/ProgressBar';
import { styles } from './ui';
import { Track } from './types';

type TestStatus = 'idle' | 'running' | 'pass' | 'fail';

interface TestResult {
  id: string;
  name: string;
  status: TestStatus;
  metrics?: Record<string, number | string>;
  thresholds?: Record<string, string>;
  error?: string;
}

interface ExecutedTestResult {
  status: 'pass' | 'fail';
  metrics?: Record<string, number | string>;
  thresholds?: Record<string, string>;
  error?: string;
}

interface TestReport {
  timestamp: string;
  platform: string;
  device: string;
  tests: Array<{
    name: string;
    status: 'pass' | 'fail';
    metrics: Record<string, number | string>;
  }>;
}

interface StoredTestReport {
  schemaVersion: number;
  savedAt: string;
  report: TestReport;
}

interface SavedReportEntry {
  id: string;
  uri: string;
  savedAt: string;
  report: TestReport;
}

interface TestFixtures {
  track1Uri: string;
  track2Uri: string;
  toneUri: string;
}

interface EngineSnapshot {
  tracks: Track[];
  masterVolume: number;
  masterPitch: number;
  masterSpeed: number;
}

interface TestLabScreenProps {
  engineReady: boolean;
  onBack: () => void;
  captureSnapshot: () => EngineSnapshot;
  restoreSnapshot: (snapshot: EngineSnapshot) => Promise<void>;
}

const DRIFT_THRESHOLD_MS = 50;
const SEEK_THRESHOLD_MS = 150;
const EXTRACTION_DURATION_TOLERANCE_MS = 200;
const SPEED_ADVANCE_MIN_MS = 2200;
const TESTLAB_REPORTS_DIR_NAME = 'sezo-testlab-reports';
const TESTLAB_REPORTS_LIMIT = 40;
const TESTLAB_REPORT_SCHEMA_VERSION = 1;

const wait = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

const resolveAssetUri = (asset: Asset) => asset.localUri ?? asset.uri;

const formatMetricValue = (key: string, value: number | string) => {
  if (typeof value === 'number') {
    if (key.toLowerCase().endsWith('ms')) {
      return `${Math.round(value)}ms`;
    }
    if (Number.isInteger(value)) {
      return `${value}`;
    }
    return value.toFixed(2);
  }
  return value;
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

const ensureRecordingPermission = async () => {
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
    console.warn('[TestLab] Permission request failed', error);
    return false;
  }
};

const loadFixtures = async (): Promise<TestFixtures> => {
  const assets = await Asset.loadAsync([
    require('./assets/track1.mp3'),
    require('./assets/track2.mp3'),
    require('./assets/tone_1khz_5s.wav'),
  ]);
  return {
    track1Uri: resolveAssetUri(assets[0]),
    track2Uri: resolveAssetUri(assets[1]),
    toneUri: resolveAssetUri(assets[2]),
  };
};

const buildTestTracks = (fixtures: TestFixtures, includeTone: boolean) => {
  const base = [
    {
      id: 'test_track1',
      uri: fixtures.track1Uri,
      volume: 1.0,
      pan: 0.0,
      muted: false,
      startTimeMs: 0,
    },
    {
      id: 'test_track2',
      uri: fixtures.track2Uri,
      volume: 1.0,
      pan: 0.0,
      muted: false,
      startTimeMs: 0,
    },
  ];

  if (!includeTone) {
    return base;
  }

  return [
    base[0],
    {
      id: 'test_tone',
      uri: fixtures.toneUri,
      volume: 0.9,
      pan: 0.0,
      muted: false,
      startTimeMs: 0,
    },
  ];
};

const loadTestSession = async (fixtures: TestFixtures) => {
  AudioEngineModule.stop();
  AudioEngineModule.unloadAllTracks();
  AudioEngineModule.setMasterVolume(1.0);
  AudioEngineModule.setPitch(0.0);
  AudioEngineModule.setSpeed(1.0);
  await wait(120);
  const tracks = buildTestTracks(fixtures, true);
  await AudioEngineModule.loadTracks(tracks);
  return tracks;
};

const resetTestState = (toneEnabled: boolean) => {
  try {
    AudioEngineModule.stop();
    AudioEngineModule.seek(0);
    AudioEngineModule.setMasterVolume(1.0);
    AudioEngineModule.setPitch(0.0);
    AudioEngineModule.setSpeed(1.0);

    const baseTrackIds = ['test_track1', 'test_track2', 'test_tone'];
    for (const trackId of baseTrackIds) {
      AudioEngineModule.setTrackVolume(trackId, 1.0);
      AudioEngineModule.setTrackPan(trackId, 0.0);
      AudioEngineModule.setTrackMuted(trackId, false);
      AudioEngineModule.setTrackSolo(trackId, false);
    }
    AudioEngineModule.setTrackMuted('test_tone', !toneEnabled);
    if (!toneEnabled) {
      AudioEngineModule.setTrackVolume('test_tone', 0.0);
    } else {
      AudioEngineModule.setTrackVolume('test_tone', 0.9);
    }
  } catch (error) {
    console.warn('[TestLab] Reset state failed', error);
  }
};

const samplePositionDrift = async (durationMs: number, intervalMs: number) => {
  const samples: Array<{ elapsed: number; position: number }> = [];
  const start = Date.now();
  await wait(intervalMs);
  while (Date.now() - start < durationMs) {
    const now = Date.now();
    samples.push({
      elapsed: now - start,
      position: AudioEngineModule.getCurrentPosition(),
    });
    await wait(intervalMs);
  }

  if (samples.length === 0) {
    return 0;
  }

  const baselineOffset = samples[0].position - samples[0].elapsed;
  let maxDrift = 0;
  for (const sample of samples) {
    const expected = sample.elapsed + baselineOffset;
    const drift = Math.abs(sample.position - expected);
    if (drift > maxDrift) {
      maxDrift = drift;
    }
  }
  return maxDrift;
};

const buildReport = (results: TestResult[]): TestReport => {
  return {
    timestamp: new Date().toISOString(),
    platform: Platform.OS,
    device: `${Platform.OS} ${String(Platform.Version)}`,
    tests: results.map((result) => ({
      name: result.name,
      status: result.status === 'pass' ? 'pass' : 'fail',
      metrics: result.metrics ?? {},
    })),
  };
};

const summarizeReport = (report: TestReport) => {
  const total = report.tests.length;
  const passed = report.tests.filter((test) => test.status === 'pass').length;
  const failed = total - passed;
  return { total, passed, failed };
};

const readPublicEnv = (key: string) => {
  const env = (
    globalThis as { process?: { env?: Record<string, string | undefined> } }
  ).process?.env;
  return env?.[key];
};

const resolveDevServerHost = () => {
  const scriptUrl = (
    NativeModules as { SourceCode?: { scriptURL?: string } }
  ).SourceCode?.scriptURL;
  if (!scriptUrl || !scriptUrl.startsWith('http')) {
    return null;
  }
  try {
    return new URL(scriptUrl).hostname;
  } catch {
    const match = scriptUrl.match(/^https?:\/\/([^/:]+)/i);
    return match?.[1] ?? null;
  }
};

const resolveDevReportEndpoint = () => {
  const explicitUrl = readPublicEnv('EXPO_PUBLIC_TESTLAB_REPORT_UPLOAD_URL');
  if (explicitUrl) {
    return explicitUrl;
  }
  const host = resolveDevServerHost();
  if (!host) {
    return null;
  }
  const port = readPublicEnv('EXPO_PUBLIC_TESTLAB_REPORT_UPLOAD_PORT') ?? '8099';
  const path = readPublicEnv('EXPO_PUBLIC_TESTLAB_REPORT_UPLOAD_PATH') ?? '/testlab/report';
  const normalizedPath = path.startsWith('/') ? path : `/${path}`;
  return `http://${host}:${port}${normalizedPath}`;
};

const getReportsDirectoryUri = () => {
  if (!FileSystem.documentDirectory) {
    return null;
  }
  return `${FileSystem.documentDirectory}${TESTLAB_REPORTS_DIR_NAME}/`;
};

const normalizeSavedReport = (value: unknown): { report: TestReport; savedAt: string } | null => {
  if (!value || typeof value !== 'object') {
    return null;
  }
  const candidate = value as Partial<StoredTestReport> & Partial<TestReport>;
  if (candidate.report && typeof candidate.savedAt === 'string') {
    return {
      report: candidate.report as TestReport,
      savedAt: candidate.savedAt,
    };
  }
  if (typeof candidate.timestamp === 'string' && Array.isArray(candidate.tests)) {
    return {
      report: candidate as TestReport,
      savedAt: candidate.timestamp,
    };
  }
  return null;
};

const getFileSize = (info: FileSystem.FileInfo) => {
  if (!info.exists || info.isDirectory || typeof info.size !== 'number') {
    return 0;
  }
  return info.size;
};

const TestLabScreen = ({
  engineReady,
  onBack,
  captureSnapshot,
  restoreSnapshot,
}: TestLabScreenProps) => {
  const initialResults = useMemo(
    () => [
      { id: 'load_playback', name: 'Load & Playback Smoke', status: 'idle' as const },
      { id: 'sync_drift', name: 'Multi-Track Sync Drift', status: 'idle' as const },
      { id: 'seek_accuracy', name: 'Seek Accuracy', status: 'idle' as const },
      { id: 'pitch_speed', name: 'Pitch/Speed Sanity', status: 'idle' as const },
      { id: 'recording_round_trip', name: 'Recording Round Trip', status: 'idle' as const },
      { id: 'extraction_output', name: 'Extraction Output', status: 'idle' as const },
    ],
    []
  );
  const [results, setResults] = useState<TestResult[]>(initialResults);
  const [fixtures, setFixtures] = useState<TestFixtures | null>(null);
  const [isRunning, setIsRunning] = useState(false);
  const [progress, setProgress] = useState(0);
  const [report, setReport] = useState<TestReport | null>(null);
  const [savedReports, setSavedReports] = useState<SavedReportEntry[]>([]);
  const [reportStorageStatus, setReportStorageStatus] = useState('');

  const summary = useMemo(() => {
    const total = results.length;
    const passed = results.filter((result) => result.status === 'pass').length;
    const failed = results.filter((result) => result.status === 'fail').length;
    const running = results.filter((result) => result.status === 'running').length;
    return { total, passed, failed, running };
  }, [results]);

  const resetResults = useCallback(() => {
    setResults(
      initialResults.map((result) => ({
        ...result,
        status: 'idle',
        metrics: undefined,
        thresholds: undefined,
        error: undefined,
      }))
    );
  }, [initialResults]);

  const refreshSavedReports = useCallback(async () => {
    const reportsDir = getReportsDirectoryUri();
    if (!reportsDir) {
      setSavedReports([]);
      return;
    }
    try {
      const dirInfo = await FileSystem.getInfoAsync(reportsDir);
      if (!dirInfo.exists) {
        setSavedReports([]);
        return;
      }
      const fileNames = await FileSystem.readDirectoryAsync(reportsDir);
      const entries: SavedReportEntry[] = [];
      for (const fileName of fileNames) {
        if (!fileName.endsWith('.json')) {
          continue;
        }
        const uri = `${reportsDir}${fileName}`;
        try {
          const raw = await FileSystem.readAsStringAsync(uri);
          const normalized = normalizeSavedReport(JSON.parse(raw));
          if (!normalized) {
            continue;
          }
          entries.push({
            id: fileName,
            uri,
            savedAt: normalized.savedAt,
            report: normalized.report,
          });
        } catch (error) {
          console.warn('[TestLab] Failed to parse saved report', fileName, error);
        }
      }
      entries.sort((a, b) => Date.parse(b.savedAt) - Date.parse(a.savedAt));
      setSavedReports(entries);
    } catch (error) {
      console.warn('[TestLab] Failed to load saved reports', error);
      setSavedReports([]);
    }
  }, []);

  useEffect(() => {
    void refreshSavedReports();
  }, [refreshSavedReports]);

  const saveReportLocally = useCallback(
    async (nextReport: TestReport) => {
      const reportsDir = getReportsDirectoryUri();
      if (!reportsDir) {
        setReportStorageStatus('Unable to access app document directory.');
        return null;
      }
      try {
        await FileSystem.makeDirectoryAsync(reportsDir, { intermediates: true });
        const fileName = `testlab-report-${Date.now()}.json`;
        const fileUri = `${reportsDir}${fileName}`;
        const savedAt = new Date().toISOString();
        const payload: StoredTestReport = {
          schemaVersion: TESTLAB_REPORT_SCHEMA_VERSION,
          savedAt,
          report: nextReport,
        };
        await FileSystem.writeAsStringAsync(fileUri, JSON.stringify(payload, null, 2), {
          encoding: FileSystem.EncodingType.UTF8,
        });

        const allFiles = await FileSystem.readDirectoryAsync(reportsDir);
        const sorted = allFiles.filter((name) => name.endsWith('.json')).sort().reverse();
        for (const staleFile of sorted.slice(TESTLAB_REPORTS_LIMIT)) {
          await FileSystem.deleteAsync(`${reportsDir}${staleFile}`, { idempotent: true });
        }

        await refreshSavedReports();
        setReportStorageStatus(`Saved in app data: ${fileName}`);
        return { fileName, fileUri, savedAt };
      } catch (error) {
        console.warn('[TestLab] Failed to save report locally', error);
        setReportStorageStatus('Failed to save report locally.');
        return null;
      }
    },
    [refreshSavedReports]
  );

  const mirrorReportToComputer = useCallback(
    async (nextReport: TestReport, localFileName: string, savedAt: string) => {
      if (!__DEV__) {
        return;
      }
      const endpoint = resolveDevReportEndpoint();
      if (!endpoint) {
        return;
      }
      try {
        const response = await fetch(endpoint, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            fileName: localFileName,
            savedAt,
            report: nextReport,
          }),
        });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const payload = (await response.json().catch(() => null)) as
          | { savedPath?: string }
          | null;
        setReportStorageStatus(
          `Saved in app data and mirrored to computer: ${payload?.savedPath ?? endpoint}`
        );
      } catch (error: any) {
        console.warn('[TestLab] Failed to mirror report to computer', error);
        setReportStorageStatus(
          `Saved in app data. Computer mirror unavailable (${error?.message ?? 'network error'}).`
        );
      }
    },
    []
  );

  const persistReport = useCallback(
    async (nextReport: TestReport) => {
      const saved = await saveReportLocally(nextReport);
      if (!saved) {
        return;
      }
      await mirrorReportToComputer(nextReport, saved.fileName, saved.savedAt);
    },
    [mirrorReportToComputer, saveReportLocally]
  );

  const shareReportUri = useCallback(async (uri: string) => {
    const shareUri = await getAndroidContentUri(uri);
    if (Platform.OS === 'android') {
      await IntentLauncher.startActivityAsync('android.intent.action.SEND', {
        type: 'application/json',
        flags: 1,
        extra: {
          'android.intent.extra.STREAM': shareUri,
        },
      });
      return;
    }
    await Share.share({
      message: shareUri,
      url: shareUri,
    });
  }, []);

  const loadSavedReport = useCallback(
    async (entry: SavedReportEntry) => {
      if (!__DEV__) {
        setReportStorageStatus('Receiver re-send is only available while running in dev mode.');
        return;
      }
      const endpoint = resolveDevReportEndpoint();
      if (!endpoint) {
        setReportStorageStatus('Receiver endpoint not configured.');
        return;
      }
      try {
        const response = await fetch(endpoint, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            fileName: entry.id,
            savedAt: entry.savedAt,
            report: entry.report,
          }),
        });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const payload = (await response.json().catch(() => null)) as
          | { savedPath?: string }
          | null;
        setReportStorageStatus(`Re-sent to receiver: ${payload?.savedPath ?? endpoint}`);
      } catch (error: any) {
        console.warn('[TestLab] Failed to re-send saved report to receiver', error);
        setReportStorageStatus(
          `Re-send failed (${error?.message ?? 'network error'}). Check receiver/network settings.`
        );
      }
    },
    []
  );

  const shareSavedReport = useCallback(
    async (entry: SavedReportEntry) => {
      try {
        await shareReportUri(entry.uri);
      } catch (error) {
        console.warn('[TestLab] Share saved report failed', error);
      }
    },
    [shareReportUri]
  );

  const deleteSavedReport = useCallback(
    async (entry: SavedReportEntry) => {
      try {
        await FileSystem.deleteAsync(entry.uri, { idempotent: true });
        await refreshSavedReports();
      } catch (error) {
        console.warn('[TestLab] Delete saved report failed', error);
      }
    },
    [refreshSavedReports]
  );

  const resendLatestSavedReport = useCallback(() => {
    if (savedReports.length === 0) {
      Alert.alert('No saved reports', 'Run tests once to create a saved report.');
      return;
    }
    void loadSavedReport(savedReports[0]);
  }, [loadSavedReport, savedReports]);

  const runAllTests = useCallback(async () => {
    if (isRunning) {
      return;
    }
    if (!engineReady) {
      Alert.alert('Engine not ready', 'Initialize the audio engine before running tests.');
      return;
    }
    if (AudioEngineModule.isRecording()) {
      Alert.alert('Recording active', 'Stop recording before running the test suite.');
      return;
    }
    if (AudioEngineModule.isPlaying()) {
      Alert.alert('Playback active', 'Stop playback before running the test suite.');
      return;
    }

    setIsRunning(true);
    setProgress(0);
    resetResults();
    setReport(null);

    const snapshot = captureSnapshot();
    let runResults = initialResults.map((result) => ({
      ...result,
      status: 'idle' as TestStatus,
      metrics: undefined,
      thresholds: undefined,
      error: undefined,
    }));
    setResults(runResults);

    try {
      const resolvedFixtures = fixtures ?? (await loadFixtures());
      if (!fixtures) {
        setFixtures(resolvedFixtures);
      }

      await loadTestSession(resolvedFixtures);
      await wait(120);

      const tests: Array<{
        id: string;
        name: string;
        run: () => Promise<ExecutedTestResult>;
      }> = [
        {
          id: 'load_playback',
          name: 'Load & Playback Smoke',
          run: async () => {
            resetTestState(true);
            AudioEngineModule.play();
            await wait(2000);
            const playingDuring = AudioEngineModule.isPlaying();
            AudioEngineModule.pause();
            const playingAfterPause = AudioEngineModule.isPlaying();
            AudioEngineModule.stop();
            const duration = AudioEngineModule.getDuration();
            const status =
              playingDuring && !playingAfterPause && duration > 0 ? 'pass' : 'fail';
            return {
              status,
              metrics: {
                durationMs: duration,
                playingDuring: playingDuring ? 'true' : 'false',
                playingAfterPause: playingAfterPause ? 'true' : 'false',
              },
            };
          },
        },
        {
          id: 'sync_drift',
          name: 'Multi-Track Sync Drift',
          run: async () => {
            resetTestState(false);
            AudioEngineModule.seek(0);
            AudioEngineModule.play();
            const maxDriftMs = await samplePositionDrift(5000, 100);
            AudioEngineModule.pause();
            const status = maxDriftMs <= DRIFT_THRESHOLD_MS ? 'pass' : 'fail';
            return {
              status,
              metrics: { maxDriftMs },
              thresholds: { maxDriftMs: `<= ${DRIFT_THRESHOLD_MS}ms` },
            };
          },
        },
        {
          id: 'seek_accuracy',
          name: 'Seek Accuracy',
          run: async () => {
            resetTestState(false);
            AudioEngineModule.seek(5000);
            AudioEngineModule.play();
            await wait(1000);
            const position = AudioEngineModule.getCurrentPosition();
            AudioEngineModule.pause();
            const expected = 6000;
            const deltaMs = Math.abs(position - expected);
            const status = deltaMs <= SEEK_THRESHOLD_MS ? 'pass' : 'fail';
            return {
              status,
              metrics: { positionMs: position, expectedMs: expected, deltaMs },
              thresholds: { deltaMs: `<= ${SEEK_THRESHOLD_MS}ms` },
            };
          },
        },
        {
          id: 'pitch_speed',
          name: 'Pitch/Speed Sanity',
          run: async () => {
            resetTestState(true);
            AudioEngineModule.setPitch(3.0);
            AudioEngineModule.setSpeed(1.25);
            AudioEngineModule.play();
            const start = AudioEngineModule.getCurrentPosition();
            await wait(2000);
            const end = AudioEngineModule.getCurrentPosition();
            AudioEngineModule.pause();
            AudioEngineModule.setPitch(0.0);
            AudioEngineModule.setSpeed(1.0);
            const advanceMs = end - start;
            const outputLevel =
              typeof AudioEngineModule.getOutputLevel === 'function'
                ? AudioEngineModule.getOutputLevel()
                : 0;
            const status = advanceMs >= SPEED_ADVANCE_MIN_MS ? 'pass' : 'fail';
            return {
              status,
              metrics: { advanceMs, outputLevel },
              thresholds: { advanceMs: `>= ${SPEED_ADVANCE_MIN_MS}ms` },
            };
          },
        },
        {
          id: 'recording_round_trip',
          name: 'Recording Round Trip',
          run: async () => {
            const permissionGranted = await ensureRecordingPermission();
            if (!permissionGranted) {
              return {
                status: 'fail',
                metrics: { permissionGranted: 'false' },
                error: 'Microphone permission denied',
              };
            }
            resetTestState(false);
            AudioEngineModule.play();
            await wait(200);
            await AudioEngineModule.startRecording({
              format: 'wav',
              quality: 'medium',
            });
            await wait(3000);
            const recording = await AudioEngineModule.stopRecording();
            AudioEngineModule.pause();
            const info = await FileSystem.getInfoAsync(recording.uri);
            let loadSucceeded = false;
            try {
              await AudioEngineModule.loadTracks([
                {
                  id: 'test_recording',
                  uri: recording.uri,
                  volume: 1.0,
                  pan: 0.0,
                  muted: false,
                  startTimeMs: 0,
                },
              ]);
              loadSucceeded = true;
              AudioEngineModule.unloadTrack('test_recording');
            } catch (error) {
              console.warn('[TestLab] Recording load failed', error);
            }
            const fileSize = getFileSize(info);
            const status: ExecutedTestResult['status'] =
              info.exists && fileSize > 0 && recording.duration > 0 && loadSucceeded
                ? 'pass'
                : 'fail';
            return {
              status,
              metrics: {
                durationMs: recording.duration,
                fileSize,
                loadSucceeded: loadSucceeded ? 'true' : 'false',
              },
            };
          },
        },
        {
          id: 'extraction_output',
          name: 'Extraction Output',
          run: async () => {
            resetTestState(false);
            const originalDuration = AudioEngineModule.getDuration();
            const aacResult = await AudioEngineModule.extractTrack('test_track1', {
              format: 'aac',
              includeEffects: false,
            });
            const wavResult = await AudioEngineModule.extractTrack('test_track1', {
              format: 'wav',
              includeEffects: false,
            });
            const aacInfo = await FileSystem.getInfoAsync(aacResult.uri);
            const wavInfo = await FileSystem.getInfoAsync(wavResult.uri);
            const aacSize = getFileSize(aacInfo);
            const wavSize = getFileSize(wavInfo);
            const aacDeltaMs = Math.abs(aacResult.duration - originalDuration);
            const wavDeltaMs = Math.abs(wavResult.duration - originalDuration);
            const status: ExecutedTestResult['status'] =
              aacInfo.exists &&
              wavInfo.exists &&
              aacSize > 0 &&
              wavSize > 0 &&
              aacDeltaMs <= EXTRACTION_DURATION_TOLERANCE_MS &&
              wavDeltaMs <= EXTRACTION_DURATION_TOLERANCE_MS
                ? 'pass'
                : 'fail';
            return {
              status,
              metrics: {
                originalDurationMs: originalDuration,
                aacDurationMs: aacResult.duration,
                aacDeltaMs,
                aacSize,
                wavDurationMs: wavResult.duration,
                wavDeltaMs,
                wavSize,
              },
              thresholds: {
                aacDeltaMs: `<= ${EXTRACTION_DURATION_TOLERANCE_MS}ms`,
                wavDeltaMs: `<= ${EXTRACTION_DURATION_TOLERANCE_MS}ms`,
              },
            };
          },
        },
      ];

      for (let index = 0; index < tests.length; index += 1) {
        const test = tests[index];
        runResults = runResults.map((result) =>
          result.id === test.id ? { ...result, status: 'running' } : result
        );
        setResults(runResults);
        try {
          const result = await test.run();
          runResults = runResults.map((item) =>
            item.id === test.id
              ? {
                  ...item,
                  status: result.status,
                  metrics: result.metrics,
                  thresholds: result.thresholds,
                  error: result.error,
                }
              : item
          );
          setResults(runResults);
        } catch (error: any) {
          runResults = runResults.map((item) =>
            item.id === test.id
              ? { ...item, status: 'fail', error: error?.message ?? 'Test failed' }
              : item
          );
          setResults(runResults);
        }
        setProgress((index + 1) / tests.length);
      }

      const builtReport = buildReport(runResults);
      setReport(builtReport);
      await persistReport(builtReport);
    } catch (error: any) {
      console.warn('[TestLab] Run failed', error);
      Alert.alert('Test run failed', error?.message ?? 'Unable to run tests.');
    } finally {
      AudioEngineModule.stop();
      AudioEngineModule.unloadAllTracks();
      await restoreSnapshot(snapshot);
      setIsRunning(false);
    }
  }, [
    captureSnapshot,
    engineReady,
    fixtures,
    isRunning,
    initialResults,
    resetResults,
    persistReport,
    restoreSnapshot,
  ]);

  const handleShareReport = useCallback(async () => {
    if (!report) {
      Alert.alert('No report', 'Run the test suite to generate a report.');
      return;
    }
    try {
      const saved = await saveReportLocally(report);
      if (saved) {
        await shareReportUri(saved.fileUri);
        return;
      }
      const fallbackFileName = `testlab-report-${Date.now()}.json`;
      const fallbackUri = `${FileSystem.documentDirectory ?? ''}${fallbackFileName}`;
      await FileSystem.writeAsStringAsync(fallbackUri, JSON.stringify(report, null, 2), {
        encoding: FileSystem.EncodingType.UTF8,
      });
      await shareReportUri(fallbackUri);
    } catch (error) {
      console.warn('[TestLab] Share report failed', error);
    }
  }, [report, saveReportLocally, shareReportUri]);

  return (
    <ScrollView contentContainerStyle={styles.content} showsVerticalScrollIndicator={false}>
      <View style={styles.heroCard}>
        <View style={styles.heroTopRow}>
          <View>
            <Text style={styles.eyebrow}>Sezo Audio Engine</Text>
            <Text style={styles.title}>Test Lab</Text>
          </View>
          <TouchableOpacity style={styles.iconResetButton} onPress={onBack}>
            <Text style={styles.iconResetText}>{'<'}</Text>
          </TouchableOpacity>
        </View>
        <Text style={styles.subtitle}>
          Automated in-app validation for playback, sync, recording, and export.
        </Text>
        <View style={styles.heroActions}>
          <TouchableOpacity
            style={[
              styles.primaryButton,
              styles.heroActionButton,
              (!engineReady || isRunning) && styles.controlButtonDisabled,
            ]}
            onPress={runAllTests}
            disabled={!engineReady || isRunning}
          >
            <Text style={styles.primaryButtonText}>
              {isRunning ? 'Running Tests...' : 'Run All Tests'}
            </Text>
          </TouchableOpacity>
          <TouchableOpacity
            style={[
              styles.resetButton,
              styles.heroActionButton,
              (!report || isRunning) && styles.controlButtonDisabled,
            ]}
            onPress={handleShareReport}
            disabled={!report || isRunning}
          >
            <Text style={styles.resetButtonText}>Share JSON</Text>
          </TouchableOpacity>
          <TouchableOpacity
            style={[
              styles.controlButton,
              styles.heroActionButton,
              (savedReports.length === 0 || isRunning) && styles.controlButtonDisabled,
            ]}
            onPress={resendLatestSavedReport}
            disabled={savedReports.length === 0 || isRunning}
          >
            <Text style={styles.controlButtonText}>Re-send Last Report</Text>
          </TouchableOpacity>
        </View>
        <ProgressBar progress={progress} />
        <View style={styles.testSummaryRow}>
          <Text style={styles.sectionHint}>
            {summary.passed}/{summary.total} passed
            {summary.failed ? ` | ${summary.failed} failed` : ''}
            {summary.running ? ` | ${summary.running} running` : ''}
          </Text>
          <Text style={styles.sectionHint}>
            {engineReady ? 'Engine ready' : 'Engine not ready'}
          </Text>
        </View>
        {reportStorageStatus ? (
          <Text style={[styles.sectionHint, styles.reportStorageHint]}>{reportStorageStatus}</Text>
        ) : null}
      </View>

      <CollapsibleSection
        title="Results"
        subtitle="Per-test status and metrics"
        defaultCollapsed={false}
      >
        <View style={styles.testList}>
          {results.map((result) => {
            const statusPillStyle =
              result.status === 'pass'
                ? styles.statusPillReady
                : result.status === 'fail'
                  ? styles.statusPillDanger
                  : result.status === 'running'
                    ? styles.statusPillActive
                    : null;
            const statusLabel =
              result.status === 'running'
                ? 'Running'
                : result.status === 'pass'
                  ? 'Pass'
                  : result.status === 'fail'
                    ? 'Fail'
                    : 'Idle';
            return (
              <View key={result.id} style={styles.testRow}>
                <View style={styles.testRowHeader}>
                  <Text style={styles.testName}>{result.name}</Text>
                  <View style={[styles.statusPill, statusPillStyle]}>
                    <Text style={styles.statusText}>{statusLabel}</Text>
                  </View>
                </View>
                {result.error ? (
                  <Text style={styles.testError}>{result.error}</Text>
                ) : null}
                {result.metrics
                  ? Object.entries(result.metrics).map(([key, value]) => (
                      <View key={key} style={styles.metricRow}>
                        <Text style={styles.metricKey}>{key}</Text>
                        <Text style={styles.metricValue}>
                          {formatMetricValue(key, value)}
                          {result.thresholds?.[key] ? ` (${result.thresholds[key]})` : ''}
                        </Text>
                      </View>
                    ))
                  : null}
              </View>
            );
          })}
        </View>
      </CollapsibleSection>

      <CollapsibleSection
        title="Saved Reports"
        subtitle="Persisted in app data and ready to re-send/share/delete"
        defaultCollapsed={false}
      >
        {savedReports.length === 0 ? (
          <Text style={styles.sectionHint}>No saved reports yet. Run the suite to generate one.</Text>
        ) : (
          <View style={styles.savedReportsList}>
            {savedReports.map((entry) => {
              const summaryForEntry = summarizeReport(entry.report);
              return (
                <View key={entry.id} style={styles.savedReportRow}>
                  <View style={styles.savedReportHeader}>
                    <Text style={styles.savedReportTitle}>{entry.report.timestamp}</Text>
                    <Text style={styles.savedReportMeta}>
                      {summaryForEntry.passed}/{summaryForEntry.total} passed
                      {summaryForEntry.failed ? ` | ${summaryForEntry.failed} failed` : ''}
                    </Text>
                  </View>
                  <Text style={styles.savedReportMeta}>file: {entry.id}</Text>
                  <View style={styles.savedReportActions}>
                    <TouchableOpacity
                      style={styles.savedReportButton}
                      onPress={() => void loadSavedReport(entry)}
                    >
                      <Text style={styles.savedReportButtonText}>Re-send to Receiver</Text>
                    </TouchableOpacity>
                    <TouchableOpacity style={styles.savedReportButton} onPress={() => shareSavedReport(entry)}>
                      <Text style={styles.savedReportButtonText}>Share</Text>
                    </TouchableOpacity>
                    <TouchableOpacity
                      style={[styles.savedReportButton, styles.savedReportDeleteButton]}
                      onPress={() => void deleteSavedReport(entry)}
                    >
                      <Text style={[styles.savedReportButtonText, styles.savedReportDeleteButtonText]}>
                        Delete
                      </Text>
                    </TouchableOpacity>
                  </View>
                </View>
              );
            })}
          </View>
        )}
      </CollapsibleSection>
    </ScrollView>
  );
};

export default TestLabScreen;
