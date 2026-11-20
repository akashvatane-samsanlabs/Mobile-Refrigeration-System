import React, {
  memo,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';
import {
  ActivityIndicator,
  Alert,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  StyleProp,
  TextInput,
  View,
  ViewStyle,
} from 'react-native';
import {
  DEVICE_CONFIG,
  MODE_VALUE_LIMITS,
  OPERATING_MODES,
  OperatingModeCode,
  RESPONSE_PATTERNS,
  TEMP_LIMITS,
} from '../constants/device';
import { AppHeader } from '../components/AppHeader';
import { bluetoothService, DeviceSummary } from '../services/BluetoothService';
import {
  getErrorDetail,
  getErrorTitle,
  isAppError,
  toUserMessage,
} from '../utils/errors';
import { requestBluetoothPermissions } from '../utils/permissions';
import {
  formatMaxTemperatureCommand,
  formatMinTemperatureCommand,
  formatModeCommand,
  formatTemperatureRangeLabel,
  getModeValueFieldError,
  getTemperatureFieldError,
  getTemperatureRangeError,
  parseModeValueInput,
  parseTemperaturePair,
  tenthsToCelsius,
} from '../utils/validation';
import { useResponsiveLayout } from '../utils/layout';

const PLACEHOLDER_COLOR = '#829AB1';

type LogEntry = {
  id: string;
  text: string;
  kind: 'info' | 'success' | 'error';
};

const MAX_LOG_ENTRIES = 40;
let logCounter = 0;

function createLog(text: string, kind: LogEntry['kind'] = 'info'): LogEntry {
  logCounter += 1;
  return { id: String(logCounter), text, kind };
}

/** Error codes that need the user to act, so a popup is shown as well */
const ALERT_ERROR_CODES = new Set([
  'INVALID_TEMPERATURE',
  'INVALID_TEMPERATURE_RANGE',
  'INVALID_MODE_VALUE',
  'PERMISSION_DENIED',
  'BLUETOOTH_OFF',
  'PAIRING_FAILED',
]);

const DeviceRow = memo(function DeviceRowView({
  device,
  active,
  disabled,
  onPress,
}: {
  device: DeviceSummary;
  active: boolean;
  disabled: boolean;
  onPress: (device: DeviceSummary) => void;
}): React.JSX.Element {
  return (
    <Pressable
      style={[styles.deviceRow, active && styles.deviceActive]}
      onPress={() => onPress(device)}
      disabled={disabled}
    >
      <View style={styles.deviceInfo}>
        <Text style={styles.deviceName} numberOfLines={1}>
          {device.name}
        </Text>
        <Text style={styles.deviceAddress} numberOfLines={1}>
          {device.address}
        </Text>
      </View>
      <Text style={styles.deviceBonded}>
        {active ? 'Connected' : device.bonded ? 'Paired' : 'Tap to pair'}
      </Text>
    </Pressable>
  );
});

const LogLine = memo(function LogLineView({
  entry,
}: {
  entry: LogEntry;
}): React.JSX.Element {
  return (
    <Text
      style={[
        styles.logLine,
        entry.kind === 'success' && styles.logSuccess,
        entry.kind === 'error' && styles.logError,
      ]}
    >
      {entry.text}
    </Text>
  );
});

type FieldErrors = {
  tempMin?: string;
  tempMax?: string;
  modeValue?: string;
};

function getModeLabel(code: OperatingModeCode): string {
  return OPERATING_MODES.find(mode => mode.code === code)?.label ?? code;
}

export function ConfigScreen(): React.JSX.Element {
  const [devices, setDevices] = useState<DeviceSummary[]>([]);
  const [connectedDevice, setConnectedDevice] = useState<DeviceSummary | null>(
    null,
  );
  const [tempMin, setTempMin] = useState('10.0');
  const [tempMax, setTempMax] = useState('20.0');
  const [mode, setMode] = useState<OperatingModeCode>('N');
  const [modeValue, setModeValue] = useState('');
  const [modeMenuOpen, setModeMenuOpen] = useState(false);
  const [fieldErrors, setFieldErrors] = useState<FieldErrors>({});
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [busy, setBusy] = useState(false);
  const [statusMessage, setStatusMessage] = useState('Disconnected');

  const isConnected = connectedDevice !== null;

  const layout = useResponsiveLayout();
  const sidePadding = useMemo(
    () => ({
      paddingLeft: layout.horizontalPadding,
      paddingRight: layout.horizontalPadding,
    }),
    [layout.horizontalPadding],
  );
  const contentWidth = useMemo(
    () => [styles.content, { maxWidth: layout.maxContentWidth }],
    [layout.maxContentWidth],
  );

  const mountedRef = useRef(true);
  const busyRef = useRef(false);

  const pushLog = useCallback(
    (text: string, kind: LogEntry['kind'] = 'info') => {
      if (!mountedRef.current) {
        return;
      }
      setLogs(prev =>
        [createLog(text, kind), ...prev].slice(0, MAX_LOG_ENTRIES),
      );
    },
    [],
  );

  const clearFieldError = useCallback((field: keyof FieldErrors) => {
    setFieldErrors(prev => {
      if (!prev[field]) {
        return prev;
      }
      const next = { ...prev };
      delete next[field];
      return next;
    });
  }, []);

  const applyFieldError = useCallback((error: unknown) => {
    if (!isAppError(error) || !error.field) {
      return;
    }

    setFieldErrors(prev => ({
      ...prev,
      [error.field!]: error.message,
    }));
  }, []);

  const validateTemperatureFields = useCallback((): boolean => {
    const nextErrors: FieldErrors = {};
    const minError = getTemperatureFieldError(tempMin, 'tempMin');
    const maxError = getTemperatureFieldError(tempMax, 'tempMax');
    const rangeError =
      !minError && !maxError
        ? getTemperatureRangeError(tempMin, tempMax)
        : null;

    if (minError) {
      nextErrors.tempMin = minError;
    }
    if (maxError) {
      nextErrors.tempMax = maxError;
    }
    if (rangeError) {
      nextErrors.tempMin = rangeError;
    }

    setFieldErrors(prev => ({ ...prev, ...nextErrors }));
    return Object.keys(nextErrors).length === 0;
  }, [tempMax, tempMin]);

  const validateModeValueField = useCallback((): boolean => {
    const modeError = getModeValueFieldError(modeValue);
    if (modeError) {
      setFieldErrors(prev => ({ ...prev, modeValue: modeError }));
      return false;
    }
    clearFieldError('modeValue');
    return true;
  }, [clearFieldError, modeValue]);

  const handleDeviceResponse = useCallback(
    (line: string) => {
      pushLog(`RX: ${line}`, 'info');

      if (line.includes(RESPONSE_PATTERNS.connected)) {
        setStatusMessage('Connected to VAARA');
      } else if (line.includes(RESPONSE_PATTERNS.tempOk)) {
        setStatusMessage('Maximum temperature updated');
        pushLog('Maximum temperature command accepted by device.', 'success');
      } else if (line.includes(RESPONSE_PATTERNS.minTempOk)) {
        setStatusMessage('Minimum temperature updated');
        pushLog('Minimum temperature command accepted by device.', 'success');
      } else if (line.includes(RESPONSE_PATTERNS.modeOk)) {
        setStatusMessage('Mode updated');
        pushLog('Mode command accepted by device.', 'success');
      } else if (
        line.includes(RESPONSE_PATTERNS.invalidTemp) ||
        line.includes(RESPONSE_PATTERNS.invalidMinTemp) ||
        line.includes(RESPONSE_PATTERNS.invalidMode) ||
        line.includes(RESPONSE_PATTERNS.unknown)
      ) {
        setStatusMessage('Device rejected command');
        pushLog(line, 'error');
      }
    },
    [pushLog],
  );

  useEffect(() => {
    mountedRef.current = true;
    bluetoothService.onData(handleDeviceResponse);
    bluetoothService.onConnectionLost(reason => {
      if (!mountedRef.current) {
        return;
      }
      setConnectedDevice(null);
      setStatusMessage(reason);
      pushLog(`${reason} Tap Connect VAARA to reconnect.`, 'error');
    });
    requestBluetoothPermissions().catch(error => {
      pushLog(toUserMessage(error), 'error');
    });
    return () => {
      mountedRef.current = false;
      bluetoothService.removeSystemListeners();
      bluetoothService.disconnect().catch(() => undefined);
    };
  }, [handleDeviceResponse, pushLog]);

  /**
   * Runs one Bluetooth action at a time. Ignores taps while another action is
   * running, never throws, and never updates state after the screen is gone.
   */
  const runSafely = useCallback(
    async (action: () => Promise<void>, busyLabel: string) => {
      if (busyRef.current) {
        return;
      }
      busyRef.current = true;
      setBusy(true);
      setStatusMessage(busyLabel);
      try {
        await action();
      } catch (error) {
        if (!mountedRef.current) {
          return;
        }
        const message = toUserMessage(error);
        setStatusMessage(message);
        pushLog(message, 'error');
        const detail = getErrorDetail(error);
        if (detail) {
          pushLog(`Details: ${detail}`, 'error');
        }
        applyFieldError(error);

        if (isAppError(error)) {
          if (ALERT_ERROR_CODES.has(error.code)) {
            Alert.alert(getErrorTitle(error.code), message);
          }

          if (
            error.code === 'NOT_CONNECTED' ||
            error.code === 'CONNECTION_FAILED' ||
            error.code === 'CONNECTION_LOST'
          ) {
            setConnectedDevice(null);
          }
        }
      } finally {
        busyRef.current = false;
        if (mountedRef.current) {
          setBusy(false);
        }
      }
    },
    [applyFieldError, pushLog],
  );

  const refreshDevices = useCallback(async () => {
    await runSafely(async () => {
      try {
        const found = await bluetoothService.listDevices(partial => {
          if (mountedRef.current) {
            setDevices(partial);
          }
        });
        if (mountedRef.current) {
          setDevices(found);
        }
        pushLog(`Found ${found.length} VAARA device(s).`);
      } catch (error) {
        if (mountedRef.current) {
          setDevices([]);
        }
        throw error;
      }
    }, 'Scanning for VAARA devices...');
  }, [pushLog, runSafely]);

  const markConnected = useCallback((device: DeviceSummary) => {
    setConnectedDevice(device);
    setDevices(prev =>
      prev.map(item =>
        item.address === device.address ? { ...item, bonded: true } : item,
      ),
    );
  }, []);

  const connectToDevice = useCallback(
    async (device: DeviceSummary) => {
      await runSafely(
        async () => {
          const connected = await bluetoothService.connect(device);
          markConnected(connected);
          setStatusMessage(`Connected to ${connected.name}`);
          pushLog(
            `Connected to ${connected.name} (${connected.address})`,
            'success',
          );
        },
        device.bonded
          ? `Connecting to ${device.name}...`
          : `Pairing with ${device.name} (PIN ${DEVICE_CONFIG.pin})...`,
      );
    },
    [markConnected, pushLog, runSafely],
  );

  const connectVaara = useCallback(async () => {
    await runSafely(async () => {
      const found = await bluetoothService.listDevices();
      if (mountedRef.current) {
        setDevices(found);
      }
      const device = await bluetoothService.connectToVaara(found);
      markConnected(device);
      setStatusMessage(`Connected to ${device.name}`);
      pushLog(`Connected to ${device.name}`, 'success');
    }, 'Searching for VAARA devices...');
  }, [markConnected, pushLog, runSafely]);

  const disconnect = useCallback(async () => {
    await runSafely(async () => {
      await bluetoothService.disconnect();
      setConnectedDevice(null);
      setStatusMessage('Disconnected');
      pushLog('Disconnected.', 'info');
    }, 'Disconnecting...');
  }, [pushLog, runSafely]);

  const sendTemperature = useCallback(async () => {
    if (!validateTemperatureFields()) {
      return;
    }

    await runSafely(async () => {
      const { minTenths, maxTenths } = parseTemperaturePair(tempMin, tempMax);
      const minCommand = formatMinTemperatureCommand(minTenths);
      const maxCommand = formatMaxTemperatureCommand(maxTenths);

      await bluetoothService.sendCommand(minCommand);
      pushLog(
        `TX: ${minCommand} (min ${tenthsToCelsius(minTenths).toFixed(1)}°C)`,
        'info',
      );

      await bluetoothService.sendCommand(maxCommand);
      pushLog(
        `TX: ${maxCommand} (max ${tenthsToCelsius(maxTenths).toFixed(1)}°C)`,
        'info',
      );

      setFieldErrors(prev => {
        const next = { ...prev };
        delete next.tempMin;
        delete next.tempMax;
        return next;
      });
    }, 'Sending temperature range...');
  }, [pushLog, runSafely, tempMax, tempMin, validateTemperatureFields]);

  const sendMode = useCallback(async () => {
    if (!validateModeValueField()) {
      return;
    }

    await runSafely(async () => {
      const value = parseModeValueInput(modeValue);
      const command = formatModeCommand(mode, value);
      await bluetoothService.sendCommand(command);
      pushLog(
        `TX: ${command} (${getModeLabel(mode)} mode, value ${value})`,
        'info',
      );
      clearFieldError('modeValue');
    }, `Sending ${getModeLabel(mode)} mode...`);
  }, [
    clearFieldError,
    mode,
    modeValue,
    pushLog,
    runSafely,
    validateModeValueField,
  ]);

  const sendAll = useCallback(async () => {
    const temperatureOk = validateTemperatureFields();
    const modeOk = validateModeValueField();
    if (!temperatureOk || !modeOk) {
      return;
    }

    await runSafely(async () => {
      const { minTenths, maxTenths } = parseTemperaturePair(tempMin, tempMax);
      const value = parseModeValueInput(modeValue);
      const minCommand = formatMinTemperatureCommand(minTenths);
      const maxCommand = formatMaxTemperatureCommand(maxTenths);
      const modeCommand = formatModeCommand(mode, value);

      await bluetoothService.sendCommand(minCommand);
      pushLog(
        `TX: ${minCommand} (min ${tenthsToCelsius(minTenths).toFixed(1)}°C)`,
        'info',
      );

      await bluetoothService.sendCommand(maxCommand);
      pushLog(
        `TX: ${maxCommand} (max ${tenthsToCelsius(maxTenths).toFixed(1)}°C)`,
        'info',
      );

      await bluetoothService.sendCommand(modeCommand);
      pushLog(
        `TX: ${modeCommand} (${getModeLabel(mode)} mode, value ${value})`,
        'info',
      );

      setFieldErrors({});
    }, 'Sending configuration...');
  }, [
    mode,
    modeValue,
    pushLog,
    runSafely,
    tempMax,
    tempMin,
    validateModeValueField,
    validateTemperatureFields,
  ]);

  const vaaraDevice = useMemo(
    () => bluetoothService.findVaaraDevice(devices),
    [devices],
  );

  const openBluetoothSettings = useCallback(() => {
    bluetoothService.openBluetoothSettings();
    pushLog('Opened phone Bluetooth settings.', 'info');
  }, [pushLog]);

  const showHelp = useCallback(() => {
    Alert.alert(
      'VAARA Bluetooth',
      [
        '1. Power on the VAARA device and keep it near the phone',
        '2. Tap Scan. Only VAARA devices are listed',
        '3. Tap a device to pair and connect',
        `4. If asked for a PIN, enter ${DEVICE_CONFIG.pin}`,
        '',
        `SPP server: ${DEVICE_CONFIG.sppServer}`,
        `Temperature range: ${formatTemperatureRangeLabel()}`,
        'Commands: T-100 (min), T+200 (max)',
        'Modes: E20 (ECO), N20 (Normal), P20 (Power), A20 (Auto)',
      ].join('\n'),
    );
  }, []);

  return (
    <View style={styles.screen}>
      <View
        style={[
          styles.headerBar,
          sidePadding,
          { paddingTop: layout.topInset + (layout.compact ? 8 : 12) },
        ]}
      >
        <View style={contentWidth}>
          <AppHeader compact={layout.compact} />
        </View>
      </View>
      <ScrollView
        style={styles.scroll}
        contentContainerStyle={[
          styles.container,
          sidePadding,
          { paddingBottom: layout.bottomInset },
        ]}
        keyboardShouldPersistTaps="handled"
      >
        <View style={contentWidth}>
          <Text style={styles.subtitle}>
            Configure min/max temperature and mode on {DEVICE_CONFIG.name}
          </Text>

          <View style={styles.card}>
            <Text style={styles.statusLabel}>Status</Text>
            <Text style={styles.statusValue}>{statusMessage}</Text>
            {connectedDevice ? (
              <Text style={styles.meta}>
                {connectedDevice.name} · {connectedDevice.address}
              </Text>
            ) : null}
            {vaaraDevice && !isConnected ? (
              <Text style={styles.hint}>
                VAARA found: {vaaraDevice.name} ({vaaraDevice.address})
              </Text>
            ) : null}
          </View>

          <View style={styles.row}>
            <ActionButton
              label="Scan"
              onPress={refreshDevices}
              disabled={busy}
              variant="secondary"
              style={styles.rowButton}
            />
            <ActionButton
              label="Connect VAARA"
              onPress={connectVaara}
              disabled={busy}
              style={styles.rowButton}
            />
            <ActionButton
              label="Disconnect"
              onPress={disconnect}
              disabled={busy || !isConnected}
              variant="secondary"
              style={styles.rowButton}
            />
          </View>

          <View style={styles.row}>
            <ActionButton
              label="Bluetooth Settings"
              onPress={openBluetoothSettings}
              disabled={busy}
              variant="secondary"
              style={styles.rowButton}
            />
          </View>

          <Text style={styles.pairingNote}>
            Tap Scan to list nearby VAARA devices, then tap one to pair (PIN{' '}
            {DEVICE_CONFIG.pin}) and connect.
          </Text>

          <Pressable onPress={showHelp} style={styles.helpLink}>
            <Text style={styles.helpText}>
              Pairing help (PIN {DEVICE_CONFIG.pin})
            </Text>
          </Pressable>

          <Text style={styles.sectionTitle}>VAARA devices</Text>
          {busy && devices.length === 0 ? (
            <ActivityIndicator color="#0B5FFF" style={styles.loader} />
          ) : devices.length === 0 ? (
            <Text style={styles.empty}>No VAARA devices yet. Tap Scan.</Text>
          ) : (
            devices.map(item => (
              <DeviceRow
                key={item.address}
                device={item}
                active={connectedDevice?.address === item.address}
                disabled={busy}
                onPress={connectToDevice}
              />
            ))
          )}

          <Text style={styles.sectionTitle}>Configuration</Text>

          <View style={layout.wide ? styles.columns : undefined}>
            <View style={[styles.card, layout.wide && styles.column]}>
              <Text style={styles.fieldLabel}>Temperature (°C)</Text>
              <Text style={styles.rangeBanner}>
                Allowed range: {TEMP_LIMITS.minCelsius}°C to{' '}
                {TEMP_LIMITS.maxCelsius}°C
              </Text>

              <Text style={styles.subFieldLabel}>Minimum temperature</Text>
              <TextInput
                value={tempMin}
                onChangeText={value => {
                  setTempMin(value);
                  clearFieldError('tempMin');
                }}
                onBlur={validateTemperatureFields}
                keyboardType="decimal-pad"
                placeholder={`e.g. ${TEMP_LIMITS.minCelsius}`}
                style={[styles.input, fieldErrors.tempMin && styles.inputError]}
                placeholderTextColor={PLACEHOLDER_COLOR}
                editable={!busy}
              />
              {fieldErrors.tempMin ? (
                <Text style={styles.errorText}>{fieldErrors.tempMin}</Text>
              ) : (
                <Text style={styles.fieldHint}>
                  Sent as T-{'{tenths}'} — e.g. 10.0°C becomes T-100.
                </Text>
              )}

              <Text style={styles.subFieldLabel}>Maximum temperature</Text>
              <TextInput
                value={tempMax}
                onChangeText={value => {
                  setTempMax(value);
                  clearFieldError('tempMax');
                }}
                onBlur={validateTemperatureFields}
                keyboardType="decimal-pad"
                placeholder={`e.g. 20.0`}
                style={[styles.input, fieldErrors.tempMax && styles.inputError]}
                placeholderTextColor={PLACEHOLDER_COLOR}
                editable={!busy}
              />
              {fieldErrors.tempMax ? (
                <Text style={styles.errorText}>{fieldErrors.tempMax}</Text>
              ) : (
                <Text style={styles.fieldHint}>
                  Sent as T+{'{tenths}'} — e.g. 20.0°C becomes T+200.
                </Text>
              )}

              <ActionButton
                label="Send Temperature Range"
                onPress={sendTemperature}
                disabled={busy || !isConnected}
              />
            </View>

            <View style={[styles.card, layout.wide && styles.column]}>
              <Text style={styles.fieldLabel}>Mode of operation</Text>

              <Pressable
                onPress={() => setModeMenuOpen(open => !open)}
                disabled={busy}
                style={[styles.input, styles.dropdown]}
              >
                <Text style={styles.dropdownText}>{getModeLabel(mode)}</Text>
                <Text style={styles.dropdownArrow}>
                  {modeMenuOpen ? '▲' : '▼'}
                </Text>
              </Pressable>
              {modeMenuOpen ? (
                <View style={styles.dropdownMenu}>
                  {OPERATING_MODES.map(option => (
                    <Pressable
                      key={option.code}
                      onPress={() => {
                        setMode(option.code);
                        setModeMenuOpen(false);
                      }}
                      style={[
                        styles.dropdownOption,
                        option.code === mode && styles.dropdownOptionActive,
                      ]}
                    >
                      <Text
                        style={[
                          styles.dropdownText,
                          option.code === mode && styles.dropdownTextActive,
                        ]}
                      >
                        {option.label} ({option.code})
                      </Text>
                    </Pressable>
                  ))}
                </View>
              ) : null}

              <Text style={styles.subFieldLabel}>
                {getModeLabel(mode)} value ({MODE_VALUE_LIMITS.min} -{' '}
                {MODE_VALUE_LIMITS.max})
              </Text>
              <TextInput
                value={modeValue}
                onChangeText={value => {
                  setModeValue(value);
                  clearFieldError('modeValue');
                }}
                onBlur={validateModeValueField}
                keyboardType="number-pad"
                placeholder={`${MODE_VALUE_LIMITS.min} - ${MODE_VALUE_LIMITS.max}`}
                style={[
                  styles.input,
                  fieldErrors.modeValue && styles.inputError,
                ]}
                placeholderTextColor={PLACEHOLDER_COLOR}
                editable={!busy}
              />
              {fieldErrors.modeValue ? (
                <Text style={styles.errorText}>{fieldErrors.modeValue}</Text>
              ) : (
                <Text style={styles.fieldHint}>
                  Sent as {mode}
                  {'{value}'} — e.g. 20 becomes {mode}20.
                </Text>
              )}
              <ActionButton
                label="Send Mode"
                onPress={sendMode}
                disabled={busy || !isConnected}
              />
            </View>
          </View>

          <ActionButton
            label="Send All (Min + Max + Mode)"
            onPress={sendAll}
            disabled={busy || !isConnected}
          />

          <Text style={styles.sectionTitle}>Device log</Text>
          <View style={styles.logBox}>
            {logs.length === 0 ? (
              <Text style={styles.empty}>Responses will appear here.</Text>
            ) : (
              logs.map(entry => <LogLine key={entry.id} entry={entry} />)
            )}
          </View>
        </View>
      </ScrollView>
    </View>
  );
}

type ActionButtonProps = {
  label: string;
  onPress: () => void;
  disabled?: boolean;
  variant?: 'primary' | 'secondary';
  style?: StyleProp<ViewStyle>;
};

function ActionButton({
  label,
  onPress,
  disabled = false,
  variant = 'primary',
  style,
}: ActionButtonProps): React.JSX.Element {
  return (
    <Pressable
      onPress={onPress}
      disabled={disabled}
      style={[
        styles.button,
        variant === 'secondary' && styles.buttonSecondary,
        disabled && styles.buttonDisabled,
        style,
      ]}
    >
      <Text
        style={[
          styles.buttonText,
          variant === 'secondary' && styles.buttonTextSecondary,
        ]}
      >
        {label}
      </Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  screen: {
    flex: 1,
    backgroundColor: '#F4F7FB',
  },
  headerBar: {
    paddingBottom: 12,
    backgroundColor: '#F4F7FB',
    borderBottomWidth: 1,
    borderBottomColor: '#D9E2EC',
  },
  scroll: {
    flex: 1,
  },
  container: {
    paddingTop: 16,
    backgroundColor: '#F4F7FB',
    flexGrow: 1,
  },
  content: {
    width: '100%',
    alignSelf: 'center',
  },
  columns: {
    flexDirection: 'row',
    alignItems: 'flex-start',
    gap: 14,
  },
  column: {
    flex: 1,
  },
  rowButton: {
    flexGrow: 1,
    flexBasis: 96,
  },
  deviceInfo: {
    flex: 1,
    minWidth: 0,
    marginRight: 10,
  },
  subtitle: {
    marginBottom: 18,
    color: '#486581',
    fontSize: 15,
  },
  card: {
    backgroundColor: '#FFFFFF',
    borderRadius: 14,
    padding: 16,
    marginBottom: 14,
    borderWidth: 1,
    borderColor: '#D9E2EC',
  },
  statusLabel: {
    fontSize: 12,
    textTransform: 'uppercase',
    color: '#627D98',
    letterSpacing: 0.6,
  },
  statusValue: {
    marginTop: 4,
    fontSize: 18,
    fontWeight: '600',
    color: '#102A43',
  },
  meta: {
    marginTop: 6,
    color: '#486581',
    fontSize: 13,
  },
  hint: {
    marginTop: 8,
    color: '#0B5FFF',
    fontSize: 13,
  },
  row: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 10,
    marginBottom: 8,
  },
  helpLink: {
    marginBottom: 16,
  },
  helpText: {
    color: '#0B5FFF',
    fontSize: 14,
    fontWeight: '600',
  },
  pairingNote: {
    color: '#486581',
    fontSize: 13,
    marginBottom: 12,
    lineHeight: 18,
  },
  sectionTitle: {
    fontSize: 16,
    fontWeight: '700',
    color: '#243B53',
    marginBottom: 10,
    marginTop: 8,
  },
  loader: {
    marginVertical: 12,
  },
  empty: {
    color: '#829AB1',
    fontSize: 14,
    marginBottom: 10,
  },
  deviceRow: {
    backgroundColor: '#FFFFFF',
    borderRadius: 12,
    padding: 14,
    marginBottom: 8,
    borderWidth: 1,
    borderColor: '#D9E2EC',
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  deviceActive: {
    borderColor: '#0B5FFF',
    backgroundColor: '#E6F0FF',
  },
  deviceName: {
    fontSize: 15,
    fontWeight: '600',
    color: '#102A43',
  },
  deviceAddress: {
    fontSize: 12,
    color: '#627D98',
    marginTop: 2,
  },
  deviceBonded: {
    fontSize: 12,
    color: '#0B5FFF',
    fontWeight: '600',
  },
  fieldLabel: {
    fontSize: 14,
    fontWeight: '600',
    color: '#243B53',
    marginBottom: 8,
  },
  subFieldLabel: {
    fontSize: 13,
    fontWeight: '600',
    color: '#486581',
    marginBottom: 6,
    marginTop: 4,
  },
  rangeBanner: {
    fontSize: 12,
    color: '#0B5FFF',
    fontWeight: '600',
    marginBottom: 10,
    backgroundColor: '#E6F0FF',
    paddingHorizontal: 10,
    paddingVertical: 8,
    borderRadius: 8,
  },
  fieldHint: {
    fontSize: 12,
    color: '#627D98',
    marginBottom: 12,
  },
  errorText: {
    fontSize: 12,
    color: '#D64545',
    marginBottom: 12,
    fontWeight: '600',
  },
  dropdown: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  dropdownText: {
    fontSize: 16,
    color: '#102A43',
  },
  dropdownTextActive: {
    color: '#0B5FFF',
    fontWeight: '600',
  },
  dropdownArrow: {
    fontSize: 12,
    color: '#627D98',
  },
  dropdownMenu: {
    borderWidth: 1,
    borderColor: '#BCCCDC',
    borderRadius: 10,
    backgroundColor: '#FFFFFF',
    marginBottom: 8,
    overflow: 'hidden',
  },
  dropdownOption: {
    paddingHorizontal: 12,
    paddingVertical: 12,
    borderBottomWidth: 1,
    borderBottomColor: '#EEF2F6',
  },
  dropdownOptionActive: {
    backgroundColor: '#E6F0FF',
  },
  inputError: {
    borderColor: '#D64545',
    backgroundColor: '#FFF5F5',
  },
  input: {
    borderWidth: 1,
    borderColor: '#BCCCDC',
    borderRadius: 10,
    paddingHorizontal: 12,
    paddingVertical: 10,
    fontSize: 16,
    color: '#102A43',
    backgroundColor: '#F9FAFB',
    marginBottom: 8,
  },
  button: {
    backgroundColor: '#0B5FFF',
    borderRadius: 10,
    paddingVertical: 12,
    paddingHorizontal: 16,
    alignItems: 'center',
    marginBottom: 10,
  },
  buttonSecondary: {
    backgroundColor: '#FFFFFF',
    borderWidth: 1,
    borderColor: '#0B5FFF',
  },
  buttonDisabled: {
    opacity: 0.5,
  },
  buttonText: {
    color: '#FFFFFF',
    fontWeight: '700',
    fontSize: 14,
    textAlign: 'center',
  },
  buttonTextSecondary: {
    color: '#0B5FFF',
  },
  logBox: {
    backgroundColor: '#102A43',
    borderRadius: 12,
    padding: 14,
    minHeight: 140,
  },
  logLine: {
    color: '#D9E2EC',
    fontSize: 12,
    marginBottom: 6,
    fontFamily: 'monospace',
  },
  logSuccess: {
    color: '#3DDBA5',
  },
  logError: {
    color: '#FF8B8B',
  },
});
