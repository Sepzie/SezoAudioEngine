import { Text, TouchableOpacity, View } from 'react-native';
import Slider from '@react-native-community/slider';
import { styles } from '../ui';

interface LabeledSliderProps {
  label: string;
  value: number;
  minimumValue: number;
  maximumValue: number;
  onValueChange?: (value: number) => void;
  onSlidingComplete?: (value: number) => void;
  formatValue?: (value: number) => string;
  onReset?: () => void;
  resetDisabled?: boolean;
  disabled?: boolean;
  minimumTrackTintColor?: string;
  maximumTrackTintColor?: string;
  thumbTintColor?: string;
  step?: number;
}

const LabeledSlider = ({
  label,
  value,
  minimumValue,
  maximumValue,
  onValueChange,
  onSlidingComplete,
  formatValue,
  onReset,
  resetDisabled,
  disabled,
  minimumTrackTintColor,
  maximumTrackTintColor,
  thumbTintColor,
  step,
}: LabeledSliderProps) => {
  const displayValue = formatValue ? formatValue(value) : value.toFixed(2);
  const isResetDisabled = resetDisabled ?? disabled;

  return (
    <View style={styles.controlGroup}>
      <View style={styles.labelRow}>
        <Text style={styles.label}>{label}</Text>
        <View style={styles.valueRow}>
          <Text style={styles.valuePill}>{displayValue}</Text>
          {onReset ? (
            <TouchableOpacity
              style={[
                styles.iconResetButton,
                isResetDisabled && styles.iconResetButtonDisabled,
              ]}
              onPress={onReset}
              disabled={isResetDisabled}
            >
              <Text style={styles.iconResetText}>↺</Text>
            </TouchableOpacity>
          ) : null}
        </View>
      </View>
      <Slider
        style={styles.slider}
        minimumValue={minimumValue}
        maximumValue={maximumValue}
        value={value}
        step={step}
        onValueChange={onValueChange}
        onSlidingComplete={onSlidingComplete}
        disabled={disabled}
        minimumTrackTintColor={minimumTrackTintColor}
        maximumTrackTintColor={maximumTrackTintColor}
        thumbTintColor={thumbTintColor}
      />
    </View>
  );
};

export default LabeledSlider;
