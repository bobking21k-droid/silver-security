import Foundation

struct AdvertisementSample: Equatable {
    let timestamp: Date
    let peripheralID: UUID
    let rssi: Int
    let companyIdentifier: UInt16?
    let payloadDigest: UInt64
    let serviceCount: Int

    var looksLikeAppleVendorData: Bool { companyIdentifier == 0x004C }
}
