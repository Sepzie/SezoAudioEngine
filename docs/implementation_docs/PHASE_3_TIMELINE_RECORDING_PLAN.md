# Patch Plan: Timeline-Aligned Recordings & Track Offsets

## Goal
Enable DAW-style placement where a recorded file can start and end anywhere on the timeline and still be mixed/exported correctly.

## Non-Goals (for this patch)
- Region editing, trimming, fades, or crossfades
- Clip-level automation
- UI/UX beyond basic placement

## Current Behavior (Summary)
- Tracks always play from timeline t=0.
- Recording results do not include the start timestamp relative to the master clock.
- Offline extraction uses the same zero-offset mix assumptions.

## Proposed API Changes
### TypeScript
- `AudioTrack` gains optional `startTimeMs?: number` (default 0).
- `RecordingResult` gains `startTimeMs: number` (and optionally `startTimeSamples: number`).

### Kotlin/JSI (Expo module)
- `loadTracks(...)` accepts `startTimeMs` per track.
- `stopRecording()` returns `startTimeMs` (and `startTimeSamples` if exposed).

## Engine Changes (C++)
### 1) Track placement metadata
- Add `start_time_samples_` to `playback::Track` (default 0).
- Add `SetStartTimeSamples(int64_t)` and `GetStartTimeSamples()`.

### 2) Mixer timeline awareness
- Update `MultiTrackMixer::Mix(...)` to accept the current timeline position:
  - New signature: `Mix(float* output, size_t frames, int64_t timeline_start_sample)`.
  - For each track, compute `track_frame = timeline_start_sample - track_start_time_samples`.
  - If `track_frame < 0`, return silence for that track until its start time is reached.

### 3) Master clock integration
- In `OboePlayer::onAudioReady`, capture the current clock position before mixing:
  - `const int64_t timeline_start = clock_->GetPosition();`
  - `mixer_->Mix(output, num_frames, timeline_start);`

### 4) Recording start timestamps
- In `AudioEngine::StartRecording(...)`, capture `clock_->GetPosition()` as `record_start_samples`.
- Store `record_start_samples` with the recording pipeline or in `AudioEngine` state.
- Include `record_start_samples` in `RecordingResult` so the app can place the file at the correct timeline position.

### 5) Offline extraction
- Update offline mix path to respect `start_time_samples_` (same math as real-time mixer).
- Ensure export duration accounts for the latest end time (max of `track_start + track_duration`).

## Backward Compatibility
- If `startTimeMs` is omitted, default to 0 (current behavior).
- If recording happens while transport is stopped, return `startTimeMs = 0`.

## Testing Plan
- Unit: Mixer offset math (track starts late, track starts early, silence before start).
- Integration: Record while playing, load recorded file with `startTimeMs`, verify alignment.
- Export: Ensure track offsets are reflected in offline mix output.

## Rollout Notes
- Keep changes behind defaults to avoid breaking existing apps.
- Add a simple example in the demo app after engine support lands.
