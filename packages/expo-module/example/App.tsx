import { useEffect, useState, useCallback } from 'react';
import {
  StyleSheet,
  Text,
  View,
  TouchableOpacity,
  ScrollView,
  Alert,
} from 'react-native';
import { AudioEngineModule } from 'sezo-audio-engine';
import Slider from '@react-native-community/slider';
import { Asset } from 'expo-asset';

interface Track {
  id: string;
  name: string;
  uri: string;
  volume: number;
  pan: number;
  muted: boolean;
  solo: boolean;
}

export default function App() {
  const [status, setStatus] = useState('Idle');
  const [isPlaying, setIsPlaying] = useState(false);
  const [position, setPosition] = useState(0);
  const [duration, setDuration] = useState(0);
  const [masterVolume, setMasterVolume] = useState(1.0);
  const [tracks, setTracks] = useState<Track[]>([]);

  // Initialize engine
  useEffect(() => {
    let mounted = true;
    AudioEngineModule.initialize({ sampleRate: 44100, maxTracks: 4 })
      .then(() => {
        if (mounted) {
          setStatus('Ready');
        }
      })
      .catch((error) => {
        if (mounted) {
          setStatus('Init failed');
          console.error('Init error:', error);
        }
      });

    return () => {
      mounted = false;
      AudioEngineModule.stop();
      AudioEngineModule.unloadAllTracks();
      AudioEngineModule.release().catch(() => {});
    };
  }, []);

  // Position update loop
  useEffect(() => {
    if (!isPlaying) return;

    const interval = setInterval(() => {
      try {
        const pos = AudioEngineModule.getCurrentPosition();
        const dur = AudioEngineModule.getDuration();
        setPosition(pos);
        setDuration(dur);

        // Auto-stop at end
        if (pos >= dur && dur > 0) {
          handleStop();
        }
      } catch (error) {
        console.error('Position update error:', error);
      }
    }, 100);

    return () => clearInterval(interval);
  }, [isPlaying]);

  const loadTracks = useCallback(async () => {
    try {
      setStatus('Loading tracks...');

      // Load audio assets
      const [asset1, asset2] = await Promise.all([
        Asset.loadAsync(require('./assets/track1.mp3')),
        Asset.loadAsync(require('./assets/track2.mp3')),
      ]);

      const track1Uri = asset1[0].localUri || asset1[0].uri;
      const track2Uri = asset2[0].localUri || asset2[0].uri;

      const newTracks: Track[] = [
        {
          id: 'track1',
          name: 'Track 1',
          uri: track1Uri,
          volume: 1.0,
          pan: 0.0,
          muted: false,
          solo: false,
        },
        {
          id: 'track2',
          name: 'Track 2',
          uri: track2Uri,
          volume: 1.0,
          pan: 0.0,
          muted: false,
          solo: false,
        },
      ];

      // Load tracks into engine
      await AudioEngineModule.loadTracks(
        newTracks.map((t) => ({
          id: t.id,
          uri: t.uri,
          volume: t.volume,
          pan: t.pan,
          muted: t.muted,
        }))
      );

      setTracks(newTracks);
      setDuration(AudioEngineModule.getDuration());
      setStatus('Tracks loaded');
    } catch (error: any) {
      setStatus('Load failed');
      Alert.alert('Error', 'Failed to load tracks: ' + error.message);
      console.error('Load error:', error);
    }
  }, []);

  const handlePlay = useCallback(() => {
    try {
      AudioEngineModule.play();
      setIsPlaying(true);
      setStatus('Playing');
    } catch (error) {
      console.error('Play error:', error);
    }
  }, []);

  const handlePause = useCallback(() => {
    try {
      AudioEngineModule.pause();
      setIsPlaying(false);
      setStatus('Paused');
    } catch (error) {
      console.error('Pause error:', error);
    }
  }, []);

  const handleStop = useCallback(() => {
    try {
      AudioEngineModule.stop();
      setIsPlaying(false);
      setPosition(0);
      setStatus('Stopped');
    } catch (error) {
      console.error('Stop error:', error);
    }
  }, []);

  const handleSeek = useCallback((value: number) => {
    try {
      AudioEngineModule.seek(value);
      setPosition(value);
    } catch (error) {
      console.error('Seek error:', error);
    }
  }, []);

  const handleMasterVolumeChange = useCallback((value: number) => {
    try {
      AudioEngineModule.setMasterVolume(value);
      setMasterVolume(value);
    } catch (error) {
      console.error('Master volume error:', error);
    }
  }, []);

  const handleTrackVolumeChange = useCallback((trackId: string, value: number) => {
    try {
      AudioEngineModule.setTrackVolume(trackId, value);
      setTracks((prev) =>
        prev.map((t) => (t.id === trackId ? { ...t, volume: value } : t))
      );
    } catch (error) {
      console.error('Track volume error:', error);
    }
  }, []);

  const handleTrackPanChange = useCallback((trackId: string, value: number) => {
    try {
      AudioEngineModule.setTrackPan(trackId, value);
      setTracks((prev) =>
        prev.map((t) => (t.id === trackId ? { ...t, pan: value } : t))
      );
    } catch (error) {
      console.error('Track pan error:', error);
    }
  }, []);

  const handleTrackMuteToggle = useCallback((trackId: string) => {
    setTracks((prev) => {
      const track = prev.find((t) => t.id === trackId);
      if (!track) return prev;

      const newMuted = !track.muted;
      try {
        AudioEngineModule.setTrackMuted(trackId, newMuted);
      } catch (error) {
        console.error('Track mute error:', error);
      }

      return prev.map((t) => (t.id === trackId ? { ...t, muted: newMuted } : t));
    });
  }, []);

  const handleTrackSoloToggle = useCallback((trackId: string) => {
    setTracks((prev) => {
      const track = prev.find((t) => t.id === trackId);
      if (!track) return prev;

      const newSolo = !track.solo;
      try {
        AudioEngineModule.setTrackSolo(trackId, newSolo);
      } catch (error) {
        console.error('Track solo error:', error);
      }

      return prev.map((t) => (t.id === trackId ? { ...t, solo: newSolo } : t));
    });
  }, []);

  const formatTime = (ms: number) => {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    return `${minutes}:${seconds.toString().padStart(2, '0')}`;
  };

  return (
    <ScrollView style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>Sezo Audio Engine</Text>
        <Text style={styles.status}>Status: {status}</Text>
      </View>

      {/* Load Tracks Button */}
      {tracks.length === 0 && (
        <TouchableOpacity style={styles.loadButton} onPress={loadTracks}>
          <Text style={styles.buttonText}>Load Tracks</Text>
        </TouchableOpacity>
      )}

      {/* Transport Controls */}
      {tracks.length > 0 && (
        <>
          <View style={styles.transportControls}>
            <TouchableOpacity
              style={[styles.controlButton, isPlaying && styles.controlButtonDisabled]}
              onPress={handlePlay}
              disabled={isPlaying}
            >
              <Text style={styles.controlButtonText}>▶ Play</Text>
            </TouchableOpacity>

            <TouchableOpacity
              style={[styles.controlButton, !isPlaying && styles.controlButtonDisabled]}
              onPress={handlePause}
              disabled={!isPlaying}
            >
              <Text style={styles.controlButtonText}>⏸ Pause</Text>
            </TouchableOpacity>

            <TouchableOpacity style={styles.controlButton} onPress={handleStop}>
              <Text style={styles.controlButtonText}>⏹ Stop</Text>
            </TouchableOpacity>
          </View>

          {/* Position Display */}
          <View style={styles.positionContainer}>
            <Text style={styles.timeText}>{formatTime(position)}</Text>
            <Text style={styles.timeText}>{formatTime(duration)}</Text>
          </View>

          {/* Seek Bar */}
          <Slider
            style={styles.slider}
            minimumValue={0}
            maximumValue={duration}
            value={position}
            onSlidingComplete={handleSeek}
            minimumTrackTintColor="#1DB954"
            maximumTrackTintColor="#404040"
            thumbTintColor="#1DB954"
          />

          {/* Master Volume */}
          <View style={styles.controlGroup}>
            <Text style={styles.label}>
              Master Volume: {(masterVolume * 100).toFixed(0)}%
            </Text>
            <Slider
              style={styles.slider}
              minimumValue={0}
              maximumValue={2.0}
              value={masterVolume}
              onValueChange={handleMasterVolumeChange}
              minimumTrackTintColor="#1DB954"
              maximumTrackTintColor="#404040"
              thumbTintColor="#1DB954"
            />
          </View>

          {/* Track Controls */}
          {tracks.map((track) => (
            <View key={track.id} style={styles.trackContainer}>
              <View style={styles.trackHeader}>
                <Text style={styles.trackName}>{track.name}</Text>
                <View style={styles.trackButtons}>
                  <TouchableOpacity
                    style={[
                      styles.trackButton,
                      track.muted && styles.trackButtonActive,
                    ]}
                    onPress={() => handleTrackMuteToggle(track.id)}
                  >
                    <Text style={styles.trackButtonText}>M</Text>
                  </TouchableOpacity>
                  <TouchableOpacity
                    style={[
                      styles.trackButton,
                      track.solo && styles.trackButtonActive,
                    ]}
                    onPress={() => handleTrackSoloToggle(track.id)}
                  >
                    <Text style={styles.trackButtonText}>S</Text>
                  </TouchableOpacity>
                </View>
              </View>

              <View style={styles.controlGroup}>
                <Text style={styles.trackLabel}>
                  Volume: {(track.volume * 100).toFixed(0)}%
                </Text>
                <Slider
                  style={styles.slider}
                  minimumValue={0}
                  maximumValue={2.0}
                  value={track.volume}
                  onValueChange={(value: number) =>
                    handleTrackVolumeChange(track.id, value)
                  }
                  minimumTrackTintColor="#1DB954"
                  maximumTrackTintColor="#404040"
                  thumbTintColor="#1DB954"
                />
              </View>

              <View style={styles.controlGroup}>
                <Text style={styles.trackLabel}>
                  Pan: {track.pan > 0 ? 'R' : track.pan < 0 ? 'L' : 'C'}{' '}
                  {Math.abs(track.pan * 100).toFixed(0)}
                </Text>
                <Slider
                  style={styles.slider}
                  minimumValue={-1.0}
                  maximumValue={1.0}
                  value={track.pan}
                  onValueChange={(value: number) => handleTrackPanChange(track.id, value)}
                  minimumTrackTintColor="#1DB954"
                  maximumTrackTintColor="#404040"
                  thumbTintColor="#1DB954"
                />
              </View>
            </View>
          ))}
        </>
      )}
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#0b0b0b',
    padding: 20,
  },
  header: {
    alignItems: 'center',
    marginTop: 40,
    marginBottom: 20,
  },
  title: {
    fontSize: 28,
    fontWeight: 'bold',
    color: '#f5f5f5',
    marginBottom: 8,
  },
  status: {
    fontSize: 16,
    color: '#9aa0a6',
  },
  loadButton: {
    backgroundColor: '#1DB954',
    padding: 16,
    borderRadius: 8,
    alignItems: 'center',
    marginVertical: 20,
  },
  buttonText: {
    color: '#fff',
    fontSize: 18,
    fontWeight: 'bold',
  },
  transportControls: {
    flexDirection: 'row',
    justifyContent: 'space-around',
    marginVertical: 20,
  },
  controlButton: {
    backgroundColor: '#1DB954',
    padding: 12,
    borderRadius: 8,
    minWidth: 100,
    alignItems: 'center',
  },
  controlButtonDisabled: {
    backgroundColor: '#404040',
  },
  controlButtonText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '600',
  },
  positionContainer: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginBottom: 8,
  },
  timeText: {
    color: '#9aa0a6',
    fontSize: 14,
  },
  controlGroup: {
    marginVertical: 12,
  },
  label: {
    color: '#f5f5f5',
    fontSize: 16,
    marginBottom: 8,
  },
  slider: {
    width: '100%',
    height: 40,
  },
  trackContainer: {
    backgroundColor: '#1a1a1a',
    borderRadius: 8,
    padding: 16,
    marginVertical: 8,
  },
  trackHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 12,
  },
  trackName: {
    color: '#f5f5f5',
    fontSize: 18,
    fontWeight: 'bold',
  },
  trackButtons: {
    flexDirection: 'row',
    gap: 8,
  },
  trackButton: {
    backgroundColor: '#2a2a2a',
    width: 36,
    height: 36,
    borderRadius: 18,
    alignItems: 'center',
    justifyContent: 'center',
  },
  trackButtonActive: {
    backgroundColor: '#1DB954',
  },
  trackButtonText: {
    color: '#fff',
    fontSize: 14,
    fontWeight: 'bold',
  },
  trackLabel: {
    color: '#9aa0a6',
    fontSize: 14,
    marginBottom: 4,
  },
});
