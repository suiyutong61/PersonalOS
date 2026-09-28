#pragma once

#include <optional>
#include <string>
#include <vector>

// AI 结构化响应契约验证（DR-015：独立带版本号的响应结构，不用万能 JSON）
// 规则：按契约类型检查必填字段与基本类型；业务规则校验由各用例执行。
// 返回 nullopt = 通过；否则返回错误说明。
namespace PersonOS::Application {

class ContractValidator
{
public:
    struct FieldSpec
    {
        const char *name;
        bool required;
        bool isNumber;      // 数字型（REAL/INTEGER）
        bool isArray;
    };

    static std::optional<std::string> validate(const std::string &contractType,
                                               const std::string &jsonText);

    // 已知契约的必填字段规格（v1 最小集；新增契约在此登记并升版本）
    static const std::vector<FieldSpec> &specsFor(const std::string &contractType);

    // 契约的结构说明（注入提示词，让模型知道要输出的 JSON 形状）；
    // 空字符串表示该契约暂无提示词模板（调用方按需内联 schema）
    static std::string promptSchema(const std::string &contractType,
                                    const std::string &version);
};

} // namespace PersonOS::Application
