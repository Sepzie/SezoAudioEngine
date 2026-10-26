import type { AudioEngine } from '../src';

type Listener = (payload: unknown) => void;

/**
 * Every method the JS wrapper exposes. Kept as an explicit list (not derived
 * from the wrapper) so removing or renaming a public method fails a test.
 */
export const PUBLIC_METHODS = [
  'initialize',
  'release',
  'loadTracks',
  'unloadTrack',
  'unloadAllTracks',
  'getLoadedTracks',
  'play',
  'pause',
  'stop',
  'seek',
  'isPlaying',
  'getCurrentPosition',
  'getDuration',
  'setTrackVolume',
  'setTrackMuted',
  'setTrackSolo',
  'setTrackPan',
  'setTrackPitch',
  'getTrackPitch',
  'setTrackSpeed',
  'getTrackSpeed',
  'setMasterVolume',
  'getMasterVolume',
  'setPitch',
  'getPitch',
  'setSpeed',
  'getSpeed',
  'setTempoAndPitch',
  'startRecording',
  'stopRecording',
  'isRecording',
  'setRecordingVolume',
  'extractTrack',
  'extractAllTracks',
  'cancelExtraction',
  'getInputLevel',
  'getOutputLevel',
  'getTrackLevel',
  'enableBackgroundPlayback',
  'updateNowPlayingInfo',
  'disableBackgroundPlayback',
  'addListener',
] as const satisfies ReadonlyArray<keyof AudioEngine>;

export type FakeNativeModule = {
  [K in (typeof PUBLIC_METHODS)[number]]: jest.Mock;
} & {
  /** Simulate the native side emitting an event. */
  emit(event: string, payload: unknown): void;
  listenerCount(event: string): number;
};

/**
 * Stand-in for the object `requireNativeModule('ExpoAudioEngineModule')`
 * returns. Methods are bare jest.fn()s; `addListener` behaves like the Expo
 * EventEmitter: it returns a subscription whose `remove()` detaches it.
 */
export function createFakeNativeModule(): FakeNativeModule {
  const listeners = new Map<string, Set<Listener>>();
  const fake: Record<string, unknown> = {};

  for (const name of PUBLIC_METHODS) {
    fake[name] = jest.fn();
  }

  fake.addListener = jest.fn((event: string, callback: Listener) => {
    const set = listeners.get(event) ?? new Set<Listener>();
    listeners.set(event, set);
    set.add(callback);
    return { remove: jest.fn(() => set.delete(callback)) };
  });

  fake.emit = (event: string, payload: unknown) => {
    for (const callback of [...(listeners.get(event) ?? [])]) {
      callback(payload);
    }
  };

  fake.listenerCount = (event: string) => listeners.get(event)?.size ?? 0;

  return fake as FakeNativeModule;
}

/**
 * Import `src/index.ts` in an isolated module registry with
 * `expo-modules-core` replaced, so each test gets a fresh wrapper bound to a
 * fresh fake.
 */
export async function loadWithFakeNative(
  requireNativeModule: jest.Mock = jest.fn(() => createFakeNativeModule())
) {
  let mod: typeof import('../src') | undefined;
  await jest.isolateModulesAsync(async () => {
    jest.doMock('expo-modules-core', () => ({ requireNativeModule }), { virtual: true });
    mod = await import('../src');
  });
  const native = requireNativeModule.mock.results[0]?.value as FakeNativeModule;
  return { AudioEngineModule: mod!.AudioEngineModule, index: mod!, native, requireNativeModule };
}
