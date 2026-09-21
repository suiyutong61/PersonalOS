#pragma once

#include <cstdio>

#include <QString>
#include <QtLogging>

// 测试程序共享工具（README 3.6 踩坑）
// Qt 6 在 Windows"无控制台"环境下默认把日志写入 OutputDebugString（终端不可见），
// 测试程序统一安装自定义消息处理器直写 stderr。
namespace PersonOS::Test {

inline void installMessageHandler()
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &msg) {
        std::fprintf(stderr, "%s\n", msg.toUtf8().constData());
        std::fflush(stderr);
    });
}

} // namespace PersonOS::Test
