import React from 'react';
import { Image, StyleSheet, Text, View } from 'react-native';

type AppHeaderProps = {
  compact?: boolean;
};

export function AppHeader({
  compact = false,
}: AppHeaderProps): React.JSX.Element {
  return (
    <View style={[styles.header, compact && styles.headerCompact]}>
      <Image
        source={require('../assets/vaara-logo.png')}
        style={[styles.logo, compact && styles.logoCompact]}
        resizeMode="contain"
        accessibilityLabel="VAARA logo"
      />
      <View style={styles.textBlock}>
        <Text
          style={[styles.title, compact && styles.titleCompact]}
          numberOfLines={1}
          adjustsFontSizeToFit
          maxFontSizeMultiplier={1.3}
        >
          Bluetooth Configuration
        </Text>
        <Text
          style={styles.subtitle}
          numberOfLines={1}
          maxFontSizeMultiplier={1.3}
        >
          VAARA device setup
        </Text>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: '#1E4D45',
    borderRadius: 16,
    paddingHorizontal: 16,
    paddingVertical: 14,
    gap: 14,
  },
  headerCompact: {
    borderRadius: 12,
    paddingHorizontal: 12,
    paddingVertical: 10,
    gap: 10,
  },
  logo: {
    width: 64,
    height: 64,
    borderRadius: 12,
  },
  logoCompact: {
    width: 44,
    height: 44,
    borderRadius: 10,
  },
  textBlock: {
    flex: 1,
    minWidth: 0,
  },
  title: {
    fontSize: 20,
    fontWeight: '700',
    color: '#E8F7F1',
    letterSpacing: 0.3,
  },
  titleCompact: {
    fontSize: 17,
  },
  subtitle: {
    marginTop: 4,
    fontSize: 13,
    color: '#A8D8C8',
  },
});
