// PROD-47 build stream C — account core tests.
//
// Covers the pieces that do not need a network, a GUI or a real OS key store:
//   - vault fail-closed behaviour with an injected backend
//   - base64url / SHA-256 vectors
//   - JWK canonicalisation and the RFC 7638 thumbprint
//   - P-256 raw signature round-trip
//   - X-Device-Proof payload bytes
//   - consent payload canonical bytes
//   - legiongames:// parsing
//   - Ed25519 manifest canonicalisation and signature verification
//
// The full manifest accept path against the pinned production key is a CI
// item for after WP0 installs the real key (see updatemanifest.cpp).

#include <QtTest>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <openssl/evp.h>

#include "../app/account/accountclient.h"
#include "../app/account/consent.h"
#include "../app/account/cryptoutil.h"
#include "../app/account/deeplink.h"
#include "../app/account/devicekey.h"
#include "../app/account/updatemanifest.h"
#include "../app/account/vault.h"

namespace {

class FakeSecretBackend : public SecretBackend
{
public:
    explicit FakeSecretBackend(bool available) :
        m_available(available)
    {
    }

    bool isAvailable() const override { return m_available; }

    QByteArray load(const QString &name, bool *ok) override
    {
        if (ok) {
            *ok = false;
        }
        m_error.clear();

        if (!m_available) {
            m_error = QStringLiteral("vault_unavailable");
            return QByteArray();
        }
        if (!m_entries.contains(name)) {
            m_error = QStringLiteral("vault_missing");
            return QByteArray();
        }

        if (ok) {
            *ok = true;
        }
        return m_entries.value(name);
    }

    bool save(const QString &name, const QByteArray &data) override
    {
        m_error.clear();
        if (!m_available) {
            m_error = QStringLiteral("vault_unavailable");
            return false;
        }
        m_entries.insert(name, data);
        return true;
    }

    bool remove(const QString &name) override
    {
        m_error.clear();
        if (!m_available) {
            m_error = QStringLiteral("vault_unavailable");
            return false;
        }
        m_entries.remove(name);
        return true;
    }

    QString backendName() const override
    {
        return m_available ? QStringLiteral("fake") : QStringLiteral("none");
    }

    QString lastErrorCode() const override { return m_error; }

    int size() const { return m_entries.size(); }

private:
    bool m_available;
    QHash<QString, QByteArray> m_entries;
    QString m_error;
};

QString sha256Hex64(char fill)
{
    return QString(64, QLatin1Char(fill));
}

bool generateEd25519(QByteArray *publicKey, EVP_PKEY **key)
{
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (ctx == nullptr) {
        return false;
    }

    EVP_PKEY *generated = nullptr;
    const bool ok = EVP_PKEY_keygen_init(ctx) > 0 && EVP_PKEY_keygen(ctx, &generated) > 0;
    EVP_PKEY_CTX_free(ctx);

    if (!ok || generated == nullptr) {
        return false;
    }

    unsigned char raw[32];
    size_t length = sizeof(raw);
    if (EVP_PKEY_get_raw_public_key(generated, raw, &length) != 1 || length != 32) {
        EVP_PKEY_free(generated);
        return false;
    }

    *publicKey = QByteArray(reinterpret_cast<const char *>(raw), 32);
    *key = generated;
    return true;
#else
    Q_UNUSED(publicKey);
    Q_UNUSED(key);
    return false;
#endif
}

QByteArray ed25519Sign(EVP_PKEY *key, const QByteArray &message)
{
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        return QByteArray();
    }

    QByteArray signature;
    if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key) == 1) {
        size_t length = 64;
        QByteArray buffer(static_cast<int>(length), '\0');
        if (EVP_DigestSign(ctx, reinterpret_cast<unsigned char *>(buffer.data()), &length,
                           reinterpret_cast<const unsigned char *>(message.constData()),
                           static_cast<size_t>(message.size())) == 1) {
            buffer.resize(static_cast<int>(length));
            signature = buffer;
        }
    }

    EVP_MD_CTX_free(ctx);
    return signature;
#else
    Q_UNUSED(key);
    Q_UNUSED(message);
    return QByteArray();
#endif
}

QJsonObject sampleManifest()
{
    QJsonObject windowsAsset;
    windowsAsset.insert(QStringLiteral("url"),
                        QStringLiteral("https://pkgs.legiongames.ru/moonlight/latest/MoonlightSetup.exe"));
    windowsAsset.insert(QStringLiteral("size"), 12.0);
    windowsAsset.insert(QStringLiteral("sha256"),
                        QStringLiteral("0123456789abcdef0123456789abcdef"
                                       "0123456789abcdef0123456789abcdef"));

    QJsonObject assets;
    assets.insert(QStringLiteral("windows"), windowsAsset);

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"), 1);
    manifest.insert(QStringLiteral("version"), QStringLiteral("1.2.3"));
    manifest.insert(QStringLiteral("build"), 456);
    manifest.insert(QStringLiteral("commit_full"),
                    QStringLiteral("0123456789abcdef0123456789abcdef01234567"));
    manifest.insert(QStringLiteral("min_supported_build"), 400);
    manifest.insert(QStringLiteral("key_id"), QStringLiteral("prod-2026-01"));
    manifest.insert(QStringLiteral("assets"), assets);
    return manifest;
}

} // namespace

class TstAccountCore : public QObject
{
    Q_OBJECT

private slots:
    void vault_unavailable_never_falls_back()
    {
        Vault vault(std::unique_ptr<SecretBackend>(new FakeSecretBackend(false)));

        QVERIFY(!vault.isAvailable());
        QCOMPARE(vault.backendName(), QStringLiteral("none"));

        QVERIFY(!vault.save(QStringLiteral("device.key"), QByteArray("secret")));
        QCOMPARE(vault.lastErrorCode(), QStringLiteral("vault_unavailable"));

        bool ok = true;
        const QByteArray loaded = vault.load(QStringLiteral("device.key"), &ok);
        QVERIFY(!ok);
        QVERIFY(loaded.isEmpty());
        QCOMPARE(vault.lastErrorCode(), QStringLiteral("vault_unavailable"));
    }

    void vault_roundtrip_with_backend()
    {
        Vault vault(std::unique_ptr<SecretBackend>(new FakeSecretBackend(true)));
        QVERIFY(vault.isAvailable());

        QVERIFY(vault.save(QStringLiteral("device.key"), QByteArray("ciphertext")));

        bool ok = false;
        QCOMPARE(vault.load(QStringLiteral("device.key"), &ok), QByteArray("ciphertext"));
        QVERIFY(ok);

        QVERIFY(vault.remove(QStringLiteral("device.key")));
        QVERIFY(vault.load(QStringLiteral("device.key"), &ok).isEmpty());
        QVERIFY(!ok);
        QCOMPARE(vault.lastErrorCode(), QStringLiteral("vault_missing"));
    }

    void base64url_and_sha256_vectors()
    {
        QCOMPARE(CryptoUtil::base64UrlString(QByteArray("hello")), QStringLiteral("aGVsbG8"));
        QCOMPARE(CryptoUtil::base64UrlString(CryptoUtil::sha256(QByteArray())),
                 QStringLiteral("47DEQpj8HBSa-_TImW-5JCeuQeRkm5NMpJWZG3hSuFU"));
        QCOMPARE(CryptoUtil::base64UrlDecode(QByteArray("aGVsbG8")), QByteArray("hello"));
    }

    void jwk_canonical_and_thumbprint()
    {
        // Public coordinates only (JOSE cookbook P-256 key). No private key.
        QJsonObject jwk;
        jwk.insert(QStringLiteral("y"),
                   QStringLiteral("x_FEzRu9m36HLN_tue659LNpXW6pCyStikYjKIWI5a0"));
        jwk.insert(QStringLiteral("x"),
                   QStringLiteral("f83OJ3D2xF1Bg8vub9tLe1gHMzV76e8Tus9uPHvRVEU"));
        jwk.insert(QStringLiteral("kty"), QStringLiteral("EC"));
        jwk.insert(QStringLiteral("crv"), QStringLiteral("P-256"));

        const QByteArray canonical =
            "{\"crv\":\"P-256\",\"kty\":\"EC\","
            "\"x\":\"f83OJ3D2xF1Bg8vub9tLe1gHMzV76e8Tus9uPHvRVEU\","
            "\"y\":\"x_FEzRu9m36HLN_tue659LNpXW6pCyStikYjKIWI5a0\"}";

        QCOMPARE(CryptoUtil::canonicalJson(jwk), canonical);

        const QString expected =
            CryptoUtil::base64UrlString(CryptoUtil::sha256(canonical));
        QCOMPARE(DeviceKey::thumbprintForJwk(jwk), expected);
        QCOMPARE(expected.size(), 43);
        QVERIFY(!expected.contains(QLatin1Char('=')));
        QVERIFY(!expected.contains(QLatin1Char('+')));
        QVERIFY(!expected.contains(QLatin1Char('/')));
    }

    void devicekey_sign_verify_roundtrip()
    {
        DeviceKey *key = DeviceKey::generate();
        QVERIFY(key != nullptr);
        QVERIFY(key->isValid());

        const QByteArray message = QByteArray("cer-1.chal-1.register");
        const QByteArray signature = key->signRaw(message);
        QCOMPARE(signature.size(), 64);

        const QJsonObject jwk = key->publicJwk();
        QVERIFY(DeviceKey::verifyRaw(jwk, message, signature));
        QVERIFY(!DeviceKey::verifyRaw(jwk, message + QByteArray("x"), signature));
        QVERIFY(!DeviceKey::verifyRaw(jwk, message, QByteArray(64, '\0')));

        const QByteArray der = key->toPkcs8();
        QVERIFY(!der.isEmpty());
        DeviceKey *restored = DeviceKey::fromPkcs8(der);
        QVERIFY(restored != nullptr);
        QCOMPARE(restored->thumbprint(), key->thumbprint());

        delete restored;
        delete key;
    }

    void proof_payload_is_canonical_and_hashes_body()
    {
        const QByteArray jti = CryptoUtil::base64UrlEncode(QByteArray(12, '\0'));

        const QByteArray payload =
            AccountClient::proofPayload(QByteArray(), QStringLiteral("POST"),
                                        QStringLiteral("/app/v1/me"), 1763700000LL,
                                        1763700060LL, jti);

        const QByteArray expected =
            "{\"bh\":\"47DEQpj8HBSa-_TImW-5JCeuQeRkm5NMpJWZG3hSuFU\","
            "\"exp\":1763700060,\"htm\":\"POST\",\"htu\":\"/app/v1/me\","
            "\"iat\":1763700000,\"jti\":\"AAAAAAAAAAAAAAAA\",\"v\":1}";

        QCOMPARE(payload, expected);

        // A body changes only bh.
        const QByteArray withBody =
            AccountClient::proofPayload(QByteArray("{}"), QStringLiteral("post"),
                                        QStringLiteral("/app/v1/me"), 1, 2, jti);
        QVERIFY(withBody.contains(QByteArrayLiteral("\"htm\":\"POST\"")));
        QVERIFY(!withBody.contains(
            QByteArrayLiteral("47DEQpj8HBSa-_TImW-5JCeuQeRkm5NMpJWZG3hSuFU")));
    }

    void consent_payload_bytes()
    {
        LegalDocumentInfo agreement;
        agreement.documentId = Consent::userAgreementId();
        agreement.version = QStringLiteral("2026-11-01");
        agreement.sha256 = sha256Hex64('a');
        agreement.locale = QStringLiteral("ru-RU");
        agreement.url = QStringLiteral("https://legiongames.ru/legal/user-agreement/");

        LegalDocumentInfo pdn;
        pdn.documentId = Consent::pdnConsentId();
        pdn.version = QStringLiteral("2026-11-01");
        pdn.sha256 = sha256Hex64('b');
        pdn.locale = QStringLiteral("ru-RU");
        pdn.url = QStringLiteral("https://legiongames.ru/legal/pdn/");

        QList<LegalDocumentInfo> documents;
        documents << agreement << pdn;

        QString errorCode;
        const QJsonObject payload = Consent::buildPayload(
            QStringLiteral("cer-1"), true, true, documents, QStringLiteral("DESKTOP-01"),
            QStringLiteral("1.2.3+456"), QStringLiteral("2026-11-21T12:00:00Z"),
            &errorCode);

        QVERIFY(errorCode.isEmpty());
        QVERIFY(!payload.isEmpty());

        const QString expected =
            QStringLiteral(
                "{\"agreement\":true,\"app_version\":\"1.2.3+456\","
                "\"ceremony_id\":\"cer-1\","
                "\"client_accepted_at\":\"2026-11-21T12:00:00Z\","
                "\"device_label\":\"DESKTOP-01\","
                "\"documents\":[{\"document_id\":\"user_agreement\",\"sha256\":\"%1\","
                "\"version\":\"2026-11-01\"},"
                "{\"document_id\":\"pdn_consent\",\"sha256\":\"%2\","
                "\"version\":\"2026-11-01\"}],"
                "\"locale\":\"ru-RU\",\"pdn_consent\":true,\"surface\":\"app\",\"v\":1}")
                .arg(sha256Hex64('a'), sha256Hex64('b'));

        QCOMPARE(Consent::signedBytes(payload), expected.toUtf8());

        QJsonObject stored;
        stored.insert(QStringLiteral("agreement"), true);
        stored.insert(QStringLiteral("pdn_consent"), true);
        stored.insert(QStringLiteral("documents"), payload.value(QStringLiteral("documents")));
        QVERIFY(Consent::isCurrent(stored, documents));

        QJsonObject declined = Consent::buildPayload(
            QStringLiteral("cer-1"), false, true, documents, QStringLiteral("PC"),
            QStringLiteral("1.2.3+456"), QStringLiteral("2026-11-21T12:00:00Z"),
            &errorCode);
        QVERIFY(declined.isEmpty());
        QCOMPARE(errorCode, QStringLiteral("consent_required"));
    }

    void deep_link_parsing()
    {
        const QString id = QStringLiteral("0b7f3f9e-2d1a-4c3b-8e4f-1234567890ab");
        const QString url = QStringLiteral("legiongames://payment/") + id;

        QCOMPARE(DeepLink::paymentIdFromUrl(url), id);
        QCOMPARE(DeepLink::paymentIdFromUrl(QStringLiteral("LEGIONGAMES://PAYMENT/") +
                                            id.toUpper()),
                 id);
        QVERIFY(DeepLink::paymentIdFromUrl(QStringLiteral("https://legiongames.ru/pay/") + id)
                    .isEmpty());
        QVERIFY(DeepLink::paymentIdFromUrl(QStringLiteral("legiongames://payment/not-a-uuid"))
                    .isEmpty());
        QVERIFY(DeepLink::paymentIdFromUrl(QString()).isEmpty());

        QVERIFY(DeepLink::isDeepLinkLaunch(QStringList() << QStringLiteral("moonlight")
                                                         << QStringLiteral("--url") << url));
        QVERIFY(!DeepLink::isDeepLinkLaunch(QStringList() << QStringLiteral("moonlight")));

        QCOMPARE(DeepLink::paymentIdFromArguments(
                     QStringList() << QStringLiteral("moonlight") << QStringLiteral("--url") << url),
                 id);
        QCOMPARE(DeepLink::paymentIdFromArguments(
                     QStringList() << QStringLiteral("moonlight")
                                   << (QStringLiteral("--url=") + url)),
                 id);
        QCOMPARE(DeepLink::paymentIdFromArguments(QStringList() << QStringLiteral("moonlight") << url),
                 id);

        QCOMPARE(DeepLink::canonicalPaymentUrl(id), url);
        QVERIFY(DeepLink::canonicalPaymentUrl(QStringLiteral("nope")).isEmpty());
    }

    void manifest_canonical_bytes_and_ed25519()
    {
        QJsonObject manifest = sampleManifest();
        manifest.insert(QStringLiteral("signature"), QStringLiteral("ignored"));

        QJsonObject unsignedManifest = manifest;
        unsignedManifest.remove(QStringLiteral("signature"));

        const QByteArray signedBytes =
            UpdateManifestParser::canonicalSignedBytes(unsignedManifest);

        // Keys are sorted at every level and there is no whitespace.
        QVERIFY(signedBytes.startsWith(QByteArrayLiteral("{\"assets\":{\"windows\":{")));
        QVERIFY(signedBytes.contains(QByteArrayLiteral("\"build\":456")));
        QVERIFY(signedBytes.contains(QByteArrayLiteral("\"schema\":1")));
        QVERIFY(!signedBytes.contains(' '));
        QVERIFY(!signedBytes.contains(QByteArrayLiteral("signature")));

        QByteArray publicKey;
        EVP_PKEY *key = nullptr;
        if (!generateEd25519(&publicKey, &key)) {
            QSKIP("Ed25519 requires OpenSSL 1.1.1 or newer");
        }

        const QByteArray signature = ed25519Sign(key, signedBytes);
        QCOMPARE(signature.size(), 64);

        QString errorCode;
        QVERIFY(UpdateManifestParser::verifyEd25519(signedBytes, signature, publicKey,
                                                    &errorCode));
        QVERIFY(errorCode.isEmpty());

        QByteArray tampered = signedBytes;
        tampered.replace(QByteArrayLiteral("\"build\":456"),
                         QByteArrayLiteral("\"build\":457"));
        QVERIFY(!UpdateManifestParser::verifyEd25519(tampered, signature, publicKey,
                                                     &errorCode));
        QCOMPARE(errorCode, QStringLiteral("update_signature_invalid"));

        EVP_PKEY_free(key);
    }

    void manifest_parse_rules()
    {
        QJsonObject manifest = sampleManifest();

        UpdateManifest parsed;
        QString errorCode;
        QVERIFY(UpdateManifestParser::parseVerified(manifest, &parsed, &errorCode));
        QVERIFY(errorCode.isEmpty());
        QCOMPARE(parsed.build, 456);
        QCOMPARE(parsed.minSupportedBuild, 400);
        QCOMPARE(parsed.keyId, QStringLiteral("prod-2026-01"));
        QVERIFY(parsed.assets.contains(QStringLiteral("windows")));
        QVERIFY(parsed.assets.value(QStringLiteral("windows")).isValid());

        // Rollback floor must not be above the offered build.
        QJsonObject floorAboveBuild = manifest;
        floorAboveBuild.insert(QStringLiteral("min_supported_build"), 999);
        UpdateManifest rejected;
        QVERIFY(!UpdateManifestParser::parseVerified(floorAboveBuild, &rejected, &errorCode));
        QCOMPARE(errorCode, QStringLiteral("update_manifest_invalid"));

        // http assets are refused.
        QJsonObject httpAsset = manifest;
        QJsonObject assets = httpAsset.value(QStringLiteral("assets")).toObject();
        QJsonObject windows = assets.value(QStringLiteral("windows")).toObject();
        windows.insert(QStringLiteral("url"), QStringLiteral("http://example.invalid/x.exe"));
        assets.insert(QStringLiteral("windows"), windows);
        httpAsset.insert(QStringLiteral("assets"), assets);
        QVERIFY(!UpdateManifestParser::parseVerified(httpAsset, &rejected, &errorCode));

        // Malformed SHA-256 is refused.
        QJsonObject badHash = manifest;
        assets = badHash.value(QStringLiteral("assets")).toObject();
        windows = assets.value(QStringLiteral("windows")).toObject();
        windows.insert(QStringLiteral("sha256"), QStringLiteral("abc"));
        assets.insert(QStringLiteral("windows"), windows);
        badHash.insert(QStringLiteral("assets"), assets);
        QVERIFY(!UpdateManifestParser::parseVerified(badHash, &rejected, &errorCode));

        // Unknown schema is refused.
        QJsonObject badSchema = manifest;
        badSchema.insert(QStringLiteral("schema"), 2);
        QVERIFY(!UpdateManifestParser::parseVerified(badSchema, &rejected, &errorCode));
        QCOMPARE(errorCode, QStringLiteral("update_schema_unsupported"));

        // A signed "disabled" manifest is accepted without asset metadata.
        QJsonObject disabled;
        disabled.insert(QStringLiteral("schema"), 1);
        disabled.insert(QStringLiteral("key_id"), QStringLiteral("prod-2026-01"));
        disabled.insert(QStringLiteral("disabled"), true);
        QVERIFY(UpdateManifestParser::parseVerified(disabled, &parsed, &errorCode));
        QVERIFY(parsed.disabled);
    }

    void manifest_placeholder_fails_closed()
    {
        QByteArray key;
        QVERIFY(!UpdateManifestParser::pinnedKey(QStringLiteral("unknown"), &key));

        if (UpdateManifestParser::verificationConfigured()) {
            QSKIP("A real pinned key is installed; the placeholder fail-closed case no longer applies");
        }

        // The shipped key is a placeholder until WP0 installs the real one.
        // Whatever the bytes say, the updater must refuse to trust the manifest.
        const QByteArray raw =
            QJsonDocument(sampleManifest()).toJson(QJsonDocument::Compact);

        UpdateManifest parsed;
        QString errorCode;
        QVERIFY(!UpdateManifestParser::verifyAndParse(raw, &parsed, &errorCode));
        QCOMPARE(errorCode, QStringLiteral("update_verification_unavailable"));

        QVERIFY(!UpdateManifestParser::pinnedKey(QStringLiteral("prod-2026-01"), &key));
    }
};

int runAccountCoreTests(int argc, char **argv)
{
    TstAccountCore test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_account_core.moc"
