import type { AudioEngine, AudioTrack, MediaMetadata, RecordingConfig } from '../src';
import {
  PUBLIC_METHODS,
  createFakeNativeModule,
  loadWithFakeNative,
  type FakeNativeModule,
} from './fakeNativeModule';

type ForwardCase = {
  method: Exclude<keyof AudioEngine, 'addListener'>;
  args: unknown[];
  returns: unknown;
};

const track: AudioTrack = { id: 'vocals', uri: 'file:///vocals.m4a', volume: 0.8, pan: -0.5 };
const metadata: MediaMetadata = {
  title: 'Song',
  artist: 'Artist',
  playbackCard: { showNext: false, seekStepMs: 10_000 },
};
const recordingResult = {
  uri: 'file:///rec.m4a',
  duration: 1200,
  startTimeMs: 500,
  sampleRate: 48000,
  channels: 1,
  format: 'm4a',
  fileSize: 4096,
};
const extraction = { trackId: 'vocals', uri: 'file:///x.wav', duration: 10, format: 'wav', fileSize: 1 };

// One row per public method: the args a caller passes and what native returns.
const FORWARD_CASES: ForwardCase[] = [
  { method: 'initialize', args: [{ sampleRate: 48000, maxTracks: 8 }], returns: Promise.resolve() },
  { method: 'release', args: [], returns: Promise.resolve() },
  { method: 'loadTracks', args: [[track]], returns: Promise.resolve() },
  { method: 'unloadTrack', args: ['vocals'], returns: Promise.resolve() },
  { method: 'unloadAllTracks', args: [], returns: Promise.resolve() },
  { method: 'getLoadedTracks', args: [], returns: [track] },
  { method: 'play', args: [], returns: undefined },
  { method: 'pause', args: [], returns: undefined },
  { method: 'stop', args: [], returns: undefined },
  { method: 'seek', args: [1500], returns: undefined },
  { method: 'isPlaying', args: [], returns: true },
  { method: 'getCurrentPosition', args: [], returns: 1234 },
  { method: 'getDuration', args: [], returns: 180000 },
  { method: 'setTrackVolume', args: ['vocals', 0.25], returns: undefined },
  { method: 'setTrackMuted', args: ['vocals', true], returns: undefined },
  { method: 'setTrackSolo', args: ['vocals', false], returns: undefined },
  { method: 'setTrackPan', args: ['vocals', -1], returns: undefined },
  { method: 'setTrackPitch', args: ['vocals', -3.5], returns: undefined },
  { method: 'getTrackPitch', args: ['vocals'], returns: -3.5 },
  { method: 'setTrackSpeed', args: ['vocals', 0.75], returns: undefined },
  { method: 'getTrackSpeed', args: ['vocals'], returns: 0.75 },
  { method: 'setMasterVolume', args: [0.5], returns: undefined },
  { method: 'getMasterVolume', args: [], returns: 0.5 },
  { method: 'setPitch', args: [2], returns: undefined },
  { method: 'getPitch', args: [], returns: 2 },
  { method: 'setSpeed', args: [1.25], returns: undefined },
  { method: 'getSpeed', args: [], returns: 1.25 },
  { method: 'setTempoAndPitch', args: [0.9, -1], returns: undefined },
  { method: 'startRecording', args: [{ format: 'wav', channels: 2 }], returns: Promise.resolve() },
  { method: 'stopRecording', args: [], returns: Promise.resolve(recordingResult) },
  { method: 'isRecording', args: [], returns: false },
  { method: 'setRecordingVolume', args: [0.7], returns: undefined },
  { method: 'extractTrack', args: ['vocals', { format: 'wav' }], returns: Promise.resolve(extraction) },
  { method: 'extractAllTracks', args: [{ includeEffects: true }], returns: Promise.resolve([extraction]) },
  { method: 'cancelExtraction', args: [7], returns: true },
  { method: 'getInputLevel', args: [], returns: -12 },
  { method: 'getOutputLevel', args: [], returns: -6 },
  { method: 'getTrackLevel', args: ['vocals'], returns: -9 },
  { method: 'enableBackgroundPlayback', args: [metadata], returns: Promise.resolve() },
  { method: 'updateNowPlayingInfo', args: [{ title: 'Other' }], returns: undefined },
  { method: 'disableBackgroundPlayback', args: [], returns: Promise.resolve() },
];

let engine: AudioEngine;
let native: FakeNativeModule;

beforeEach(async () => {
  ({ AudioEngineModule: engine, native } = await loadWithFakeNative());
});

describe('module loading', () => {
  it('resolves the ExpoAudioEngineModule native module exactly once', async () => {
    const requireNativeModule = jest.fn(() => createFakeNativeModule());
    await loadWithFakeNative(requireNativeModule);
    expect(requireNativeModule).toHaveBeenCalledTimes(1);
    expect(requireNativeModule).toHaveBeenCalledWith('ExpoAudioEngineModule');
  });

  it('does not call into native at import time', () => {
    for (const name of PUBLIC_METHODS) {
      expect(native[name]).not.toHaveBeenCalled();
    }
  });

  it('surfaces a missing native module as an import-time error', async () => {
    const requireNativeModule = jest.fn(() => {
      throw new Error("Cannot find native module 'ExpoAudioEngineModule'");
    });
    await expect(loadWithFakeNative(requireNativeModule)).rejects.toThrow(
      "Cannot find native module 'ExpoAudioEngineModule'"
    );
  });

  it('re-exports the module object from the package entry point', async () => {
    const { index, AudioEngineModule } = await loadWithFakeNative();
    expect(index.AudioEngineModule).toBe(AudioEngineModule);
  });
});

describe('public API surface', () => {
  it('exposes exactly the documented methods', () => {
    expect(Object.keys(engine).sort()).toEqual([...PUBLIC_METHODS].sort());
  });

  it('exposes only functions', () => {
    for (const name of PUBLIC_METHODS) {
      expect(typeof engine[name]).toBe('function');
    }
  });

  it('has a forwarding case for every method except addListener', () => {
    const covered = FORWARD_CASES.map((c) => c.method).sort();
    expect(covered).toEqual(PUBLIC_METHODS.filter((m) => m !== 'addListener').sort());
  });
});

describe('forwarding to native', () => {
  it.each(FORWARD_CASES)('$method forwards its arguments and return value', ({ method, args, returns }) => {
    native[method].mockReturnValue(returns);

    const result = (engine[method] as (...a: unknown[]) => unknown)(...args);

    expect(native[method]).toHaveBeenCalledTimes(1);
    expect(native[method]).toHaveBeenCalledWith(...args);
    expect(result).toBe(returns);
    for (const other of PUBLIC_METHODS) {
      if (other !== method) expect(native[other]).not.toHaveBeenCalled();
    }
  });
});

describe('argument passing', () => {
  it('passes config objects through by reference without copying or mutating', async () => {
    const config: RecordingConfig = Object.freeze({ format: 'aac', quality: 'high', enableNoiseGate: true });
    native.startRecording.mockResolvedValue(undefined);

    await engine.startRecording(config);

    expect(native.startRecording.mock.calls[0][0]).toBe(config);
  });

  it('passes the track array through unchanged', async () => {
    const tracks: AudioTrack[] = [track, { id: 'band', uri: 'https://x/band.mp3', type: 'remote', startTimeMs: 250 }];
    native.loadTracks.mockResolvedValue(undefined);

    await engine.loadTracks(tracks);

    expect(native.loadTracks.mock.calls[0][0]).toBe(tracks);
    expect(native.loadTracks.mock.calls[0][0]).toEqual([track, expect.objectContaining({ startTimeMs: 250 })]);
  });

  it.each([
    ['startRecording', () => engine.startRecording(), [undefined]],
    ['extractTrack', () => engine.extractTrack('vocals'), ['vocals', undefined]],
    ['extractAllTracks', () => engine.extractAllTracks(), [undefined]],
    ['cancelExtraction', () => engine.cancelExtraction(), [undefined]],
  ] as const)('%s forwards omitted optional arguments as undefined', (method, call, expected) => {
    call();
    expect(native[method]).toHaveBeenCalledWith(...expected);
  });

  it('does not validate or clamp values in JS (native owns validation)', () => {
    engine.setMasterVolume(5);
    engine.setTrackPan('vocals', -42);
    engine.seek(-1);
    expect(native.setMasterVolume).toHaveBeenCalledWith(5);
    expect(native.setTrackPan).toHaveBeenCalledWith('vocals', -42);
    expect(native.seek).toHaveBeenCalledWith(-1);
  });

  it('forwards setTempoAndPitch as (tempo, pitch) in that order', () => {
    engine.setTempoAndPitch(1.5, -2);
    expect(native.setTempoAndPitch).toHaveBeenCalledWith(1.5, -2);
  });
});

describe('error propagation', () => {
  it.each(['initialize', 'loadTracks', 'stopRecording', 'extractTrack', 'enableBackgroundPlayback'] as const)(
    '%s rejects with the native rejection',
    async (method) => {
      const error = Object.assign(new Error('native failure'), { code: 'ERR_TEST' });
      native[method].mockRejectedValue(error);

      const call = (engine[method] as (...a: unknown[]) => Promise<unknown>)(...([] as unknown[]));

      await expect(call).rejects.toBe(error);
    }
  );

  it.each(['play', 'seek', 'setTrackVolume', 'getCurrentPosition'] as const)(
    '%s rethrows synchronous native errors',
    (method) => {
      native[method].mockImplementation(() => {
        throw new Error('engine not initialized');
      });

      expect(() => (engine[method] as (...a: unknown[]) => unknown)()).toThrow('engine not initialized');
    }
  );

  it('resolves stopRecording with the native result object', async () => {
    native.stopRecording.mockResolvedValue(recordingResult);
    await expect(engine.stopRecording()).resolves.toEqual(recordingResult);
  });
});

describe('event subscriptions', () => {
  it('registers the listener on the native module and returns its subscription', () => {
    const callback = jest.fn();
    const sub = engine.addListener('positionUpdate', callback);

    expect(native.addListener).toHaveBeenCalledTimes(1);
    expect(native.addListener).toHaveBeenCalledWith('positionUpdate', callback);
    expect(sub).toBe(native.addListener.mock.results[0].value);
    expect(typeof sub.remove).toBe('function');
  });

  it('delivers native event payloads to the callback unchanged', () => {
    const callback = jest.fn();
    engine.addListener('playbackStateChange', callback);
    const payload = { state: 'playing', positionMs: 10, durationMs: 100 };

    native.emit('playbackStateChange', payload);

    expect(callback).toHaveBeenCalledTimes(1);
    expect(callback.mock.calls[0][0]).toBe(payload);
  });

  it('only delivers events to listeners of that event', () => {
    const onLoaded = jest.fn();
    const onError = jest.fn();
    engine.addListener('trackLoaded', onLoaded);
    engine.addListener('error', onError);

    native.emit('trackLoaded', { trackId: 'vocals' });

    expect(onLoaded).toHaveBeenCalledWith({ trackId: 'vocals' });
    expect(onError).not.toHaveBeenCalled();
  });

  it('stops delivering after remove()', () => {
    const callback = jest.fn();
    const sub = engine.addListener('recordingStopped', callback);

    native.emit('recordingStopped', recordingResult);
    sub.remove();
    native.emit('recordingStopped', recordingResult);

    expect(callback).toHaveBeenCalledTimes(1);
    expect(native.listenerCount('recordingStopped')).toBe(0);
  });

  it('removing one subscription leaves the others attached', () => {
    const first = jest.fn();
    const second = jest.fn();
    const firstSub = engine.addListener('engineStateChanged', first);
    engine.addListener('engineStateChanged', second);

    firstSub.remove();
    native.emit('engineStateChanged', { reason: 'mediaServicesReset' });

    expect(first).not.toHaveBeenCalled();
    expect(second).toHaveBeenCalledWith({ reason: 'mediaServicesReset' });
    expect(native.listenerCount('engineStateChanged')).toBe(1);
  });

  it('tolerates remove() being called twice', () => {
    const sub = engine.addListener('debugLog', jest.fn());
    sub.remove();
    expect(() => sub.remove()).not.toThrow();
    expect(native.listenerCount('debugLog')).toBe(0);
  });

  it('supports subscribing to every documented event name', () => {
    const events = [
      'playbackStateChange',
      'positionUpdate',
      'playbackComplete',
      'trackLoaded',
      'trackUnloaded',
      'recordingStarted',
      'recordingStopped',
      'extractionProgress',
      'extractionComplete',
      'engineStateChanged',
      'debugLog',
      'error',
    ] as const;
    const subs = events.map((event) => engine.addListener(event, jest.fn()));

    expect(native.addListener.mock.calls.map(([event]) => event)).toEqual(events);
    subs.forEach((sub) => sub.remove());
    for (const event of events) expect(native.listenerCount(event)).toBe(0);
  });
});
