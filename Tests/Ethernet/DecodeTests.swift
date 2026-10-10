import Foundation
import CoreMedia
import AppKit

final class LogManager { static let shared = LogManager(); func log(_ message: String) {} }
final class Probe: ReceiverVideoDecoderDelegate {
    var decoded = 0, requests = 0
    let renderer = ReceiverVideoRenderer()
    func didDecode(sampleBuffer: CMSampleBuffer) {
        decoded += 1
        let dimensions = CMVideoFormatDescriptionGetDimensions(CMSampleBufferGetFormatDescription(sampleBuffer)!)
        precondition(dimensions.width == 1920 && dimensions.height == 1080)
        renderer.offer(sampleBuffer)
    }
    func decoderDidChangeFormat() {}
    func decoderNeedsKeyframe() { requests += 1 }
}

@main struct DecodeTests {
    static func main() throws {
        let decoder = ReceiverVideoDecoder()
        let probe = Probe()
        decoder.dedicatedLowLatency = true; decoder.delegate = probe
        let input = [UInt8](try Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1])))
        var nalus: [Data] = []
        var i = 0, start: Int?
        while i + 3 <= input.count {
            var length = 0
            if i + 4 <= input.count && Array(input[i..<i+4]) == [0,0,0,1] { length = 4 }
            else if Array(input[i..<i+3]) == [0,0,1] { length = 3 }
            if length != 0 {
                if let start, i > start { nalus.append(Data(input[start..<i])) }
                start = i + length; i += length
            } else { i += 1 }
        }
        if let start, start < input.count { nalus.append(Data(input[start...])) }
        var frames: [Data] = [], current = Data()
        for nalu in nalus {
            if nalu[0] & 0x1f == 9 && !current.isEmpty { frames.append(current); current = Data() }
            var length = UInt32(nalu.count).bigEndian
            current.append(Data(bytes: &length, count: 4)); current.append(nalu)
        }
        if !current.isEmpty { frames.append(current) }
        precondition(frames.count == 240, "Expected 240 moving frames in the generated 120fps fixture")
        let began = ProcessInfo.processInfo.systemUptime
        for (index, frame) in frames.enumerated() {
            var pts = (UInt64(index) * 1_000_000_000 / 120).littleEndian
            var payload = Data(bytes: &pts, count: 8); payload.append(frame)
            decoder.decode(data: payload)
        }
        let elapsed = ProcessInfo.processInfo.systemUptime - began
        precondition(probe.decoded == frames.count, "Every reference frame must decode")
        precondition(probe.renderer.pendingDepth == 1)
        precondition(probe.renderer.droppedFrames == 239)
        precondition(decoder.inFlightCount == 0)
        // Empty NALU and truncated NALU previously could read past Data bounds.
        decoder.decode(data: Data(repeating: 0, count: 12))
        decoder.decode(data: Data([0,0,0,0,0,0,0,0,0,0,1,0,0x65]))
        print(String(format: "PASS: %d/240 1080p frames decoded, %@, %.2fs (%.1f FPS batch throughput); mailbox 1, drops 239", probe.decoded, decoder.hardwareDescription, elapsed, Double(probe.decoded)/elapsed))
        print("Batch decode throughput is not capture/network/presentation FPS or end-to-end latency.")
    }
}
