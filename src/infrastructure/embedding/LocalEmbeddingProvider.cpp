#include "infrastructure/embedding/LocalEmbeddingProvider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <cmath>

#include "infrastructure/embedding/SentencePieceUnigram.h"

#ifdef PERSONOS_LOCAL_EMBEDDING
#include "onnxruntime_c_api.h"
#endif

namespace PersonOS::Infrastructure {

#ifdef PERSONOS_LOCAL_EMBEDDING
namespace {
// 推理互斥(会话非线程安全);批量场景调用方串行使用
const OrtApi *ortApi()
{
    static const OrtApi *api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    return api;
}
} // namespace
#endif

LocalEmbeddingProvider::LocalEmbeddingProvider(QString modelPath, QString tokenizerPath)
    : m_modelPath(std::move(modelPath)), m_tokenizerPath(std::move(tokenizerPath))
{}

LocalEmbeddingProvider &sharedLocalEmbedding()
{
    static LocalEmbeddingProvider provider;
    return provider;
}

LocalEmbeddingProvider::~LocalEmbeddingProvider()
{
#ifdef PERSONOS_LOCAL_EMBEDDING
    QMutexLocker locker(&m_mutex);
    const OrtApi *api = ortApi();
    if (api) {
        if (m_session)
            api->ReleaseSession(static_cast<OrtSession *>(m_session));
        if (m_env)
            api->ReleaseEnv(static_cast<OrtEnv *>(m_env));
    }
    m_session = nullptr;
    m_env = nullptr;
#else
    Q_UNUSED(m_mutex);
#endif
}

QString LocalEmbeddingProvider::defaultModelPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/embedding/model.onnx");
}

QString LocalEmbeddingProvider::defaultTokenizerPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/embedding/tokenizer.json");
}

bool LocalEmbeddingProvider::isReady()
{
    return ensureLoaded();
}

bool LocalEmbeddingProvider::filesPresent()
{
    return QFile::exists(defaultModelPath()) && QFile::exists(defaultTokenizerPath());
}

bool LocalEmbeddingProvider::ensureLoaded()
{
#ifdef PERSONOS_LOCAL_EMBEDDING
    QMutexLocker locker(&m_mutex);
    if (m_ready)
        return true;
    const OrtApi *api = ortApi();
    if (!api || !QFile::exists(m_modelPath) || !QFile::exists(m_tokenizerPath)) {
        m_lastError = QStringLiteral("本地嵌入模型未安装(model.onnx/tokenizer.json 缺失)");
        return false;
    }

    m_tokenizer = std::make_unique<SentencePieceUnigram>();
    QFile tokenizerFile(m_tokenizerPath);
    if (!tokenizerFile.open(QIODevice::ReadOnly)
        || !m_tokenizer->loadFromTokenizerJson(tokenizerFile.readAll())) {
        m_tokenizer.reset();
        m_lastError = QStringLiteral("分词器加载失败");
        return false;
    }

    OrtEnv *env = nullptr;
    if (api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "local_embedding", &env) != nullptr) {
        m_lastError = QStringLiteral("ORT 环境创建失败");
        return false;
    }
    OrtSessionOptions *options = nullptr;
    api->CreateSessionOptions(&options);
    const std::wstring widePath = m_modelPath.toStdWString();
    OrtSession *session = nullptr;
    OrtStatus *status = api->CreateSession(env, widePath.c_str(), options, &session);
    api->ReleaseSessionOptions(options);
    if (status != nullptr) {
        m_lastError = QStringLiteral("模型会话创建失败: %1")
                          .arg(QString::fromUtf8(api->GetErrorMessage(status)));
        api->ReleaseStatus(status);
        api->ReleaseEnv(env);
        return false;
    }
    m_env = env;
    m_session = session;
    m_ready = true;
    m_lastError.clear();
    return true;
#else
    Q_UNUSED(m_mutex);
    m_lastError = QStringLiteral("本地向量化未编译(缺少 onnxruntime 依赖)");
    return false;
#endif
}

Application::Result<std::vector<float>, Application::ApplicationError>
LocalEmbeddingProvider::embed(const std::string &text)
{
    return embedQuery(text);
}

Application::Result<std::vector<float>, Application::ApplicationError>
LocalEmbeddingProvider::embedDocument(const std::string &text)
{
    return embedPassage(text);
}

Application::Result<std::vector<float>, Application::ApplicationError>
LocalEmbeddingProvider::embedQuery(const std::string &text)
{
    return embedWithPrefix(text, QStringLiteral("query: "));
}

Application::Result<std::vector<float>, Application::ApplicationError>
LocalEmbeddingProvider::embedPassage(const std::string &text)
{
    return embedWithPrefix(text, QStringLiteral("passage: "));
}

Application::Result<std::vector<float>, Application::ApplicationError>
LocalEmbeddingProvider::embedWithPrefix(const std::string &text, const QString &prefix)
{
    if (!ensureLoaded())
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            {Application::ErrorCode::ExternalUnavailable, m_lastError.toStdString(), {}, true});
#ifdef PERSONOS_LOCAL_EMBEDDING

    QMutexLocker locker(&m_mutex);
    const OrtApi *api = ortApi();

    const QString inputText = prefix + QString::fromStdString(text);
    std::vector<int> tokens = m_tokenizer->encode(inputText);
    // e5 位置编码上限 512(含 <s>/</s>):正文截断到 510,超长摘要不再越界
    constexpr size_t kMaxContentTokens = 510;
    if (tokens.size() > kMaxContentTokens)
        tokens.resize(kMaxContentTokens);
    std::vector<int64_t> ids = {0};   // <s>
    for (const int id : tokens)
        ids.push_back(id);
    ids.push_back(2);   // </s>
    std::vector<int64_t> mask(ids.size(), 1);
    std::vector<int64_t> types(ids.size(), 0);

    OrtMemoryInfo *memoryInfo = nullptr;
    if (api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memoryInfo) != nullptr)
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            {Application::ErrorCode::ExternalUnavailable, "ort memory info failed", {}, true});
    const int64_t shape[] = {1, static_cast<int64_t>(ids.size())};
    OrtValue *inputTensor = nullptr;
    OrtValue *maskTensor = nullptr;
    OrtValue *typeTensor = nullptr;
    api->CreateTensorWithDataAsOrtValue(memoryInfo, ids.data(), ids.size() * sizeof(int64_t),
                                        shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
                                        &inputTensor);
    api->CreateTensorWithDataAsOrtValue(memoryInfo, mask.data(), mask.size() * sizeof(int64_t),
                                        shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
                                        &maskTensor);
    api->CreateTensorWithDataAsOrtValue(memoryInfo, types.data(), types.size() * sizeof(int64_t),
                                        shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
                                        &typeTensor);

    const char *inputNames[] = {"input_ids", "attention_mask", "token_type_ids"};
    const OrtValue *inputs[] = {inputTensor, maskTensor, typeTensor};
    const char *outputNames[] = {"last_hidden_state"};
    OrtValue *output = nullptr;
    OrtStatus *status = api->Run(static_cast<OrtSession *>(m_session), nullptr, inputNames,
                                 inputs, 3, outputNames, 1, &output);
    api->ReleaseValue(inputTensor);
    api->ReleaseValue(maskTensor);
    api->ReleaseValue(typeTensor);
    api->ReleaseMemoryInfo(memoryInfo);
    if (status != nullptr || !output) {
        const std::string error =
            status ? api->GetErrorMessage(status) : "empty output";
        if (status)
            api->ReleaseStatus(status);
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            {Application::ErrorCode::ExternalUnavailable, error, {}, true});
    }

    OrtTensorTypeAndShapeInfo *info = nullptr;
    api->GetTensorTypeAndShape(output, &info);
    int64_t dims[3] = {1, 1, 384};
    size_t dimCount = 0;
    api->GetDimensionsCount(info, &dimCount);
    api->GetDimensions(info, dims, dimCount);
    api->ReleaseTensorTypeAndShapeInfo(info);
    float *data = nullptr;
    api->GetTensorMutableData(output, reinterpret_cast<void **>(&data));
    const int64_t hidden = dimCount >= 3 ? dims[2] : 384;
    const int64_t seq = dimCount >= 3 ? dims[1] : 1;
    std::vector<float> result(static_cast<size_t>(hidden), 0.0f);
    int64_t valid = 0;
    for (int64_t t = 0; t < seq; ++t) {
        if (mask[static_cast<size_t>(t)] == 0)
            continue;
        ++valid;
        for (int64_t h = 0; h < hidden; ++h)
            result[static_cast<size_t>(h)] += data[t * hidden + h];
    }
    api->ReleaseValue(output);
    if (valid > 0)
        for (auto &v : result)
            v /= static_cast<float>(valid);
    double norm = 0.0;
    for (const float v : result)
        norm += static_cast<double>(v) * v;
    norm = std::sqrt(norm);
    if (norm > 1e-12)
        for (auto &v : result)
            v = static_cast<float>(v / norm);
    return Application::Result<std::vector<float>, Application::ApplicationError>::success(
        std::move(result));
#else
    return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
        {Application::ErrorCode::ExternalUnavailable, m_lastError.toStdString(), {}, true});
#endif
}

} // namespace PersonOS::Infrastructure
