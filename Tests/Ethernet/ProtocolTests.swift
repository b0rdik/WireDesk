import Foundation

@main struct ProtocolTests {
    static func main() {
        precondition(EthernetProtocol.bodyLength(Data([0,0,0,0])) == nil)
        precondition(EthernetProtocol.bodyLength(Data([0,64,0,1])) == nil)
        precondition(EthernetProtocol.bodyLength(Data([0,64,0,0])) == EthernetProtocol.maxBody)
        precondition(EthernetProtocol.bodyLength(Data([0,0,0])) == nil)
        for size in [0, 1, 100, 65535, EthernetProtocol.maxBody - 1] {
            let payload = Data(repeating: 0xab, count: size)
            let packet = EthernetProtocol.packet(type: 1, payload: payload)!
            precondition(EthernetProtocol.bodyLength(Data(packet.prefix(4))) == size + 1)
            precondition(packet[4] == 1 && Data(packet.dropFirst(5)) == payload)
        }
        precondition(EthernetProtocol.packet(type: 1, payload: Data(count: EthernetProtocol.maxBody)) == nil)
        for fps in [60, 120] {
            let valid: [String: Any] = ["op":"hello", "version":1, "fps":fps, "codec":"h264"]
            precondition(EthernetProtocol.hello(valid) == fps)
        }
        for invalid: [String: Any] in [[:], ["op":"hello", "version":2, "fps":120, "codec":"h264"],
            ["op":"hello", "version":1, "fps":125, "codec":"h264"],
            ["op":"hello", "version":1, "fps":120, "codec":"hevc"]] {
            precondition(EthernetProtocol.hello(invalid) == nil)
        }
        // Deterministic fuzz cases at the untrusted framing boundary.
        var seed: UInt32 = 123
        for _ in 0..<10000 {
            seed = seed &* 1664525 &+ 1013904223
            let bytes = Data([UInt8(truncatingIfNeeded: seed >> 24), UInt8(truncatingIfNeeded: seed >> 16),
                              UInt8(truncatingIfNeeded: seed >> 8), UInt8(truncatingIfNeeded: seed)])
            let parsed = EthernetProtocol.bodyLength(bytes)
            precondition(parsed == ((seed > 0 && seed <= EthernetProtocol.maxBody) ? Int(seed) : nil))
        }
        print("PASS framing limits, version/mode negotiation, 10000 malformed-header cases")
    }
}
