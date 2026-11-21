#include "consent.h"

#include "cryptoutil.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

namespace {

const char kUserAgreement[] = "user_agreement";
const char kPdnConsent[] = "pdn_consent";
const char kLocale[] = "ru-RU";

bool isValidSha256(const QString &value)
{
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return hex.match(value).hasMatch();
}

bool pickDocument(const QList<LegalDocumentInfo> &documents, const QString &documentId,
                  LegalDocumentInfo *out)
{
    bool found = false;
    for (const LegalDocumentInfo &document : documents) {
        if (document.documentId != documentId) {
            continue;
        }
        if (!found || document.locale == QLatin1String(kLocale)) {
            if (out) {
                *out = document;
            }
            found = true;
        }
        if (document.locale == QLatin1String(kLocale)) {
            break;
        }
    }
    return found;
}

bool documentEntry(const LegalDocumentInfo &document, QJsonObject *out)
{
    if (document.documentId.isEmpty() || document.version.isEmpty() ||
        !isValidSha256(document.sha256)) {
        return false;
    }

    QJsonObject entry;
    entry.insert(QStringLiteral("document_id"), document.documentId);
    entry.insert(QStringLiteral("sha256"), document.sha256.toLower());
    entry.insert(QStringLiteral("version"), document.version);
    if (out) {
        *out = entry;
    }
    return true;
}

} // namespace

QString Consent::userAgreementId()
{
    return QString::fromLatin1(kUserAgreement);
}

QString Consent::pdnConsentId()
{
    return QString::fromLatin1(kPdnConsent);
}

bool Consent::documentsFromJson(const QJsonObject &response,
                                QList<LegalDocumentInfo> *documents, QString *errorCode)
{
    if (documents == nullptr) {
        if (errorCode) {
            *errorCode = QStringLiteral("legal_unavailable");
        }
        return false;
    }
    documents->clear();

    const QJsonValue value = response.value(QStringLiteral("documents"));
    if (!value.isArray()) {
        if (errorCode) {
            *errorCode = QStringLiteral("legal_unavailable");
        }
        return false;
    }

    for (const QJsonValue &item : value.toArray()) {
        const QJsonObject object = item.toObject();
        LegalDocumentInfo document;
        document.documentId = object.value(QStringLiteral("document_id")).toString();
        document.version = object.value(QStringLiteral("version")).toString();
        document.sha256 = object.value(QStringLiteral("sha256")).toString();
        document.url = object.value(QStringLiteral("url")).toString();
        document.locale = object.value(QStringLiteral("locale")).toString();
        if (document.documentId.isEmpty()) {
            continue;
        }
        documents->append(document);
    }

    LegalDocumentInfo agreement;
    LegalDocumentInfo pdn;
    if (!pickDocument(*documents, userAgreementId(), &agreement) ||
        !pickDocument(*documents, pdnConsentId(), &pdn) ||
        !documentEntry(agreement, nullptr) || !documentEntry(pdn, nullptr)) {
        documents->clear();
        if (errorCode) {
            *errorCode = QStringLiteral("legal_unavailable");
        }
        return false;
    }

    if (errorCode) {
        errorCode->clear();
    }
    return true;
}

bool Consent::find(const QList<LegalDocumentInfo> &documents, const QString &documentId,
                   LegalDocumentInfo *out)
{
    return pickDocument(documents, documentId, out);
}

QJsonObject Consent::buildPayload(const QString &ceremonyId, bool agreement, bool pdnConsent,
                                  const QList<LegalDocumentInfo> &documents,
                                  const QString &deviceLabel, const QString &appVersion,
                                  const QString &clientAcceptedAt, QString *errorCode)
{
    if (!agreement || !pdnConsent) {
        if (errorCode) {
            *errorCode = QStringLiteral("consent_required");
        }
        return QJsonObject();
    }
    if (ceremonyId.isEmpty() || clientAcceptedAt.isEmpty()) {
        if (errorCode) {
            *errorCode = QStringLiteral("consent_invalid");
        }
        return QJsonObject();
    }

    LegalDocumentInfo agreementDocument;
    LegalDocumentInfo pdnDocument;
    if (!pickDocument(documents, userAgreementId(), &agreementDocument) ||
        !pickDocument(documents, pdnConsentId(), &pdnDocument)) {
        if (errorCode) {
            *errorCode = QStringLiteral("legal_unavailable");
        }
        return QJsonObject();
    }

    QJsonObject agreementEntry;
    QJsonObject pdnEntry;
    if (!documentEntry(agreementDocument, &agreementEntry) ||
        !documentEntry(pdnDocument, &pdnEntry)) {
        if (errorCode) {
            *errorCode = QStringLiteral("legal_unavailable");
        }
        return QJsonObject();
    }

    QJsonArray documentArray;
    documentArray.append(agreementEntry);
    documentArray.append(pdnEntry);

    QJsonObject payload;
    payload.insert(QStringLiteral("v"), 1);
    payload.insert(QStringLiteral("ceremony_id"), ceremonyId);
    payload.insert(QStringLiteral("surface"), QStringLiteral("app"));
    payload.insert(QStringLiteral("agreement"), true);
    payload.insert(QStringLiteral("pdn_consent"), true);
    payload.insert(QStringLiteral("locale"), QString::fromLatin1(kLocale));
    payload.insert(QStringLiteral("client_accepted_at"), clientAcceptedAt);
    payload.insert(QStringLiteral("documents"), documentArray);
    payload.insert(QStringLiteral("device_label"),
                   deviceLabel.isEmpty() ? QStringLiteral("PC") : deviceLabel);
    payload.insert(QStringLiteral("app_version"),
                   appVersion.isEmpty() ? QStringLiteral("0.0.0+0") : appVersion);

    if (errorCode) {
        errorCode->clear();
    }
    return payload;
}

QByteArray Consent::signedBytes(const QJsonObject &payload)
{
    return CryptoUtil::canonicalJson(payload);
}

bool Consent::isCurrent(const QJsonObject &storedRecord,
                        const QList<LegalDocumentInfo> &documents)
{
    if (!storedRecord.value(QStringLiteral("agreement")).toBool(false) ||
        !storedRecord.value(QStringLiteral("pdn_consent")).toBool(false)) {
        return false;
    }

    const QJsonValue stored = storedRecord.value(QStringLiteral("documents"));
    if (!stored.isArray()) {
        return false;
    }

    for (const QString &documentId : {userAgreementId(), pdnConsentId()}) {
        LegalDocumentInfo current;
        if (!pickDocument(documents, documentId, &current)) {
            return false;
        }

        bool matched = false;
        for (const QJsonValue &item : stored.toArray()) {
            const QJsonObject object = item.toObject();
            if (object.value(QStringLiteral("document_id")).toString() != documentId) {
                continue;
            }
            matched = object.value(QStringLiteral("version")).toString() == current.version &&
                      object.value(QStringLiteral("sha256")).toString().toLower() ==
                          current.sha256.toLower();
            if (matched) {
                break;
            }
        }

        if (!matched) {
            return false;
        }
    }

    return true;
}
