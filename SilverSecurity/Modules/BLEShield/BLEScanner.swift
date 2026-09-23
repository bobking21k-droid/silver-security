import CoreBluetooth
import Combine
import Foundation

@MainActor
final class BLEScanner: NSObject, ObservableObject, CBCentralManagerDelegate {
    enum State: Equatable { case idle, starting, monitoring, unavailable(String) }

    @Published private(set) var state: State = .idle
    var onSample: ((AdvertisementSample) -> Void)?
    private var central: CBCentralManager?
    private var requested = false

    func start() {
        requested = true
        state = .starting
        if central == nil { central = CBCentralManager(delegate: self, queue: .main, options: [CBCentralManagerOptionShowPowerAlertKey: false]) }
        else { startScanIfReady() }
    }

    func stop() {
        requested = false
        central?.stopScan()
        state = .idle
    }

    func pauseForBackgroundIfNeeded() {
        central?.stopScan()
        if requested { state = .starting }
    }

    func resumeIfNeeded() {
        guard requested else { return }
        startScanIfReady()
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        startScanIfReady()
    }

    private func startScanIfReady() {
        guard requested, let central else { return }
        guard central.state == .poweredOn else {
            state = .unavailable(Self.statusMessage(for: central))
            return
        }
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
        state = .monitoring
    }

    private static func statusMessage(for central: CBCentralManager) -> String {
        switch central.authorization {
        case .denied:
            return "Bluetooth access is off. Enable it in Settings to observe nearby BLE."
        case .restricted:
            return "Bluetooth access is restricted on this device."
        case .allowedAlways:
            switch central.state {
            case .poweredOff: return "Bluetooth is off. Turn it on to start observation."
            case .unauthorized: return "Bluetooth permission is not granted."
            case .unsupported: return "This device does not support Bluetooth Low Energy."
            default: return "Bluetooth is becoming available…"
            }
        @unknown default:
            return "Bluetooth permission or availability needs attention."
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        let manufacturer = advertisementData[CBAdvertisementDataManufacturerDataKey] as? Data
        let serviceUUIDs = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID]
        let sample = AdvertisementSample(timestamp: .now, peripheralID: peripheral.identifier, rssi: RSSI.intValue,
                                          companyIdentifier: manufacturer.flatMap(Self.companyIdentifier),
                                          payloadDigest: manufacturer?.silverDigest ?? Self.digest(advertisementData),
                                          serviceCount: serviceUUIDs?.count ?? 0)
        onSample?(sample)
    }

    private static func companyIdentifier(_ data: Data) -> UInt16? {
        guard data.count >= 2 else { return nil }
        return UInt16(data[data.startIndex]) | (UInt16(data[data.index(data.startIndex, offsetBy: 1)]) << 8)
    }

    private static func digest(_ advertisementData: [String: Any]) -> UInt64 {
        var value: UInt64 = 5381
        for key in advertisementData.keys.sorted() { for byte in key.utf8 { value = (value &* 33) ^ UInt64(byte) } }
        return value
    }
}
