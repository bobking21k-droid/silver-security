# Test plan

## Unit tests (CI-safe)

- Empty stream remains `.normal`.
- A sustained high-rate stream reaches `.elevated` or higher.
- Repeated payload churn from one peripheral reaches `.high`.
- Samples older than four seconds age out.
- A single normal advertisement never raises an incident.
- Detector does not retain raw manufacturer bytes.

## Simulator smoke test (Debug builds)

Use “Run simulator demo” in BLE Shield. It feeds a synthetic high-rate, Apple-company-ID burst through the same detector and UI path without touching Core Bluetooth. This validates presentation and notification wiring; it is not evidence about real radio behavior.

## Device tests (requires Mac, Xcode, and two iPhones or an approved BLE test peripheral)

1. Grant Bluetooth permission; start and stop a monitoring session.
2. Confirm AirPods audio is not touched by Silver while scanning.
3. Confirm a controlled high-rate advertisement test produces an in-app event and, after notification permission, a quiet notification.
4. Background the app with a Live Activity on an iOS 26 device; measure event delivery while the screen is on/lock screen and again after display sleep. Record limits rather than treating missed events as a bug.
5. Verify the app never labels an observation “blocked,” never connects to unknown peripherals, and does not open private Settings URLs.
6. Test denied Bluetooth and notification permission paths.

## App Review checks

- Explain Bluetooth use in the permission string and review notes.
- Make monitoring user-started and session-based.
- Disclose that detection is best effort and does not filter system UI.
- Do not market the app as an AirPods or Flipper blocker.
