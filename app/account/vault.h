#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

// Backend interface implemented per platform. Kept in the header so the unit
// tests can inject a fake backend and prove the "no plaintext fallback"
// behaviour without needing a real OS key store.
class SecretBackend
{
public:
    virtual ~SecretBackend() = default;

    virtual bool isAvailable() const = 0;
    virtual QByteArray load(const QString &name, bool *ok) = 0;
    virtual bool save(const QString &name, const QByteArray &data) = 0;
    virtual bool remove(const QString &name) = 0;
    virtual QString backendName() const = 0;
    virtual QString lastErrorCode() const = 0;
};

// Named encrypted blobs in the OS key store.
//
//   Windows: DPAPI (CryptProtectData/CryptUnprotectData, current-user scope),
//            ciphertext file under QStandardPaths::AppDataLocation.
//   macOS:   Keychain generic password (compile-guarded; macOS is not a
//            supported product target).
//   Linux:   libsecret-1 default collection at compile time.
//
// There is deliberately no file/plaintext fallback. When no backend is
// available, isAvailable() returns false, every write fails and account
// actions must stop with the vault_unavailable error. The same is true when
// the OS store exists but the user denies access.
class Vault
{
public:
    enum class Backend {
        None,
        Dpapi,
        Keychain,
        Libsecret,
    };

    Vault();
    explicit Vault(std::unique_ptr<SecretBackend> backend);
    ~Vault();

    Vault(const Vault &) = delete;
    Vault &operator=(const Vault &) = delete;

    bool isAvailable() const;
    Backend backend() const;
    QString backendName() const;

    // Returns the decrypted blob. `ok` is false when the blob is missing
    // (lastErrorCode() == "vault_missing") or the backend failed.
    QByteArray load(const QString &name, bool *ok = nullptr);
    bool save(const QString &name, const QByteArray &data);
    bool remove(const QString &name);

    QString lastErrorCode() const { return m_lastErrorCode; }
    QString lastErrorMessage() const { return m_lastErrorMessage; }

    // Russian, user-safe message for an error code produced by this class.
    static QString errorMessageFor(const QString &code);

private:
    void ensureBackend();
    void setError(const QString &code);

    std::unique_ptr<SecretBackend> m_backend;
    bool m_backendResolved = false;
    QString m_lastErrorCode;
    QString m_lastErrorMessage;
};

// Creates the backend for the current platform/build configuration.
std::unique_ptr<SecretBackend> createPlatformSecretBackend();
