# VAARA Bluetooth Config

Android React Native app to configure **VAARA_V8.0** over Bluetooth Classic SPP.

## Device protocol (ESP32 firmware)

| Command | Example | Meaning |
|---------|---------|---------|
| Max temperature | `T+200` | Maximum temperature in tenths of °C (200 = 20.0°C) |
| Min temperature | `T-100` | Minimum temperature in tenths of °C (100 = 10.0°C) |
| Mode | `E20` / `N20` / `P20` / `A20` | ECO / Normal / Power / Auto mode with a value 0–100 |
| Pairing PIN | `1234` | Fixed Bluetooth PIN |

## Prerequisites

- Node.js 22.13+ recommended
- Android Studio + JDK 17
- Android phone with Bluetooth Classic support
- **At least 2–3 GB free disk space** on `C:` for `node_modules`

## Setup

```bash
cd VaaraBluetoothConfig
npm install
```

## Run on Android

1. Enable USB debugging on the phone.
2. Pair **VAARA_V8.0** in Android Bluetooth settings (PIN `1234`).
3. Start Metro:

```bash
npm start
```

4. In another terminal:

```bash
npm run android
```

## App features

- Scan and list Bluetooth devices
- Auto-find `VAARA_V8.0`
- Set min/max temperature (°C) and operating mode (ECO / Normal / Power / Auto)
- Input validation and error messages
- Live RX/TX log from the device

## Firmware note

Your firmware stores the temperature in `bt_temp` but does not yet apply it to `main_glob_limitTemp`. Apply it inside `process_received_line()` after a successful command so the mobile app changes take effect on the hardware.

## Platform support

- **Android**: Full SPP support
- **iOS**: Not supported (iOS does not expose generic Bluetooth Classic SPP to third-party apps)
