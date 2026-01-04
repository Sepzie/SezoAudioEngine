# Expo Module Usage

```ts
import { AudioEngineModule } from 'sezo-audio-engine';

await AudioEngineModule.initialize({ sampleRate: 44100, maxTracks: 8 });

await AudioEngineModule.loadTracks([
  { id: 'backing', uri: 'file:///sdcard/Music/backing.wav' },
  { id: 'vocals', uri: 'file:///sdcard/Music/vocals.wav', startTimeMs: 250 }
]);

AudioEngineModule.setTrackVolume('vocals', 0.8);
AudioEngineModule.play();
```

## Recording

```ts
await AudioEngineModule.startRecording({ format: 'aac', quality: 'medium' });
const result = await AudioEngineModule.stopRecording();
```

## Extraction

```ts
const result = await AudioEngineModule.extractTrack('backing', { format: 'wav' });
```
