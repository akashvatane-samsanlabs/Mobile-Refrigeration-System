declare module 'react-native-bluetooth-classic' {
  export type BluetoothDevice = {
    name?: string;
    address: string;
    bonded?: boolean;
    write: (data: string) => Promise<boolean>;
    onDataReceived: (callback: (event: { data: string }) => void) => {
      remove: () => void;
    };
  };

  export type ConnectOptions = {
    connectorType?: 'rfcomm' | 'ble';
    secure?: boolean;
  };

  export type BluetoothEventSubscription = {
    remove: () => void;
  };

  export type BluetoothDeviceEvent = {
    device?: { address?: string; name?: string };
  };

  const RNBluetoothClassic: {
    isBluetoothEnabled: () => Promise<boolean>;
    requestBluetoothEnabled: () => Promise<boolean>;
    getBondedDevices: () => Promise<BluetoothDevice[]>;
    startDiscovery: () => Promise<BluetoothDevice[]>;
    cancelDiscovery: () => Promise<boolean>;
    pairDevice: (address: string) => Promise<BluetoothDevice>;
    connectToDevice: (
      address: string,
      options?: ConnectOptions,
    ) => Promise<BluetoothDevice | false>;
    disconnectFromDevice: (address: string) => Promise<boolean>;
    isDeviceConnected: (address: string) => Promise<boolean>;
    getConnectedDevices: () => Promise<BluetoothDevice[]>;
    openBluetoothSettings: () => void;
    onDeviceDisconnected: (
      listener: (event: BluetoothDeviceEvent) => void,
    ) => BluetoothEventSubscription;
    onBluetoothDisabled: (listener: () => void) => BluetoothEventSubscription;
  };

  export default RNBluetoothClassic;
}
