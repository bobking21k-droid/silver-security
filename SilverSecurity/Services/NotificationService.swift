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
        do {
            let granted = try await UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .badge])
            if granted { scheduleAltStoreRefreshReminder() }
            return granted
        }
        catch { return false }
    }

    /// Free Apple-ID installs expire after seven days. This reminder helps the
    /// user keep AltServer available; it does not attempt to renew signing.
    func scheduleAltStoreRefreshReminder() {
        let content = UNMutableNotificationContent()
        content.title = "Refresh Silver Security"
        content.body = "Open AltStore while your Windows laptop is nearby to renew the free 7-day install."
        content.sound = .default
        let trigger = UNTimeIntervalNotificationTrigger(timeInterval: 6 * 24 * 60 * 60, repeats: true)
        let request = UNNotificationRequest(identifier: "altstore-refresh-reminder", content: content, trigger: trigger)
        let center = UNUserNotificationCenter.current()
        center.removePendingNotificationRequests(withIdentifiers: [request.identifier])
        center.add(request) { _ in }
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
