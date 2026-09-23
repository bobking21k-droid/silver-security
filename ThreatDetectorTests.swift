import XCTest
@testable import SilverSecurity

final class ThreatDetectorTests: XCTestCase {
    func testNormalSampleDoesNotRaiseIncident() {
        var detector = ThreatDetector()
        let assessment = detector.ingest(sample(at: .now, index: 0))
        XCTAssertEqual(assessment.level, .normal)
    }

    func testSustainedBurstRaisesHigh() {
        var detector = ThreatDetector()
        var result = ThreatAssessment(level: .normal, score: 0, eventsPerSecond: 0, sampleCount: 0, explanation: "")
        let start = Date()
        for i in 0..<40 { result = detector.ingest(sample(at: start.addingTimeInterval(Double(i) / 100), index: i, apple: true)) }
        XCTAssertGreaterThanOrEqual(result.level, .elevated)
        XCTAssertGreaterThan(result.score, 0)
    }

    func testOldSamplesAgeOut() {
        var detector = ThreatDetector()
        let now = Date()
        _ = detector.ingest(sample(at: now.addingTimeInterval(-5), index: 1))
        let result = detector.ingest(sample(at: now, index: 2))
        XCTAssertEqual(result.sampleCount, 1)
    }

    private func sample(at date: Date, index: Int, apple: Bool = false) -> AdvertisementSample {
        AdvertisementSample(timestamp: date, peripheralID: UUID(), rssi: -50, companyIdentifier: apple ? 0x004C : nil, payloadDigest: UInt64(index), serviceCount: 0)
    }
}
