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

## Example

```ts
const subscription = AudioEngineModule.addListener('playbackStateChange', (event) => {
  console.log('state', event);
});

// Later
subscription.remove();
```
