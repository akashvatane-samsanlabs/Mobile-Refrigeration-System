import { Platform, StatusBar, useWindowDimensions } from 'react-native';

/** Content stops growing past this width so tablets/landscape stay readable */
const MAX_CONTENT_WIDTH = 720;

/** Room kept below the content for the gesture / navigation bar */
const BOTTOM_INSET = 32;

export type ResponsiveLayout = {
  /** Narrow phones (e.g. 320–359 dp wide) */
  compact: boolean;
  /** Room for two configuration cards side by side */
  wide: boolean;
  horizontalPadding: number;
  maxContentWidth: number;
  /** Status bar height, so content starts below it (edge-to-edge) */
  topInset: number;
  bottomInset: number;
};

export function useResponsiveLayout(): ResponsiveLayout {
  const { width } = useWindowDimensions();
  const compact = width < 360;

  return {
    compact,
    wide: width >= 700,
    horizontalPadding: compact ? 12 : width >= 600 ? 24 : 16,
    maxContentWidth: MAX_CONTENT_WIDTH,
    topInset: Platform.OS === 'android' ? StatusBar.currentHeight ?? 24 : 0,
    bottomInset: BOTTOM_INSET,
  };
}
