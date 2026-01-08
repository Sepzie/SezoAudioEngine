# Expo Example App Test Plan

Purpose: Define a repeatable in-app test harness for validating the Expo module
across iOS and Android without external tooling.

## Scope

- Validate core playback, sync, seek accuracy, and recording/extraction outputs.
- Measure timing drift and basic quality signals (RMS, peak, duration deltas).
- Produce a JSON report and a human-readable summary in the example UI.

## Test Harness UX

- Add a "Test Lab" section in the example app.
- Provide a "Run All Tests" button and per-test status list.
- Show timing metrics and pass/fail thresholds.
- Export results as JSON via Share (iOS) or file intent (Android).

## Test Fixtures

- Use bundled assets (track1.mp3, track2.mp3) plus at least one WAV fixture.
- Optionally add a known tone file (1kHz sine, 5s) for RMS checks.

## Test Modules

### 1) Load & Playback Smoke

- Load 2 tracks, play for 2s, pause, stop.
- Expect no crash, `isPlaying()` toggles correctly, duration > 0.

### 2) Multi-Track Sync Drift

- Start both tracks with identical start times.
- Sample `getCurrentPosition()` every 100ms for 5s.
- Compute drift = max |pos - expected| across samples.
- Pass threshold: <= 50ms (tunable per device).

### 3) Seek Accuracy

- Seek to 5s, play for 1s, read current position.
- Pass threshold: |reported - 6000ms| <= 150ms.

### 4) Pitch/Speed Sanity

- Set pitch +3 semitones and speed 1.25x.
- Play 2s and confirm no crash, position advances at > 1x.
- Optional: RMS check to confirm audio output is non-zero.

### 5) Recording Round Trip

- Start recording for 3s during playback.
- Stop recording and verify file exists, size > 0, duration > 0.
- Auto-load recording and verify track load succeeds.

### 6) Extraction Output

- Extract track 1 to AAC and WAV.
- Verify file exists, duration is within 200ms of original.
- Verify file size > 0.

## Output Schema (JSON)

{
  "timestamp": "ISO8601",
  "platform": "ios|android",
  "device": "string",
  "tests": [
    {
      "name": "Multi-Track Sync Drift",
      "status": "pass|fail",
      "metrics": {
        "maxDriftMs": 42
      }
    }
  ]
}

## Future Enhancements

- Add cross-correlation quality scoring (reference vs rendered).
- Persist historical runs to compare regressions.
- Attach raw sample traces for offline analysis.
