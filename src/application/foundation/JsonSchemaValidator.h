#pragma once

#include <optional>
#include <string>

// 最小 JSON Schema 验证器（参照 JSON Schema Draft 2020-12 的常用子集；
// domain-configuration-design-v1.md §2 要求清单结构验证，运行时不得联网）。
// 支持关键字：type / required / properties / items / enum / minimum / maximum /
// minLength / maxLength / additionalProperties(false)。
// 返回值：nullopt = 通过；否则为错误说明（含 JSON 路径）。
namespace PersonOS::Application {

class JsonSchemaValidator
{
public:
    static std::optional<std::string> validate(const std::string &schemaJson,
                                               const std::string &instanceJson);
};

} // namespace PersonOS::Application
