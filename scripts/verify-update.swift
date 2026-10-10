import Foundation
import CryptoKit
@main struct VerifyUpdate {
 static func main() throws {
  guard CommandLine.arguments.count == 4 else { fatalError("Usage: verify-update PUBLIC_KEY_FILE ARCHIVE BASE64_SIGNATURE") }
  let encoded = try String(contentsOfFile: CommandLine.arguments[1], encoding: .utf8).trimmingCharacters(in: .whitespacesAndNewlines)
  guard let raw = Data(base64Encoded: encoded), let signature = Data(base64Encoded: CommandLine.arguments[3]) else { fatalError("Invalid public key/signature") }
  let key = try Curve25519.Signing.PublicKey(rawRepresentation: raw)
  let archive = try Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[2]), options: .mappedIfSafe)
  guard key.isValidSignature(signature, for: archive) else { fputs("Archive signature does not match pinned public key\n", stderr); exit(1) }
  print("PASS pinned Ed25519 archive signature")
 }
}
