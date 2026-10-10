"""macOS loopback TLS integration; test-only interface/approval seams cannot ship in production."""
import os, pathlib, re, struct, subprocess, sys, time
base = pathlib.Path(__file__).resolve().parents[2]
work = pathlib.Path(sys.argv[1]).resolve()
qt_prefix = os.environ.get('CMAKE_PREFIX_PATH', '/opt/homebrew')
openssl = os.environ.get('OPENSSL_BIN', 'openssl')
work.mkdir(parents=True, exist_ok=True)
sources = base / 'Sources/BetterCastSender'
subprocess.run(['cmake', '-S', str(base/'Tests/Ethernet'), '-B', str(work/'cpp'), '-DCMAKE_PREFIX_PATH='+qt_prefix], check=True)
subprocess.run(['cmake', '--build', str(work/'cpp')], check=True)
subprocess.run([str(work/'cpp/encoder-tests')], check=True)
subprocess.run(['swiftc', '-D', 'BETTERCAST_ETHERNET_TEST', *[str(sources/f) for f in
    ['InputEvent.swift', 'ReceiverVideoDecoder.swift', 'ReceiverVideoRenderer.swift', 'EthernetProtocol.swift', 'DedicatedEthernetReceiver.swift']],
    str(base/'Tests/Ethernet/ReceiverHarness.swift'), '-framework', 'VideoToolbox', '-framework', 'AVFoundation', '-framework', 'AppKit',
    '-o', str(work/'receiver-harness')], check=True)
raw = (work/'moving-1080p120.h264').read_bytes()
positions = list(re.finditer(b'\x00\x00(?:\x00)?\x01', raw)); frames = []; current = bytearray()
for index, pos in enumerate(positions):
    nalu = raw[pos.end():positions[index+1].start() if index+1<len(positions) else len(raw)]
    if not nalu: continue
    if nalu[0] & 31 == 9 and current: frames.append(current); current = bytearray()
    current += struct.pack('!I', len(nalu)) + nalu
if current: frames.append(current)
assert len(frames) == 240
fixture = work/'framed-fixture.bin'
with fixture.open('wb') as handle:
    for index, frame in enumerate(frames):
        payload = struct.pack('<Q', index*1000000000//120) + frame
        handle.write(struct.pack('!I', len(payload))+payload)
identity = work/'identity'
if not identity.exists(): subprocess.run(['bash',str(base/'scripts/create-ethernet-identity.sh'),str(identity)], check=True)
pin = subprocess.check_output([openssl,'x509','-in',str(identity/'receiver.crt'),'-noout','-fingerprint','-sha256'],text=True).strip().split('=')[1].replace(':','').lower()
server_log = work/'receiver-harness.log'
with server_log.open('w') as log:
    server = subprocess.Popen([str(work/'receiver-harness'),str(identity/'receiver.p12')], stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic()+10
        while 'READY' not in server_log.read_text() and time.monotonic()<deadline: time.sleep(.1)
        assert 'READY' in server_log.read_text(), server_log.read_text()
        time.sleep(.3)
        for mode in ['good','reconnect','bad','forbidden','overload']:
            result = subprocess.run([str(work/'cpp/transport-tests'),pin,str(fixture),'lo0',mode], capture_output=True,text=True,timeout=20)
            print(mode, result.stdout, result.stderr, flush=True)
            assert result.returncode == 0
        server.wait(timeout=35)
        assert server.returncode == 0, server_log.read_text()
    finally:
        if server.poll() is None: server.terminate(); server.wait()
print(server_log.read_text())
print('PASS TLS interoperability, reference recovery, certificate rejection, interface fail-closed and bounded send queues.')
