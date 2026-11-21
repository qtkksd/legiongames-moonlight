#include "cryptoutil.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QStringList>
#include <QUuid>
#include <QRandomGenerator>

#include <cmath>

namespace {

void writeString(QByteArray &out, const QString &value)
{
    QString escaped;
    escaped.reserve(value.size() + 2);
    escaped += QLatin1Char('"');

    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        switch (code) {
        case '"':
            escaped += QLatin1String("\\\"");
            break;
        case '\\':
            escaped += QLatin1String("\\\\");
            break;
        case '\b':
            escaped += QLatin1String("\\b");
            break;
        case '\f':
            escaped += QLatin1String("\\f");
            break;
        case '\n':
            escaped += QLatin1String("\\n");
            break;
        case '\r':
            escaped += QLatin1String("\\r");
            break;
        case '\t':
            escaped += QLatin1String("\\t");
            break;
        default:
            if (code < 0x20) {
                escaped += QStringLiteral("\\u%1").arg(code, 4, 16, QLatin1Char('0'));
            }
            else {
                escaped += ch;
            }
            break;
        }
    }

    escaped += QLatin1Char('"');
    out += escaped.toUtf8();
}

void writeNumber(QByteArray &out, double value)
{
    if (std::isfinite(value) && std::floor(value) == value &&
        std::fabs(value) < 9.0e15) {
        out += QByteArray::number(static_cast<qint64>(value));
    }
    else {
        // Deterministic fallback for the rare non-integral value. RFC 8785
        // requires ECMAScript number formatting here; signed structures must
        // stay integral so this branch never affects a real signature.
        out += QByteArray::number(value, 'g', 17);
    }
}

void writeValue(QByteArray &out, const QJsonValue &value);

void writeArray(QByteArray &out, const QJsonArray &array)
{
    out += '[';
    bool first = true;
    for (const QJsonValue &item : array) {
        if (!first) {
            out += ',';
        }
        first = false;
        writeValue(out, item);
    }
    out += ']';
}

void writeObject(QByteArray &out, const QJsonObject &object)
{
    QStringList keys = object.keys();
    keys.sort();

    out += '{';
    bool first = true;
    for (const QString &key : keys) {
        if (!first) {
            out += ',';
        }
        first = false;
        writeString(out, key);
        out += ':';
        writeValue(out, object.value(key));
    }
    out += '}';
}

void writeValue(QByteArray &out, const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null:
        out += "null";
        break;
    case QJsonValue::Bool:
        out += value.toBool() ? "true" : "false";
        break;
    case QJsonValue::Double:
        writeNumber(out, value.toDouble());
        break;
    case QJsonValue::String:
        writeString(out, value.toString());
        break;
    case QJsonValue::Array:
        writeArray(out, value.toArray());
        break;
    case QJsonValue::Object:
        writeObject(out, value.toObject());
        break;
    case QJsonValue::Undefined:
        // Undefined members are not part of the object; write null so the
        // output is still valid JSON if a caller passes one explicitly.
        out += "null";
        break;
    }
}

} // namespace

QByteArray CryptoUtil::base64UrlEncode(const QByteArray &data)
{
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray CryptoUtil::base64UrlDecode(const QByteArray &text)
{
    QByteArray padded = text;
    while (padded.size() % 4 != 0) {
        padded.append('=');
    }
    return QByteArray::fromBase64(padded, QByteArray::Base64UrlEncoding);
}

QString CryptoUtil::base64UrlString(const QByteArray &data)
{
    return QString::fromLatin1(base64UrlEncode(data));
}

QByteArray CryptoUtil::sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QByteArray CryptoUtil::randomBytes(int count)
{
    if (count <= 0) {
        return QByteArray();
    }

    QByteArray out;
    out.resize(count);

    // QRandomGenerator::system() is backed by the OS CSPRNG (BCryptGenRandom,
    // getrandom/urandom, SecRandomCopyBytes).
    QRandomGenerator *rng = QRandomGenerator::system();
    for (int i = 0; i < count; ++i) {
        out[i] = static_cast<char>(rng->bounded(256));
    }
    return out;
}

QString CryptoUtil::randomUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QByteArray CryptoUtil::canonicalJson(const QJsonObject &object)
{
    QByteArray out;
    writeObject(out, object);
    return out;
}

QString CryptoUtil::canonicalJsonString(const QJsonObject &object)
{
    return QString::fromUtf8(canonicalJson(object));
}
