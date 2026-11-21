#include "vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtDebug>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <wincrypt.h>
#include <sddl.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(HAVE_LIBSECRET)
// glib/gio headers use "signals" as a struct field name, while Qt defines
// "signals" as a macro. Drop the macro for the C headers; this file declares
// no Qt signals after this point.
#undef signals
#include <libsecret/secret.h>
#endif

namespace {

const char kServiceName[] = "ru.legiongames.moonlight";
const char kDpapiEntropy[] = "ru.legiongames.moonlight/vault/v1";

QString sanitizeName(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar ch : name) {
        if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
            (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
            (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
            ch == QLatin1Char('.') || ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            out += ch;
        }
        else {
            out += QLatin1Char('_');
        }
    }
    return out;
}

class NoneSecretBackend : public SecretBackend
{
public:
    bool isAvailable() const override { return false; }
    QByteArray load(const QString &, bool *ok) override
    {
        if (ok) {
            *ok = false;
        }
        return QByteArray();
    }
    bool save(const QString &, const QByteArray &) override { return false; }
    bool remove(const QString &) override { return false; }
    QString backendName() const override { return QStringLiteral("none"); }
    QString lastErrorCode() const override { return QStringLiteral("vault_unavailable"); }
};

#ifdef Q_OS_WIN32

class DpapiSecretBackend : public SecretBackend
{
public:
    bool isAvailable() const override { return true; }
    QString backendName() const override { return QStringLiteral("dpapi"); }
    QString lastErrorCode() const override { return m_error; }

    QByteArray load(const QString &name, bool *ok) override
    {
        if (ok) {
            *ok = false;
        }
        m_error.clear();

        QFile file(filePath(name));
        if (!file.exists()) {
            m_error = QStringLiteral("vault_missing");
            return QByteArray();
        }
        if (!file.open(QIODevice::ReadOnly)) {
            m_error = QStringLiteral("vault_error");
            qWarning() << "Vault: cannot read DPAPI blob" << sanitizeName(name)
                       << file.errorString();
            return QByteArray();
        }
        const QByteArray blob = file.readAll();
        file.close();
        if (blob.isEmpty()) {
            m_error = QStringLiteral("vault_error");
            return QByteArray();
        }

        DATA_BLOB in;
        in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(blob.constData()));
        in.cbData = static_cast<DWORD>(blob.size());

        DATA_BLOB entropy = entropyBlob();
        DATA_BLOB out;
        out.pbData = nullptr;
        out.cbData = 0;

        const BOOL okProtect = CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr,
                                                  CRYPTPROTECT_UI_FORBIDDEN, &out);

        if (!okProtect) {
            m_error = QStringLiteral("vault_unavailable");
            qWarning() << "Vault: CryptUnprotectData failed" << GetLastError();
            return QByteArray();
        }

        QByteArray plain(reinterpret_cast<const char *>(out.pbData),
                         static_cast<int>(out.cbData));
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);

        if (ok) {
            *ok = true;
        }
        return plain;
    }

    bool save(const QString &name, const QByteArray &data) override
    {
        m_error.clear();

        DATA_BLOB in;
        in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(data.constData()));
        in.cbData = static_cast<DWORD>(data.size());

        DATA_BLOB entropy = entropyBlob();
        DATA_BLOB out;
        out.pbData = nullptr;
        out.cbData = 0;

        const BOOL okProtect = CryptProtectData(&in, L"LegionGames Moonlight vault", &entropy,
                                                nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                                                &out);

        if (!okProtect) {
            m_error = QStringLiteral("vault_unavailable");
            qWarning() << "Vault: CryptProtectData failed" << GetLastError();
            return false;
        }

        QDir().mkpath(directoryPath());
        const QString path = filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_error = QStringLiteral("vault_error");
            qWarning() << "Vault: cannot write DPAPI blob" << path << file.errorString();
            SecureZeroMemory(out.pbData, out.cbData);
            LocalFree(out.pbData);
            return false;
        }

        const qint64 written = file.write(reinterpret_cast<const char *>(out.pbData),
                                          static_cast<qint64>(out.cbData));
        file.close();
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);

        if (written < 0) {
            m_error = QStringLiteral("vault_error");
            QFile::remove(path);
            return false;
        }

        if (!restrictToCurrentUser(path)) {
            // The blob is still DPAPI-encrypted, but a failed ACL change means
            // another local user could read (not decrypt) it. Treat as an error
            // rather than silently keeping a weaker file.
            m_error = QStringLiteral("vault_error");
            qWarning() << "Vault: cannot restrict ACL on" << path;
            QFile::remove(path);
            return false;
        }

        return true;
    }

    bool remove(const QString &name) override
    {
        m_error.clear();
        const QString path = filePath(name);
        if (!QFile::exists(path)) {
            return true;
        }
        if (!QFile::remove(path)) {
            m_error = QStringLiteral("vault_error");
            return false;
        }
        return true;
    }

private:
    static QString directoryPath()
    {
        QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (base.isEmpty()) {
            base = QDir::homePath() + QStringLiteral("/.legiongames");
        }
        return base + QStringLiteral("/vault");
    }

    static QString filePath(const QString &name)
    {
        return directoryPath() + QLatin1Char('/') + sanitizeName(name) + QStringLiteral(".bin");
    }

    static DATA_BLOB entropyBlob()
    {
        // DPAPI only reads the entropy during the call and never takes
        // ownership, so a static buffer is enough.
        DATA_BLOB entropy;
        entropy.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(kDpapiEntropy));
        entropy.cbData = static_cast<DWORD>(sizeof(kDpapiEntropy) - 1);
        return entropy;
    }

    static bool restrictToCurrentUser(const QString &path)
    {
        // Protected DACL granting full access to the file owner only. This
        // removes inherited ACEs so other local users cannot read the blob.
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;FA;;;OW)", SDDL_REVISION_1, &sd, nullptr)) {
            return false;
        }

        const BOOL ok = SetFileSecurityW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                         DACL_SECURITY_INFORMATION |
                                             PROTECTED_DACL_SECURITY_INFORMATION,
                                         sd);
        LocalFree(sd);
        return ok != FALSE;
    }

    QString m_error;
};

#elif defined(Q_OS_MACOS)

class KeychainSecretBackend : public SecretBackend
{
public:
    bool isAvailable() const override { return true; }
    QString backendName() const override { return QStringLiteral("keychain"); }
    QString lastErrorCode() const override { return m_error; }

    QByteArray load(const QString &name, bool *ok) override
    {
        if (ok) {
            *ok = false;
        }
        m_error.clear();

        CFMutableDictionaryRef query = baseQuery(name);
        if (!query) {
            m_error = QStringLiteral("vault_error");
            return QByteArray();
        }
        CFDictionaryAddValue(query, kSecReturnData, kCFBooleanTrue);
        CFDictionaryAddValue(query, kSecMatchLimit, kSecMatchLimitOne);

        CFTypeRef result = nullptr;
        const OSStatus status = SecItemCopyMatching(query, &result);
        CFRelease(query);

        if (status == errSecItemNotFound) {
            m_error = QStringLiteral("vault_missing");
            return QByteArray();
        }
        if (status != errSecSuccess || result == nullptr) {
            m_error = (status == errSecInteractionNotAllowed)
                          ? QStringLiteral("vault_locked")
                          : QStringLiteral("vault_unavailable");
            return QByteArray();
        }

        const CFDataRef data = static_cast<CFDataRef>(result);
        QByteArray out(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
                       static_cast<int>(CFDataGetLength(data)));
        CFRelease(result);

        if (ok) {
            *ok = true;
        }
        return out;
    }

    bool save(const QString &name, const QByteArray &data) override
    {
        m_error.clear();

        // Replace semantics: a stale item from an older schema must not win.
        remove(name);

        CFMutableDictionaryRef item = baseQuery(name);
        if (!item) {
            m_error = QStringLiteral("vault_error");
            return false;
        }

        CFDataRef payload = CFDataCreate(kCFAllocatorDefault,
                                         reinterpret_cast<const UInt8 *>(data.constData()),
                                         static_cast<CFIndex>(data.size()));
        if (payload == nullptr) {
            CFRelease(item);
            m_error = QStringLiteral("vault_error");
            return false;
        }

        CFDictionaryAddValue(item, kSecValueData, payload);
        CFDictionaryAddValue(item, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);

        const OSStatus status = SecItemAdd(item, nullptr);
        CFRelease(payload);
        CFRelease(item);

        if (status != errSecSuccess) {
            m_error = (status == errSecInteractionNotAllowed)
                          ? QStringLiteral("vault_locked")
                          : QStringLiteral("vault_unavailable");
            qWarning() << "Vault: SecItemAdd failed" << status;
            return false;
        }
        return true;
    }

    bool remove(const QString &name) override
    {
        m_error.clear();
        CFMutableDictionaryRef query = baseQuery(name);
        if (!query) {
            m_error = QStringLiteral("vault_error");
            return false;
        }
        const OSStatus status = SecItemDelete(query);
        CFRelease(query);

        if (status == errSecSuccess || status == errSecItemNotFound) {
            return true;
        }
        m_error = QStringLiteral("vault_error");
        return false;
    }

private:
    static CFStringRef makeString(const QString &value)
    {
        return CFStringCreateWithCharacters(kCFAllocatorDefault,
                                            reinterpret_cast<const UniChar *>(value.utf16()),
                                            static_cast<CFIndex>(value.size()));    }

    static CFMutableDictionaryRef baseQuery(const QString &name)
    {
        CFMutableDictionaryRef query =
            CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                      &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
        if (!query) {
            return nullptr;
        }

        CFStringRef service = makeString(QString::fromLatin1(kServiceName));
        CFStringRef account = makeString(name);
        if (!service || !account) {
            if (service) {
                CFRelease(service);
            }
            if (account) {
                CFRelease(account);
            }
            CFRelease(query);
            return nullptr;
        }

        CFDictionaryAddValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionaryAddValue(query, kSecAttrService, service);
        CFDictionaryAddValue(query, kSecAttrAccount, account);
        CFRelease(service);
        CFRelease(account);
        return query;
    }

    QString m_error;
};

#elif defined(HAVE_LIBSECRET)

class LibsecretSecretBackend : public SecretBackend
{
public:
    LibsecretSecretBackend() = default;

    bool isAvailable() const override
    {
        if (m_available < 0) {
            GError *error = nullptr;
            SecretService *service =
                secret_service_get_sync(SECRET_SERVICE_NONE, nullptr, &error);
            m_available = (service != nullptr) ? 1 : 0;
            if (service) {
                g_object_unref(service);
            }
            if (error) {
                g_error_free(error);
            }
        }
        return m_available == 1;
    }

    QString backendName() const override { return QStringLiteral("libsecret"); }
    QString lastErrorCode() const override { return m_error; }

    QByteArray load(const QString &name, bool *ok) override
    {
        if (ok) {
            *ok = false;
        }
        m_error.clear();

        GError *error = nullptr;
        const QByteArray nameUtf8 = name.toUtf8();

        // Binary API: an unencrypted PKCS#8 DER key contains NUL bytes and would
        // be truncated by the string-based helper.
        SecretValue *secret = secret_password_lookup_binary_sync(
            schema(), nullptr, &error, "name", nameUtf8.constData(), nullptr);
        if (error) {
            m_error = QStringLiteral("vault_unavailable");
            g_error_free(error);
            return QByteArray();
        }
        if (secret == nullptr) {
            m_error = QStringLiteral("vault_missing");
            return QByteArray();
        }

        gsize length = 0;
        const gchar *data = secret_value_get(secret, &length);
        QByteArray out;
        if (data != nullptr && length > 0) {
            out = QByteArray(data, static_cast<int>(length));
        }
        secret_value_unref(secret);

        if (ok) {
            *ok = true;
        }
        return out;
    }

    bool save(const QString &name, const QByteArray &data) override
    {
        m_error.clear();

        const QByteArray nameUtf8 = name.toUtf8();
        const QByteArray label = QByteArray("LegionGames Moonlight: ") + nameUtf8;

        SecretValue *value = secret_value_new(
            data.constData(), static_cast<gssize>(data.size()),
            "application/octet-stream");
        if (value == nullptr) {
            m_error = QStringLiteral("vault_error");
            return false;
        }

        GError *error = nullptr;
        const gboolean ok = secret_password_store_binary_sync(
            schema(), SECRET_COLLECTION_DEFAULT, label.constData(), value, nullptr,
            &error, "name", nameUtf8.constData(), nullptr);
        secret_value_unref(value);

        if (error) {
            m_error = QStringLiteral("vault_unavailable");
            g_error_free(error);
            return false;
        }
        if (!ok) {
            m_error = QStringLiteral("vault_error");
            return false;
        }
        return true;
    }

    bool remove(const QString &name) override
    {
        m_error.clear();

        GError *error = nullptr;
        const QByteArray nameUtf8 = name.toUtf8();
        const gboolean ok = secret_password_clear_sync(schema(), nullptr, &error,
                                                       "name", nameUtf8.constData(), nullptr);
        if (error) {
            m_error = QStringLiteral("vault_error");
            g_error_free(error);
            return false;
        }
        return ok != FALSE;
    }

private:
    static const SecretSchema *schema()
    {
        static const SecretSchema vaultSchema = {
            const_cast<gchar *>("ru.legiongames.moonlight.vault"),
            SECRET_SCHEMA_NONE,
            {
                { "name", SECRET_SCHEMA_ATTRIBUTE_STRING },
                { nullptr, static_cast<SecretSchemaAttributeType>(0) },
            },
        };
        return &vaultSchema;
    }

    mutable int m_available = -1;
    QString m_error;
};

#endif

} // namespace

std::unique_ptr<SecretBackend> createPlatformSecretBackend()
{
#if defined(Q_OS_WIN32)
    return std::unique_ptr<SecretBackend>(new DpapiSecretBackend());
#elif defined(Q_OS_MACOS)
    return std::unique_ptr<SecretBackend>(new KeychainSecretBackend());
#elif defined(HAVE_LIBSECRET)
    return std::unique_ptr<SecretBackend>(new LibsecretSecretBackend());
#else
    return std::unique_ptr<SecretBackend>(new NoneSecretBackend());
#endif
}

Vault::Vault() = default;

Vault::Vault(std::unique_ptr<SecretBackend> backend) :
    m_backend(std::move(backend)),
    m_backendResolved(true)
{
}

Vault::~Vault() = default;

void Vault::ensureBackend()
{
    if (!m_backendResolved) {
        m_backend = createPlatformSecretBackend();
        m_backendResolved = true;
    }
}

bool Vault::isAvailable() const
{
    const_cast<Vault *>(this)->ensureBackend();
    return m_backend && m_backend->isAvailable();
}

Vault::Backend Vault::backend() const
{
    const QString name = backendName();
    if (name == QLatin1String("dpapi")) {
        return Backend::Dpapi;
    }
    if (name == QLatin1String("keychain")) {
        return Backend::Keychain;
    }
    if (name == QLatin1String("libsecret")) {
        return Backend::Libsecret;
    }
    return Backend::None;
}

QString Vault::backendName() const
{
    const_cast<Vault *>(this)->ensureBackend();
    if (!m_backend) {
        return QStringLiteral("none");
    }
    return m_backend->backendName();
}

QByteArray Vault::load(const QString &name, bool *ok)
{
    if (ok) {
        *ok = false;
    }

    ensureBackend();
    if (!m_backend || !m_backend->isAvailable()) {
        setError(QStringLiteral("vault_unavailable"));
        return QByteArray();
    }

    bool loaded = false;
    const QByteArray data = m_backend->load(name, &loaded);
    if (!loaded) {
        QString code = m_backend->lastErrorCode();
        if (code.isEmpty()) {
            code = QStringLiteral("vault_error");
        }
        setError(code);
        return QByteArray();
    }

    m_lastErrorCode.clear();
    m_lastErrorMessage.clear();
    if (ok) {
        *ok = true;
    }
    return data;
}

bool Vault::save(const QString &name, const QByteArray &data)
{
    ensureBackend();
    if (!m_backend || !m_backend->isAvailable()) {
        setError(QStringLiteral("vault_unavailable"));
        return false;
    }

    if (!m_backend->save(name, data)) {
        QString code = m_backend->lastErrorCode();
        if (code.isEmpty()) {
            code = QStringLiteral("vault_error");
        }
        setError(code);
        return false;
    }

    m_lastErrorCode.clear();
    m_lastErrorMessage.clear();
    return true;
}

bool Vault::remove(const QString &name)
{
    ensureBackend();
    if (!m_backend || !m_backend->isAvailable()) {
        setError(QStringLiteral("vault_unavailable"));
        return false;
    }

    if (!m_backend->remove(name)) {
        QString code = m_backend->lastErrorCode();
        if (code.isEmpty()) {
            code = QStringLiteral("vault_error");
        }
        setError(code);
        return false;
    }

    m_lastErrorCode.clear();
    m_lastErrorMessage.clear();
    return true;
}

void Vault::setError(const QString &code)
{
    m_lastErrorCode = code;
    m_lastErrorMessage = errorMessageFor(code);
}

QString Vault::errorMessageFor(const QString &code)
{
    if (code == QLatin1String("vault_unavailable")) {
        return QStringLiteral("Не удалось открыть хранилище ключей. Аккаунт недоступен.");
    }
    if (code == QLatin1String("vault_locked")) {
        return QStringLiteral("Хранилище ключей заблокировано. Разблокируйте его и повторите.");
    }
    if (code == QLatin1String("vault_missing")) {
        return QStringLiteral("Данные аккаунта не найдены на этом компьютере.");
    }
    return QStringLiteral("Ошибка хранилища ключей. Попробуйте ещё раз.");
}
