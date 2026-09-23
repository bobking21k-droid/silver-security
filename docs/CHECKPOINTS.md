# Staged build checkpoints

1. **Scaffold (included):** XcodeGen project specification, hub UI, BLE adapter, detector, notification service, privacy manifest, Live Activity, tests, and constraints docs.
2. **Mac build:** Run `scripts/build-silver.sh`, generate the Xcode project, select a unique bundle identifier, and sign with the user’s Apple Developer team.
3. **Real-device validation:** Tune thresholds against benign BLE environments and controlled test advertisements. Do not infer attacker identity from heuristics alone.
4. **iOS 26 optimization (implemented, device validation pending):** The Live Activity monitoring workflow is present; validate screen-on, lock-screen, and display-sleep behavior on a physical iOS 26 device.
5. **Store readiness:** Add privacy manifest, accessibility pass, localization, App Review notes, and a privacy policy after deciding whether any analytics or persistence will be added.

Requires user: a Mac with Xcode, Apple Developer account for signing/TestFlight, and a real iPhone for Core Bluetooth testing.
