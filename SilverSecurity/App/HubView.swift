import SwiftUI

struct HubView: View {
    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    VStack(alignment: .leading, spacing: 6) {
                        Text("Silver Security").font(.largeTitle.bold())
                        Text("A calm defensive hub for the signals around you.").foregroundStyle(.secondary)
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
                        Text("Silver watches BLE advertisements without disconnecting AirPods or changing the system Bluetooth radio. It reports suspicious bursts for you to act on.")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                    .padding()
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(.green.opacity(0.10))
                    .clipShape(RoundedRectangle(cornerRadius: 16))
                    Text("Silver observes. It does not control Apple’s Bluetooth system UI or claim to block radio advertisements.")
                        .font(.footnote).foregroundStyle(.secondary).padding(.top, 4)
                }.padding()
            }
            .navigationTitle("Hub")
        }
    }
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
