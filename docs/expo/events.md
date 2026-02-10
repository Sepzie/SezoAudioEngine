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
