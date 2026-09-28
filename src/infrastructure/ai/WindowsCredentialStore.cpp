#include "infrastructure/ai/WindowsCredentialStore.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

#include <QString>

#include <string>

namespace PersonOS::Infrastructure {

namespace {
Application::ApplicationError failure(const std::string &operation)
{
    return {Application::ErrorCode::Storage, operation,
#ifdef Q_OS_WIN
            std::to_string(GetLastError()),
#else
            "windows credential store unavailable on this platform",
#endif
            false};
}

#ifdef Q_OS_WIN
// QString::toStdWCharArray 的中间 QByteArray 必须存活到 Win32 调用结束
LPWSTR toTarget(const std::string &reference, std::wstring &storage)
{
    storage = QString::fromStdString(reference).toStdWString();
    return storage.data();
}
#endif
} // namespace

Application::Result<void, Application::ApplicationError> WindowsCredentialStore::write(
    const std::string &reference, const std::string &secret)
{
#ifdef Q_OS_WIN
    std::wstring targetStorage;
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = toTarget(reference, targetStorage);
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&credential, 0))
        return Application::Result<void, Application::ApplicationError>::failure(
            failure("credential write failed"));
    return Application::Result<void, Application::ApplicationError>::success();
#else
    Q_UNUSED(reference)
    Q_UNUSED(secret)
    return Application::Result<void, Application::ApplicationError>::failure(
        failure("credential write failed"));
#endif
}

Application::Result<std::string, Application::ApplicationError> WindowsCredentialStore::read(
    const std::string &reference)
{
#ifdef Q_OS_WIN
    std::wstring targetStorage;
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(toTarget(reference, targetStorage), CRED_TYPE_GENERIC, 0, &credential))
        return Application::Result<std::string, Application::ApplicationError>::failure(
            failure("credential read failed"));
    std::string secret(reinterpret_cast<const char *>(credential->CredentialBlob),
                       credential->CredentialBlobSize);
    CredFree(credential);
    return Application::Result<std::string, Application::ApplicationError>::success(
        std::move(secret));
#else
    Q_UNUSED(reference)
    return Application::Result<std::string, Application::ApplicationError>::failure(
        failure("credential read failed"));
#endif
}

Application::Result<void, Application::ApplicationError> WindowsCredentialStore::remove(
    const std::string &reference)
{
#ifdef Q_OS_WIN
    std::wstring targetStorage;
    if (!CredDeleteW(toTarget(reference, targetStorage), CRED_TYPE_GENERIC, 0))
        return Application::Result<void, Application::ApplicationError>::failure(
            failure("credential remove failed"));
    return Application::Result<void, Application::ApplicationError>::success();
#else
    Q_UNUSED(reference)
    return Application::Result<void, Application::ApplicationError>::failure(
        failure("credential remove failed"));
#endif
}

} // namespace PersonOS::Infrastructure
