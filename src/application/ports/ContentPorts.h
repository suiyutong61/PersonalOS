#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/foundation/Uid.h"
#include "domain/knowledge/Ingest.h"

// 内容摄入端口（DD-001 §5.4 ContentFetcherPort；§8.1 状态机）
namespace PersonOS::Application {

// 内容获取适配器（通用网页/文本为 v1 基础实现；平台适配器可扩展，DR-023）
struct FetchedContent
{
    bool ok = false;
    std::string title;
    std::string author;
    std::optional<std::string> publishedAt;
    std::string bodyText;             // 正文/字幕/转写
    std::string contentType;          // article / transcript / text
    std::string errorMessage;
};

class ContentSourcePort
{
public:
    virtual ~ContentSourcePort() = default;
    virtual FetchedContent fetch(const std::string &uri) = 0;
};

class ContentIngestRepositoryPort
{
public:
    virtual ~ContentIngestRepositoryPort() = default;

    virtual std::optional<Domain::ContentImportJob> findByUid(const Domain::Uid &uid) = 0;
    virtual SaveResult insert(const Domain::ContentImportJob &job) = 0;
    virtual SaveResult update(const Domain::ContentImportJob &job, int expectedRevision) = 0;
    virtual bool existsIdempotencyKey(const std::string &key) = 0;
};

} // namespace PersonOS::Application
