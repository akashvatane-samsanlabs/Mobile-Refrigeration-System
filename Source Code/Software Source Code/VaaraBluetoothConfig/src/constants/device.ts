/** Must match ESP32 firmware defines */
export const DEVICE_CONFIG = {
  name: 'VAARA_V8.0',
  /** Only devices whose Bluetooth name starts with this are listed */
  namePrefix: 'VAARA',
  pin: '1234',
  sppServer: 'VAARA_SPP',
} as const;

/** Firmware limits (temperature stored as tenths of °C, e.g. 200 = 20.0°C) */
export const TEMP_LIMITS = {
  minTenths: -200,
  maxTenths: 1000,
  minCelsius: -20,
  maxCelsius: 100,
} as const;

/** Operating modes — command letter is followed by the mode value, e.g. E20 */
export const OPERATING_MODES = [
  { code: 'E', label: 'ECO' },
  { code: 'N', label: 'Normal' },
  { code: 'P', label: 'Power' },
  { code: 'A', label: 'Auto' },
] as const;

export type OperatingModeCode = (typeof OPERATING_MODES)[number]['code'];

export const MODE_VALUE_LIMITS = {
  min: 0,
  max: 100,
} as const;

export const RESPONSE_PATTERNS = {
  connected: 'VAARA CONNECTED',
  tempOk: 'TEMP RECEIVED',
  minTempOk: 'MIN TEMP RECEIVED',
  modeOk: 'MODE RECEIVED',
  invalidMode: 'INVALID MODE COMMAND',
  invalidTemp: 'INVALID TEMP COMMAND',
  invalidMinTemp: 'INVALID MIN TEMP COMMAND',
  unknown: 'UNKNOWN COMMAND',
} as const;
