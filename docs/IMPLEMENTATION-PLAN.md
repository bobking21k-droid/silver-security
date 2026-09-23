# Implementation plan

## Targets and source layout

`SilverSecurity` is the iOS app target. `SilverSecurityWidget` is a WidgetKit extension containing the BLE Shield Live Activity. `SilverSecurityTests` runs the detector without Core Bluetooth. The hub lives under `App/` and `Core/`; each defensive module owns its UI, adapter, model, and detector under `Modules/`.

## Swift/SwiftUI seams

- `BLEScanner` is the only type that touches Core Bluetooth.
- `AdvertisementSample` is the privacy-minimized boundary object.
- `ThreatDetector` is a value type with deterministic rolling-window behavior.
- `BLEShieldViewModel` coordinates scanning, scoring, notifications, and the Live Activity.
- `BLEShieldView` renders status and user guidance; it never presents a “block” result.
- `SilverShieldAttributes` is shared by the app and WidgetKit extension.

## Permissions and entitlements

Required now:

- `NSBluetoothAlwaysUsageDescription` in the app Info.plist.
- `bluetooth-central` in `UIBackgroundModes`, justified by a user-started monitoring session.
- `NSSupportsLiveActivities = YES` for the status surface.
- No special Bluetooth entitlement, no Network Extension entitlement, no location permission, no microphone permission, no push entitlement, and no private API.

Notification authorization is requested only when the user starts monitoring. The app asks for `.alert` and `.badge`, not sound, so a detection does not intentionally interrupt music. The notification delegate opts into a banner while the app is foregrounded.

## Detection logic

The scanner requests duplicate discoveries while foregrounded. The detector retains four seconds of derived samples and scores: at least 35 events in one second, at least 18 events from one ephemeral peripheral in one second, at least 12 distinct payload digests from one peripheral in three seconds, and a high-rate burst using the Bluetooth SIG Apple company identifier. These are indicators, not an attacker classifier. Thresholds must be calibrated with benign captures before release.

## Mitigation logic that iOS permits

When elevated, Silver updates the UI and rate-limited local notification. When high, it provides the same observation plus guidance to keep Silver open and use Apple’s own Bluetooth controls only as a last resort. Silver never attempts to connect to an unknown peripheral, modify another device, invoke private Settings URLs, or claim that it stopped system prompts. Any future selective mitigation must be gated on a new, documented Apple API and reviewed separately.
