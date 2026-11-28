#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

// Current legal document as returned by the public
// GET /app/v1/legal/documents endpoint.
struct LegalDocumentInfo
{
    QString documentId;
    QString version;
    QString sha256;
    QString url;
    QString locale;
};

// Builds the signed consent payload. The payload layout is fixed by Contract
// v1.4 §7: it must be byte-identical on the client and on the server because
// the signature covers the exact bytes that are sent.
class Consent
{
public:
    static QString userAgreementId();
    static QString pdnConsentId();

    // Documents the app advertises on the first-run screen. They carry the
    // production URLs and no local version/hash: the server stamps the current
    // version and hash into the consent record.
    static QList<LegalDocumentInfo> staticDocuments();

    // Parses the legal/documents response. Fails closed when the two gating
    // documents are not present with a version and a 64-hex SHA-256.
    static bool documentsFromJson(const QJsonObject &response,
                                  QList<LegalDocumentInfo> *documents,
                                  QString *errorCode);

    static bool find(const QList<LegalDocumentInfo> &documents, const QString &documentId,
                     LegalDocumentInfo *out);

    // Returns an empty object and sets errorCode when the inputs cannot form a
    // valid consent (no current documents, missing checkbox, no ceremony).
    static QJsonObject buildPayload(const QString &ceremonyId, bool agreement, bool pdnConsent,
                                    const QList<LegalDocumentInfo> &documents,
                                    const QString &deviceLabel, const QString &appVersion,
                                    const QString &clientAcceptedAt,
                                    QString *errorCode,
                                    const QString &referralCode = QString());

    // Exact bytes that are signed. `payload` is sent as a JSON string with the
    // same bytes, so no cross-language canonicalisation is needed.
    static QByteArray signedBytes(const QJsonObject &payload);

    // Registration-only referral attribution. The builder reads the pending
    // code when the explicit buildPayload argument is empty; callers clear it
    // after the registration response and before any other consent ceremony.
    static void setPendingReferralCode(const QString &code);

    // True when a stored consent record still matches the current documents.
    static bool isCurrent(const QJsonObject &storedRecord,
                          const QList<LegalDocumentInfo> &documents);
};
