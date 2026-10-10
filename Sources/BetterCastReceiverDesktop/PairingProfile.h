#pragma once
#include <QString>
#include <QHostAddress>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
struct PairingProfile {
    QString ip, fingerprint;
    static bool parse(const QString& text, PairingProfile* profile) {
        if (!profile || text.size() > 4096) return false;
        const QUrl url(text.trimmed(), QUrl::StrictMode);
        const QUrlQuery query(url);
        if (!url.isValid() || url.scheme() != "wiredesk" || url.host() != "pair" || url.port() != -1 || !url.userInfo().isEmpty() || !url.fragment().isEmpty() || !url.path().isEmpty()) return false;
        const auto entries = query.queryItems();
        if (entries.size() != 4 || query.allQueryItemValues("v").size() != 1 || query.queryItemValue("v") != "1" ||
            query.allQueryItemValues("ip").size() != 1 || query.allQueryItemValues("pin").size() != 1 ||
            query.allQueryItemValues("port").size() != 1 || query.queryItemValue("port") != "51820") return false;
        const auto ip = query.queryItemValue("ip"), pin = query.queryItemValue("pin").toLower();
        const QHostAddress address(ip);
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.toString() != ip || address.isNull() || address.isLoopback() || address.isMulticast() || address == QHostAddress::AnyIPv4 || address.toIPv4Address() == 0xffffffff) return false;
        if (!QRegularExpression("^[0-9a-f]{64}$").match(pin).hasMatch()) return false;
        profile->ip = ip; profile->fingerprint = pin; return true;
    }
};
