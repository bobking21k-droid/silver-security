import SwiftUI

struct HubView: View {
    @State private var showingPrivacyLimits = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    VStack(alignment: .leading, spacing: 6) {
                        Text("Silver Security").font(.largeTitle.bold())
                        Text("A calm defensive hub for the signals around you.").foregroundStyle(.secondary)
                        Text("Version \(Bundle.main.shortVersionString) (\(Bundle.main.buildNumber))")
                            .font(.caption)
                            .foregroundStyle(.tertiary)
                    }
                    ForEach(SilverModules.all) { module in
                        if module.availability == .available {
                            NavigationLink { BLEShieldView() } label: { ModuleCard(module: module) }
                                .buttonStyle(.plain)
                        } else { ModuleCard(module: module) }
                    }
                    VStack(alignment: .leading, spacing: 8) {
                        Label("Music-safe observation", systemImage: "music.note")
                            .font(.headline)
                        Text("Silver watches BLE advertisements passively and does not intentionally disconnect AirPods or change the system Bluetooth radio. Radio conditions can still affect audio; Silver reports suspicious bursts for you to act on.")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        if let guideURL = URL(string: "https://faq.altstore.io/altstore-classic/altserver") {
                            Link("AltStore refresh guide", destination: guideURL)
                                .font(.caption.weight(.semibold))
                        }
                    }
                    .padding()
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(.green.opacity(0.10))
                    .clipShape(RoundedRectangle(cornerRadius: 16))
                    Text("Silver observes. It does not control Apple’s Bluetooth system UI or claim to block radio advertisements.")
                        .font(.footnote).foregroundStyle(.secondary).padding(.top, 4)
                    Button { showingPrivacyLimits = true } label: {
                        Label("Privacy & limits", systemImage: "lock.shield")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                }.padding()
            }
            .navigationTitle("Hub")
            .sheet(isPresented: $showingPrivacyLimits) {
                PrivacyLimitsView()
            }
        }
    }
}

private struct PrivacyLimitsView: View {
    var body: some View {
        NavigationStack {
            List {
                Section("What Silver does") {
                    Label("Observes nearby BLE advertisements while a session is active.", systemImage: "antenna.radiowaves.left.and.right")
                    Label("Scores rate and payload patterns locally on the phone.", systemImage: "chart.line.uptrend.xyaxis")
                    Label("Shows an alert and optional local notification for elevated activity.", systemImage: "bell.badge")
                }
                Section("What Silver cannot do") {
                    Label("It cannot selectively block advertisements or dismiss Apple system prompts.", systemImage: "nosign")
                    Label("It does not disconnect AirPods or switch off the system Bluetooth radio.", systemImage: "airpods")
                    Label("Detection is not proof of intent; nearby devices can create benign bursts.", systemImage: "exclamationmark.triangle")
                }
                Section("Privacy") {
                    Label("Observation samples are processed locally and are not uploaded by Silver.", systemImage: "iphone")
                    Label("Bluetooth and notification access are requested only when you start monitoring.", systemImage: "checkmark.shield")
                }
            }
            .navigationTitle("Privacy & limits")
            .navigationBarTitleDisplayMode(.inline)
        }
    }
}

private extension Bundle {
    var shortVersionString: String { infoDictionary?["CFBundleShortVersionString"] as? String ?? "development" }
    var buildNumber: String { infoDictionary?["CFBundleVersion"] as? String ?? "0" }
}

private struct ModuleCard: View {
    let module: ModuleDescriptor
    var body: some View {
        HStack(spacing: 14) {
            Image(systemName: module.symbol).font(.title2).frame(width: 38, height: 38).background(.tint.opacity(0.12)).clipShape(RoundedRectangle(cornerRadius: 10))
            VStack(alignment: .leading, spacing: 3) {
                Text(module.title).font(.headline)
                Text(module.subtitle).font(.subheadline).foregroundStyle(.secondary)
            }
            Spacer()
            if module.availability == .planned { Text("Soon").font(.caption).foregroundStyle(.secondary) }
            else { Image(systemName: "chevron.right").foregroundStyle(.secondary) }
        }.padding().background(.thinMaterial).clipShape(RoundedRectangle(cornerRadius: 16))
    }
}
