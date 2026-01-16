# iOS Recording Sync Fix Plan

## Problem Statement
iOS recording exhibits ~0.5s drift from original playback when playing them together in the same mix. Android achieves sample-accurate sync through a shared MasterClock, but iOS uses approximate host-time calculations that accumulate drift.

## Root Cause
- Current implementation ([NativeAudioEngine.swift:377](../packages/expo-module/ios/AudioEngine/NativeAudioEngine.swift#L377)) uses `currentPlaybackPositionMs()` which calculates position via `mach_absolute_time()` elapsed time
- This is NOT sample-accurate - it's a time-domain approximation
- Recording tap and playback nodes operate independently without sample-level correlation
- No compensation for recording tap latency

## Solution Overview
Use AVAudioEngine's built-in sample time tracking to capture the exact sample position when recording starts, matching Android's MasterClock approach.

## Implementation Plan

### Step 1: Capture Sample-Accurate Start Time
**File:** `NativeAudioEngine.swift`

**Location:** `startRecording(config:)` method, around line 377

**Current code:**
```swift
let startTimeMs = isPlayingFlag ? currentPlaybackPositionMs() : 0.0
```

**Replace with:**
```swift
let startTimeMs: Double
let startTimeSamples: Int64

if isPlayingFlag {
  // Get the current node time from any playing track's player node
  if let firstTrack = tracks.values.first,
     let nodeTime = firstTrack.playerNode.lastRenderTime,
     let playerTime = firstTrack.playerNode.playerTime(forNodeTime: nodeTime) {
    // Use sample-accurate time from the player node
    startTimeSamples = playerTime.sampleTime
    startTimeMs = Double(startTimeSamples) / playerTime.sampleRate * 1000.0
  } else {
    // Fallback to current position if no active playback
    startTimeSamples = Int64(currentPositionMs / 1000.0 * sampleRate)
    startTimeMs = currentPositionMs
  }
} else {
  startTimeSamples = 0
  startTimeMs = 0.0
}
```

### Step 2: Store Sample Position in RecordingState
**Location:** `RecordingState` struct definition (line 47-54)

**Update struct:**
```swift
private struct RecordingState {
  let url: URL
  let file: AVAudioFile
  let format: String
  let sampleRate: Double
  let channels: Int
  let startTimeMs: Double
  let startTimeSamples: Int64  // ADD THIS
}
```

### Step 3: Update RecordingState Initialization
**Location:** Line 378-385

**Update initialization:**
```swift
recordingState = RecordingState(
  url: outputURL,
  file: file,
  format: formatInfo.format,
  sampleRate: sampleRate,
  channels: channels,
  startTimeMs: startTimeMs,
  startTimeSamples: startTimeSamples  // ADD THIS
)
```

### Step 4: Return Sample Position in Stop Recording
**Location:** `stopRecording()` method (line 410-440)

**Update to include sample position:**
```swift
return [
  "uri": state.url.absoluteString,
  "duration": durationMs,
  "startTimeMs": state.startTimeMs,
  "startTimeSamples": state.startTimeSamples,  // Ensure this is returned
  "sampleRate": state.sampleRate,
  "channels": state.channels,
  "format": state.format,
  "fileSize": fileSize
]
```

### Step 5: Add Fallback Safety Check
If no tracks are playing when recording starts, we need a safe fallback. Consider:

**Option A:** Require at least one track loaded before recording
```swift
guard !tracks.isEmpty else {
  lastRecordingError = "no tracks loaded - cannot determine sample position"
  return false
}
```

**Option B:** Allow recording from stopped state with position 0
```swift
// Already handled in Step 1 with the fallback logic
```

Recommend **Option B** for flexibility.

## Testing Strategy

### Unit Test: Sample Accuracy
1. Load a reference track at position 0ms
2. Start playback
3. Seek to 5000ms
4. Start recording
5. Verify `startTimeSamples` matches expected: `5000ms * (sampleRate/1000)`

### Integration Test: Playback Alignment
1. Load backing track
2. Play and record at various positions (0ms, 2500ms, 5000ms)
3. Stop recording
4. Load recording as a new track with `startTimeMs` from result
5. Play both tracks together
6. Measure phase alignment - should be <10ms drift

### Device Test Matrix
- iPhone SE (older A-series chip)
- iPhone 13/14 (newer chip)
- iPad Air (different audio hardware)
- Test with different sample rates (44.1kHz, 48kHz)

## Expected Outcome
- Drift reduced from ~500ms to <10ms
- Sample-accurate sync matching Android implementation
- Meets PRD requirement: "<30ms acceptable" for input-to-output latency

## Rollback Plan
If issues arise, the change is isolated to `startRecording()`. Simply revert to:
```swift
let startTimeMs = isPlayingFlag ? currentPlaybackPositionMs() : 0.0
```

## Notes
- `playerTime(forNodeTime:)` returns `nil` if the node isn't playing, handled by fallback
- `lastRenderTime` gives the most recent audio render timestamp
- This approach mirrors the Android `clock_->GetPosition()` strategy
- Consider logging both old and new start times during beta to validate improvement
