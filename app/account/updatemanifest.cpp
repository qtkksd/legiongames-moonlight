#include "updatemanifest.h"

#include "cryptoutil.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>

#include <openssl/evp.h>

// ---------------------------------------------------------------------------
// UPDATE TRUST ANCHOR (PLACEHOLDER - MUST BE REPLACED BEFORE RELEASE)
//
// `kUpdateKeys` pins the Ed25519 public keys that sign the update manifest.
// The entry shipped here is a placeholder: 32 zero bytes. No real signature can
// verify against it, so the updater fails closed and refuses every update until
// the operator replaces the bytes with the production key from WP0 key custody.
// The key id "prod-2026-01" is the documented rotation slot; add a second entry
// for a new key id during rotation and keep the old one until the cohort moved.
//
// Do not weaken this to "accept if not configured": a client that downloads and
// executes an unverified installer hands full vault access to the update
// channel.
// ---------------------------------------------------------------------------
static const unsigned char kPlaceholderKey[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

#ifdef LG_STAGING_BUILD
// Staging-only update trust anchor. The private key lives outside the repo in
// the deployment secret store; production builds keep the fail-closed
// placeholder until the production key is installed.
static const unsigned char kStagingKey[32] = {
    0x90, 0x44, 0xa5, 0x78, 0x26, 0x7a, 0xd5, 0x46,
    0xfa, 0x52, 0x77, 0xe7, 0x27, 0x07, 0x48, 0x33,
    0xa0, 0xd6, 0x58, 0xc7, 0xa6, 0xf1, 0x58, 0xb9,
    0xba, 0x5d, 0xa0, 0xa4, 0x10, 0x2a, 0x78, 0xb3,
};
#endif

namespace {

struct PinnedKey
{
    const char *id;
    const unsigned char *bytes;
};

const PinnedKey kUpdateKeys[] = {
#ifdef LG_STAGING_BUILD
    { "stg-2026-01", kStagingKey },
#else
    { "prod-2026-01", kPlaceholderKey },
#endif
};

bool isPlaceholder(const QByteArray &key)
{
    if (key.size() != 32) {
        return true;
    }
    for (const char byte : key) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

bool isValidSha256Hex(const QString &value)
{
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return hex.match(value).hasMatch();
}

bool decodeSignature(const QString &encoded, QByteArray *out)
{
    QByteArray decoded = CryptoUtil::base64UrlDecode(encoded.toLatin1());
    if (decoded.size() != 64) {
        QByteArray padded = encoded.toLatin1();
        while (padded.size() % 4 != 0) {
            padded.append('=');
        }
        decoded = QByteArray::fromBase64(padded);
    }
    if (decoded.size() != 64) {
        return false;
    }
    *out = decoded;
    return true;
}

} // namespace

bool UpdateAsset::isValid() const
{
    if (url.isEmpty() || size <= 0 || !isValidSha256Hex(sha256)) {
        return false;
    }
    const QUrl parsed(url);
    return parsed.isValid() &&
           parsed.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0 &&
           !parsed.host().isEmpty();
}

bool UpdateManifestParser::verificationConfigured()
{
    for (const PinnedKey &key : kUpdateKeys) {
        const QByteArray bytes(reinterpret_cast<const char *>(key.bytes), 32);
        if (!isPlaceholder(bytes)) {
            return true;
        }
    }
    return false;
}

bool UpdateManifestParser::pinnedKey(const QString &keyId, QByteArray *publicKey)
{
    if (keyId.isEmpty() || !verificationConfigured()) {
        return false;
    }

    for (const PinnedKey &key : kUpdateKeys) {
        if (keyId != QLatin1String(key.id)) {
            continue;
        }
        const QByteArray bytes(reinterpret_cast<const char *>(key.bytes), 32);
        if (isPlaceholder(bytes)) {
            return false;
        }
        if (publicKey) {
            *publicKey = bytes;
        }
        return true;
    }

    return false;
}

QByteArray UpdateManifestParser::canonicalSignedBytes(const QJsonObject &objectWithoutSignature)
{
    return CryptoUtil::canonicalJson(objectWithoutSignature);
}

QString UpdateManifestParser::sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(CryptoUtil::sha256(data).toHex());
}

bool UpdateManifestParser::verifyEd25519(const QByteArray &message,
                                         const QByteArray &signature,
                                         const QByteArray &publicKey, QString *errorCode)
{
    if (signature.size() != 64 || publicKey.size() != 32) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_signature_invalid");
        }
        return false;
    }

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    EVP_PKEY *key = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(publicKey.constData()),
        static_cast<size_t>(publicKey.size()));
    if (key == nullptr) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_verification_unavailable");
        }
        return false;
    }

    bool valid = false;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx != nullptr) {
        if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1) {
            const int result = EVP_DigestVerify(
                ctx,
                reinterpret_cast<const unsigned char *>(signature.constData()),
                static_cast<size_t>(signature.size()),
                reinterpret_cast<const unsigned char *>(message.constData()),
                static_cast<size_t>(message.size()));
            valid = (result == 1);
        }
        EVP_MD_CTX_free(ctx);
    }
    EVP_PKEY_free(key);

    if (!valid && errorCode) {
        *errorCode = QStringLiteral("update_signature_invalid");
    }
    return valid;
#else
    // The pinned build toolchain always ships OpenSSL 1.1.1+ (macOS 3.x,
    // Windows 3.x, Linux pkg-config). Refuse rather than skip verification.
    if (errorCode) {
        *errorCode = QStringLiteral("update_verification_unavailable");
    }
    return false;
#endif
}

bool UpdateManifestParser::verifyAndParse(const QByteArray &raw, UpdateManifest *out,
                                          QString *errorCode)
{
    if (out == nullptr) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_manifest_malformed");
        }
        return false;
    }
    *out = UpdateManifest();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (document.isNull() || !document.isObject()) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_manifest_malformed");
        }
        return false;
    }

    const QJsonObject object = document.object();

    if (!verificationConfigured()) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_verification_unavailable");
        }
        return false;
    }

    const QString keyId = object.value(QStringLiteral("key_id")).toString();
    QByteArray publicKey;
    if (!pinnedKey(keyId, &publicKey)) {
        if (errorCode) {
            *errorCode = keyId.isEmpty() ? QStringLiteral("update_manifest_malformed")
                                         : QStringLiteral("update_key_unknown");
        }
        return false;
    }

    const QString signatureField = object.value(QStringLiteral("signature")).toString();
    QByteArray signature;
    if (!decodeSignature(signatureField, &signature)) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_signature_invalid");
        }
        return false;
    }

    QJsonObject signedObject = object;
    signedObject.remove(QStringLiteral("signature"));

    const QByteArray message = canonicalSignedBytes(signedObject);
    if (!verifyEd25519(message, signature, publicKey, errorCode)) {
        return false;
    }

    return parseVerified(signedObject, out, errorCode);
}

bool UpdateManifestParser::parseVerified(const QJsonObject &object, UpdateManifest *out,
                                         QString *errorCode)
{
    if (out == nullptr) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_manifest_malformed");
        }
        return false;
    }

    UpdateManifest manifest;
    manifest.disabled = object.value(QStringLiteral("disabled")).toBool(false);

    const QJsonValue schemaValue = object.value(QStringLiteral("schema"));
    if (schemaValue.isString()) {
        manifest.schema = schemaValue.toString();
    }
    else if (schemaValue.isDouble()) {
        manifest.schema = QString::number(static_cast<qint64>(schemaValue.toDouble()));
    }

    if (manifest.schema != QLatin1String("1")) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_schema_unsupported");
        }
        return false;
    }

    manifest.keyId = object.value(QStringLiteral("key_id")).toString();

    if (manifest.disabled) {
        // A signed "disabled" manifest is a valid way to stop update offers
        // without inventing asset metadata.
        *out = manifest;
        if (errorCode) {
            errorCode->clear();
        }
        return true;
    }

    manifest.version = object.value(QStringLiteral("version")).toString();
    manifest.build = static_cast<int>(object.value(QStringLiteral("build")).toDouble(0));
    manifest.commitFull = object.value(QStringLiteral("commit_full")).toString();
    manifest.minSupportedBuild =
        static_cast<int>(object.value(QStringLiteral("min_supported_build")).toDouble(0));

    if (manifest.version.isEmpty() || manifest.build <= 0 ||
        manifest.commitFull.isEmpty() || manifest.minSupportedBuild < 0) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_manifest_malformed");
        }
        return false;
    }

    if (manifest.build < manifest.minSupportedBuild) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_manifest_invalid");
        }
        return false;
    }

    const QJsonValue assetsValue = object.value(QStringLiteral("assets"));
    if (!assetsValue.isObject()) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_manifest_malformed");
        }
        return false;
    }

    const QJsonObject assets = assetsValue.toObject();
    for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        UpdateAsset asset;
        asset.url = entry.value(QStringLiteral("url")).toString();
        asset.size = static_cast<qint64>(entry.value(QStringLiteral("size")).toDouble(0));
        asset.sha256 = entry.value(QStringLiteral("sha256")).toString();
        if (!asset.isValid()) {
            if (errorCode) {
                *errorCode = QStringLiteral("update_manifest_invalid");
            }
            return false;
        }
        asset.sha256 = asset.sha256.toLower();
        manifest.assets.insert(it.key(), asset);
    }

    if (manifest.assets.isEmpty()) {
        if (errorCode) {
            *errorCode = QStringLiteral("update_manifest_invalid");
        }
        return false;
    }

    *out = manifest;
    if (errorCode) {
        errorCode->clear();
    }
    return true;
}

QString UpdateManifestParser::errorMessageFor(const QString &code)
{
    if (code == QLatin1String("update_verification_unavailable")) {
        return QStringLiteral("Обновления отключены: не настроен ключ проверки подписи.");
    }
    if (code == QLatin1String("update_key_unknown")) {
        return QStringLiteral("Обновление подписано неизвестным ключом и отклонено.");
    }
    if (code == QLatin1String("update_signature_invalid")) {
        return QStringLiteral("Подпись обновления неверна. Обновление отклонено.");
    }
    if (code == QLatin1String("update_schema_unsupported")) {
        return QStringLiteral("Формат обновления не поддерживается. Обновите вручную.");
    }
    if (code == QLatin1String("update_hash_mismatch")) {
        return QStringLiteral("Файл обновления повреждён. Обновление отменено.");
    }
    if (code == QLatin1String("update_size_mismatch")) {
        return QStringLiteral("Размер файла обновления не совпадает. Обновление отменено.");
    }
    return QStringLiteral("Не удалось проверить обновление. Обновление отменено.");
}
