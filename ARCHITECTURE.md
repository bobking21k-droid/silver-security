# Silver Security architecture

`SilverSecurityApp` owns the hub and injects one `BLEShieldViewModel`. A future module conforms conceptually to the same module-card pattern without sharing radio state.

```text
SwiftUI hub
  └─ BLE Shield view model
       ├─ BLEScanner (Core Bluetooth adapter)
       ├─ ThreatDetector (pure rolling-window rules)
       ├─ NotificationService (local, user-authorized)
       └─ ActivityController (ActivityKit status only)
```

The scanner maps Apple dictionaries to a small `AdvertisementSample`: timestamp, ephemeral peripheral UUID, RSSI, service count, company identifier, and a non-persistent payload digest. The detector holds at most four seconds of samples and emits a score. No names, raw payloads, or location are stored.

The view model keeps up to 20 elevated/high observations in memory for the current session so the user can see a small incident timeline. The timeline is cleared when monitoring starts or stops and is never written to disk.

Detector scoring remains per advertisement, while ActivityKit updates are throttled to at most twice per second to avoid flooding the system with status updates during a burst.

The first release has three explicit states: Ready, Monitoring, and Detected. “Detected” means a statistical anomaly, not proof of a Flipper Zero or any specific attacker. Signatures are heuristics and must be tuned with real-device captures before shipping.
