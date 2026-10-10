import Foundation
import CryptoKit
let directory = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
guard !FileManager.default.fileExists(atPath: directory.path) else { fatalError("Refusing to overwrite signing keys") }
try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
let key = Curve25519.Signing.PrivateKey()
let privateURL = directory.appendingPathComponent("sparkle-private.key")
try key.rawRepresentation.base64EncodedString().data(using: .utf8)!.write(to: privateURL, options: .atomic)
try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: privateURL.path)
try key.publicKey.rawRepresentation.base64EncodedString().data(using: .utf8)!.write(to: directory.appendingPathComponent("public-key.txt"))
print("Created Ed25519 signing key. The private seed stays outside the repository.")
