import SwiftUI
import AppKit
import UniformTypeIdentifiers

private let wireMint = Color(red: 0.38, green: 0.91, blue: 0.76)
private let wireBackground = Color(red: 0.035, green: 0.065, blue: 0.105)

struct WireDeskDashboard: View {
    @ObservedObject private var manager = ReceiverManager.shared
    @ObservedObject private var link = ReceiverManager.shared.ethernet
    @ObservedObject private var updater = UpdateChecker.shared
    @AppStorage("wiredesk.autoListen") private var autoListen = true
    @State private var preparing = false
    @State private var setupError: String?
    @State private var copied = false
    let showAllFeatures: () -> Void

    private var selectedIP: String { link.addresses.first(where: { $0.id == link.selectedAddress })?.ip ?? "" }
    private var code: String? { WireDeskSetup.pairingCode(ip: selectedIP, fingerprint: link.fingerprint) }
    private var title: String { link.connected ? "Второй экран подключён" : link.running ? "Mac готов к подключению" : "Больше места для ваших идей" }

    var body: some View {
        GeometryReader { geometry in
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                header
                hero
                if geometry.size.width >= 820 {
                    HStack(alignment: .top, spacing: 16) {
                        cableCard.frame(maxWidth: .infinity)
                        pairingCard.frame(maxWidth: .infinity)
                    }
                } else {
                    VStack(spacing: 16) { cableCard; pairingCard }
                }
                if link.connected { metrics }
                controls
                DisclosureGroup("Дополнительные настройки") { advanced.padding(.top, 14) }
                    .font(.callout).foregroundStyle(Color.white.opacity(0.8)).padding(20).background(cardBackground)
                DisclosureGroup("Диагностика подключения") {
                    Text(link.diagnostics == "no data" ? "Данные появятся после подключения Windows." : link.diagnostics)
                        .font(.system(size: 11, design: .monospaced)).textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading).padding(.top, 14)
                    Text(link.status).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
                }.foregroundStyle(Color.white.opacity(0.8)).padding(20).background(cardBackground)
                footer
            }.padding(28).frame(maxWidth: 1180).frame(maxWidth: .infinity)
        }
        }
        .background(wireBackground).preferredColorScheme(.dark).tint(wireMint)
        .frame(minWidth: 580, minHeight: 530)
        .onAppear {
            manager.dedicatedEthernet = true
            updater.start()
            prepareAndListen()
        }
        .onChange(of: link.addresses.map(\.id)) { _ in prepareAndListen() }
        .onChange(of: link.selectedAddress) { _ in prepareAndListen() }
        .onChange(of: autoListen) { enabled in if enabled { prepareAndListen() } }
        .onChange(of: link.connected) { connected in if !connected { updater.resumeDeferredInstallation() } }
    }
    private var cardBackground: some View { RoundedRectangle(cornerRadius: 20).fill(Color.white.opacity(0.045)).overlay(RoundedRectangle(cornerRadius: 20).stroke(Color.white.opacity(0.08))) }
    private var header: some View {
        ViewThatFits(in: .horizontal) {
            HStack(spacing: 12) { brand; Spacer(); statusPill }
            VStack(alignment: .leading, spacing: 16) { brand; statusPill }
        }
    }
    private var brand: some View {
        HStack(spacing: 12) {
            Image(nsImage: NSApp.applicationIconImage).resizable().frame(width: 46, height: 46)
            VStack(alignment: .leading, spacing: 2) { Text("WireDesk").font(.system(size: 23, weight: .bold, design: .rounded)); Text("Windows → Mac · Ethernet").font(.caption).foregroundStyle(.secondary) }
        }
    }
    private var statusPill: some View {
        Label(link.connected ? "Подключено" : link.running ? "Ожидание Windows" : preparing ? "Подготовка" : "Подключите Ethernet", systemImage: link.connected ? "checkmark.circle.fill" : "circle.dotted")
            .font(.caption.weight(.medium)).foregroundStyle(wireMint).padding(.horizontal, 14).padding(.vertical, 9).background(wireMint.opacity(0.09), in: Capsule())
    }
    private var hero: some View {
        HStack(alignment: .center, spacing: 24) {
            VStack(alignment: .leading, spacing: 13) {
                Text(title).font(.system(size: 32, weight: .bold, design: .rounded)).fixedSize(horizontal: false, vertical: true)
                Text("Переносите окна с Windows на Mac, как на обычный монитор. Один кабель. Ваш привычный рабочий стол.")
                    .font(.system(size: 15)).foregroundStyle(Color.white.opacity(0.65)).fixedSize(horizontal: false, vertical: true)
                Label("До 120 FPS · защищённое соединение", systemImage: "bolt.shield.fill").font(.caption.weight(.medium)).foregroundStyle(wireMint)
            }
            Image(systemName: "display.2").font(.system(size: 60, weight: .light)).foregroundStyle(wireMint).accessibilityHidden(true)
        }.padding(28).frame(maxWidth: .infinity, alignment: .leading)
            .background(LinearGradient(colors: [Color(red: 0.06, green: 0.18, blue: 0.22), Color(red: 0.06, green: 0.1, blue: 0.17)], startPoint: .topLeading, endPoint: .bottomTrailing), in: RoundedRectangle(cornerRadius: 24))
    }
    private func step(_ number: String, _ name: String, icon: String) -> some View {
        HStack { Text(number).font(.caption.bold()).foregroundStyle(wireMint).frame(width: 27, height: 27).background(wireMint.opacity(0.1), in: Circle()); Text(name).font(.headline); Spacer(); Image(systemName: icon).foregroundStyle(wireMint) }
    }
    private var cableCard: some View {
        VStack(alignment: .leading, spacing: 16) {
            step("1", "Подключите кабель", icon: "cable.connector")
            Text(link.addresses.isEmpty ? "Соедините Windows и Mac через Ethernet. Адаптер появится автоматически." : "Ethernet найден. Интернет может оставаться на другой сети.").font(.callout).foregroundStyle(.secondary)
            Picker("Адаптер", selection: $link.selectedAddress) {
                Text("Выберите Ethernet").tag("")
                ForEach(link.addresses) { address in Text("\(address.name) · \(address.ip)").tag(address.id) }
            }.labelsHidden().disabled(link.running).frame(maxWidth: .infinity)
            if !selectedIP.isEmpty { Label(selectedIP, systemImage: "network").font(.system(.caption, design: .monospaced)).textSelection(.enabled) }
            Text("Для отдельного кабеля обычно подходят автоматические адреса 169.254.x.x. Если адреса не появились, настройте отдельную подсеть без шлюза.").font(.caption).foregroundStyle(.secondary)
        }.padding(22).frame(maxWidth: .infinity, minHeight: 230, alignment: .topLeading).background(cardBackground)
    }
    private var pairingCard: some View {
        VStack(alignment: .leading, spacing: 16) {
            step("2", "Свяжите устройства", icon: "link")
            Text(link.connected ? "Сопряжение сохранено. При обрыве WireDesk дождётся того же Ethernet-подключения." : "Откройте WireDesk на Windows и вставьте код этого Mac. Первое подключение подтвердите здесь.").font(.callout).foregroundStyle(.secondary)
            ViewThatFits(in: .horizontal) {
                HStack { copyButton; exportButton }
                VStack(alignment: .leading) { copyButton; exportButton }
            }
            Text("Код содержит только адрес и отпечаток сертификата. Приватный ключ остаётся на Mac.").font(.caption).foregroundStyle(.secondary)
            if preparing { ProgressView("Создаём защищённое подключение…").font(.caption) }
            if let setupError { Text(setupError).font(.caption).foregroundStyle(.orange).textSelection(.enabled) }
        }.padding(22).frame(maxWidth: .infinity, minHeight: 230, alignment: .topLeading).background(cardBackground)
    }
    private var copyButton: some View { Button { if let code { NSPasteboard.general.clearContents(); NSPasteboard.general.setString(code, forType: .string); copied = true; DispatchQueue.main.asyncAfter(deadline: .now()+2) { copied = false } } } label: { Label(copied ? "Скопировано" : "Скопировать код", systemImage: copied ? "checkmark" : "doc.on.doc") }.buttonStyle(.borderedProminent).disabled(code == nil) }
    private var exportButton: some View { Button("Сохранить файл", systemImage: "square.and.arrow.down") { exportPairing() }.buttonStyle(.bordered).disabled(code == nil) }
    private var metrics: some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 140))], alignment: .leading, spacing: 16) {
            metric("Получение", value: String(format: "%.1f FPS", link.measuredReceiveFPS))
            metric("Декодирование", value: String(format: "%.1f FPS", link.measuredDecodeFPS))
            metric("Новые кадры в вывод", value: String(format: "%.1f FPS", link.measuredPresentationFPS))
        }
    }
    private func metric(_ name: String, value: String) -> some View { VStack(alignment: .leading, spacing: 7) { Text(name).font(.caption).foregroundStyle(.secondary); Text(value).font(.title2.monospacedDigit().weight(.semibold)).foregroundStyle(wireMint) }.padding(20).frame(maxWidth: .infinity, alignment: .leading).background(cardBackground) }
    private var controls: some View {
        ViewThatFits(in: .horizontal) { HStack { primaryControls; Spacer(); listenControl }; VStack(alignment: .leading, spacing: 12) { primaryControls; listenControl } }
    }
    private var primaryControls: some View {
        HStack {
            Button("Показать экран", systemImage: "macwindow") { manager.showWindow() }.buttonStyle(.borderedProminent).disabled(!link.connected)
            Button("На весь экран", systemImage: "arrow.up.left.and.arrow.down.right") { manager.showWindow(); manager.videoRenderer.view.window?.toggleFullScreen(nil) }.buttonStyle(.bordered).disabled(!link.connected)
        }.controlSize(.large)
    }
    private var listenControl: some View {
        Button(link.running ? "Остановить ожидание" : "Подготовить подключение") {
            if link.running { autoListen = false; manager.stop() }
            else { autoListen = true; prepareAndListen(force: true) }
        }.buttonStyle(.bordered).disabled(preparing)
    }
    private var advanced: some View {
        VStack(alignment: .leading, spacing: 14) {
            Toggle("Автоматически ждать Windows при запуске", isOn: $autoListen)
            TextField("Ожидаемый IP Windows (можно оставить пустым до сопряжения)", text: $link.expectedPeer).textFieldStyle(.roundedBorder).disabled(link.running)
            HStack { Text("Сертификат: \(link.identityPath.isEmpty ? "создаётся автоматически" : URL(fileURLWithPath: link.identityPath).lastPathComponent)").font(.caption); Spacer(); Button("Выбрать свой") { chooseIdentity() }.disabled(link.running) }
            SecureField("Пароль своей identity (пустой для автоматической)", text: $link.identityPassword).textFieldStyle(.roundedBorder).disabled(link.running)
            Text("SHA-256: \(link.fingerprint)").font(.system(size: 10, design: .monospaced)).textSelection(.enabled)
            Toggle("Проверять обновления автоматически", isOn: $updater.automaticChecksEnabled)
            Text("Обновления загружаются через интернет отдельно от видеопотока. Установка ждёт окончания активного сеанса.").font(.caption).foregroundStyle(.secondary)
        }
    }
    private var footer: some View {
        ViewThatFits(in: .horizontal) {
            HStack { footerLinks; Spacer(); Text("v\(UpdateChecker.displayVersion)").font(.caption).foregroundStyle(.secondary) }
            VStack(alignment: .leading, spacing: 12) { footerLinks; Text("v\(UpdateChecker.displayVersion)").font(.caption).foregroundStyle(.secondary) }
        }
    }
    private var footerLinks: some View { HStack(spacing: 16) { Button("Все возможности") { showAllFeatures() }; Button("Обновления") { updater.downloadAndOpen() }; Link("GitHub", destination: URL(string: "https://github.com/b0rdik/WireDesk")!) }.buttonStyle(.plain).font(.caption).foregroundStyle(.secondary) }
    private func prepareAndListen(force: Bool = false) {
        guard (autoListen || force), !link.running, !preparing else { return }
        if link.selectedAddress.isEmpty, link.addresses.count == 1 { link.selectedAddress = link.addresses[0].id }
        if link.identityPath.isEmpty {
            preparing = true
            WireDeskSetup.prepareIdentity { result in
                preparing = false
                switch result { case .success(let path): link.identityPath = path; setupError = nil; prepareAndListen()
                case .failure(let error): setupError = error.localizedDescription }
            }
            return
        }
        guard !link.selectedAddress.isEmpty, link.addresses.contains(where: { $0.id == link.selectedAddress }) else { return }
        manager.start()
    }
    private func exportPairing() {
        guard let code else { return }
        let panel = NSSavePanel(); panel.nameFieldStringValue = "Mac-WireDesk.wiredesk-pair"
        if panel.runModal() == .OK, let url = panel.url {
            do { try code.write(to: url, atomically: true, encoding: .utf8) } catch { setupError = error.localizedDescription }
        }
    }
    private func chooseIdentity() {
        let panel = NSOpenPanel(); panel.allowedContentTypes = [UTType(filenameExtension: "p12") ?? .data]; panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url { link.identityPath = url.path }
    }
}
