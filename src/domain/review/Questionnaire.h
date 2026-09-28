#pragma once

#include <optional>
#include <string>

#include "domain/foundation/Uid.h"

// 复盘状态问卷（requirements R3.5/R4.3.3；数据库设计 §4.2 questionnaires_v4）
// 纪律：问卷可跳过、可修改；跳过或未回答不视为"无明显变化"确认，
// 只有显式 no_change 才算一次新确认；缺失值不解释为状态正常。
namespace PersonOS::Domain {

struct Questionnaire
{
    Uid uid;
    std::string code;                   // 如 review_extended_states
    int versionNo = 0;                  // >0；问卷改版产生新行（UNIQUE(code,version_no)）
    std::string name;
    std::string schemaJson = "{}";      // {"items":[{"code":...},...]}
    std::string status = "active";
    int revision = 1;
    std::string createdAt;
    std::string updatedAt;

    bool isValid() const
    {
        return !uid.empty() && !code.empty() && versionNo > 0 && !name.empty()
               && !schemaJson.empty();
    }
};

struct QuestionnaireResponse
{
    Uid uid;
    Uid reviewId;
    Uid questionnaireId;
    std::string responsesJson = "{}";   // {"answers":{code:value},"no_change":[..],"skipped":[..]}
    std::string startedAt;
    std::optional<std::string> submittedAt;
    double completionRatio = 0.0;       // (answers + no_change) / 题目总数；skipped 不计入

    bool isValid() const
    {
        return !uid.empty() && !reviewId.empty() && !questionnaireId.empty()
               && !responsesJson.empty() && !startedAt.empty()
               && completionRatio >= 0.0 && completionRatio <= 1.0;
    }
};

} // namespace PersonOS::Domain
