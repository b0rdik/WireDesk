#!/usr/bin/env python3
"""Sign both archives with Sparkle's official tool; refuse mismatched key/invalid archives."""
import argparse, base64, datetime, pathlib, re, subprocess, xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument("--sign-tool",required=True);p.add_argument("--key-file",required=True);p.add_argument("--mac",required=True);p.add_argument("--windows",required=True);p.add_argument("--output",required=True);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parent.parent
version=subprocess.check_output(["python3",str(root/"scripts/check-version.py")],text=True).strip()
# Never include the private key in output. Both update clients carry this public key.
public=(root/"updates/public-key.txt").read_text().strip();assert len(base64.b64decode(public))==32
ns="http://www.andymatuschak.org/xml-namespaces/sparkle";ET.register_namespace("sparkle",ns)
out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=True)
verifier=out/"verify-update"
subprocess.run(["swiftc","-parse-as-library",str(root/"scripts/verify-update.swift"),"-o",str(verifier)],check=True)
for platform,file in [("mac",pathlib.Path(a.mac)),("windows",pathlib.Path(a.windows))]:
 assert file.is_file() and file.stat().st_size>0
 signature=subprocess.check_output([a.sign_tool,"--ed-key-file",a.key_file,"-p",str(file)],text=True).strip()
 assert len(base64.b64decode(signature))==64
 subprocess.run([a.sign_tool,"--verify","--ed-key-file",a.key_file,str(file),signature],check=True)
 subprocess.run([str(verifier.resolve()), str(root/"updates/public-key.txt"), str(file), signature],check=True)
 rss=ET.Element("rss",version="2.0");channel=ET.SubElement(rss,"channel");ET.SubElement(channel,"title").text="WireDesk updates"
 item=ET.SubElement(channel,"item");ET.SubElement(item,"title").text="WireDesk "+version
 ET.SubElement(item,"pubDate").text=datetime.datetime.now(datetime.timezone.utc).strftime("%a, %d %b %Y %H:%M:%S GMT")
 ET.SubElement(item,"description").text="Signed WireDesk release. End your active display session before installing."
 for name,value in [("version",version),("shortVersionString",version)]:ET.SubElement(item,"{XXX}".replace("XXX",ns)+name).text=value
 if platform=="mac":ET.SubElement(item,"{XXX}".replace("XXX",ns)+"minimumSystemVersion").text="13.0"
 attrs={"url":"https://github.com/b0rdik/WireDesk/releases/download/v"+version+"/"+file.name,"length":str(file.stat().st_size),"type":"application/octet-stream","{XXX}edSignature".replace("XXX",ns):signature,"{XXX}os".replace("XXX",ns):"macos" if platform=="mac" else "windows"}
 if platform=="windows":attrs["{XXX}installerArguments".replace("XXX",ns)]="/S"
 ET.SubElement(item,"enclosure",attrs)
 ET.indent(rss);ET.ElementTree(rss).write(out/(platform+"-appcast.xml"),encoding="utf-8",xml_declaration=True)
 print("Signed and verified",file.name)

verifier.unlink()
