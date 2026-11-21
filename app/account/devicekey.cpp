#include "devicekey.h"

#include "cryptoutil.h"
#include "vault.h"

#include <QtDebug>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <cstring>

namespace {

EVP_MD_CTX *createMdContext()
{
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    return EVP_MD_CTX_new();
#else
    // Steam Link ships an older OpenSSL; the create/destroy names predate the
    // new/free rename.
    return EVP_MD_CTX_create();
#endif
}

void freeMdContext(EVP_MD_CTX *ctx)
{
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    EVP_MD_CTX_free(ctx);
#else
    EVP_MD_CTX_destroy(ctx);
#endif
}

// DER helpers. Keeping the conversions in this file means the header does not
// expose any OpenSSL types.
QByteArray derTlv(unsigned char tag, const QByteArray &content)
{
    QByteArray out;
    out.append(static_cast<char>(tag));

    const int length = content.size();
    if (length < 0x80) {
        out.append(static_cast<char>(length));
    }
    else if (length < 0x100) {
        out.append(static_cast<char>(0x81));
        out.append(static_cast<char>(length));
    }
    else {
        out.append(static_cast<char>(0x82));
        out.append(static_cast<char>((length >> 8) & 0xff));
        out.append(static_cast<char>(length & 0xff));
    }

    out += content;
    return out;
}

bool readDerLength(const unsigned char *data, int size, int *offset, int *length)
{
    if (*offset >= size) {
        return false;
    }

    unsigned char first = data[(*offset)++];
    if ((first & 0x80) == 0) {
        *length = first;
        return true;
    }

    const int count = first & 0x7f;
    if (count == 0 || count > 2 || *offset + count > size) {
        return false;
    }

    int value = 0;
    for (int i = 0; i < count; ++i) {
        value = (value << 8) | data[(*offset)++];
    }
    *length = value;
    return true;
}

bool normalizeDerInteger(const unsigned char *data, int length, unsigned char out[32])
{
    // Strip a leading zero used only for the DER sign bit.
    while (length > 1 && data[0] == 0x00) {
        ++data;
        --length;
    }
    if (length <= 0 || length > 32) {
        return false;
    }

    std::memset(out, 0, 32);
    std::memcpy(out + (32 - length), data, static_cast<size_t>(length));
    return true;
}

bool derToRawSignature(const QByteArray &der, QByteArray *raw)
{
    const unsigned char *data = reinterpret_cast<const unsigned char *>(der.constData());
    const int size = der.size();

    int offset = 0;
    if (size < 8 || data[offset++] != 0x30) {
        return false;
    }

    int sequenceLength = 0;
    if (!readDerLength(data, size, &offset, &sequenceLength) ||
        offset + sequenceLength != size) {
        return false;
    }

    if (offset >= size || data[offset++] != 0x02) {
        return false;
    }
    int rLength = 0;
    if (!readDerLength(data, size, &offset, &rLength) || offset + rLength > size) {
        return false;
    }
    const unsigned char *r = data + offset;
    offset += rLength;

    if (offset >= size || data[offset++] != 0x02) {
        return false;
    }
    int sLength = 0;
    if (!readDerLength(data, size, &offset, &sLength) || offset + sLength != size) {
        return false;
    }
    const unsigned char *s = data + offset;

    unsigned char rFixed[32];
    unsigned char sFixed[32];
    if (!normalizeDerInteger(r, rLength, rFixed) ||
        !normalizeDerInteger(s, sLength, sFixed)) {
        return false;
    }

    raw->resize(64);
    std::memcpy(raw->data(), rFixed, 32);
    std::memcpy(raw->data() + 32, sFixed, 32);
    return true;
}

QByteArray rawToDerSignature(const QByteArray &raw)
{
    if (raw.size() != 64) {
        return QByteArray();
    }

    const unsigned char *r =
        reinterpret_cast<const unsigned char *>(raw.constData());
    const unsigned char *s = r + 32;

    QByteArray rContent;
    if (r[0] & 0x80) {
        rContent.append('\0');
    }
    rContent.append(reinterpret_cast<const char *>(r), 32);

    QByteArray sContent;
    if (s[0] & 0x80) {
        sContent.append('\0');
    }
    sContent.append(reinterpret_cast<const char *>(s), 32);

    return derTlv(0x30, derTlv(0x02, rContent) + derTlv(0x02, sContent));
}

// SubjectPublicKeyInfo for an uncompressed P-256 point. Built by hand so the
// public-key path does not depend on the deprecated EC_KEY API.
QByteArray publicKeyDer(const QByteArray &x, const QByteArray &y)
{
    if (x.size() != 32 || y.size() != 32) {
        return QByteArray();
    }

    static const unsigned char algorithmIdentifier[] = {
        0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,
        0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07,
    };

    QByteArray point;
    point.append('\x04');
    point += x;
    point += y;

    QByteArray bitString;
    bitString.append('\0');
    bitString += point;

    QByteArray sequence;
    sequence += QByteArray(reinterpret_cast<const char *>(algorithmIdentifier),
                           sizeof(algorithmIdentifier));
    sequence += derTlv(0x03, bitString);

    return derTlv(0x30, sequence);
}

bool publicPoint(evp_pkey_st *key, QByteArray *x, QByteArray *y)
{
    unsigned char *der = nullptr;
    const int length = i2d_PUBKEY(key, &der);
    if (der == nullptr || length < 65) {
        if (der != nullptr) {
            OPENSSL_free(der);
        }
        return false;
    }

    // The encoded SPKI ends with the uncompressed point for a P-256 key.
    const unsigned char *point = der + length - 65;
    const bool valid = point[0] == 0x04;
    if (valid) {
        *x = QByteArray(reinterpret_cast<const char *>(point + 1), 32);
        *y = QByteArray(reinterpret_cast<const char *>(point + 33), 32);
    }

    OPENSSL_free(der);
    return valid;
}

evp_pkey_st *generateP256()
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    // Scoped so the OpenSSL 3 fast path does not collide with the fallback
    // keygen variable below when both blocks are compiled.
    {
        evp_pkey_st *generated = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
        if (generated != nullptr) {
            return generated;
        }
    }
#endif

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (ctx == nullptr) {
        return nullptr;
    }

    EVP_PKEY *generated = nullptr;
    const bool ok = EVP_PKEY_keygen_init(ctx) > 0 &&
                    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) > 0 &&
                    EVP_PKEY_keygen(ctx, &generated) > 0;

    EVP_PKEY_CTX_free(ctx);
    return ok ? generated : nullptr;
}

} // namespace

DeviceKey::DeviceKey(evp_pkey_st *key) :
    m_key(key)
{
}

DeviceKey::~DeviceKey()
{
    if (m_key != nullptr) {
        EVP_PKEY_free(m_key);
        m_key = nullptr;
    }
}

QString DeviceKey::vaultKey()
{
    return QStringLiteral("device.key");
}

DeviceKey *DeviceKey::generate()
{
    evp_pkey_st *key = generateP256();
    if (key == nullptr) {
        qWarning() << "DeviceKey: P-256 key generation failed";
        return nullptr;
    }

    DeviceKey *result = new DeviceKey(key);
    if (!result->isValid()) {
        qWarning() << "DeviceKey: generated key could not be exported";
        delete result;
        return nullptr;
    }
    return result;
}

DeviceKey *DeviceKey::fromPkcs8(const QByteArray &der)
{
    if (der.isEmpty()) {
        return nullptr;
    }

    BIO *bio = BIO_new_mem_buf(der.constData(), der.size());
    if (bio == nullptr) {
        return nullptr;
    }

    EVP_PKEY *key = d2i_PKCS8PrivateKey_bio(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (key == nullptr) {
        return nullptr;
    }

    DeviceKey *result = new DeviceKey(key);
    if (!result->isValid()) {
        delete result;
        return nullptr;
    }
    return result;
}

DeviceKey *DeviceKey::load(Vault *vault, QString *errorCode)
{
    if (vault == nullptr) {
        if (errorCode) {
            *errorCode = QStringLiteral("vault_unavailable");
        }
        return nullptr;
    }
    if (!vault->isAvailable()) {
        if (errorCode) {
            *errorCode = QStringLiteral("vault_unavailable");
        }
        return nullptr;
    }

    bool ok = false;
    const QByteArray der = vault->load(vaultKey(), &ok);
    if (!ok) {
        if (errorCode) {
            *errorCode = vault->lastErrorCode().isEmpty()
                             ? QStringLiteral("vault_error")
                             : vault->lastErrorCode();
        }
        return nullptr;
    }

    DeviceKey *key = fromPkcs8(der);
    if (key == nullptr) {
        if (errorCode) {
            *errorCode = QStringLiteral("device_key_corrupt");
        }
        return nullptr;
    }

    if (errorCode) {
        errorCode->clear();
    }
    return key;
}

DeviceKey *DeviceKey::loadOrCreate(Vault *vault, bool *created, QString *errorCode)
{
    if (created) {
        *created = false;
    }

    if (vault == nullptr || !vault->isAvailable()) {
        if (errorCode) {
            *errorCode = QStringLiteral("vault_unavailable");
        }
        return nullptr;
    }

    bool haveExisting = false;
    const QByteArray existing = vault->load(vaultKey(), &haveExisting);
    if (haveExisting) {
        // Never silently replace an existing key: a fresh key would orphan the
        // registered account. A corrupt blob must go through recovery.
        DeviceKey *key = fromPkcs8(existing);
        if (key == nullptr) {
            if (errorCode) {
                *errorCode = QStringLiteral("device_key_corrupt");
            }
            return nullptr;
        }
        if (errorCode) {
            errorCode->clear();
        }
        return key;
    }

    DeviceKey *key = generate();
    if (key == nullptr) {
        if (errorCode) {
            *errorCode = QStringLiteral("key_generation_failed");
        }
        return nullptr;
    }

    if (!vault->save(vaultKey(), key->toPkcs8())) {
        if (errorCode) {
            *errorCode = vault->lastErrorCode().isEmpty()
                             ? QStringLiteral("vault_error")
                             : vault->lastErrorCode();
        }
        delete key;
        return nullptr;
    }

    if (created) {
        *created = true;
    }
    if (errorCode) {
        errorCode->clear();
    }
    return key;
}

bool DeviceKey::isValid() const
{
    if (m_key == nullptr) {
        return false;
    }

    QByteArray x;
    QByteArray y;
    return publicPoint(m_key, &x, &y);
}

QByteArray DeviceKey::toPkcs8() const
{
    if (m_key == nullptr) {
        return QByteArray();
    }

    BIO *bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
        return QByteArray();
    }

    QByteArray der;
    if (i2d_PKCS8PrivateKey_bio(bio, m_key, nullptr, nullptr, 0, nullptr, nullptr) == 1) {
        BUF_MEM *mem = nullptr;
        BIO_get_mem_ptr(bio, &mem);
        if (mem != nullptr && mem->data != nullptr) {
            der = QByteArray(mem->data, static_cast<int>(mem->length));
        }
    }

    BIO_free(bio);
    return der;
}

QJsonObject DeviceKey::publicJwk() const
{
    QJsonObject jwk;
    QByteArray x;
    QByteArray y;
    if (m_key == nullptr || !publicPoint(m_key, &x, &y)) {
        return jwk;
    }

    jwk.insert(QStringLiteral("crv"), QStringLiteral("P-256"));
    jwk.insert(QStringLiteral("kty"), QStringLiteral("EC"));
    jwk.insert(QStringLiteral("x"), CryptoUtil::base64UrlString(x));
    jwk.insert(QStringLiteral("y"), CryptoUtil::base64UrlString(y));
    return jwk;
}

QByteArray DeviceKey::canonicalJwk() const
{
    return CryptoUtil::canonicalJson(publicJwk());
}

QString DeviceKey::thumbprint() const
{
    return thumbprintForJwk(publicJwk());
}

QString DeviceKey::thumbprintForJwk(const QJsonObject &jwk)
{
    if (jwk.value(QStringLiteral("kty")).toString() != QLatin1String("EC") ||
        jwk.value(QStringLiteral("crv")).toString() != QLatin1String("P-256")) {
        return QString();
    }

    const QString x = jwk.value(QStringLiteral("x")).toString();
    const QString y = jwk.value(QStringLiteral("y")).toString();
    if (x.isEmpty() || y.isEmpty()) {
        return QString();
    }

    // RFC 7638: the required members in lexicographic order, no whitespace.
    QJsonObject required;
    required.insert(QStringLiteral("crv"), QStringLiteral("P-256"));
    required.insert(QStringLiteral("kty"), QStringLiteral("EC"));
    required.insert(QStringLiteral("x"), x);
    required.insert(QStringLiteral("y"), y);

    return CryptoUtil::base64UrlString(CryptoUtil::sha256(CryptoUtil::canonicalJson(required)));
}

QByteArray DeviceKey::signRaw(const QByteArray &message) const
{
    if (m_key == nullptr) {
        return QByteArray();
    }

    EVP_MD_CTX *ctx = createMdContext();
    if (ctx == nullptr) {
        return QByteArray();
    }

    QByteArray signature;
    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, m_key) == 1 &&
        EVP_DigestSignUpdate(ctx, message.constData(),
                             static_cast<size_t>(message.size())) == 1) {
        size_t length = 0;
        if (EVP_DigestSignFinal(ctx, nullptr, &length) == 1 && length > 0) {
            QByteArray der(static_cast<int>(length), '\0');
            if (EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char *>(der.data()),
                                    &length) == 1) {
                der.resize(static_cast<int>(length));

                QByteArray raw;
                if (derToRawSignature(der, &raw)) {
                    signature = raw;
                }
            }
        }
    }

    freeMdContext(ctx);
    return signature;
}

bool DeviceKey::verifyRaw(const QJsonObject &jwk, const QByteArray &message,
                          const QByteArray &signature)
{
    if (jwk.value(QStringLiteral("kty")).toString() != QLatin1String("EC") ||
        jwk.value(QStringLiteral("crv")).toString() != QLatin1String("P-256") ||
        signature.size() != 64) {
        return false;
    }

    const QByteArray x = CryptoUtil::base64UrlDecode(
        jwk.value(QStringLiteral("x")).toString().toLatin1());
    const QByteArray y = CryptoUtil::base64UrlDecode(
        jwk.value(QStringLiteral("y")).toString().toLatin1());
    if (x.size() != 32 || y.size() != 32) {
        return false;
    }

    const QByteArray spki = publicKeyDer(x, y);
    if (spki.isEmpty()) {
        return false;
    }

    const unsigned char *cursor =
        reinterpret_cast<const unsigned char *>(spki.constData());
    EVP_PKEY *key = d2i_PUBKEY(nullptr, &cursor, spki.size());
    if (key == nullptr) {
        return false;
    }

    const QByteArray derSignature = rawToDerSignature(signature);
    bool valid = false;

    EVP_MD_CTX *ctx = createMdContext();
    if (ctx != nullptr && !derSignature.isEmpty()) {
        valid = EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, key) == 1 &&
                EVP_DigestVerify(ctx,
                                 reinterpret_cast<const unsigned char *>(
                                     derSignature.constData()),
                                 static_cast<size_t>(derSignature.size()),
                                 reinterpret_cast<const unsigned char *>(
                                     message.constData()),
                                 static_cast<size_t>(message.size())) == 1;
        freeMdContext(ctx);
    }

    EVP_PKEY_free(key);
    return valid;
}
