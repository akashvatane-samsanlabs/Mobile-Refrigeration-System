import { PermissionsAndroid, Platform } from 'react-native';

import { AppError } from './errors';

async function requestAndroid31Permissions(): Promise<boolean> {
  const result = await PermissionsAndroid.requestMultiple([
    PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
    PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
  ]);

  const scanGranted =
    result[PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN] ===
    PermissionsAndroid.RESULTS.GRANTED;
  const connectGranted =
    result[PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT] ===
    PermissionsAndroid.RESULTS.GRANTED;

  return scanGranted && connectGranted;
}

async function requestLegacyLocationPermission(): Promise<boolean> {
  const result = await PermissionsAndroid.request(
    PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
    {
      title: 'Location permission',
      message:
        'Android requires location permission to scan for nearby Bluetooth devices.',
      buttonPositive: 'Allow',
      buttonNegative: 'Deny',
    },
  );

  return result === PermissionsAndroid.RESULTS.GRANTED;
}

export async function requestBluetoothPermissions(): Promise<void> {
  if (Platform.OS !== 'android') {
    return;
  }

  const apiLevel = Number(Platform.Version);
  const granted =
    apiLevel >= 31
      ? await requestAndroid31Permissions()
      : await requestLegacyLocationPermission();

  if (!granted) {
    throw new AppError(
      'PERMISSION_DENIED',
      'Bluetooth permissions were denied. Open app Settings and allow Nearby devices (Android 12+) or Location (older Android), then try again.',
    );
  }
}
