#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

struct evp_pkey_st;
class Vault;

// P-256 ECDSA device key.
//
// The private key lives only in the OS vault as an unencrypted PKCS#8 DER blob;
// the vault does the at-rest encryption. The server stores the public JWK, and
// every authenticated request is signed with a raw r||s signature to match
// `web/device_auth.py` on the Django side.
class DeviceKey
{
public:
    ~DeviceKey();

    DeviceKey(const DeviceKey &) = delete;
    DeviceKey &operator=(const DeviceKey &) = delete;

    static DeviceKey *generate();
    static DeviceKey *fromPkcs8(const QByteArray &der);

    // Vault key that holds the private key.
    static QString vaultKey();

    // Loads the key from the vault. When it is missing, generates a new one and
    // saves it before returning so a retried registration reuses the same key.
    // Returns nullptr and sets errorCode on failure; never writes plaintext.
    static DeviceKey *loadOrCreate(Vault *vault, bool *created, QString *errorCode);
    static DeviceKey *load(Vault *vault, QString *errorCode);

    bool isValid() const;
    QByteArray toPkcs8() const;

    QJsonObject publicJwk() const;
    QByteArray canonicalJwk() const;
    QString thumbprint() const;

    // RFC 7638 thumbprint for a JWK object that has crv, kty, x and y.
    static QString thumbprintForJwk(const QJsonObject &jwk);

    // Detached signature: SHA-256, raw 64-byte r||s, empty on failure.
    QByteArray signRaw(const QByteArray &message) const;

    static bool verifyRaw(const QJsonObject &jwk, const QByteArray &message,
                          const QByteArray &signature);

private:
    explicit DeviceKey(evp_pkey_st *key);

    evp_pkey_st *m_key = nullptr;
};
