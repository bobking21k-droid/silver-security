import Foundation

struct ModuleDescriptor: Identifiable {
    let id: String
    let title: String
    let subtitle: String
    let symbol: String
    let availability: Availability

    enum Availability { case available, planned }
}

enum SilverModules {
    static let all = [
        ModuleDescriptor(id: "ble-shield", title: "BLE Shield", subtitle: "Detect abnormal nearby Bluetooth bursts", symbol: "dot.radiowaves.left.and.right", availability: .available),
        ModuleDescriptor(id: "airdrop-watch", title: "Nearby Share Watch", subtitle: "Defensive awareness for nearby sharing", symbol: "person.2.wave.2", availability: .planned),
        ModuleDescriptor(id: "device-integrity", title: "Device Integrity", subtitle: "On-device checks and safety guidance", symbol: "checkmark.shield", availability: .planned)
    ]
}
