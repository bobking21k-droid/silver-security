import SwiftUI
import Foundation

struct BLEShieldView: View {
    @EnvironmentObject private var model: BLEShieldViewModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                statusCard
                Button { Task { if model.isMonitoring { model.stop() } else { await model.start() } } } label: {
                    Label(model.isMonitoring ? "Stop monitoring" : "Start monitoring", systemImage: model.isMonitoring ? "stop.fill" : "shield.lefthalf.filled")
                        .frame(maxWidth: .infinity)
                }
                .accessibilityHint(model.isMonitoring ? "Stops the current Bluetooth observation session." : "Requests Bluetooth and notification access, then starts a monitoring session.")
                .buttonStyle(.borderedProminent).controlSize(.large)
                #if DEBUG
                Button { model.runDemoBurst() } label: {
                    Label("Run simulator demo", systemImage: "waveform.path.ecg")
                        .frame(maxWidth: .infinity)
                }.accessibilityHint("Feeds synthetic samples through the detector for simulator testing only.").buttonStyle(.bordered)
                #endif
                if !model.recentDetections.isEmpty {
                    incidentTimeline
                }
                ShareLink(item: model.sessionSummary) {
                    Label("Share session summary", systemImage: "square.and.arrow.up")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .accessibilityHint("Shares a local text summary without uploading raw Bluetooth data.")
                guidance
            }.padding()
        }
        .navigationTitle("BLE Shield")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var statusCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Image(systemName: icon).foregroundStyle(color).font(.title)
                VStack(alignment: .leading) {
                    Text(title).font(.title3.bold())
                    Text(model.scannerState.label).font(.subheadline).foregroundStyle(.secondary)
                }
                Spacer()
            }
            ProgressView(value: Double(model.assessment.score), total: 100)
                .tint(color)
            Text(model.assessment.explanation).font(.subheadline)
            HStack {
                Label("\(model.assessment.eventsPerSecond)/s", systemImage: "waveform.path.ecg")
                Spacer()
                Text("\(model.observedSampleCount) observed").font(.caption).foregroundStyle(.secondary)
            }.font(.caption)
            if let started = model.sessionStartedAt {
                Text("Session started \(started, style: .time)")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
            Text("Observed locally; not blocked")
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("BLE Shield status: \(title). \(model.assessment.explanation)")
        .padding().background(color.opacity(0.10)).clipShape(RoundedRectangle(cornerRadius: 18))
    }

    private var guidance: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("If an attack is active").font(.headline)
            Text("Keep Silver open for the strongest scan the system allows. If the system prompt is disruptive, use Apple’s Bluetooth controls as a last resort—this may disconnect AirPods and pause music. Silver cannot selectively remove only the suspicious advertisements.")
                .font(.subheadline).foregroundStyle(.secondary)
        }.padding().background(.thinMaterial).clipShape(RoundedRectangle(cornerRadius: 16))
    }

    private var incidentTimeline: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Recent observations").font(.headline)
            ForEach(model.recentDetections) { event in
                HStack(alignment: .top, spacing: 10) {
                    Image(systemName: "circle.fill").font(.caption2).foregroundStyle(event.level == .high ? .red : .orange).padding(.top, 4)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(event.level == .high ? "High-rate burst" : "Elevated activity").font(.subheadline.bold())
                        Text(event.explanation).font(.caption).foregroundStyle(.secondary)
                    }
                    Spacer()
                    Text(event.time, style: .time).font(.caption2).foregroundStyle(.secondary)
                }
            }
        }.padding().background(.thinMaterial).clipShape(RoundedRectangle(cornerRadius: 16))
    }

    private var title: String { model.assessment.level == .high ? "High-rate burst detected" : model.assessment.level == .elevated ? "Unusual activity" : model.isMonitoring ? "Watching nearby BLE" : "Ready" }
    private var icon: String { model.assessment.level == .normal ? "checkmark.shield" : "exclamationmark.shield" }
    private var color: Color { model.assessment.level == .high ? .red : model.assessment.level == .elevated ? .orange : .green }
}

private extension BLEScanner.State {
    var label: String {
        switch self { case .idle: return "Ready"; case .starting: return "Starting Bluetooth observation…"; case .monitoring: return "Bluetooth observation active"; case .unavailable(let reason): return reason }
    }
}
