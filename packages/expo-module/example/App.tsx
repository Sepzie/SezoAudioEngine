import { useEffect, useState, useCallback } from 'react';
import {
  Animated,
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
  pitch: number;
  speed: number;
}

const theme = {
  colors: {
    background: '#0b0f10',
    surface: '#141b1f',
    surfaceAlt: '#1b252a',
    surfaceStrong: '#1f2b31',
    text: '#f5f7f9',
    textMuted: '#8a949c',
    accent: '#ffb454',
    accentStrong: '#ff8a3d',
    success: '#23d18b',
    danger: '#ff5d5d',
    border: '#253038',
    track: '#2a343a',
  },
  radius: {
    card: 18,
    pill: 999,
    button: 12,
  },
};

export default function App() {
  const [status, setStatus] = useState('Idle');
  const [isPlaying, setIsPlaying] = useState(false);
  const [position, setPosition] = useState(0);
  const [duration, setDuration] = useState(0);
  const [masterVolume, setMasterVolume] = useState(1.0);
  const [masterPitch, setMasterPitch] = useState(0.0);
  const [masterSpeed, setMasterSpeed] = useState(1.0);
  const [tracks, setTracks] = useState<Track[]>([]);
  const engineAny = AudioEngineModule as Record<string, any>;
  const supportsTrackPitch = typeof engineAny.setTrackPitch === 'function';
  const supportsTrackSpeed = typeof engineAny.setTrackSpeed === 'function';
  const introAnim = useState(() => new Animated.Value(0))[0];
  const controlsAnim = useState(() => new Animated.Value(0))[0];
  const tracksAnim = useState(() => new Animated.Value(0))[0];

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

  useEffect(() => {
    Animated.stagger(120, [
      Animated.timing(introAnim, {
        toValue: 1,
        duration: 420,
        useNativeDriver: true,
      }),
      Animated.timing(controlsAnim, {
        toValue: 1,
        duration: 420,
        useNativeDriver: true,
      }),
      Animated.timing(tracksAnim, {
        toValue: 1,
        duration: 420,
        useNativeDriver: true,
      }),
    ]).start();
  }, [introAnim, controlsAnim, tracksAnim]);

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
          pitch: 0.0,
          speed: 1.0,
        },
        {
          id: 'track2',
          name: 'Track 2',
          uri: track2Uri,
          volume: 1.0,
          pan: 0.0,
          muted: false,
          solo: false,
          pitch: 0.0,
          speed: 1.0,
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

  const handleReset = useCallback(() => {
    try {
      AudioEngineModule.stop();
      AudioEngineModule.seek(0);
      AudioEngineModule.setMasterVolume(1.0);
      AudioEngineModule.setPitch(0.0);
      AudioEngineModule.setSpeed(1.0);

      tracks.forEach((track) => {
        AudioEngineModule.setTrackVolume(track.id, 1.0);
        AudioEngineModule.setTrackPan(track.id, 0.0);
        AudioEngineModule.setTrackMuted(track.id, false);
        AudioEngineModule.setTrackSolo(track.id, false);
        if (supportsTrackPitch) {
          engineAny.setTrackPitch(track.id, 0.0);
        }
        if (supportsTrackSpeed) {
          engineAny.setTrackSpeed(track.id, 1.0);
        }
      });

      setMasterVolume(1.0);
      setMasterPitch(0.0);
      setMasterSpeed(1.0);
      setTracks((prev) =>
        prev.map((track) => ({
          ...track,
          volume: 1.0,
          pan: 0.0,
          muted: false,
          solo: false,
          pitch: supportsTrackPitch ? 0.0 : track.pitch,
          speed: supportsTrackSpeed ? 1.0 : track.speed,
        }))
      );
      setIsPlaying(false);
      setPosition(0);
      setStatus('Reset');
    } catch (error) {
      console.error('Reset error:', error);
    }
  }, [engineAny, supportsTrackPitch, supportsTrackSpeed, tracks]);

  const handleMasterReset = useCallback(() => {
    try {
      AudioEngineModule.setMasterVolume(1.0);
      AudioEngineModule.setPitch(0.0);
      AudioEngineModule.setSpeed(1.0);
      setMasterVolume(1.0);
      setMasterPitch(0.0);
      setMasterSpeed(1.0);
      setTracks((prev) =>
        prev.map((track) => ({
          ...track,
          pitch: supportsTrackPitch ? 0.0 : track.pitch,
          speed: supportsTrackSpeed ? 1.0 : track.speed,
        }))
      );
    } catch (error) {
      console.error('Master reset error:', error);
    }
  }, [supportsTrackPitch, supportsTrackSpeed]);

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

  const handleMasterPitchChange = useCallback((value: number) => {
    try {
      AudioEngineModule.setPitch(value);
      setMasterPitch(value);
    } catch (error) {
      console.error('Master pitch error:', error);
    }
  }, []);

  const handleMasterSpeedChange = useCallback((value: number) => {
    try {
      AudioEngineModule.setSpeed(value);
      AudioEngineModule.setPitch(masterPitch);
      setMasterSpeed(value);
    } catch (error) {
      console.error('Master speed error:', error);
    }
  }, [masterPitch]);

  const handleTrackPitchChange = useCallback((trackId: string, value: number) => {
    try {
      if (!supportsTrackPitch) {
        return;
      }
      engineAny.setTrackPitch(trackId, value);
      setTracks((prev) =>
        prev.map((t) => (t.id === trackId ? { ...t, pitch: value } : t))
      );
    } catch (error) {
      console.error('Track pitch error:', error);
    }
  }, [engineAny, supportsTrackPitch]);

  const handleTrackSpeedChange = useCallback((trackId: string, value: number) => {
    try {
      if (!supportsTrackSpeed) {
        return;
      }
      engineAny.setTrackSpeed(trackId, value);
      if (supportsTrackPitch) {
        const currentPitch =
          tracks.find((track) => track.id === trackId)?.pitch ?? 0.0;
        engineAny.setTrackPitch(trackId, currentPitch);
      }
      setTracks((prev) =>
        prev.map((t) => (t.id === trackId ? { ...t, speed: value } : t))
      );
    } catch (error) {
      console.error('Track speed error:', error);
    }
  }, [engineAny, supportsTrackPitch, supportsTrackSpeed, tracks]);

  const formatTime = (ms: number) => {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    return `${minutes}:${seconds.toString().padStart(2, '0')}`;
  };

  const heroStyle = {
    opacity: introAnim,
    transform: [
      {
        translateY: introAnim.interpolate({
          inputRange: [0, 1],
          outputRange: [16, 0],
        }),
      },
    ],
  };
  const controlsStyle = {
    opacity: controlsAnim,
    transform: [
      {
        translateY: controlsAnim.interpolate({
          inputRange: [0, 1],
          outputRange: [18, 0],
        }),
      },
    ],
  };
  const tracksStyle = {
    opacity: tracksAnim,
    transform: [
      {
        translateY: tracksAnim.interpolate({
          inputRange: [0, 1],
          outputRange: [20, 0],
        }),
      },
    ],
  };

  return (
    <View style={styles.root}>
      <View style={styles.background}>
        <View style={styles.glowTop} />
        <View style={styles.glowBottom} />
      </View>
      <ScrollView contentContainerStyle={styles.content} showsVerticalScrollIndicator={false}>
        <Animated.View style={[styles.heroCard, heroStyle]}>
          <View style={styles.heroTopRow}>
            <View>
              <Text style={styles.eyebrow}>Sezo Audio Engine</Text>
              <Text style={styles.title}>Phase 2 Playground</Text>
            </View>
            <View
              style={[
                styles.statusPill,
                status === 'Ready' && styles.statusPillReady,
                status === 'Playing' && styles.statusPillActive,
                status === 'Init failed' && styles.statusPillDanger,
              ]}
            >
              <Text style={styles.statusText}>{status}</Text>
            </View>
          </View>
          <Text style={styles.subtitle}>
            Real-time pitch, time stretch, and multi-track mixing in one place.
          </Text>

          {tracks.length === 0 && (
            <TouchableOpacity style={styles.primaryButton} onPress={loadTracks}>
              <Text style={styles.primaryButtonText}>Load Tracks</Text>
            </TouchableOpacity>
          )}
        </Animated.View>

        {tracks.length > 0 && (
          <>
            <Animated.View style={[styles.card, controlsStyle]}>
              <View style={styles.sectionHeader}>
                <Text style={styles.sectionTitle}>Transport</Text>
                <View style={styles.timePill}>
                  <Text style={styles.timeText}>
                    {formatTime(position)} / {formatTime(duration)}
                  </Text>
                </View>
              </View>

              <View style={styles.transportRow}>
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

                <TouchableOpacity style={styles.resetButton} onPress={handleReset}>
                  <Text style={styles.resetButtonText}>Reset</Text>
                </TouchableOpacity>
              </View>

              <Slider
                style={styles.slider}
                minimumValue={0}
                maximumValue={duration}
                value={position}
                onSlidingComplete={handleSeek}
                minimumTrackTintColor={theme.colors.accent}
                maximumTrackTintColor={theme.colors.track}
                thumbTintColor={theme.colors.accentStrong}
              />
            </Animated.View>

            <Animated.View style={[styles.card, controlsStyle]}>
              <View style={styles.sectionHeader}>
                <Text style={styles.sectionTitle}>Master</Text>
                <View style={styles.sectionHeaderActions}>
                  <Text style={styles.sectionHint}>Global mix controls</Text>
                  <TouchableOpacity
                    style={styles.iconResetButton}
                    onPress={handleMasterReset}
                  >
                    <Text style={styles.iconResetText}>↺</Text>
                  </TouchableOpacity>
                </View>
              </View>

              <View style={styles.controlGroup}>
                <View style={styles.labelRow}>
                  <Text style={styles.label}>Master Volume</Text>
                  <View style={styles.valueRow}>
                    <Text style={styles.valuePill}>
                      {(masterVolume * 100).toFixed(0)}%
                    </Text>
                    <TouchableOpacity
                      style={styles.iconResetButton}
                      onPress={() => handleMasterVolumeChange(1.0)}
                    >
                      <Text style={styles.iconResetText}>↺</Text>
                    </TouchableOpacity>
                  </View>
                </View>
                <Slider
                  style={styles.slider}
                  minimumValue={0}
                  maximumValue={2.0}
                  value={masterVolume}
                  onValueChange={handleMasterVolumeChange}
                  minimumTrackTintColor={theme.colors.accent}
                  maximumTrackTintColor={theme.colors.track}
                  thumbTintColor={theme.colors.accentStrong}
                />
              </View>

              <View style={styles.controlGroup}>
                <View style={styles.labelRow}>
                  <Text style={styles.label}>Master Pitch</Text>
                  <View style={styles.valueRow}>
                    <Text style={styles.valuePill}>
                      {masterPitch > 0 ? '+' : ''}
                      {masterPitch.toFixed(1)} st
                    </Text>
                    <TouchableOpacity
                      style={styles.iconResetButton}
                      onPress={() => handleMasterPitchChange(0.0)}
                    >
                      <Text style={styles.iconResetText}>↺</Text>
                    </TouchableOpacity>
                  </View>
                </View>
                <Slider
                  style={styles.slider}
                  minimumValue={-12.0}
                  maximumValue={12.0}
                  value={masterPitch}
                  onValueChange={handleMasterPitchChange}
                  minimumTrackTintColor={theme.colors.accent}
                  maximumTrackTintColor={theme.colors.track}
                  thumbTintColor={theme.colors.accentStrong}
                />
              </View>

              <View style={styles.controlGroup}>
                <View style={styles.labelRow}>
                  <Text style={styles.label}>Master Speed</Text>
                  <View style={styles.valueRow}>
                    <Text style={styles.valuePill}>
                      {(masterSpeed * 100).toFixed(0)}%
                    </Text>
                    <TouchableOpacity
                      style={styles.iconResetButton}
                      onPress={() => handleMasterSpeedChange(1.0)}
                    >
                      <Text style={styles.iconResetText}>↺</Text>
                    </TouchableOpacity>
                  </View>
                </View>
                <Slider
                  style={styles.slider}
                  minimumValue={0.5}
                  maximumValue={2.0}
                  value={masterSpeed}
                  onValueChange={handleMasterSpeedChange}
                  minimumTrackTintColor={theme.colors.accent}
                  maximumTrackTintColor={theme.colors.track}
                  thumbTintColor={theme.colors.accentStrong}
                />
              </View>
            </Animated.View>

            <Animated.View style={[styles.sectionTitleRow, tracksStyle]}>
              <Text style={styles.sectionTitle}>Tracks</Text>
              <Text style={styles.sectionHint}>{tracks.length} loaded</Text>
            </Animated.View>

            {tracks.map((track) => (
              <Animated.View key={track.id} style={[styles.trackCard, tracksStyle]}>
                <View style={styles.trackHeader}>
                  <View>
                    <Text style={styles.trackName}>{track.name}</Text>
                    <Text style={styles.trackMeta}>
                      Pitch {track.pitch > 0 ? '+' : ''}
                      {track.pitch.toFixed(1)} st | Speed {(track.speed * 100).toFixed(0)}%
                    </Text>
                  </View>
                  <View style={styles.trackButtons}>
                    <TouchableOpacity
                      style={[styles.trackButton, track.muted && styles.trackButtonMuted]}
                      onPress={() => handleTrackMuteToggle(track.id)}
                    >
                      <Text style={styles.trackButtonText}>M</Text>
                    </TouchableOpacity>
                    <TouchableOpacity
                      style={[styles.trackButton, track.solo && styles.trackButtonSolo]}
                      onPress={() => handleTrackSoloToggle(track.id)}
                    >
                      <Text style={styles.trackButtonText}>S</Text>
                    </TouchableOpacity>
                  </View>
                </View>

                <View style={styles.controlGroup}>
                  <View style={styles.labelRow}>
                    <Text style={styles.label}>Volume</Text>
                    <View style={styles.valueRow}>
                      <Text style={styles.valuePill}>
                        {(track.volume * 100).toFixed(0)}%
                      </Text>
                      <TouchableOpacity
                        style={styles.iconResetButton}
                        onPress={() => handleTrackVolumeChange(track.id, 1.0)}
                      >
                        <Text style={styles.iconResetText}>↺</Text>
                      </TouchableOpacity>
                    </View>
                  </View>
                  <Slider
                    style={styles.slider}
                    minimumValue={0}
                    maximumValue={2.0}
                    value={track.volume}
                    onValueChange={(value: number) =>
                      handleTrackVolumeChange(track.id, value)
                    }
                    minimumTrackTintColor={theme.colors.accent}
                    maximumTrackTintColor={theme.colors.track}
                    thumbTintColor={theme.colors.accentStrong}
                  />
                </View>

                <View style={styles.controlGroup}>
                  <View style={styles.labelRow}>
                    <Text style={styles.label}>Pan</Text>
                    <View style={styles.valueRow}>
                      <Text style={styles.valuePill}>
                        {track.pan > 0 ? 'R' : track.pan < 0 ? 'L' : 'C'}{' '}
                        {Math.abs(track.pan * 100).toFixed(0)}
                      </Text>
                      <TouchableOpacity
                        style={styles.iconResetButton}
                        onPress={() => handleTrackPanChange(track.id, 0.0)}
                      >
                        <Text style={styles.iconResetText}>↺</Text>
                      </TouchableOpacity>
                    </View>
                  </View>
                  <Slider
                    style={styles.slider}
                    minimumValue={-1.0}
                    maximumValue={1.0}
                    value={track.pan}
                    onValueChange={(value: number) => handleTrackPanChange(track.id, value)}
                    minimumTrackTintColor={theme.colors.accent}
                    maximumTrackTintColor={theme.colors.track}
                    thumbTintColor={theme.colors.accentStrong}
                  />
                </View>

                <View style={styles.controlGroup}>
                  <View style={styles.labelRow}>
                    <Text style={styles.label}>Pitch</Text>
                    <View style={styles.valueRow}>
                      <Text style={styles.valuePill}>
                        {track.pitch > 0 ? '+' : ''}
                        {track.pitch.toFixed(1)} st
                      </Text>
                      <TouchableOpacity
                        style={[
                          styles.iconResetButton,
                          !supportsTrackPitch && styles.iconResetButtonDisabled,
                        ]}
                        onPress={() => handleTrackPitchChange(track.id, 0.0)}
                        disabled={!supportsTrackPitch}
                      >
                        <Text style={styles.iconResetText}>↺</Text>
                      </TouchableOpacity>
                    </View>
                  </View>
                  <Slider
                    style={styles.slider}
                    minimumValue={-12.0}
                    maximumValue={12.0}
                    value={track.pitch}
                    disabled={!supportsTrackPitch}
                    onValueChange={(value: number) => handleTrackPitchChange(track.id, value)}
                    minimumTrackTintColor={
                      supportsTrackPitch ? theme.colors.accent : theme.colors.track
                    }
                    maximumTrackTintColor={theme.colors.track}
                    thumbTintColor={
                      supportsTrackPitch ? theme.colors.accentStrong : theme.colors.border
                    }
                  />
                </View>

                <View style={styles.controlGroup}>
                  <View style={styles.labelRow}>
                    <Text style={styles.label}>Speed</Text>
                    <View style={styles.valueRow}>
                      <Text style={styles.valuePill}>
                        {(track.speed * 100).toFixed(0)}%
                      </Text>
                      <TouchableOpacity
                        style={[
                          styles.iconResetButton,
                          !supportsTrackSpeed && styles.iconResetButtonDisabled,
                        ]}
                        onPress={() => handleTrackSpeedChange(track.id, 1.0)}
                        disabled={!supportsTrackSpeed}
                      >
                        <Text style={styles.iconResetText}>↺</Text>
                      </TouchableOpacity>
                    </View>
                  </View>
                  <Slider
                    style={styles.slider}
                    minimumValue={0.5}
                    maximumValue={2.0}
                    value={track.speed}
                    disabled={!supportsTrackSpeed}
                    onValueChange={(value: number) => handleTrackSpeedChange(track.id, value)}
                    minimumTrackTintColor={
                      supportsTrackSpeed ? theme.colors.accent : theme.colors.track
                    }
                    maximumTrackTintColor={theme.colors.track}
                    thumbTintColor={
                      supportsTrackSpeed ? theme.colors.accentStrong : theme.colors.border
                    }
                  />
                </View>
              </Animated.View>
            ))}
          </>
        )}
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    backgroundColor: theme.colors.background,
  },
  background: {
    ...StyleSheet.absoluteFillObject,
  },
  glowTop: {
    position: 'absolute',
    width: 320,
    height: 320,
    borderRadius: 160,
    backgroundColor: 'rgba(255, 180, 84, 0.22)',
    top: -140,
    right: -80,
  },
  glowBottom: {
    position: 'absolute',
    width: 360,
    height: 360,
    borderRadius: 180,
    backgroundColor: 'rgba(35, 209, 139, 0.18)',
    bottom: -160,
    left: -120,
  },
  content: {
    padding: 20,
    paddingTop: 56,
    paddingBottom: 80,
  },
  heroCard: {
    backgroundColor: theme.colors.surface,
    borderRadius: theme.radius.card,
    padding: 20,
    borderWidth: 1,
    borderColor: theme.colors.border,
    shadowColor: '#000',
    shadowOpacity: 0.25,
    shadowRadius: 16,
    shadowOffset: { width: 0, height: 10 },
    elevation: 4,
  },
  heroTopRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    gap: 12,
  },
  eyebrow: {
    color: theme.colors.textMuted,
    textTransform: 'uppercase',
    letterSpacing: 2,
    fontSize: 12,
    fontWeight: '600',
    marginBottom: 6,
  },
  title: {
    fontSize: 28,
    fontWeight: '700',
    color: theme.colors.text,
    fontFamily: 'serif',
  },
  subtitle: {
    color: theme.colors.textMuted,
    fontSize: 14,
    marginTop: 12,
    marginBottom: 16,
  },
  statusPill: {
    paddingVertical: 6,
    paddingHorizontal: 12,
    borderRadius: theme.radius.pill,
    backgroundColor: theme.colors.surfaceAlt,
    borderWidth: 1,
    borderColor: theme.colors.border,
  },
  statusPillReady: {
    borderColor: 'rgba(35, 209, 139, 0.6)',
  },
  statusPillActive: {
    backgroundColor: 'rgba(35, 209, 139, 0.2)',
    borderColor: 'rgba(35, 209, 139, 0.7)',
  },
  statusPillDanger: {
    backgroundColor: 'rgba(255, 93, 93, 0.18)',
    borderColor: 'rgba(255, 93, 93, 0.7)',
  },
  statusText: {
    color: theme.colors.text,
    fontSize: 12,
    fontWeight: '600',
  },
  primaryButton: {
    backgroundColor: theme.colors.accent,
    paddingVertical: 14,
    borderRadius: theme.radius.button,
    alignItems: 'center',
  },
  primaryButtonText: {
    color: '#1b1206',
    fontSize: 16,
    fontWeight: '700',
    letterSpacing: 0.3,
  },
  card: {
    backgroundColor: theme.colors.surface,
    borderRadius: theme.radius.card,
    padding: 18,
    borderWidth: 1,
    borderColor: theme.colors.border,
    marginTop: 18,
    shadowColor: '#000',
    shadowOpacity: 0.2,
    shadowRadius: 14,
    shadowOffset: { width: 0, height: 8 },
    elevation: 3,
  },
  sectionHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 12,
  },
  sectionHeaderActions: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  sectionTitle: {
    color: theme.colors.text,
    fontSize: 18,
    fontWeight: '700',
  },
  sectionTitleRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginTop: 26,
  },
  sectionHint: {
    color: theme.colors.textMuted,
    fontSize: 12,
  },
  timePill: {
    paddingHorizontal: 10,
    paddingVertical: 4,
    borderRadius: theme.radius.pill,
    backgroundColor: theme.colors.surfaceAlt,
  },
  timeText: {
    color: theme.colors.textMuted,
    fontSize: 12,
    fontFamily: 'monospace',
  },
  transportRow: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 10,
    marginBottom: 12,
  },
  controlButton: {
    backgroundColor: theme.colors.surfaceStrong,
    paddingVertical: 10,
    paddingHorizontal: 16,
    borderRadius: theme.radius.button,
    borderWidth: 1,
    borderColor: theme.colors.border,
    minWidth: 96,
    alignItems: 'center',
  },
  controlButtonDisabled: {
    opacity: 0.5,
  },
  controlButtonText: {
    color: theme.colors.text,
    fontSize: 14,
    fontWeight: '600',
  },
  resetButton: {
    backgroundColor: 'rgba(255, 180, 84, 0.15)',
    paddingVertical: 10,
    paddingHorizontal: 16,
    borderRadius: theme.radius.button,
    borderWidth: 1,
    borderColor: 'rgba(255, 180, 84, 0.6)',
    minWidth: 96,
    alignItems: 'center',
  },
  resetButtonText: {
    color: theme.colors.accent,
    fontSize: 14,
    fontWeight: '600',
  },
  controlGroup: {
    marginVertical: 10,
  },
  labelRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 6,
  },
  valueRow: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  label: {
    color: theme.colors.text,
    fontSize: 14,
    fontWeight: '600',
  },
  valuePill: {
    color: theme.colors.textMuted,
    fontSize: 12,
    paddingHorizontal: 10,
    paddingVertical: 4,
    borderRadius: theme.radius.pill,
    backgroundColor: theme.colors.surfaceAlt,
    overflow: 'hidden',
  },
  iconResetButton: {
    width: 28,
    height: 28,
    borderRadius: 14,
    borderWidth: 1,
    borderColor: theme.colors.border,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: theme.colors.surfaceStrong,
  },
  iconResetButtonDisabled: {
    opacity: 0.4,
  },
  iconResetText: {
    color: theme.colors.text,
    fontSize: 14,
    fontWeight: '600',
  },
  slider: {
    width: '100%',
    height: 40,
  },
  trackCard: {
    backgroundColor: theme.colors.surface,
    borderRadius: theme.radius.card,
    padding: 16,
    borderWidth: 1,
    borderColor: theme.colors.border,
    marginTop: 14,
  },
  trackHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 12,
  },
  trackName: {
    color: theme.colors.text,
    fontSize: 17,
    fontWeight: '700',
  },
  trackMeta: {
    color: theme.colors.textMuted,
    fontSize: 12,
    marginTop: 4,
  },
  trackButtons: {
    flexDirection: 'row',
    gap: 8,
  },
  trackButton: {
    backgroundColor: theme.colors.surfaceStrong,
    width: 34,
    height: 34,
    borderRadius: 17,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 1,
    borderColor: theme.colors.border,
  },
  trackButtonMuted: {
    backgroundColor: 'rgba(255, 93, 93, 0.2)',
    borderColor: 'rgba(255, 93, 93, 0.8)',
  },
  trackButtonSolo: {
    backgroundColor: 'rgba(35, 209, 139, 0.2)',
    borderColor: 'rgba(35, 209, 139, 0.8)',
  },
  trackButtonText: {
    color: theme.colors.text,
    fontSize: 13,
    fontWeight: '700',
  },
});
