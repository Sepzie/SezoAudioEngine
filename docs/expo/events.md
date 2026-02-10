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
- `error`

## Error Payload

`error` events include:

- `code` (string)
- `message` (string)
- `details` (object, optional)

Async functions may also reject with the same `code`/`message` shape.

## Example

```ts
const subscription = AudioEngineModule.addListener('playbackStateChange', (event) => {
  console.log('state', event);
});

// Later
subscription.remove();
```
