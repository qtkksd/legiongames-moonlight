#pragma once

#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QSettings>

#include <memory>

class Vault;

// The legacy GameStream TLS identity (self-signed RSA cert + key) used when
// pairing with a host. It is NOT the LegionGames account device key.
//
// The key used to be stored as plaintext PEM in QSettings. It now lives in the
// OS vault. A plaintext copy found on disk is migrated once and deleted; when
// no vault is available the identity is generated in memory only and is never
// written to disk.
class IdentityManager
{
public:
    ~IdentityManager();

    QString
    getUniqueId();

    QByteArray
    getCertificate();

    QByteArray
    getPrivateKey();

    QSslConfiguration
    getSslConfig();

    static
    IdentityManager*
    get();

private:
    IdentityManager();

    QSslCertificate
    getSslCertificate();

    QSslKey
    getSslKey();

    void
    createCredentials();

    void
    loadCredentials();

    // Initialized in constructor
    QByteArray m_CachedPrivateKey;
    QByteArray m_CachedPemCert;

    // Lazy initialized
    QString m_CachedUniqueId;
    QSslCertificate m_CachedSslCert;
    QSslKey m_CachedSslKey;

    std::unique_ptr<Vault> m_Vault;

    static IdentityManager* s_Im;
};
