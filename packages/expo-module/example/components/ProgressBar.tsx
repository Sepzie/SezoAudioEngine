import { StyleProp, View, ViewStyle } from 'react-native';
import { styles } from '../ui';

interface ProgressBarProps {
  progress: number;
  fillStyle?: StyleProp<ViewStyle>;
}

const ProgressBar = ({ progress, fillStyle }: ProgressBarProps) => {
  const clamped = Math.max(0, Math.min(1, progress));

  return (
    <View style={styles.progressTrack}>
      <View
        style={[
          styles.progressFill,
          fillStyle,
          { width: `${Math.round(clamped * 100)}%` },
        ]}
      />
    </View>
  );
};

export default ProgressBar;
