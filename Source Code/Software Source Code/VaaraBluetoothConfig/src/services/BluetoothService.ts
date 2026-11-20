import { Platform } from 'react-native';
import RNBluetoothClassic, {
  BluetoothDevice,
  BluetoothEventSubscription,
} from 'react-native-bluetooth-classic';

import { DEVICE_CONFIG } from '../constants/device';
import { AppError, withTimeout, wrapUnknown } from '../utils/errors';
import { requestBluetoothPermissions } from '../utils/permissions';

const DISCOVERY_TIMEOUT_MS = 20000;
const PAIRING_TIMEOUT_MS = 60000;
const CONNECT_ATTEMPT_TIMEOUT_MS = 12000;
const MAX_RX_BUFFER = 4096;

function delay(ms: number): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms));
}

export type DeviceSummary = {
  name: string;
  address: string;
  bonded: boolean;
};

export function isVaaraName(name: string | undefined): boolean {
  return (name ?? '')
    .trim()
    .toUpperCase()
    .startsWith(DEVICE_CONFIG.namePrefix.toUpperCase());
}

class BluetoothService {
  private device: BluetoothDevice | null = null;
  private dataListener: { remove: () => void } | null = null;
  private onDataCallback: ((line: string) => void) | null = null;
  private onConnectionLostCallback: ((reason: string) => void) | null = null;
  private systemListeners: BluetoothEventSubscription[] = [];
  private rxBuffer = '';

  async ensureReady(): Promise<void> {
    try {
      const enabled = await RNBluetoothClassic.isBluetoothEnabled();
      if (!enabled) {
        const accepted = await RNBluetoothClassic.requestBluetoothEnabled();
        if (!accepted) {
          throw new AppError(
            'BLUETOOTH_OFF',
            'Bluetooth is turned off. Enable Bluetooth to continue.',
          );
        }
      }

      await requestBluetoothPermissions();
    } catch (error) {
      throw wrapUnknown(error, 'Failed to prepare Bluetooth.');
    }
  }

  openBluetoothSettings(): void {
    if (Platform.OS !== 'android') {
      return;
    }
    try {
      RNBluetoothClassic.openBluetoothSettings();
    } catch {
      // Some OEM builds block this intent; the user can open Settings manually.
    }
  }

  private toSummary(
    item: BluetoothDevice,
    bondedOverride?: boolean,
  ): DeviceSummary | null {
    if (!item?.address || !isVaaraName(item.name)) {
      return null;
    }

    return {
      name: item.name!.trim(),
      address: item.address,
      bonded: bondedOverride ?? item.bonded ?? false,
    };
  }

  private mergeDevices(
    bonded: BluetoothDevice[],
    discovered: BluetoothDevice[],
  ): DeviceSummary[] {
    const map = new Map<string, DeviceSummary>();

    for (const item of bonded) {
      const summary = this.toSummary(item, true);
      if (summary) {
        map.set(summary.address, summary);
      }
    }

    for (const item of discovered) {
      const summary = this.toSummary(item);
      if (!summary) {
        continue;
      }

      const existing = map.get(summary.address);
      map.set(summary.address, {
        ...summary,
        bonded: existing?.bonded ?? summary.bonded,
      });
    }

    return Array.from(map.values()).sort((a, b) => {
      if (a.bonded !== b.bonded) {
        return a.bonded ? -1 : 1;
      }
      return a.name.localeCompare(b.name);
    });
  }

  private async stopDiscovery(): Promise<void> {
    try {
      await RNBluetoothClassic.cancelDiscovery();
    } catch {
      // Discovery may already be stopped.
    }
  }

  /**
   * Lists only VAARA devices. Paired ones are reported through `onPartial`
   * right away so the list appears before the slower discovery finishes.
   */
  async listDevices(
    onPartial?: (devices: DeviceSummary[]) => void,
  ): Promise<DeviceSummary[]> {
    await this.ensureReady();

    let bonded: BluetoothDevice[] = [];
    let discovered: BluetoothDevice[] = [];
    let discoveryError: unknown = null;

    try {
      bonded = (await RNBluetoothClassic.getBondedDevices()) ?? [];
    } catch (error) {
      throw wrapUnknown(
        error,
        'Could not read paired Bluetooth devices. Check app permissions.',
      );
    }

    onPartial?.(this.mergeDevices(bonded, []));

    try {
      discovered =
        (await withTimeout(
          RNBluetoothClassic.startDiscovery(),
          DISCOVERY_TIMEOUT_MS,
          'Bluetooth scan took too long.',
        )) ?? [];
    } catch (error) {
      discoveryError = error;
    } finally {
      await this.stopDiscovery();
    }

    const devices = this.mergeDevices(bonded, discovered);

    if (devices.length === 0) {
      const hint =
        Platform.OS === 'android' && Number(Platform.Version) < 31
          ? ' Turn on phone Location (GPS), power on the VAARA device, then scan again.'
          : ' Power on the VAARA device, keep it near the phone, then scan again.';

      throw new AppError(
        discoveryError ? 'DISCOVERY_FAILED' : 'DEVICE_NOT_FOUND',
        `No VAARA devices found.${hint}`,
        { cause: discoveryError ?? undefined },
      );
    }

    return devices;
  }

  /** Prefers the exact configured name, then any paired VAARA, then any VAARA */
  findVaaraDevice(devices: DeviceSummary[]): DeviceSummary | undefined {
    const exact = DEVICE_CONFIG.name.toUpperCase();
    return (
      devices.find(device => device.name.toUpperCase() === exact) ??
      devices.find(device => device.bonded) ??
      devices[0]
    );
  }

  onData(callback: (line: string) => void): void {
    this.onDataCallback = callback;
  }

  /** Called when the device drops the connection or Bluetooth is turned off */
  onConnectionLost(callback: (reason: string) => void): void {
    this.onConnectionLostCallback = callback;
    this.attachSystemListeners();
  }

  private attachSystemListeners(): void {
    if (this.systemListeners.length > 0) {
      return;
    }

    try {
      this.systemListeners.push(
        RNBluetoothClassic.onDeviceDisconnected(event => {
          const address = event?.device?.address;
          if (this.device && (!address || address === this.device.address)) {
            this.handleConnectionLost('The VAARA device disconnected.');
          }
        }),
        RNBluetoothClassic.onBluetoothDisabled(() => {
          if (this.device) {
            this.handleConnectionLost('Bluetooth was turned off.');
          }
        }),
      );
    } catch {
      // Older native module without events; write failures still detect loss.
    }
  }

  removeSystemListeners(): void {
    for (const listener of this.systemListeners) {
      try {
        listener.remove();
      } catch {
        // Already removed.
      }
    }
    this.systemListeners = [];
  }

  private handleConnectionLost(reason: string, notify = true): void {
    this.detachDataListener();
    this.rxBuffer = '';
    this.device = null;
    if (notify) {
      this.onConnectionLostCallback?.(reason);
    }
  }

  private attachDataListener(device: BluetoothDevice): void {
    this.detachDataListener();
    this.rxBuffer = '';

    this.dataListener = device.onDataReceived((event: { data: string }) => {
      try {
        this.rxBuffer += event?.data ?? '';
        const parts = this.rxBuffer.split(/\r?\n/);
        this.rxBuffer = (parts.pop() ?? '').slice(-MAX_RX_BUFFER);

        for (const part of parts) {
          const line = part.trim();
          if (line) {
            this.onDataCallback?.(line);
          }
        }
      } catch {
        this.rxBuffer = '';
      }
    });
  }

  private detachDataListener(): void {
    if (this.dataListener) {
      try {
        this.dataListener.remove();
      } catch {
        // Listener already gone with the socket.
      }
      this.dataListener = null;
    }
  }

  private async closeSocket(address: string): Promise<void> {
    try {
      await RNBluetoothClassic.disconnectFromDevice(address);
    } catch {
      // No socket was open.
    }
  }

  async pair(target: DeviceSummary): Promise<DeviceSummary> {
    await this.ensureReady();
    await this.stopDiscovery();

    try {
      await withTimeout(
        RNBluetoothClassic.pairDevice(target.address),
        PAIRING_TIMEOUT_MS,
        'Pairing took too long.',
      );
    } catch (error) {
      throw new AppError(
        'PAIRING_FAILED',
        `Could not pair with ${target.name}. Accept the pairing request and enter PIN ${DEVICE_CONFIG.pin} when asked, then try again.`,
        { cause: error },
      );
    }

    return { ...target, bonded: true };
  }

  async connect(target: DeviceSummary): Promise<DeviceSummary> {
    await this.ensureReady();
    await this.disconnect();
    await this.stopDiscovery();

    const device = target.bonded ? target : await this.pair(target);

    // Android often fails the first RFCOMM attempt with "read failed, socket
    // might closed"; the ESP32 SPP server needs a moment before a retry.
    const attempts = [true, false, true, false];
    let lastError: unknown = null;

    for (let i = 0; i < attempts.length; i++) {
      if (i > 0) {
        await this.closeSocket(device.address);
        await delay(800);
      }

      try {
        const connected = await withTimeout(
          this.connectWithOptions(device.address, attempts[i]),
          CONNECT_ATTEMPT_TIMEOUT_MS,
          'Connection attempt timed out.',
        );
        if (connected) {
          this.device = connected;
          this.attachDataListener(connected);
          return device;
        }
      } catch (error) {
        lastError = error;
      }
    }

    this.device = null;
    await this.closeSocket(device.address);
    throw new AppError(
      'CONNECTION_FAILED',
      `Could not connect to ${device.name}. Make sure it is powered on, nearby and not connected to another phone, then tap Connect again.`,
      { cause: lastError },
    );
  }

  private connectWithOptions(
    address: string,
    secure: boolean,
  ): Promise<BluetoothDevice | false> {
    return RNBluetoothClassic.connectToDevice(address, {
      connectorType: 'rfcomm',
      secure,
    });
  }

  async connectToVaara(devices?: DeviceSummary[]): Promise<DeviceSummary> {
    const list = devices ?? (await this.listDevices());
    const vaara = this.findVaaraDevice(list);

    if (!vaara) {
      throw new AppError(
        'DEVICE_NOT_FOUND',
        'No VAARA device was found. Power it on, keep it near the phone, then scan again.',
      );
    }

    return this.connect(vaara);
  }

  isConnected(): boolean {
    return this.device !== null;
  }

  async sendCommand(command: string): Promise<void> {
    const device = this.device;
    if (!device) {
      throw new AppError('NOT_CONNECTED', 'Not connected to the device.');
    }

    let written = false;
    let cause: unknown = null;
    try {
      written = await withTimeout(
        device.write(`${command}\r\n`),
        5000,
        'Sending took too long.',
      );
    } catch (error) {
      cause = error;
    }

    if (written) {
      return;
    }

    let stillConnected = false;
    try {
      stillConnected = await RNBluetoothClassic.isDeviceConnected(
        device.address,
      );
    } catch {
      stillConnected = false;
    }

    if (!stillConnected) {
      this.handleConnectionLost(
        'Connection to the VAARA device was lost.',
        false,
      );
      throw new AppError(
        'CONNECTION_LOST',
        'Connection to the VAARA device was lost. Tap Connect VAARA to reconnect.',
        { cause },
      );
    }

    throw new AppError('WRITE_FAILED', 'Failed to send command to device.', {
      cause,
    });
  }

  async disconnect(): Promise<void> {
    this.detachDataListener();
    this.rxBuffer = '';

    if (!this.device) {
      return;
    }

    const address = this.device.address;
    this.device = null;
    await this.closeSocket(address);
  }
}

export const bluetoothService = new BluetoothService();
