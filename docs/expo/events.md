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
