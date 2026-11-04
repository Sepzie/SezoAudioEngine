# Expo Events

The module emits events for playback, recording, and extraction.

## Event Names

- `playbackStateChange`
- `positionUpdate`
- `playbackComplete`
- `trackLoaded`
- `trackUnloaded`
- `recordingStarted`
- `recordingStopped`
- `extractionProgress`
- `extractionComplete`
- `engineStateChanged`
- `inputPitch`
- `debugLog`
- `error`

## Recording Lifecycle

- `recordingStopped` carries the take (`uri`, `duration`, `startTimeMs`, …)
  and `reason`: `requested` after `stopRecording()`, `interrupted` when the
  engine ended the take itself (iOS audio session interruption). Treat an
  `interrupted` take like a normal one: the file is finalised.
- `engineStateChanged` reasons related to recording:
  - `recordingStarted` (Android adds `foregroundService: boolean`, whether the
    recording foreground service was started)
  - `recordingForegroundServiceStarted` (Android; `foregroundServiceTypes`,
    e.g. `['microphone']` or `['microphone', 'mediaPlayback']`)
  - `recordingStopped`
  - `recordingInterrupted` (iOS; `uri` of the finalised take)

## Input Pitch Monitor

- `inputPitch` carries `hz`, `confidence`, `level`, `positionMs` and
  `timestampMs` for every analysed microphone window while the monitor runs
  (see [Input Pitch Monitor](input-monitor.md)).
- `engineStateChanged` with `reason: 'inputMonitorStopped'` when the module
  stopped the monitor itself: `cause: 'background'` (the app went to the
  background without a recording) or `cause: 'interrupted'` (iOS audio session
  interruption). Restart it with `startInputMonitor()`.

## Error Payload

`error` events include:

- `code` (string)
- `message` (string)
- `details` (unknown, optional)
- `severity` (`warning` | `fatal`)
- `recoverable` (boolean)
- `source` (`engine` | `session` | `playback` | `recording` | `extraction` | `focus` | `system`)
- `timestampMs` (number)
- `platform` (`ios` | `android`)

Async functions may also reject with the same `code`/`message` shape.

## Example

```ts
const subscription = AudioEngineModule.addListener('playbackStateChange', (event) => {
  console.log('state', event);
});

// Later
subscription.remove();
```
