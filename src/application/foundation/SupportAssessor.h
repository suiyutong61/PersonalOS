#pragma once

#include <string>

#include "domain/knowledge/Retrieval.h"

// 知识支持程度判定（DR-027/028 的确定性规则部分；AI 评估部分在 IMP-006 接入）
// 规则：以"关键子问题覆盖情况"为主要输入；重要冲突作为独立标志，不简单
// 等同于知识不足；不以检索条数或单一向量相似度决定。
namespace PersonOS::Application {

struct SupportAssessmentInput
{
    int totalKeySubQuestions = 0;       // 形成可用答案所必需的关键子问题数
    int coveredSubQuestions = 0;        // 有相关、可信且适用资料实质支持的子问题数
    bool hasMaterialConflict = false;   // 是否存在会改变结论的重要冲突
};

struct SupportAssessmentOutput
{
    Domain::KnowledgeSupportLevel level = Domain::KnowledgeSupportLevel::Ungrounded;
    bool hasMaterialConflict = false;
    std::string coverageSummary;
    std::string reasoning;
};

class SupportAssessor
{
public:
    static SupportAssessmentOutput assess(const SupportAssessmentInput &input)
    {
        SupportAssessmentOutput output;
        output.hasMaterialConflict = input.hasMaterialConflict;
        if (input.totalKeySubQuestions <= 0) {
            output.level = Domain::KnowledgeSupportLevel::Ungrounded;
            output.coverageSummary = "未定义关键子问题";
            output.reasoning = "缺少关键子问题分解，无法判定实质覆盖";
        } else if (input.coveredSubQuestions >= input.totalKeySubQuestions) {
            output.level = Domain::KnowledgeSupportLevel::Grounded;
            output.coverageSummary = "覆盖 " + std::to_string(input.coveredSubQuestions) + "/"
                                     + std::to_string(input.totalKeySubQuestions);
            output.reasoning = "关键子问题均有相关且适用的资料支持";
        } else if (input.coveredSubQuestions > 0) {
            output.level = Domain::KnowledgeSupportLevel::PartiallyGrounded;
            output.coverageSummary = "覆盖 " + std::to_string(input.coveredSubQuestions) + "/"
                                     + std::to_string(input.totalKeySubQuestions);
            output.reasoning = "仅部分关键子问题获得支持";
        } else {
            output.level = Domain::KnowledgeSupportLevel::Ungrounded;
            output.coverageSummary = "覆盖 0/" + std::to_string(input.totalKeySubQuestions);
            output.reasoning = "未检索到能够实质支持任何关键子问题的内容";
        }
        return output;
    }
};

} // namespace PersonOS::Application
