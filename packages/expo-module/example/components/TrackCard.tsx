import { Animated, StyleProp, Text, TouchableOpacity, View, ViewStyle } from 'react-native';
import { theme, styles } from '../ui';
import LabeledSlider from './LabeledSlider';
import { Track } from '../types';

interface TrackCardProps {
  track: Track;
  supportsTrackPitch: boolean;
  supportsTrackSpeed: boolean;
  onToggleMute: (trackId: string) => void;
  onToggleSolo: (trackId: string) => void;
  onVolumeChange: (trackId: string, value: number) => void;
  onPanChange: (trackId: string, value: number) => void;
  onPitchChange: (trackId: string, value: number) => void;
  onSpeedChange: (trackId: string, value: number) => void;
  formatStartTime: (ms?: number) => string;
  containerStyle?: StyleProp<ViewStyle>;
}

const TrackCard = ({
  track,
  supportsTrackPitch,
  supportsTrackSpeed,
  onToggleMute,
  onToggleSolo,
  onVolumeChange,
  onPanChange,
  onPitchChange,
  onSpeedChange,
  formatStartTime,
  containerStyle,
}: TrackCardProps) => {
  return (
    <Animated.View style={[styles.trackCard, containerStyle]}>
      <View style={styles.trackHeader}>
        <View>
          <Text style={styles.trackName}>{track.name}</Text>
          <Text style={styles.trackMeta}>
            Pitch {track.pitch > 0 ? '+' : ''}
            {track.pitch.toFixed(1)} st | Speed {(track.speed * 100).toFixed(0)}%
            {typeof track.startTimeMs === 'number'
              ? ` | Start ${formatStartTime(track.startTimeMs)}`
              : ''}
          </Text>
        </View>
        <View style={styles.trackButtons}>
          <TouchableOpacity
            style={[styles.trackButton, track.muted && styles.trackButtonMuted]}
            onPress={() => onToggleMute(track.id)}
          >
            <Text style={styles.trackButtonText}>M</Text>
          </TouchableOpacity>
          <TouchableOpacity
            style={[styles.trackButton, track.solo && styles.trackButtonSolo]}
            onPress={() => onToggleSolo(track.id)}
          >
            <Text style={styles.trackButtonText}>S</Text>
          </TouchableOpacity>
        </View>
      </View>

      <LabeledSlider
        label="Volume"
        value={track.volume}
        minimumValue={0}
        maximumValue={2.0}
        formatValue={(value) => `${(value * 100).toFixed(0)}%`}
        onValueChange={(value) => onVolumeChange(track.id, value)}
        onReset={() => onVolumeChange(track.id, 1.0)}
        minimumTrackTintColor={theme.colors.accent}
        maximumTrackTintColor={theme.colors.track}
        thumbTintColor={theme.colors.accentStrong}
      />

      <LabeledSlider
        label="Pan"
        value={track.pan}
        minimumValue={-1.0}
        maximumValue={1.0}
        formatValue={(value) => {
          if (value === 0) {
            return 'C 0';
          }
          const side = value > 0 ? 'R' : 'L';
          return `${side} ${Math.abs(value * 100).toFixed(0)}`;
        }}
        onValueChange={(value) => onPanChange(track.id, value)}
        onReset={() => onPanChange(track.id, 0.0)}
        minimumTrackTintColor={theme.colors.accent}
        maximumTrackTintColor={theme.colors.track}
        thumbTintColor={theme.colors.accentStrong}
      />

      <LabeledSlider
        label="Pitch"
        value={track.pitch}
        minimumValue={-12.0}
        maximumValue={12.0}
        formatValue={(value) => `${value > 0 ? '+' : ''}${value.toFixed(1)} st`}
        onValueChange={(value) => onPitchChange(track.id, value)}
        onReset={() => onPitchChange(track.id, 0.0)}
        disabled={!supportsTrackPitch}
        resetDisabled={!supportsTrackPitch}
        minimumTrackTintColor={
          supportsTrackPitch ? theme.colors.accent : theme.colors.track
        }
        maximumTrackTintColor={theme.colors.track}
        thumbTintColor={
          supportsTrackPitch ? theme.colors.accentStrong : theme.colors.border
        }
      />

      <LabeledSlider
        label="Speed"
        value={track.speed}
        minimumValue={0.5}
        maximumValue={2.0}
        formatValue={(value) => `${(value * 100).toFixed(0)}%`}
        onValueChange={(value) => onSpeedChange(track.id, value)}
        onReset={() => onSpeedChange(track.id, 1.0)}
        disabled={!supportsTrackSpeed}
        resetDisabled={!supportsTrackSpeed}
        minimumTrackTintColor={
          supportsTrackSpeed ? theme.colors.accent : theme.colors.track
        }
        maximumTrackTintColor={theme.colors.track}
        thumbTintColor={
          supportsTrackSpeed ? theme.colors.accentStrong : theme.colors.border
        }
      />
    </Animated.View>
  );
};

export default TrackCard;
