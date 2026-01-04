# Concepts

## Timing and Units

- **Sample rate**: number of audio samples per second (e.g., 44100 Hz).
- **Frame**: one sample per channel. Stereo audio has 2 samples per frame.
- **Position**: the engine reports playback position in milliseconds.

## Tracks

Each track is identified by a string `id` and has a source file path/URI. Tracks can have a start offset (`startTimeMs`) to align material on the timeline.

## Synchronization

All tracks share a master clock. Timing conversions are handled internally so playback stays sample-accurate across tracks.

## File Formats

Input formats:
- MP3
- WAV

Recording/export formats:
- AAC (Android MediaCodec)
- MP3 (optional LAME)
- WAV (lossless)

## Threading

Audio processing runs on a real-time audio thread. Keep heavy work off the callback and update parameters from a separate thread/UI layer.
