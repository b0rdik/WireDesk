import Foundation

/// Shared by production receiver and the standalone protocol tests. No clock arithmetic across hosts.
enum EthernetProtocol {
    static let version = 1
    static let maxBody = 4 * 1024 * 1024
    static let maxControl = 65536
    static let port: UInt16 = 51820

    static func bodyLength(_ header: Data) -> Int? {
        guard header.count == 4 else { return nil }
        let length = header.reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
        guard length > 0, length <= maxBody else { return nil }
        return Int(length)
    }
    static func packet(type: UInt8, payload: Data) -> Data? {
        guard payload.count < maxBody else { return nil }
        var length = UInt32(payload.count + 1).bigEndian
        var packet = Data(bytes: &length, count: 4)
        packet.append(type); packet.append(payload)
        return packet
    }
    static func hello(_ object: [String: Any]) -> Int? {
        guard object["op"] as? String == "hello", object["version"] as? Int == version,
              object["codec"] as? String == "h264", let fps = object["fps"] as? Int,
              fps == 60 || fps == 120 else { return nil }
        return fps
    }
}
