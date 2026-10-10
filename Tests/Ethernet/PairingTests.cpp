#include "../../Sources/BetterCastReceiverDesktop/PairingProfile.h"
#include <QCoreApplication>
#include <cstdio>
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv); PairingProfile p;
 const QString good="wiredesk://pair?v=1&ip=192.168.50.2&port=51820&pin="+QString(64,'A');
 if(!PairingProfile::parse(good,&p)||p.ip!="192.168.50.2"||p.fingerprint!=QString(64,'a'))return 1;
 const QStringList bad={good+"&v=1",good+"&junk=1",good+"#frag",QString(good).replace("pair?","pair:99?"),QString(good).replace("wiredesk:","https:"),QString(good).replace("192.168.50.2","127.0.0.1"),QString(good).replace("192.168.50.2","0.0.0.0"),QString(good).replace("192.168.50.2","::1"),QString(good).replace("192.168.50.2","224.0.0.1"),QString(good).replace("51820","9999"),QString(good).replace("v=1","v=2"),good.left(good.size()-1),good+QString(4096,'x')};
 for(const auto& value:bad)if(PairingProfile::parse(value,&p))return 2;
 puts("PASS pairing: canonical IPv4, pinned fingerprint, protocol version, duplicate/extra query and malformed input rejection");
}
