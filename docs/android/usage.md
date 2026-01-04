# Android Engine Usage

Below is a minimal Kotlin example using the `AudioEngine` wrapper.

```kotlin
import com.sezo.audioengine.AudioEngine

val engine = AudioEngine()
val ok = engine.initialize(sampleRate = 44100, maxTracks = 8)
if (!ok) {
  throw IllegalStateException("Failed to initialize audio engine")
}

engine.loadTrack("backing", "/sdcard/Music/backing.wav", startTimeMs = 0.0)
engine.loadTrack("vocals", "/sdcard/Music/vocals.wav", startTimeMs = 250.0)

engine.setTrackVolume("vocals", 0.8f)
engine.setPitch(0.0f)
engine.setSpeed(1.0f)

engine.play()

// ...
engine.pause()
engine.stop()
engine.release()
engine.destroy()
```

## File Paths

The native engine expects an absolute file path. If you have a `content://` URI, copy it to a temp file and pass the resulting path.
