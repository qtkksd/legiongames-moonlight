#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

// Small, dependency-free crypto/encoding helpers shared by the account modules.
//
// Canonical JSON follows the subset of RFC 8785 (JCS) that the client and the
// server actually exchange: object keys are sorted by code point, no
// whitespace, the string escapes are the RFC 8785 set, booleans/null are
// literal, and numbers are emitted as integers when the value is integral.
// Signed payloads (consent, update manifest) must therefore contain only
// strings, booleans, integers, arrays and objects. A non-integral number is
// still formatted deterministically, but the publisher must not use one.
namespace CryptoUtil
{
    QByteArray base64UrlEncode(const QByteArray &data);
    QByteArray base64UrlDecode(const QByteArray &text);
    QString base64UrlString(const QByteArray &data);

    QByteArray sha256(const QByteArray &data);
    QByteArray randomBytes(int count);
    QString randomUuid();

    QByteArray canonicalJson(const QJsonObject &object);
    QString canonicalJsonString(const QJsonObject &object);
}
