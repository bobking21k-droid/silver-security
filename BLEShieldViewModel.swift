import Foundation
import Combine
import SwiftUI

@MainActor
final class BLEShieldViewModel: ObservableObject {
    struct DetectionRecord: Identifiable {
        let id = UUID()
        let time: Date
        let level: ThreatLevel
        let score: Int
        let explanation: String
    }

    @Published private(set) var isMonitoring = false
    @Published private(set) var assessment = ThreatAssessment(level: .normal, score: 0, eventsPerSecond: 0, sampleCount: 0, explanation: "Start a session to observe nearby BLE activity.")
    @Published private(set) var lastDetection: Date?
    @Published private(set) var recentDetections: [DetectionRecord] = []
    @Published private(set) var scannerState: BLEScanner.State = .idle

    private let scanner = BLEScanner()
    private let notifications = NotificationService()
    private let activity = ActivityController()
    private var detector = ThreatDetector()
    private var lastNotificationAt: Date?
    private var lastActivityUpdateAt: Date?
    private var demoTask: Task<Void, Never>?

    init() {
        scanner.onSample = { [weak self] sample in self?.receive(sample) }
        scanner.$state.assign(to: &$scannerState)
    }

    func start() async {
        demoTask?.cancel()
        demoTask = nil
        _ = await notifications.requestAuthorization()
        detector.reset()
        recentDetections.removeAll()
        lastActivityUpdateAt = nil
        isMonitoring = true
        activity.start()
        scanner.start()
    }

    func stop() {
        demoTask?.cancel()
        demoTask = nil
        scanner.stop()
        activity.end()
        isMonitoring = false
        detector.reset()
        recentDetections.removeAll()
        lastActivityUpdateAt = nil
        assessment = ThreatAssessment(level: .normal, score: 0, eventsPerSecond: 0, sampleCount: 0, explanation: "Monitoring stopped.")
    }

    #if DEBUG
    /// Simulator-safe UI validation. It feeds derived samples into the same detector path;
    /// it never touches Core Bluetooth and is excluded from release builds.
    func runDemoBurst() {
        demoTask?.cancel()
        scanner.stop()
        detector.reset()
        isMonitoring = true
        demoTask = Task { @MainActor [weak self] in
            guard let self else { return }
            let source = UUID()
            for index in 0..<42 {
                if Task.isCancelled { return }
                let sample = AdvertisementSample(timestamp: .now, peripheralID: source, rssi: -48,
                                                  companyIdentifier: 0x004C, payloadDigest: UInt64(index), serviceCount: 0)
                self.receive(sample)
                try? await Task.sleep(nanoseconds: 25_000_000)
            }
        }
    }
    #endif

    func scenePhaseChanged(_ phase: ScenePhase) {
        if phase == .active { scanner.resumeIfNeeded() }
        else { scanner.pauseForBackgroundIfNeeded() }
    }

    private func receive(_ sample: AdvertisementSample) {
        let next = detector.ingest(sample)
        assessment = next
        if lastActivityUpdateAt.map({ sample.timestamp.timeIntervalSince($0) >= 0.5 }) != false {
            lastActivityUpdateAt = sample.timestamp
            activity.update(next)
        }
        guard next.level >= .elevated else { return }
        lastDetection = sample.timestamp
        recentDetections.insert(DetectionRecord(time: sample.timestamp, level: next.level, score: next.score, explanation: next.explanation), at: 0)
        if recentDetections.count > 20 { recentDetections.removeLast() }
        if lastNotificationAt.map({ sample.timestamp.timeIntervalSince($0) < 30 }) != true {
            lastNotificationAt = sample.timestamp
            Task { await notifications.notifyDetection(next) }
        }
    }
}
