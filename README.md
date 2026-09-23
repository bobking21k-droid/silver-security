# Silver Security

Silver Security is an App Store-safe iPhone defensive app scaffold. The first module, BLE Shield, detects abnormal Bluetooth Low Energy advertisement bursts and explains what the user can do. It does **not** claim to block advertisements or suppress Apple’s system pairing UI.

## What is implemented

- SwiftUI hub designed for additional defensive modules.
- Core Bluetooth central scan with duplicate discoveries in the foreground.
- Short-window, in-memory anomaly scoring for high-rate and payload-churn bursts.
- Quiet local notification path for a detected episode.
- ActivityKit Live Activity status surface for session status; it does not promise continuous background scanning.
- App Store-safe permission strings and an explicit, conservative `bluetooth-central` declaration.
- Pure unit-testable detector with no radio dependency.

## Build

This workspace is Windows-based, so Xcode cannot run here. On a Mac with Xcode and XcodeGen installed:

```sh
chmod +x scripts/build-silver.sh
./scripts/build-silver.sh
open SilverSecurity.xcodeproj
```

Then select a real iPhone. The simulator cannot provide meaningful BLE-radio validation.

## Product truth

Detection is not filtering. iOS public APIs expose Core Bluetooth discovery to an app, but do not let an App Store app drop another transmitter’s packets, prevent system nearby-device prompts, or toggle the system Bluetooth radio. The UI therefore uses “Detected” and “Observed,” never “Blocked.”

## Free-install refresh

When installed with a free Apple ID through AltStore, iOS app signing lasts seven days. Silver requests a local reminder on day six, but only AltServer can renew the signature. Keep AltServer running on the Windows laptop and enable AltStore Background Refresh; the phone and laptop must share Wi-Fi or be connected by USB.
