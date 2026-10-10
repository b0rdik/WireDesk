import Foundation
import Network
import Security
import AppKit
import CryptoKit
import CoreMedia

struct EthernetAddress: Identifiable, Hashable {
    var id: String { "\(name)|\(ip)" }
    let name: String
    let ip: String
    let mask: UInt32

    static func available() -> [EthernetAddress] {
        var head: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&head) == 0, let first = head else { return [] }
        defer { freeifaddrs(head) }
        var result: [EthernetAddress] = []
        var cursor: UnsafeMutablePointer<ifaddrs>? = first
        while let pointer = cursor {
            let entry = pointer.pointee
            defer { cursor = entry.ifa_next }
            guard let addr = entry.ifa_addr, addr.pointee.sa_family == UInt8(AF_INET),
                  let netmask = entry.ifa_netmask else { continue }
            let name = String(cString: entry.ifa_name)
            let ipv4 = UnsafeRawPointer(addr).assumingMemoryBound(to: sockaddr_in.self).pointee
            let mask = UnsafeRawPointer(netmask).assumingMemoryBound(to: sockaddr_in.self).pointee.sin_addr.s_addr.bigEndian
            var address = ipv4.sin_addr
            var buffer = [CChar](repeating: 0, count: Int(INET_ADDRSTRLEN))
            guard inet_ntop(AF_INET, &address, &buffer, socklen_t(INET_ADDRSTRLEN)) != nil else { continue }
            result.append(EthernetAddress(name: name, ip: String(cString: buffer), mask: mask))
        }
        return result.sorted { $0.id < $1.id }
    }
    func isSameSubnet(_ peer: String) -> Bool {
        var local = in_addr(), remote = in_addr()
        guard inet_pton(AF_INET, ip, &local) == 1, inet_pton(AF_INET, peer, &remote) == 1,
              mask != 0, local.s_addr != remote.s_addr else { return false }
        return (local.s_addr.bigEndian & mask) == (remote.s_addr.bigEndian & mask)
    }
}

/// Manually addressed TLS/TCP receiver. Dedicated mode has no mDNS, UDP, input relay or automatic routing fallback.
/// Transport and decoder mutation are confined to one serial queue; UI publication is on main.
final class DedicatedEthernetReceiver: ObservableObject, ReceiverVideoDecoderDelegate {
    @Published var addresses = EthernetAddress.available()
    @Published var selectedAddress = UserDefaults.standard.string(forKey: "ethernet.address") ?? ""
    @Published var expectedPeer = UserDefaults.standard.string(forKey: "ethernet.peer") ?? ""
    @Published var identityPath = UserDefaults.standard.string(forKey: "ethernet.identity") ?? ""
    @Published var identityPassword = ""
    @Published var status = "Stopped"
    @Published var fingerprint = "no data"
    @Published var diagnostics = "no data"
    @Published var connected = false
    @Published var running = false
    @Published var measuredReceiveFPS: Double = 0
    @Published var measuredDecodeFPS: Double = 0
    @Published var measuredPresentationFPS: Double = 0
    private let queue = DispatchQueue(label: "com.bettercast.dedicated-ethernet", qos: .userInteractive)
    private let pathMonitor: NWPathMonitor
    private let requiredInterfaceType: NWInterface.InterfaceType
    private var interfaces: [NWInterface] = []
    private var address: EthernetAddress?
    private var listener: NWListener?
    private var client: NWConnection?
    private var approved = false
    private var pairedThisRun = false
    private var sessionFPS = 60
    private var negotiatedSize = CGSize.zero
    private var generation = 0
    private var timer: DispatchSourceTimer?
    private var retry: DispatchWorkItem?
    private var lastTraffic = ProcessInfo.processInfo.systemUptime
    private var lastStats = ProcessInfo.processInfo.systemUptime
    private var received = 0, decoded = 0, byteCount = 0
    private var senderStats: [String: Any] = [:]
    private var receiveEndpoint = "no data"
    private var actualInterface = "no data"
    private var identity: sec_identity_t?
    let decoder: ReceiverVideoDecoder
    let renderer: ReceiverVideoRenderer
    var onConnected: (() -> Void)?

    convenience init(decoder: ReceiverVideoDecoder, renderer: ReceiverVideoRenderer) {
        self.init(decoder: decoder, renderer: renderer, interfaceType: .wiredEthernet)
    }
#if BETTERCAST_ETHERNET_TEST
    // Test seam is absent from production builds; no UI or runtime setting can select loopback.
    static func loopbackForTesting(decoder: ReceiverVideoDecoder, renderer: ReceiverVideoRenderer) -> DedicatedEthernetReceiver {
        DedicatedEthernetReceiver(decoder: decoder, renderer: renderer, interfaceType: .loopback)
    }
#endif
    private init(decoder: ReceiverVideoDecoder, renderer: ReceiverVideoRenderer, interfaceType: NWInterface.InterfaceType) {
        self.requiredInterfaceType = interfaceType
        self.pathMonitor = NWPathMonitor(requiredInterfaceType: interfaceType)
        self.decoder = decoder; self.renderer = renderer
        pathMonitor.pathUpdateHandler = { [weak self] path in
            guard let self else { return }
            self.interfaces = path.availableInterfaces.filter { $0.type == self.requiredInterfaceType }
            let names = Set(self.interfaces.map { $0.name })
            DispatchQueue.main.async { self.addresses = EthernetAddress.available().filter { names.contains($0.name) } }
        }
        pathMonitor.start(queue: queue)
    }
    deinit { pathMonitor.cancel() }

    func start() {
        guard !running else { return }
        guard let selected = addresses.first(where: { $0.id == selectedAddress }) else { status = "Select Ethernet IPv4"; return }
        var validPeer = expectedPeer.isEmpty || selected.isSameSubnet(expectedPeer)
#if BETTERCAST_ETHERNET_TEST
        if requiredInterfaceType == .loopback && selected.ip == "127.0.0.1" && expectedPeer == "127.0.0.1" { validPeer = true }
#endif
        guard validPeer else {
            status = "Select Ethernet IPv4 and a different Windows IP on the same subnet"; return
        }
        do {
            let p12 = try Data(contentsOf: URL(fileURLWithPath: identityPath))
            var items: CFArray?
            let passwordURL = URL(fileURLWithPath: identityPath).deletingLastPathComponent().appendingPathComponent("receiver.password")
            let password = identityPassword.isEmpty ? (try String(contentsOf: passwordURL, encoding: .utf8)).trimmingCharacters(in: .whitespacesAndNewlines) : identityPassword
            var importOptions: [String: Any] = [kSecImportExportPassphrase as String: password]
            if #available(macOS 15, *) { importOptions[kSecImportToMemoryOnly as String] = true }
            let result = SecPKCS12Import(p12 as CFData, importOptions as CFDictionary, &items)
            guard result == errSecSuccess, let records = items as? [[String: Any]],
                  let value = records.first?[kSecImportItemIdentity as String] else {
                status = "Cannot import identity: \(result); check password/receiver.password"; return
            }
            let secIdentity = value as! SecIdentity
            guard let tlsIdentity = sec_identity_create(secIdentity) else { status = "TLS identity unavailable"; return }
            var cert: SecCertificate?
            guard SecIdentityCopyCertificate(secIdentity, &cert) == errSecSuccess, let cert else { return }
            fingerprint = SHA256.hash(data: SecCertificateCopyData(cert) as Data).map { String(format: "%02x", $0) }.joined()
            identity = tlsIdentity
        } catch { status = "Identity file: \(error.localizedDescription)"; return }
#if !BETTERCAST_ETHERNET_TEST
        UserDefaults.standard.set(selectedAddress, forKey: "ethernet.address")
        UserDefaults.standard.set(expectedPeer, forKey: "ethernet.peer")
        UserDefaults.standard.set(identityPath, forKey: "ethernet.identity")
#endif
        running = true
        let peer = expectedPeer
        queue.async { [self] in
            address = selected; pairedThisRun = false
            self.peerIP = peer
            decoder.delegate = self
            decoder.dedicatedLowLatency = true
            makeListener()
            timer?.cancel()
            let source = DispatchSource.makeTimerSource(queue: queue)
            source.schedule(deadline: .now() + 1, repeating: 1)
            source.setEventHandler { [weak self] in self?.tick() }
            timer = source; source.resume()
        }
    }
    private var peerIP = ""
    private func publish(_ text: String) { DispatchQueue.main.async { [weak self] in self?.status = text } }
    private func makeListener() {
        guard let address, let iface = interfaces.first(where: { $0.name == address.name }), let identity else {
            publish("Selected Ethernet unavailable; waiting without fallback"); scheduleListenerRetry(); return
        }
        let tls = NWProtocolTLS.Options()
        sec_protocol_options_set_min_tls_protocol_version(tls.securityProtocolOptions, .TLSv12)
        sec_protocol_options_set_local_identity(tls.securityProtocolOptions, identity)
        let tcp = NWProtocolTCP.Options()
        tcp.noDelay = true; tcp.enableKeepalive = true
        tcp.keepaliveIdle = 2; tcp.keepaliveInterval = 1; tcp.keepaliveCount = 3
        let parameters = NWParameters(tls: tls, tcp: tcp)
        parameters.requiredInterface = iface
        parameters.requiredLocalEndpoint = .hostPort(host: .init(address.ip), port: .init(rawValue: EthernetProtocol.port)!)
        parameters.includePeerToPeer = false
        parameters.prohibitExpensivePaths = true
        parameters.serviceClass = .interactiveVideo
        do {
            let newListener = try NWListener(using: parameters)
            listener = newListener
            newListener.stateUpdateHandler = { [weak self, weak newListener] state in
                guard let self, self.listener === newListener else { return }
                switch state {
                case .ready: self.publish("Waiting for \(self.peerIP) on \(address.name) \(address.ip):51820 (TLS)")
                case .failed(let error):
                    self.publish("Ethernet listener: \(error.localizedDescription); waiting on selected IP")
                    self.listener?.cancel(); self.listener = nil; self.scheduleListenerRetry()
                default: break
                }
            }
            newListener.newConnectionHandler = { [weak self] connection in self?.accept(connection) }
            newListener.start(queue: queue)
        } catch { publish("Ethernet listener failed: \(error.localizedDescription)"); scheduleListenerRetry() }
    }
    private func scheduleListenerRetry() {
        retry?.cancel()
        let work = DispatchWorkItem { [weak self] in guard let self else { return }; self.makeListener() }
        retry = work; queue.asyncAfter(deadline: .now() + 2, execute: work)
    }
    private func accept(_ connection: NWConnection) {
        guard client == nil, case let .hostPort(host, _) = connection.endpoint,
              (String(describing: host) == peerIP || (peerIP.isEmpty && address?.isSameSubnet(String(describing: host)) == true)) else { connection.cancel(); return }
        client = connection; approved = false; generation += 1
        let session = generation
        lastTraffic = ProcessInfo.processInfo.systemUptime
        connection.stateUpdateHandler = { [weak self, weak connection] state in
            guard let self, let connection, self.client === connection else { return }
            switch state {
            case .ready:
                guard let path = connection.currentPath, let address = self.address,
                      path.usesInterfaceType(self.requiredInterfaceType), path.availableInterfaces.contains(where: { $0.name == address.name }),
                      case let .hostPort(localHost, _) = path.localEndpoint, String(describing: localHost) == address.ip else {
                    self.dropClient("Actual path/IP does not match Ethernet selection"); return
                }
                self.actualInterface = address.name
                self.receiveEndpoint = "\(path.localEndpoint?.debugDescription ?? "no data") ← \(connection.endpoint)"
                self.receiveHeader(connection, session: session)
            case .failed, .cancelled: self.dropClient("Disconnected; waiting for the same Ethernet connection")
            default: break
            }
        }
        connection.start(queue: queue)
    }
    private func receiveHeader(_ connection: NWConnection, session: Int) {
        connection.receive(minimumIncompleteLength: 4, maximumLength: 4) { [weak self] data, _, complete, error in
            guard let self, self.generation == session, self.client === connection else { return }
            guard error == nil, !complete, let data, let length = EthernetProtocol.bodyLength(data) else {
                self.dropClient("Closed or invalid packet header"); return
            }
            connection.receive(minimumIncompleteLength: length, maximumLength: length) { [weak self] body, _, complete, error in
                guard let self, self.generation == session, self.client === connection else { return }
                guard error == nil, let body, body.count == length else { self.dropClient("Truncated packet"); return }
                self.lastTraffic = ProcessInfo.processInfo.systemUptime
                self.byteCount += body.count + 4
                self.handle(body, connection: connection, session: session)
                if complete { self.dropClient("Peer closed"); return }
                self.receiveHeader(connection, session: session)
            }
        }
    }
    private func handle(_ body: Data, connection: NWConnection, session: Int) {
        let type = body[body.startIndex]
        if type == 0x10 {
            guard body.count <= EthernetProtocol.maxControl,
                  let object = (try? JSONSerialization.jsonObject(with: Data(body.dropFirst()))) as? [String: Any],
                  let op = object["op"] as? String else { dropClient("Invalid control packet"); return }
            if op == "hello" && !approved {
                guard let fps = EthernetProtocol.hello(object) else { send(["op": "reject"]); dropClient("Unsupported protocol/mode"); return }
                sessionFPS = fps
#if BETTERCAST_ETHERNET_TEST
                if requiredInterfaceType == .loopback { pairedThisRun = true; finishPairing(connection, session: session); return }
#endif
                if pairedThisRun { finishPairing(connection, session: session); return }
                publish("Confirm first Windows connection")
                DispatchQueue.main.async { [weak self] in
                    guard let self else { return }
                    let alert = NSAlert()
                    alert.messageText = "Разрешить Windows \(connection.endpoint) передавать экран?"
                    alert.informativeText = "Only this Ethernet IP is permitted. Reverse input is disabled. Trust lasts until listening stops."
                    alert.addButton(withTitle: "Allow"); alert.addButton(withTitle: "Reject")
                    let allow = alert.runModal() == .alertFirstButtonReturn
                    self.queue.async {
                        guard self.generation == session, self.client === connection else { return }
                        if allow { self.pairedThisRun = true; self.finishPairing(connection, session: session) }
                        else { self.dropClient("First connection rejected") }
                    }
                }
            } else if op == "format", approved {
                guard let width = object["width"] as? Int, let height = object["height"] as? Int,
                      width > 0, height > 0, width % 2 == 0, height % 2 == 0,
                      width <= 8192, height <= 8192, width * height <= 16_777_216,
                      object["codec"] as? String == "h264", object["fps"] as? Int == sessionFPS else {
                    dropClient("Invalid stream format"); return
                }
                negotiatedSize = CGSize(width: width, height: height)
            } else if op == "ping", approved, let token = object["token"] {
                send(["op": "pong", "token": token])
            } else if op == "senderStats", approved { senderStats = object }
            else { dropClient("Unexpected control operation"); return }
        } else if type == 1, approved, negotiatedSize != .zero, body.count > 9 {
            received += 1
            decoder.decode(data: Data(body.dropFirst()))
        } else { dropClient("Video before pairing or unsupported packet type") }
    }
    private func finishPairing(_ connection: NWConnection, session: Int) {
        guard generation == session, client === connection else { return }
        if peerIP.isEmpty, case let .hostPort(host, _) = connection.endpoint {
            peerIP = String(describing: host)
            let rememberedIP = peerIP
            DispatchQueue.main.async { [weak self] in
                self?.expectedPeer = rememberedIP
#if !BETTERCAST_ETHERNET_TEST
                UserDefaults.standard.set(rememberedIP, forKey: "ethernet.peer")
#endif
            }
        }
        decoder.reset()
        approved = true; negotiatedSize = .zero; received = 0; decoded = 0; byteCount = 0; senderStats = [:]
        lastStats = ProcessInfo.processInfo.systemUptime
        send(["op": "ready", "version": EthernetProtocol.version, "codec": "h264", "fps": sessionFPS])
        DispatchQueue.main.async { [weak self] in self?.connected = true; self?.onConnected?() }
        publish("Connected: H.264, \(sessionFPS) FPS target, dedicated TLS/Ethernet")
    }
    private func send(_ object: [String: Any]) {
        guard let data = try? JSONSerialization.data(withJSONObject: object), let packet = EthernetProtocol.packet(type: 0x10, payload: data),
              let client else { return }
        let session = generation
        client.send(content: packet, completion: .contentProcessed { [weak self, weak client] error in
            guard let self, self.generation == session, self.client === client else { return }
            if let error { self.dropClient("Control send: \(error.localizedDescription)") }
        })
    }
    private func dropClient(_ reason: String) {
        let old = client; client = nil; approved = false; generation += 1
        old?.stateUpdateHandler = nil; old?.cancel()
        decoder.reset()
        DispatchQueue.main.async { [weak self] in self?.connected = false; self?.renderer.flush() }
        publish(reason)
    }
    private func tick() {
        let now = ProcessInfo.processInfo.systemUptime
        // Pairing alerts may remain open; timeout only an approved session.
        if client != nil && now - lastTraffic > (approved ? 5 : 60) { dropClient("Ethernet timeout; waiting without Wi-Fi fallback") }
        let interval = now - lastStats
        guard approved, interval >= 0.5 else { return }
        let rxFPS = Double(received) / interval, decodeFPS = Double(decoded) / interval
        let bitrate = Double(byteCount) * 8 / interval / 1_000_000
        let sender = senderStats
        let endpoint = receiveEndpoint, iface = actualInterface, fps = sessionFPS
        let selectedName = address?.name ?? "no data"
        let decoderInfo = decoder.hardwareDescription
        let decodeMS = decoder.lastDecodeMS
        let inFlight = decoder.inFlightCount
        send(["op": "stats", "receiveFPS": rxFPS, "decodeFPS": decodeFPS, "decodeMS": decodeMS, "decodeQueue": inFlight])
        received = 0; decoded = 0; byteCount = 0; lastStats = now
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            let renderFPS = self.renderer.takeSubmittedFPS(interval: interval)
            self.measuredReceiveFPS = rxFPS
            self.measuredDecodeFPS = decodeFPS
            self.measuredPresentationFPS = renderFPS
            let screen = self.renderer.screenDescription
            let warnings = (fps == 120 && self.renderer.screenMaxFPS < 120) ? "\nCurrent screen cannot present 120 new frames/s" : ""
            self.diagnostics = "Selected/actual: \(selectedName) / \(iface)\n\(endpoint)\nH.264 \(fps) FPS target; \(Int(self.renderer.videoSize.width))×\(Int(self.renderer.videoSize.height))\nReceive \(String(format: "%.1f", rxFPS)), decode \(String(format: "%.1f", decodeFPS)), new-frame submissions \(String(format: "%.1f", renderFPS)) FPS\nMeasured receive \(String(format: "%.2f", bitrate)) Mbps; decoder \(decoderInfo)\nDecode \(String(format: "%.2f", decodeMS))ms; decode queue \(inFlight)/1; render queue \(self.renderer.pendingDepth)/1; render drops \(self.renderer.droppedFrames)\n\(screen)\nSender: \(sender)\nActual scanout FPS / end-to-end latency / CPU-GPU load: no data\(warnings)"
        }
    }
    func didDecode(sampleBuffer: CMSampleBuffer) {
        // Decoder invokes this on transport queue in dedicated synchronous mode.
        guard let format = CMSampleBufferGetFormatDescription(sampleBuffer) else { return }
        let dimensions = CMVideoFormatDescriptionGetDimensions(format)
        if CGSize(width: Int(dimensions.width), height: Int(dimensions.height)) != negotiatedSize {
            queue.async { [weak self] in self?.dropClient("Decoded dimensions differ from negotiated stream") }; return
        }
        if sessionFPS == 120 && decoder.hardwareDescription == "VideoToolbox software" {
            send(["op": "reject", "reason": "Hardware decoder unavailable; choose 60 FPS"])
            queue.async { [weak self] in self?.dropClient("Hardware decoder unavailable for 120 FPS; choose 60 FPS") }; return
        }
        decoded += 1
        renderer.offer(sampleBuffer)
    }
    func decoderDidChangeFormat() { DispatchQueue.main.async { [weak self] in self?.renderer.flushForFormatChange() } }
    func decoderNeedsKeyframe() { send(["op": "keyframe"]) }
    func stop() {
        running = false
        queue.sync {
            retry?.cancel(); retry = nil; timer?.cancel(); timer = nil
            listener?.stateUpdateHandler = nil; listener?.cancel(); listener = nil
            dropClient("Stopped"); identity = nil; decoder.dedicatedLowLatency = false; decoder.delegate = nil
        }
    }
}
