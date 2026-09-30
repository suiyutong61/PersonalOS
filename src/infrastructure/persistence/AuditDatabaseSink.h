#pragma once

#include <QString>

#include "application/audit/Audit.h"

// 应用级审计落库（DR-026）：用例会在 UI 线程与后台 QtConcurrent 线程调用
// Audit::record，全局 sink 不能绑定单一数据库连接——每次 append 经
// DatabaseConnectionFactory 取当前线程连接（连接名含线程 id，按线程
// 复用，进程退出自然释放）。注册方必须使用 static 存储期实例
// （后台任务可能晚于栈对象析构）。
namespace PersonOS::Infrastructure {

class AuditDatabaseSink final : public Application::AuditSink
{
public:
    explicit AuditDatabaseSink(QString databasePath);

    Application::Result<void, Application::ApplicationError> append(
        const Application::AuditEvent &event) override;

private:
    QString m_databasePath;
};

} // namespace PersonOS::Infrastructure
