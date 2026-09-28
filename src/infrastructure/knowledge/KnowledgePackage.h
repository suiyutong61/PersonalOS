#pragma once

#include <QSqlDatabase>

#include <string>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "domain/foundation/Clock.h"

// 知识库安装包导出/导入（DR-005/009；scope-v1 §5）
// 包 = 单个 JSON 文件（含全部知识行 + 版本/来源/证据/关系/类型表），
// 头部 manifest 带 SHA-256 完整性校验；导入前校验哈希，导入后 FTS 全量重建。
// 导入不覆盖用户本地内容（按 uid 幂等跳过已存在条目）。
namespace PersonOS::Infrastructure {

class KnowledgePackageExporter
{
public:
    KnowledgePackageExporter(QSqlDatabase database, const Domain::Clock &clock);

    // 导出到 JSON 文本（调用方写文件）
    Application::Result<std::string, Application::ApplicationError> exportToJson();

private:
    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

class KnowledgePackageImporter
{
public:
    KnowledgePackageImporter(QSqlDatabase database, const Domain::Clock &clock);

    struct Report
    {
        int importedItems = 0;
        int skippedExisting = 0;
    };

    // 校验哈希后导入；任何一步失败回滚本次事务（外部调用方先备份）
    Application::Result<Report, Application::ApplicationError> importFromJson(
        const std::string &jsonText);

private:
    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
