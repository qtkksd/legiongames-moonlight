#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>

struct UpdateAsset
{
    QString url;
    qint64 size = 0;
    QString sha256;

    bool isValid() const;
};

struct UpdateManifest
{
    bool disabled = false;
    QString schema;
    QString version;
    int build = 0;
    QString commitFull;
    int minSupportedBuild = 0;
    QString keyId;
    QMap<QString, UpdateAsset> assets;
};

// Ed25519-signed update manifest verification.
//
// Manifest layout (schema 1):
//   {"schema":1,"version":"1.2.3","build":456,"commit_full":"<sha>",
//    "min_supported_build":400,"key_id":"prod-2026-01",
//    "assets":{"windows":{"url":"https://...","size":123,"sha256":"<hex>"},
//              "linux":{...}},
//    "signature":"<base64 detached Ed25519>"}
//
// The signature covers the canonical sorted-keys JSON of the object with the
// `signature` member removed (see CryptoUtil::canonicalJson). The signature is
// verified before any field is trusted, then the artifact size and SHA-256 are
// checked after download.
namespace UpdateManifestParser
{
    bool verificationConfigured();
    bool verifyAndParse(const QByteArray &raw, UpdateManifest *out, QString *errorCode);
    bool parseVerified(const QJsonObject &object, UpdateManifest *out, QString *errorCode);

    QByteArray canonicalSignedBytes(const QJsonObject &objectWithoutSignature);
    bool verifyEd25519(const QByteArray &message, const QByteArray &signature,
                       const QByteArray &publicKey, QString *errorCode);
    bool pinnedKey(const QString &keyId, QByteArray *publicKey);

    QString sha256Hex(const QByteArray &data);
    QString errorMessageFor(const QString &code);
};
