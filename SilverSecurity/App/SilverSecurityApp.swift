import SwiftUI

@main
struct SilverSecurityApp: App {
    @Environment(\.scenePhase) private var scenePhase
    @StateObject private var shield = BLEShieldViewModel()

    var body: some Scene {
        WindowGroup {
            HubView()
                .environmentObject(shield)
                .onChange(of: scenePhase) { _, newPhase in shield.scenePhaseChanged(newPhase) }
        }
    }
}
