import ActivityKit
import Foundation

public struct SilverShieldAttributes: ActivityAttributes {
    public struct ContentState: Codable, Hashable {
        public var status: String
        public var severity: String
        public var eventsPerSecond: Int
        public var observedAt: Date

        public init(status: String = "Monitoring", severity: String = "Normal", eventsPerSecond: Int = 0, observedAt: Date = .now) {
            self.status = status
            self.severity = severity
            self.eventsPerSecond = eventsPerSecond
            self.observedAt = observedAt
        }
    }

    public var startedAt: Date
    public init(startedAt: Date = .now) { self.startedAt = startedAt }
}
