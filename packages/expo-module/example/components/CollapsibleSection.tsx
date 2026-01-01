import { ReactNode, useCallback, useState } from 'react';
import {
  Animated,
  LayoutAnimation,
  Platform,
  StyleProp,
  TouchableOpacity,
  UIManager,
  View,
  ViewStyle,
  Text,
} from 'react-native';
import { styles } from '../ui';

type CollapsibleVariant = 'card' | 'plain';

interface CollapsibleSectionProps {
  title: string;
  subtitle?: string;
  right?: ReactNode;
  children: ReactNode;
  defaultCollapsed?: boolean;
  variant?: CollapsibleVariant;
  containerStyle?: StyleProp<ViewStyle>;
  contentStyle?: StyleProp<ViewStyle>;
  collapseOnTitlePress?: boolean;
}

if (Platform.OS === 'android' && UIManager.setLayoutAnimationEnabledExperimental) {
  UIManager.setLayoutAnimationEnabledExperimental(true);
}

const CollapsibleSection = ({
  title,
  subtitle,
  right,
  children,
  defaultCollapsed = false,
  variant = 'card',
  containerStyle,
  contentStyle,
  collapseOnTitlePress = true,
}: CollapsibleSectionProps) => {
  const [collapsed, setCollapsed] = useState(defaultCollapsed);

  const toggleCollapsed = useCallback(() => {
    LayoutAnimation.configureNext(LayoutAnimation.Presets.easeInEaseOut);
    setCollapsed((prev) => !prev);
  }, []);

  const headerStyle = variant === 'plain' ? styles.sectionTitleRow : styles.sectionHeader;
  const containerBaseStyle = variant === 'plain' ? null : styles.card;
  const contentBaseStyle = variant === 'plain' ? styles.sectionContent : null;

  return (
    <Animated.View style={[containerBaseStyle, containerStyle]}>
      <View style={headerStyle}>
        {collapseOnTitlePress ? (
          <TouchableOpacity
            style={styles.headerTitleAction}
            onPress={toggleCollapsed}
            activeOpacity={0.7}
          >
            <Text style={styles.sectionTitle}>{title}</Text>
            {subtitle ? <Text style={styles.sectionHint}>{subtitle}</Text> : null}
          </TouchableOpacity>
        ) : (
          <View style={styles.headerTitleAction}>
            <Text style={styles.sectionTitle}>{title}</Text>
            {subtitle ? <Text style={styles.sectionHint}>{subtitle}</Text> : null}
          </View>
        )}
        <View style={styles.sectionHeaderActions}>
          {right}
          <TouchableOpacity
            style={styles.iconResetButton}
            onPress={toggleCollapsed}
            activeOpacity={0.7}
          >
            <Text style={styles.iconResetText}>{collapsed ? '▸' : '▾'}</Text>
          </TouchableOpacity>
        </View>
      </View>
      {!collapsed && <View style={[contentBaseStyle, contentStyle]}>{children}</View>}
    </Animated.View>
  );
};

export default CollapsibleSection;
