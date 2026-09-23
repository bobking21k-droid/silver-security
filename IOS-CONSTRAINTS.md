# Verified iOS constraints

The referenced conversation did not contain a completed deep-research report; it contained only a research-start acknowledgement and preliminary discussion. These constraints were independently checked against Apple Developer documentation on 2026-09-23.

## Allowed

- `CBCentralManager` can scan and deliver discovered peripherals plus advertisement data and RSSI through `centralManager(_:didDiscover:advertisementData:rssi:)`.
- Advertisement dictionaries can expose local name, service UUIDs, service data, manufacturer data, connectability, and transmit power when the system supplies those fields.
- `CBCentralManagerScanOptionAllowDuplicatesKey` requests an event for each received advertisement in the foreground. It has a battery cost.
- A `bluetooth-central` background mode can let the system wake an app for Core Bluetooth work, but background scanning is coalesced/throttled and is not a continuous radio tap.
- iOS 26 adds a documented Live Activity path: if a `CBManager` exists and the app starts a Live Activity before backgrounding, some foreground-like scan behavior is retained while the app is sufficiently in use. The device sleeping with the display off still changes the behavior.
- Local notifications can alert the user after notification authorization.
- App Intents can expose Silver actions to Shortcuts/Siri, but they do not grant control of the system Bluetooth radio.

## Not allowed or not exposed

- No public App Store API selectively filters or drops arbitrary BLE advertisements before iOS processes them.
- No public API suppresses Apple’s nearby-device pairing prompts for another device.
- `UIApplication.openSettingsURLString` opens Silver’s own Settings page; it is not a supported deep link to the system Bluetooth pane.
- Network Extension filters IP/network flows, not raw BLE advertisements.
- Turning Bluetooth off would disconnect legitimate Bluetooth audio and is not implemented as an automatic “block” action.

## Implications

BLE Shield is a detector and incident assistant. It can identify suspicious patterns, notify, preserve a short-lived local episode in memory, and guide the user to Apple’s supported settings. It cannot promise uninterrupted AirPods audio while the radio attack is active.

Sources: [Core Bluetooth](https://developer.apple.com/documentation/corebluetooth), [Core Bluetooth background processing](https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/CoreBluetooth_concepts/CoreBluetoothBackgroundProcessingForIOSApps/PerformingTasksWhileYourAppIsInTheBackground.html), [advertisement data](https://developer.apple.com/documentation/corebluetooth/advertising-data), [Bluetooth privacy key](https://developer.apple.com/documentation/bundleresources/information-property-list/nsbluetoothalwaysusagedescription), [Live Activities](https://developer.apple.com/documentation/activitykit/displaying-live-data-with-live-activities), [local notifications](https://developer.apple.com/documentation/usernotifications/asking-permission-to-use-notifications).
