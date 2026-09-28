#include "application/foundation/ContractValidator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QString>

namespace PersonOS::Application {

namespace {
struct ContractSpec
{
    const char *type;
    std::vector<ContractValidator::FieldSpec> fields;
};

const std::vector<ContractSpec> &contracts()
{
    static const std::vector<ContractSpec> all = {
        {"mel_proposal_v1",
         {{"title", true, false, false},
          {"tasks", true, false, true},
          {"rationale", true, false, false},
          {"capacity_min", true, true, false},
          {"reserve_min", true, true, false},
          {"period_days", true, true, false}}},
        {"review_analysis_v1",
         {{"summary", true, false, false},
          {"problems", true, false, true},
          {"causes", true, false, true},
          {"next_actions", true, false, true}}},
        {"route_proposal_v1",
         {{"rationale", true, false, false},
          {"evidence_summary", true, false, false},
          {"stages", true, false, true},
          {"goal_uid", true, false, false},
          {"source_mode", true, false, false}}},
        {"method_suggestions_v1", {{"suggestions", true, false, true}}},
        {"paper_analysis_v1",
         {{"title", true, false, false},
          {"summary", true, false, false},
          {"study_type", true, false, false},
          {"core_claims", true, false, true},
          {"applicability", true, false, true},
          {"limitations", true, false, true},
          {"credibility", true, false, false},
          {"candidate_methods", true, false, true},
          {"candidate_tips", true, false, true},
          {"evidence_fragments", true, false, true},
          {"unknowns", true, false, true}}},
        {"generic_v1", {{"user_text", true, false, false}}},
    };
    return all;
}
} // namespace

const std::vector<ContractValidator::FieldSpec> &ContractValidator::specsFor(
    const std::string &contractType)
{
    static const std::vector<FieldSpec> empty;
    for (const auto &contract : contracts())
        if (contractType == contract.type)
            return contract.fields;
    return empty;
}

std::string ContractValidator::promptSchema(const std::string &contractType,
                                            const std::string &version)
{
    if (contractType == "paper_analysis_v1") {
        return R"JSON({
  "title": "字符串,论文标题",
  "summary": "字符串,本块内容的要点摘要",
  "study_type": "字符串,研究类型(如 meta-analysis/randomized-trial/observational/review/theoretical/unknown)",
  "core_claims": ["字符串,核心论点,每条一句"],
  "applicability": ["字符串,适用条件/人群/情境"],
  "limitations": ["字符串,局限与边界,如实列出"],
  "credibility": "字符串,证据可信度评估及依据",
  "candidate_methods": [{"title": "方法名", "summary": "方法说明", "risk_level": "low|medium|high|unknown", "evidence_grade": "A|B|C|D|E|unknown", "steps": ["具体步骤1"]}],
  "candidate_tips": [{"title": "贴士名", "summary": "贴士内容", "risk_level": "low|medium|high|unknown", "use_scenario": {"适用情境"}}],
  "evidence_fragments": [{"text": "论文原文逐字摘录(必须原样引用,不概括)", "locator": {"页码或位置"}, "relation": "supports|qualifies|contradicts(限制性证据用 qualifies)", "strength": "0到1之间的数字"}],
  "unknowns": ["字符串,未知项或无法判断的项,如实标注"]
})JSON";
    }
    if (contractType == "generic_v1") {
        return R"JSON({"user_text": "字符串,面向用户的说明"})JSON";
    }
    // 其余契约:由必填字段生成最小 JSON 形状
    std::string schema = "{";
    const auto &fields = specsFor(contractType);
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0)
            schema += ", ";
        schema += "\"";
        schema += fields[i].name;
        schema += fields[i].isArray ? "\": [\"字符串\"]" : "\": \"字符串\"";
    }
    schema += "}";
    return schema;
}

std::optional<std::string> ContractValidator::validate(const std::string &contractType,
                                                       const std::string &jsonText)
{
    const auto &fields = specsFor(contractType);
    if (fields.empty())
        return std::string("unsupported contract type: ") + contractType;

    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(jsonText));
    if (!doc.isObject())
        return std::string("structured response must be a JSON object");
    const QJsonObject object = doc.object();

    for (const auto &field : fields) {
        if (!object.contains(QLatin1String(field.name))) {
            if (field.required)
                return std::string("missing required field: ") + field.name;
            continue;
        }
        const QJsonValue value = object.value(QLatin1String(field.name));
        if (field.isNumber && !value.isDouble())
            return std::string("field must be numeric: ") + field.name;
        if (field.isArray && !value.isArray())
            return std::string("field must be array: ") + field.name;
    }
    return std::nullopt;
}

} // namespace PersonOS::Application
