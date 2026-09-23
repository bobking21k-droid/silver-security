import Foundation

enum ThreatLevel: Int, Comparable {
    case normal = 0
    case elevated = 1
    case high = 2

    static func < (lhs: ThreatLevel, rhs: ThreatLevel) -> Bool { lhs.rawValue < rhs.rawValue }
}

struct ThreatAssessment: Equatable {
    let level: ThreatLevel
    let score: Int
    let eventsPerSecond: Int
    let sampleCount: Int
    let explanation: String
}

struct ThreatDetector {
    private(set) var samples: [AdvertisementSample] = []
    private let retention: TimeInterval = 4

    mutating func ingest(_ sample: AdvertisementSample) -> ThreatAssessment {
        samples.append(sample)
        samples.removeAll { sample.timestamp.timeIntervalSince($0.timestamp) > retention || $0.timestamp > sample.timestamp }

        let oneSecond = samples.filter { sample.timestamp.timeIntervalSince($0.timestamp) <= 1 }
        let threeSeconds = samples.filter { sample.timestamp.timeIntervalSince($0.timestamp) <= 3 }
        let counts = Dictionary(grouping: oneSecond, by: \.peripheralID).values.map(\.count)
        let churn = Dictionary(grouping: threeSeconds, by: \.peripheralID).map { _, values in Set(values.map(\.payloadDigest)).count }.max() ?? 0
        let appleBurst = oneSecond.filter(\.looksLikeAppleVendorData).count

        var score = 0
        if oneSecond.count >= 35 { score += 45 }
        if (counts.max() ?? 0) >= 18 { score += 25 }
        if churn >= 12 { score += 20 }
        if appleBurst >= 16 { score += 20 }
        if oneSecond.count >= 30 && Set(oneSecond.map(\.peripheralID)).count >= 20 { score += 15 }
        score = min(score, 100)

        let level: ThreatLevel = score >= 70 ? .high : (score >= 35 ? .elevated : .normal)
        let explanation: String
        switch level {
        case .normal: explanation = "No unusual burst pattern observed."
        case .elevated: explanation = "Advertisement rate or payload churn is higher than a typical nearby device."
        case .high: explanation = "A sustained, high-rate advertisement burst was detected. This is an observation, not proof of a specific device or attacker."
        }
        return ThreatAssessment(level: level, score: score, eventsPerSecond: oneSecond.count, sampleCount: samples.count, explanation: explanation)
    }

    mutating func reset() { samples.removeAll(keepingCapacity: true) }
}

extension Data {
    /// A small non-persistent digest is sufficient for short-window churn detection.
    var silverDigest: UInt64 {
        var value: UInt64 = 1469598103934665603
        for byte in self { value = (value ^ UInt64(byte)) &* 1099511628211 }
        return value
    }
}
