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
        guard !hasLiveActivityBackgroundBehavior else { return }
        central?.stopScan()
        if requested { state = .starting }
    }

    func resumeIfNeeded() {
        guard requested else { return }
        startScanIfReady()
    }

    private var hasLiveActivityBackgroundBehavior: Bool {
        ProcessInfo.processInfo.isOperatingSystemAtLeast(OperatingSystemVersion(majorVersion: 26, minorVersion: 0, patchVersion: 0))
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        startScanIfReady()
    }

    private func startScanIfReady() {
        guard requested, let central else { return }
        guard central.state == .poweredOn else {
            state = .unavailable("Bluetooth is unavailable or permission is not granted.")
            return
        }
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
        state = .monitoring
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
