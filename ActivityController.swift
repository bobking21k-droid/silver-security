import ActivityKit
import Foundation

@MainActor
final class ActivityController {
    private var activity: Activity<SilverShieldAttributes>?

    func start() {
        guard ActivityAuthorizationInfo().areActivitiesEnabled else { return }
        guard activity == nil else { return }
        let attributes = SilverShieldAttributes()
        let state = SilverShieldAttributes.ContentState()
        activity = try? Activity.request(attributes: attributes, content: ActivityContent(state: state, staleDate: nil))
    }

    func update(_ assessment: ThreatAssessment) {
        guard let activity else { return }
        let state = SilverShieldAttributes.ContentState(status: "Monitoring", severity: assessment.level == .high ? "High" : assessment.level == .elevated ? "Elevated" : "Normal", eventsPerSecond: assessment.eventsPerSecond)
        Task { await activity.update(ActivityContent(state: state, staleDate: Date.now.addingTimeInterval(30))) }
    }

    func end() {
        guard let activity else { return }
        self.activity = nil
        Task { await activity.end(nil, dismissalPolicy: .immediate) }
    }
}
