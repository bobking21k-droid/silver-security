import Foundation
import UserNotifications

final class NotificationService: NSObject, UNUserNotificationCenterDelegate {
    override init() {
        super.init()
        UNUserNotificationCenter.current().delegate = self
    }

    func userNotificationCenter(_ center: UNUserNotificationCenter,
                                willPresent notification: UNNotification) async -> UNNotificationPresentationOptions {
        [.banner, .badge, .list]
    }

    func requestAuthorization() async -> Bool {
        do { return try await UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .badge]) }
        catch { return false }
    }

    func notifyDetection(_ assessment: ThreatAssessment) async {
        let content = UNMutableNotificationContent()
        content.title = "Silver detected a BLE burst"
        content.body = assessment.explanation
        content.badge = 1
        let request = UNNotificationRequest(identifier: "ble-detection-\(UUID().uuidString)", content: content, trigger: nil)
        try? await UNUserNotificationCenter.current().add(request)
    }
}
