#include "application/foundation/JsonSchemaValidator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace PersonOS::Application {

namespace {

std::optional<std::string> checkAgainst(const QJsonValue &schema, const QJsonValue &instance,
                                        const QString &path);

bool typeMatches(const QString &expected, const QJsonValue &value)
{
    if (expected == QStringLiteral("string"))
        return value.isString();
    if (expected == QStringLiteral("object"))
        return value.isObject();
    if (expected == QStringLiteral("array"))
        return value.isArray();
    if (expected == QStringLiteral("boolean"))
        return value.isBool();
    if (expected == QStringLiteral("null"))
        return value.isNull();
    if (expected == QStringLiteral("integer"))
        return value.isDouble() && value.toDouble() == qint64(value.toDouble());
    if (expected == QStringLiteral("number"))
        return value.isDouble();
    return false;
}

std::optional<std::string> checkType(const QJsonValue &schema, const QJsonValue &instance,
                                     const QString &path)
{
    const QJsonValue type = schema.toObject().value(QStringLiteral("type"));
    if (type.isString()) {
        if (!typeMatches(type.toString(), instance))
            return "type mismatch at " + path.toStdString() + " (expected "
                   + type.toString().toStdString() + ")";
    } else if (type.isArray()) {
        bool any = false;
        for (const auto &candidate : type.toArray())
            any = any || typeMatches(candidate.toString(), instance);
        if (!any)
            return "type mismatch at " + path.toStdString() + " (expected one of array)";
    }
    return std::nullopt;
}

std::optional<std::string> checkObject(const QJsonValue &schema, const QJsonValue &instance,
                                       const QString &path)
{
    if (!instance.isObject())
        return "expected object at " + path.toStdString();
    const QJsonObject schemaObject = schema.toObject();
    const QJsonObject object = instance.toObject();

    // required
    for (const auto &required : schemaObject.value(QStringLiteral("required")).toArray())
        if (!object.contains(required.toString()))
            return "missing required field '" + required.toString().toStdString()
                   + "' at " + path.toStdString();

    // additionalProperties(false)
    const QJsonValue additional = schemaObject.value(QStringLiteral("additionalProperties"));
    if (additional.isBool() && !additional.toBool()) {
        const QJsonObject properties = schemaObject.value(QStringLiteral("properties")).toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!properties.contains(it.key()))
                return "additional property '" + it.key().toStdString() + "' at "
                       + path.toStdString();
    }

    // properties
    const QJsonObject properties = schemaObject.value(QStringLiteral("properties")).toObject();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const QJsonValue subSchema = it.value();
        if (!subSchema.isObject())
            continue;
        const QString subPath = path + QStringLiteral(".") + it.key();
        const QJsonValue subValue = object.value(it.key());
        if (subValue.isUndefined())
            continue;
        if (const auto error = checkAgainst(subSchema, subValue, subPath))
            return error;
    }
    return std::nullopt;
}

std::optional<std::string> checkArray(const QJsonValue &schema, const QJsonValue &instance,
                                      const QString &path)
{
    if (!instance.isArray())
        return "expected array at " + path.toStdString();
    const QJsonValue items = schema.toObject().value(QStringLiteral("items"));
    const QJsonArray array = instance.toArray();
    if (items.isObject())
        for (int i = 0; i < array.size(); ++i)
            if (const auto error = checkAgainst(
                    items, array.at(i), path + QStringLiteral("[") + QString::number(i)
                                           + QStringLiteral("]")))
                return error;
    return std::nullopt;
}

std::optional<std::string> checkEnum(const QJsonValue &schema, const QJsonValue &instance,
                                     const QString &path)
{
    const QJsonArray allowed = schema.toObject().value(QStringLiteral("enum")).toArray();
    for (const auto &candidate : allowed)
        if (candidate == instance)
            return std::nullopt;
    return "value not in enum at " + path.toStdString();
}

std::optional<std::string> checkBounds(const QJsonValue &schema, const QJsonValue &instance,
                                       const QString &path)
{
    const QJsonObject object = schema.toObject();
    if (instance.isDouble()) {
        const double value = instance.toDouble();
        if (object.contains(QStringLiteral("minimum"))
            && value < object.value(QStringLiteral("minimum")).toDouble())
            return "below minimum at " + path.toStdString();
        if (object.contains(QStringLiteral("maximum"))
            && value > object.value(QStringLiteral("maximum")).toDouble())
            return "above maximum at " + path.toStdString();
    }
    if (instance.isString()) {
        const int length = instance.toString().size();
        if (object.contains(QStringLiteral("minLength"))
            && length < object.value(QStringLiteral("minLength")).toInt())
            return "below minLength at " + path.toStdString();
        if (object.contains(QStringLiteral("maxLength"))
            && length > object.value(QStringLiteral("maxLength")).toInt())
            return "above maxLength at " + path.toStdString();
    }
    return std::nullopt;
}

std::optional<std::string> checkAgainst(const QJsonValue &schema, const QJsonValue &instance,
                                        const QString &path)
{
    if (!schema.isObject())
        return "invalid schema at " + path.toStdString();
    if (const auto error = checkType(schema, instance, path))
        return error;
    if (const auto error = checkBounds(schema, instance, path))
        return error;
    if (schema.toObject().contains(QStringLiteral("enum")))
        if (const auto error = checkEnum(schema, instance, path))
            return error;
    if (schema.toObject().contains(QStringLiteral("properties"))
        || schema.toObject().contains(QStringLiteral("required"))
        || schema.toObject().contains(QStringLiteral("additionalProperties")))
        if (const auto error = checkObject(schema, instance, path))
            return error;
    if (schema.toObject().contains(QStringLiteral("items")))
        if (const auto error = checkArray(schema, instance, path))
            return error;
    return std::nullopt;
}

} // namespace

std::optional<std::string> JsonSchemaValidator::validate(const std::string &schemaJson,
                                                         const std::string &instanceJson)
{
    QJsonParseError schemaError{};
    const QJsonDocument schemaDoc =
        QJsonDocument::fromJson(QByteArray::fromStdString(schemaJson), &schemaError);
    if (schemaError.error != QJsonParseError::NoError || !schemaDoc.isObject())
        return std::string("schema json invalid: ") + schemaError.errorString().toStdString();

    QJsonParseError instanceError{};
    const QJsonDocument instanceDoc =
        QJsonDocument::fromJson(QByteArray::fromStdString(instanceJson), &instanceError);
    if (instanceError.error != QJsonParseError::NoError)
        return std::string("instance json invalid: ") + instanceError.errorString().toStdString();

    return checkAgainst(QJsonValue(schemaDoc.object()), QJsonValue(instanceDoc.object()),
                        QStringLiteral("$"));
}

} // namespace PersonOS::Application
