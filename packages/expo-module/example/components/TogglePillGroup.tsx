import { Text, TouchableOpacity, View } from 'react-native';
import { styles } from '../ui';

type ToggleOption<T extends string | number> = {
  value: T;
  label: string;
  disabled?: boolean;
};

interface TogglePillGroupProps<T extends string | number> {
  options: ReadonlyArray<ToggleOption<T>>;
  value: T;
  onChange: (value: T) => void;
  disabled?: boolean;
}

const TogglePillGroup = <T extends string | number>({
  options,
  value,
  onChange,
  disabled,
}: TogglePillGroupProps<T>) => {
  return (
    <View style={styles.formatRow}>
      {options.map((option) => {
        const isActive = option.value === value;
        const isDisabled = disabled || option.disabled;

        return (
          <TouchableOpacity
            key={String(option.value)}
            style={[
              styles.formatButton,
              isActive && styles.formatButtonActive,
              isDisabled && styles.formatButtonDisabled,
            ]}
            onPress={() => onChange(option.value)}
            disabled={isDisabled}
          >
            <Text
              style={[
                styles.formatButtonText,
                isActive && styles.formatButtonTextActive,
              ]}
            >
              {option.label}
            </Text>
          </TouchableOpacity>
        );
      })}
    </View>
  );
};

export default TogglePillGroup;
