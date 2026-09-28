#pragma once

#include <optional>
#include <string>

#include "application/foundation/Result.h"
#include "application/ports/ContentPorts.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/knowledge/Ingest.h"

// 内容摄入用例（DD-001 §8.1；DR-023；requirements R4.5/R4.7）
// 纪律：同一来源幂等；不可信内容先隔离（quarantined）；失败可重试/终态；
// 无法合法获取时请求用户提供替代输入，不根据标题猜测。
namespace PersonOS::Application {

class ContentIngestUseCases
{
public:
    struct SubmitInput
    {
        std::string sourceType;       // url / text / file
        std::string sourceUri;
        std::string requestedBy = "user";
        std::string idempotencyKey;   // 必填，唯一（同来源幂等）
    };

    ContentIngestUseCases(ContentIngestRepositoryPort &repo, ContentSourcePort &source,
                          UuidPort &uids, const Domain::Clock &clock);

    // 提交摄入（登记任务；重复幂等键 → Conflict）
    Result<Domain::ContentImportJob, ApplicationError> submit(const SubmitInput &input);

    // 推进状态机（逐阶段）；fetching 阶段实际调用 ContentSourcePort；
    // 获取失败进入 failed_retryable（可重试）或由调用方决定终态
    Result<Domain::ContentImportJob, ApplicationError> advance(const Domain::Uid &jobUid,
                                                               int expectedRevision);

    // 获取失败处理：failed_retryable（可重试）/ failed_terminal（如版权限制）
    Result<Domain::ContentImportJob, ApplicationError> fail(const Domain::Uid &jobUid,
                                                            int expectedRevision,
                                                            bool terminal,
                                                            const std::string &code,
                                                            const std::string &detail);

    // 用户确认后提交（awaiting_confirmation → committed）
    Result<Domain::ContentImportJob, ApplicationError> commit(const Domain::Uid &jobUid,
                                                              int expectedRevision);

private:
    ContentIngestRepositoryPort &m_repo;
    ContentSourcePort &m_source;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
    FetchedContent m_lastFetched;     // fetching 阶段的获取结果（供后续阶段使用）
};

} // namespace PersonOS::Application
