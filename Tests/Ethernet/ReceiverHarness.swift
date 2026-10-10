import AppKit
import Foundation
import Combine
final class LogManager { static let shared = LogManager(); func log(_ message: String) { print(message) } }

@main struct ReceiverHarness {
    static func main() {
        let decoder = ReceiverVideoDecoder(), renderer = ReceiverVideoRenderer()
        let receiver = DedicatedEthernetReceiver.loopbackForTesting(decoder: decoder, renderer: renderer)
        let observation = receiver.$status.sink { print("STATUS", $0); fflush(stdout) }
        let timeout = Date().addingTimeInterval(25)
        while receiver.addresses.first(where: { $0.name == "lo0" }) == nil && Date() < timeout {
            RunLoop.main.run(until: Date().addingTimeInterval(0.05))
        }
        receiver.selectedAddress = "lo0|127.0.0.1"; receiver.expectedPeer = "127.0.0.1"
        receiver.identityPath = CommandLine.arguments[1]
        receiver.start()
        precondition(receiver.running, receiver.status)
        print("READY \(receiver.fingerprint)")
        fflush(stdout)
        while Date() < timeout {
            RunLoop.main.run(until: Date().addingTimeInterval(0.05))
        }
        receiver.stop()
        withExtendedLifetime(observation) {}
        print("Decoded total", decoder.totalOutputFrames)
        // Aborting a stream deliberately discards any packet still in flight. Both complete sessions must decode.
        precondition((580...600).contains(decoder.totalOutputFrames), "Expected two complete 240-frame sessions plus the partial interrupted session")
        print("HARNESS frames \(decoder.totalOutputFrames); decoder \(decoder.hardwareDescription); mailbox \(renderer.pendingDepth)")
    }
}
