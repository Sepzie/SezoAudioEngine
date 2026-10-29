/**
 * Compile-time checks for the public types. ts-jest type-checks this file, so
 * an unused `@ts-expect-error` (i.e. a type that became too loose) fails the
 * suite just like a failing assertion.
 */
import type {
  AudioEngine,
  AudioEngineError,
  AudioEngineEvent,
  AudioEngineEventMap,
  PlaybackState,
  RecordingConfig,
} from '../src';

type Equals<A, B> = (<T>() => T extends A ? 1 : 2) extends <T>() => T extends B ? 1 : 2 ? true : false;
const assertType = <T extends true>(_value?: T) => undefined;

declare const engine: AudioEngine;

describe('public types', () => {
  it('keeps the event-name union in sync with the event payload map', () => {
    assertType<Equals<AudioEngineEvent, keyof AudioEngineEventMap>>();
    expect(true).toBe(true);
  });

  it('types listener payloads per event', () => {
    const check = () => {
      engine.addListener('positionUpdate', (p) => {
        assertType<Equals<typeof p.positionMs, number>>();
      });
      engine.addListener('error', (e) => {
        assertType<Equals<typeof e, AudioEngineError>>();
        assertType<Equals<typeof e.severity, 'warning' | 'fatal'>>();
      });
      engine.addListener('recordingStopped', (p) => {
        assertType<Equals<typeof p.reason, 'requested' | 'interrupted' | undefined>>();
      });
      engine.addListener('trackLoaded', (p) => {
        // @ts-expect-error trackLoaded payloads have no positionMs
        void p.positionMs;
      });
      // @ts-expect-error unknown event names are rejected
      engine.addListener('notAnEvent', () => undefined);
    };
    expect(typeof check).toBe('function');
  });

  it('constrains string-literal options', () => {
    const ok: RecordingConfig = { format: 'wav', quality: 'low' };
    // @ts-expect-error unsupported recording format
    const bad: RecordingConfig = { format: 'flac' };
    const state: PlaybackState = 'recording';
    // @ts-expect-error not a playback state
    const badState: PlaybackState = 'buffering';
    expect([ok, bad, state, badState]).toHaveLength(4);
  });
});
