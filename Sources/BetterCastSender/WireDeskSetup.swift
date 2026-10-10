import Foundation
import Security
import AppKit

/// Uses the OS OpenSSL CLI only for provisioning a standard TLS identity, never for transport crypto.
enum WireDeskSetup {
    static func prepareIdentity(completion: @escaping (Result<String, Error>) -> Void) {
        DispatchQueue.global(qos: .userInitiated).async {
            let result = Result { () throws -> String in
                let manager = FileManager.default
                let support = try manager.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true).appendingPathComponent("WireDesk", isDirectory: true)
                let directory = support.appendingPathComponent("Identity", isDirectory: true)
                let identity = directory.appendingPathComponent("receiver.p12")
                if manager.fileExists(atPath: identity.path) {
                    guard manager.fileExists(atPath: directory.appendingPathComponent("receiver.password").path) else { throw setupError("Не найден пароль сертификата. Выберите свою identity в дополнительных настройках.") }
                    return identity.path
                }
                try manager.createDirectory(at: support, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
                let temporary = support.appendingPathComponent(".identity-\(UUID().uuidString)", isDirectory: true)
                try manager.createDirectory(at: temporary, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
                defer { try? manager.removeItem(at: temporary) }
                var bytes = [UInt8](repeating: 0, count: 24)
                guard SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes) == errSecSuccess else { throw setupError("Не удалось создать безопасный пароль") }
                let password = bytes.map { String(format: "%02x", $0) }.joined()
                let passwordURL = temporary.appendingPathComponent("receiver.password")
                try password.data(using: .utf8)!.write(to: passwordURL)
                try manager.setAttributes([.posixPermissions: 0o600], ofItemAtPath: passwordURL.path)
                func run(_ arguments: [String]) throws {
                    let process = Process(); process.executableURL = URL(fileURLWithPath: "/usr/bin/openssl")
                    process.arguments = arguments; process.standardOutput = FileHandle.nullDevice
                    let errors = Pipe(); process.standardError = errors
                    try process.run()
                    let errorData = errors.fileHandleForReading.readDataToEndOfFile()
                    process.waitUntilExit()
                    guard process.terminationStatus == 0 else { throw setupError(String(data: errorData, encoding: .utf8) ?? "Ошибка создания сертификата") }
                }
                let key = temporary.appendingPathComponent("receiver.key").path, cert = temporary.appendingPathComponent("receiver.crt").path
                try run(["req", "-x509", "-newkey", "rsa:3072", "-sha256", "-nodes", "-days", "3650", "-subj", "/CN=WireDesk Ethernet", "-keyout", key, "-out", cert])
                try run(["pkcs12", "-export", "-inkey", key, "-in", cert, "-keypbe", "PBE-SHA1-3DES", "-certpbe", "PBE-SHA1-3DES", "-macalg", "sha1", "-passout", "file:\(passwordURL.path)", "-out", temporary.appendingPathComponent("receiver.p12").path])
                for file in try manager.contentsOfDirectory(at: temporary, includingPropertiesForKeys: nil) { try manager.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path) }
                try manager.moveItem(at: temporary, to: directory)
                return identity.path
            }
            DispatchQueue.main.async { completion(result) }
        }
    }
    private static func setupError(_ message: String) -> NSError { NSError(domain: "WireDeskSetup", code: 1, userInfo: [NSLocalizedDescriptionKey: message]) }
    static func pairingCode(ip: String, fingerprint: String) -> String? {
        guard fingerprint.count == 64, fingerprint.allSatisfy({ $0.isHexDigit }), !ip.isEmpty else { return nil }
        var parts = URLComponents(); parts.scheme = "wiredesk"; parts.host = "pair"
        parts.queryItems = [URLQueryItem(name: "v", value: "1"), URLQueryItem(name: "ip", value: ip), URLQueryItem(name: "port", value: "51820"), URLQueryItem(name: "pin", value: fingerprint.lowercased())]
        return parts.string
    }
}
