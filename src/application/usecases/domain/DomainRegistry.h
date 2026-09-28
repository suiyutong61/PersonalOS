#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "application/ports/DomainManifestRepository.h"

// 领域配置运行时（DR-038；domain-configuration-design-v1.md §2/§5/§6）
// 职责：加载版本化领域清单 → JSON Schema 结构验证 → 引用/能力/策略检查 →
// 产出可用的 DomainConfig。配置不能启用未实现能力或扩大专用领域范围；
// 启用失败保留原有效版本（每次从受管数据读取，失败即报错不缓存坏配置）。
namespace PersonOS::Application {

// 已注册的产品能力（配置只能引用这些；新增能力必须伴随真实实现）
inline const char *const kRegisteredCapabilities[] = {
    "goal_route", "mel_loop", "state_record", "assessment", "knowledge_retrieval",
    "advisory", "content_ingest", "retention_schedule", "calendar_reminder",
    "backup_restore", "domain_config",
};

struct DomainParameterSpec
{
    std::string id;
    std::string valueType;
    std::string unit;
    std::optional<double> defaultValue;
    std::optional<double> min;
    std::optional<double> max;
    bool userAdjustable = false;
    std::string missingPolicy;   // use_default / ask_user / unknown
};

struct DomainConfig
{
    std::string domainCode;
    std::string manifestVersionUid;
    std::string manifestVersion;
    std::string schemaVersion;
    std::vector<std::string> workflows;          // WF-1..WF-7
    std::vector<std::string> capabilities;
    std::vector<DomainParameterSpec> parameters;  // parameter_specs
    std::vector<std::string> decisionPoints;      // decision_code 列表
    std::vector<std::string> policyRefs;          // policy_ref 汇总（决策点 + 顶层）
    std::vector<std::string> preferredTags;       // knowledge_selectors.preferred_tags
    bool crossDomainAllowed = false;              // knowledge_selectors.cross_domain_allowed
    std::string provenanceBasis;                  // provenance.basis

    std::optional<DomainParameterSpec> parameter(const std::string &id) const;
};

class DomainRegistry
{
public:
    explicit DomainRegistry(DomainManifestRepository &repo);

    // 加载并验证某领域的最新有效清单；失败返回原因列表（message 汇总）
    Result<DomainConfig, ApplicationError> load(const std::string &domainCode);

    // 指定版本加载（回退/追溯）；同样执行全部验证
    Result<DomainConfig, ApplicationError> loadVersion(const std::string &versionUid);

private:
    Result<DomainConfig, ApplicationError> validateAndBuild(
        const std::string &domainCode, const ManifestVersionRecord &record);

    DomainManifestRepository &m_repo;
};

} // namespace PersonOS::Application
