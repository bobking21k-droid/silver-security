import ActivityKit
import SwiftUI
import WidgetKit

struct SilverSecurityLiveActivity: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for: SilverShieldAttributes.self) { context in
            HStack {
                Image(systemName: "checkmark.shield.fill").foregroundStyle(.green)
                VStack(alignment: .leading) {
                    Text("Silver BLE Shield").font(.headline)
                    Text("\(context.state.severity) · \(context.state.eventsPerSecond)/s observed").font(.caption)
                }
                Spacer()
            }.padding()
        } dynamicIsland: { context in
            DynamicIsland {
                DynamicIslandExpandedRegion(.leading) { Image(systemName: "checkmark.shield.fill") }
                DynamicIslandExpandedRegion(.center) { Text("BLE \(context.state.severity)") }
                DynamicIslandExpandedRegion(.trailing) { Text("\(context.state.eventsPerSecond)/s") }
            } compactLeading: { Image(systemName: "shield") }
              compactTrailing: { Text(context.state.severity.prefix(1)) }
              minimal: { Image(systemName: "shield") }
        }
    }
}

@main
struct SilverSecurityWidgetBundle: WidgetBundle {
    var body: some Widget { SilverSecurityLiveActivity() }
}
