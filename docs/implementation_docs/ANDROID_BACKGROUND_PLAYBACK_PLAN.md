# Android Background Playback Plan (v1 + Future)

Purpose: Define a minimal, production-ready background playback scope that
matches the current iOS implementation and can be expanded later.

## v1 Scope (Immediate)

### Foreground Service + MediaSession

- Add a foreground service in the Expo Android module.
- Create a `MediaSession` and expose play/pause actions.
- Show a persistent notification with play/pause.
- Wire notification actions to engine play/pause.

### Audio Focus & Noisy Handling

- Request audio focus on play.
- Pause or duck on focus loss.
- Listen for `ACTION_AUDIO_BECOMING_NOISY` (headphones unplugged) and pause.

### Metadata

- Expose `enableBackgroundPlayback(metadata)` and `updateNowPlayingInfo(metadata)` in JS.
- Map metadata to `MediaMetadataCompat` fields (title, artist, album, artwork).

### Lifecycle

- Start service when playback begins (and background enabled).
- Stop service on stop/pause and when playback completes.

## Recommended JS API Surface

- `enableBackgroundPlayback(metadata)`
- `updateNowPlayingInfo(metadata)`
- `disableBackgroundPlayback()`
- `addListener('playbackStateChange', ...)`

## Implementation Notes

- Use `NotificationCompat.MediaStyle` with the MediaSession.
- Use a single service to avoid duplicate notifications.
- Keep a small, static icon for the notification.

## v2 Enhancements

- Next/previous/seek actions.
- Playback position syncing in notification.
- Full persistence across process death (restore session).
- Android Auto / wearables integration.

## Test Checklist

- Background: app swiped away, audio continues.
- Lock screen: play/pause works.
- Bluetooth: play/pause works, route changes handled.
- Focus: interruptions (phone calls, notifications) pause or duck correctly.
