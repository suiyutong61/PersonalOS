#include "infrastructure/knowledge/StateDefinitionsSeed.h"

#include <QUuid>

#include "infrastructure/persistence/SqlStateRepository.h"

namespace PersonOS::Infrastructure {

namespace {

struct SeedDefinition
{
    const char *code;
    Domain::StateCategory category;
    const char *valueType;
    std::optional<double> scaleMin;
    std::optional<double> scaleMax;
    const char *unit;
    int ttlMin;
    const char *sensitivity;
};

const SeedDefinition kDefinitions[] = {
    // 基本状态
    {"energy", Domain::StateCategory::Basic, "integer", 1, 5, "级", 720, "normal"},
    {"focus", Domain::StateCategory::Basic, "integer", 1, 5, "级", 720, "normal"},
    {"mood", Domain::StateCategory::Basic, "integer", 1, 5, "级", 1440, "sensitive"},
    {"fatigue", Domain::StateCategory::Basic, "integer", 1, 5, "级", 720, "sensitive"},
    {"sleep_hours", Domain::StateCategory::Basic, "real", 0, 24, "小时", 1440, "sensitive"},
    {"available_time_min", Domain::StateCategory::Basic, "integer", 0, 1440, "分钟", 720,
     "normal"},
    // 十类拓展状态（各至少一个代表定义）
    {"cognitive_load", Domain::StateCategory::Cognitive, "integer", 1, 5, "级", 720, "normal"},
    {"stress", Domain::StateCategory::EmotionPressure, "integer", 1, 5, "级", 720, "sensitive"},
    {"motivation", Domain::StateCategory::MotivationRecreation, "integer", 1, 5, "级", 1440,
     "normal"},
    {"recreation_need", Domain::StateCategory::MotivationRecreation, "integer", 1, 5, "级", 720,
     "normal"},
    {"body_condition", Domain::StateCategory::BodyHealth, "integer", 1, 5, "级", 720,
     "sensitive"},
    {"environment_quality", Domain::StateCategory::Environment, "integer", 1, 5, "级", 1440,
     "normal"},
    {"social_load", Domain::StateCategory::ResourcesSocial, "integer", 1, 5, "级", 1440,
     "sensitive"},
    {"behavior_adherence", Domain::StateCategory::BehaviorMethod, "real", 0, 1, "比例", 1440,
     "normal"},
    {"system_feedback", Domain::StateCategory::SystemExperience, "integer", 1, 5, "级", 43200,
     "normal"},
    {"domain_progress", Domain::StateCategory::DomainSpecific, "real", 0, 1, "比例", 10080,
     "normal"},
    // R2 子问题检测（系统推断，source=system_derived；多原因假设、不贴标签）
    {"subproblem_progress_lag", Domain::StateCategory::DomainSpecific, "json", std::nullopt,
     std::nullopt, "", 1440, "normal"},
    {"subproblem_time_overrun", Domain::StateCategory::DomainSpecific, "json", std::nullopt,
     std::nullopt, "", 1440, "normal"},
    {"subproblem_weak_assessment", Domain::StateCategory::DomainSpecific, "json", std::nullopt,
     std::nullopt, "", 1440, "normal"},
    {"subproblem_inactivity", Domain::StateCategory::DomainSpecific, "json", std::nullopt,
     std::nullopt, "", 1440, "normal"},
    // R2 用户报告（source=explicit；用户陈述事实，非系统推断）
    {"subproblem_reported_prerequisite", Domain::StateCategory::DomainSpecific, "json",
     std::nullopt, std::nullopt, "", 10080, "normal"},
    {"subproblem_reported_material", Domain::StateCategory::DomainSpecific, "json",
     std::nullopt, std::nullopt, "", 10080, "normal"},
};

} // namespace

StateDefinitionsSeed::StateDefinitionsSeed(QSqlDatabase database, const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::Result<void, Application::ApplicationError> StateDefinitionsSeed::ensureSeeded()
{
    SqlStateRepository repo(m_database, m_clock);
    for (const auto &seed : kDefinitions) {
        if (repo.findDefinitionByCode(seed.code))
            continue;   // 幂等：已存在跳过
        Domain::StateDefinition def;
        const auto generatedUid =
            Domain::Uid::parse(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        if (!generatedUid)
            return Application::Result<void, Application::ApplicationError>::failure(
                {Application::ErrorCode::Storage, "uid generation failed", {}, false});
        def.uid = *generatedUid;
        def.code = seed.code;
        def.category = seed.category;
        def.valueType = seed.valueType;
        def.scaleMin = seed.scaleMin;
        def.scaleMax = seed.scaleMax;
        def.unit = seed.unit;
        def.defaultTtlMin = seed.ttlMin;
        def.sensitivity = seed.sensitivity;
        def.schemaJson = "{}";
        const auto saved = repo.insertDefinition(def);
        if (!saved.ok)
            return Application::Result<void, Application::ApplicationError>::failure(
                saved.error);
    }
    return Application::Result<void, Application::ApplicationError>::success();
}

} // namespace PersonOS::Infrastructure
