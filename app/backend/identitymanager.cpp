#include "identitymanager.h"
#include "utils.h"

#include "account/vault.h"

#include <QDebug>

#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/rand.h>

// QSettings keys. SER_CERT/SER_KEY are legacy plaintext slots: they are read
// once for migration and then deleted. New key material lives only in the OS
// vault under the names below.
#define SER_UNIQUEID "uniqueid"
#define SER_CERT "certificate"
#define SER_KEY "key"

#define VAULT_CERT "stream.identity.cert"
#define VAULT_KEY "stream.identity.key"

IdentityManager* IdentityManager::s_Im = nullptr;

IdentityManager*
IdentityManager::get()
{
    // This will always be called first on the main thread,
    // so it's safe to initialize without locks.
    if (s_Im == nullptr) {
        s_Im = new IdentityManager();
    }

    return s_Im;
}

IdentityManager::~IdentityManager() = default;

void IdentityManager::createCredentials()
{
    X509* cert = X509_new();
    THROW_BAD_ALLOC_IF_NULL(cert);

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_PKEY* pk = EVP_RSA_gen(2048);
    THROW_BAD_ALLOC_IF_NULL(pk);
#else
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    THROW_BAD_ALLOC_IF_NULL(ctx);

    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);

    // pk must be initialized on input
    EVP_PKEY* pk = NULL;
    EVP_PKEY_keygen(ctx, &pk);

    EVP_PKEY_CTX_free(ctx);
    THROW_BAD_ALLOC_IF_NULL(pk);
#endif

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 0);
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 60 * 60 * 24 * 365 * 20); // 20 yrs
#else
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 60 * 60 * 24 * 365 * 20); // 20 yrs
#endif

    X509_set_pubkey(cert, pk);

    X509_NAME* name = X509_NAME_new();
    THROW_BAD_ALLOC_IF_NULL(name);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<unsigned char *>(const_cast<char*>("NVIDIA GameStream Client")),
                               -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, name);
    X509_NAME_free(name);

    X509_sign(cert, pk, EVP_sha256());

    BIO* biokey = BIO_new(BIO_s_mem());
    THROW_BAD_ALLOC_IF_NULL(biokey);
    PEM_write_bio_PrivateKey(biokey, pk, NULL, NULL, 0, NULL, NULL);

    BIO* biocert = BIO_new(BIO_s_mem());
    THROW_BAD_ALLOC_IF_NULL(biocert);
    PEM_write_bio_X509(biocert, cert);

    BUF_MEM* mem;
    BIO_get_mem_ptr(biokey, &mem);
    m_CachedPrivateKey = QByteArray(mem->data, (int)mem->length);

    BIO_get_mem_ptr(biocert, &mem);
    m_CachedPemCert = QByteArray(mem->data, (int)mem->length);

    X509_free(cert);
    EVP_PKEY_free(pk);
    BIO_free(biokey);
    BIO_free(biocert);

    // Check that the new keypair is valid before persisting it
    if (getSslCertificate().isNull()) {
        qFatal("Newly generated certificate is unreadable");
    }
    if (getSslKey().isNull()) {
        qFatal("Newly generated private key is unreadable");
    }

    if (m_Vault->isAvailable()) {
        if (m_Vault->save(QStringLiteral(VAULT_CERT), m_CachedPemCert) &&
            m_Vault->save(QStringLiteral(VAULT_KEY), m_CachedPrivateKey)) {
            qInfo() << "Wrote a new GameStream identity to the OS vault";
        }
        else {
            qWarning() << "Failed to store the GameStream identity in the OS vault:"
                       << m_Vault->lastErrorCode();
        }
    }
    else {
        // No plaintext fallback. The identity lives for this process only and
        // the user will have to re-pair the host after a restart.
        qWarning() << "Vault unavailable: the GameStream identity will not persist";
    }
}

void IdentityManager::loadCredentials()
{
    bool haveCert = false;
    bool haveKey = false;

    if (m_Vault->isAvailable()) {
        const QString certKey = QStringLiteral(VAULT_CERT);
        const QString keyKey = QStringLiteral(VAULT_KEY);
        const QByteArray cert = m_Vault->load(certKey, &haveCert);
        const QByteArray key = m_Vault->load(keyKey, &haveKey);
        if (haveCert && haveKey) {
            m_CachedPemCert = cert;
            m_CachedPrivateKey = key;
            return;
        }
    }

    QSettings settings;
    const QByteArray legacyCert = settings.value(SER_CERT).toByteArray();
    const QByteArray legacyKey = settings.value(SER_KEY).toByteArray();
    if (legacyCert.isEmpty() || legacyKey.isEmpty()) {
        return;
    }

    m_CachedPemCert = legacyCert;
    m_CachedPrivateKey = legacyKey;

    if (m_Vault->isAvailable()) {
        if (m_Vault->save(QStringLiteral(VAULT_CERT), legacyCert) &&
            m_Vault->save(QStringLiteral(VAULT_KEY), legacyKey)) {
            qInfo() << "Migrated the GameStream identity out of plaintext settings";
        }
        else {
            qWarning() << "Vault write failed while migrating the GameStream identity:"
                       << m_Vault->lastErrorCode();
        }
    }
    else {
        qWarning() << "Vault unavailable: keeping the migrated identity in memory only";
    }

    // Always remove the plaintext copy, even when the vault write failed. A
    // re-pair is cheaper than leaving a private key on disk.
    settings.remove(SER_CERT);
    settings.remove(SER_KEY);
    settings.sync();
}

IdentityManager::IdentityManager()
{
    m_Vault.reset(new Vault());
    loadCredentials();

    if (m_CachedPemCert.isEmpty() || m_CachedPrivateKey.isEmpty() ||
        getSslCertificate().isNull() || getSslKey().isNull()) {
        qInfo() << "No usable credentials found";
        createCredentials();
    }

    // We should have valid credentials now. If not, we're screwed
    if (getSslCertificate().isNull()) {
        qFatal("Certificate is unreadable");
    }
    if (getSslKey().isNull()) {
        qFatal("Private key is unreadable");
    }
}

QSslCertificate
IdentityManager::getSslCertificate()
{
    if (m_CachedSslCert.isNull()) {
        m_CachedSslCert = QSslCertificate(m_CachedPemCert);
    }
    return m_CachedSslCert;
}

QSslKey
IdentityManager::getSslKey()
{
    if (m_CachedSslKey.isNull()) {
        // This seemingly useless const_cast is required for old OpenSSL headers
        // where BIO_new_mem_buf's parameter is not declared const like those on
        // the Steam Link hardware.
        BIO* bio = BIO_new_mem_buf(const_cast<char*>(m_CachedPrivateKey.constData()), -1);
        THROW_BAD_ALLOC_IF_NULL(bio);

        EVP_PKEY* pk = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);

        bio = BIO_new(BIO_s_mem());
        THROW_BAD_ALLOC_IF_NULL(bio);

        // We must write out our PEM in the old PKCS1 format for SecureTransport
        // on macOS/iOS to be able to read it.
#ifdef Q_OS_DARWIN
        PEM_write_bio_PrivateKey_traditional(bio, pk, nullptr, nullptr, 0, nullptr, 0);
#else
        PEM_write_bio_PrivateKey(bio, pk, nullptr, nullptr, 0, nullptr, 0);
#endif

        BUF_MEM* mem;
        BIO_get_mem_ptr(bio, &mem);
        m_CachedSslKey = QSslKey(QByteArray::fromRawData(mem->data, (int)mem->length), QSsl::Rsa);

        BIO_free(bio);
        EVP_PKEY_free(pk);
    }
    return m_CachedSslKey;
}

QSslConfiguration
IdentityManager::getSslConfig()
{
    QSslConfiguration sslConfig(QSslConfiguration::defaultConfiguration());
    sslConfig.setLocalCertificate(getSslCertificate());
    sslConfig.setPrivateKey(getSslKey());
    return sslConfig;
}

QString
IdentityManager::getUniqueId()
{
    if (m_CachedUniqueId.isEmpty()) {
        QSettings settings;

        // Load the unique ID from settings
        m_CachedUniqueId = settings.value(SER_UNIQUEID).toString();
        if (!m_CachedUniqueId.isEmpty()) {
            qInfo() << "Loaded unique ID from settings:" << m_CachedUniqueId;
        }
        else {
            // Generate a new unique ID in base 16
            uint64_t uid;
            RAND_bytes(reinterpret_cast<unsigned char*>(&uid), sizeof(uid));
            m_CachedUniqueId = QString::number(uid, 16);

            qInfo() << "Generated new unique ID:" << m_CachedUniqueId;

            settings.setValue(SER_UNIQUEID, m_CachedUniqueId);
        }
    }
    return m_CachedUniqueId;
}

QByteArray
IdentityManager::getCertificate()
{
    return m_CachedPemCert;
}

QByteArray
IdentityManager::getPrivateKey()
{
    return m_CachedPrivateKey;
}
