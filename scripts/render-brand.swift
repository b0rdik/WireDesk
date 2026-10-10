import AppKit
import Foundation
let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
func color(_ hex: UInt32) -> NSColor { NSColor(calibratedRed: CGFloat((hex >> 16) & 255)/255, green: CGFloat((hex >> 8) & 255)/255, blue: CGFloat(hex & 255)/255, alpha: 1) }
for size in [16, 32, 48, 64, 128, 256, 512, 1024] {
 let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
 let context = NSGraphicsContext(bitmapImageRep: bitmap)!
 NSGraphicsContext.saveGraphicsState(); NSGraphicsContext.current = context
 context.cgContext.scaleBy(x: CGFloat(size)/1024, y: CGFloat(size)/1024)
 let outer = NSBezierPath(roundedRect: NSRect(x: 38, y: 38, width: 948, height: 948), xRadius: 216, yRadius: 216)
 NSGradient(starting: color(0x18293D), ending: color(0x07111E))!.draw(in: outer, angle: -45)
 color(0x28435A).setStroke(); outer.lineWidth = 8; outer.stroke()
 let back = NSBezierPath(roundedRect: NSRect(x: 230, y: 465, width: 505, height: 325), xRadius: 46, yRadius: 46)
 color(0x2C4D69).setFill(); back.fill(); color(0x8BBBE6).setStroke(); back.lineWidth = 20; back.stroke()
 let front = NSBezierPath(roundedRect: NSRect(x: 348, y: 289, width: 480, height: 315), xRadius: 46, yRadius: 46)
 NSGradient(starting: color(0x58F1C5), ending: color(0x339DDC))!.draw(in: front, angle: -45)
 color(0xD4FFF1).setStroke(); front.lineWidth = 16; front.stroke()
 let inset = NSBezierPath(roundedRect: NSRect(x: 376, y: 317, width: 424, height: 259), xRadius: 24, yRadius: 24)
 color(0x102735).setFill(); inset.fill()
 let arrow = NSBezierPath(); arrow.move(to: NSPoint(x: 496, y: 446)); arrow.line(to: NSPoint(x: 674, y: 446)); arrow.move(to: NSPoint(x: 621, y: 499)); arrow.line(to: NSPoint(x: 674, y: 446)); arrow.line(to: NSPoint(x: 621, y: 393))
 color(0x78F4D2).setStroke(); arrow.lineWidth = 25; arrow.lineCapStyle = .round; arrow.lineJoinStyle = .round; arrow.stroke()
 let cable = NSBezierPath(); cable.move(to: NSPoint(x: 270, y: 465)); cable.line(to: NSPoint(x: 270, y: 257)); cable.curve(to: NSPoint(x: 349, y: 178), controlPoint1: NSPoint(x: 270,y:178), controlPoint2:NSPoint(x:270,y:178)); cable.line(to: NSPoint(x: 514,y:178)); cable.curve(to:NSPoint(x:588,y:252),controlPoint1:NSPoint(x:588,y:178),controlPoint2:NSPoint(x:588,y:178)); cable.line(to:NSPoint(x:588,y:289))
 color(0x78F4D2).setStroke(); cable.lineWidth = 27; cable.lineCapStyle = .round; cable.stroke()
 NSGraphicsContext.restoreGraphicsState()
 try bitmap.representation(using: .png, properties: [:])!.write(to: output.appendingPathComponent("icon-\(size).png"))
}
