#include "application/usecases/detection/DetectionUseCases.h"

#include "application/audit/Audit.h"

namespace PersonOS::Application {

namespace {

// 规范 UTC ISO → 自纪元分钟数（YYYY-MM-DDTHH:MM:SSZ）
std::optional<long long> parseIsoMinutes(const std::string &iso)
{
    if (iso.size() != 20 || iso[19] != 'Z')
        return std::nullopt;
    const auto num = [&iso](int begin, int count) -> long long {
        long long value = 0;
        for (int i = begin; i < begin + count; ++i) {
            if (iso[i] < '0' || iso[i] > '9')
                return -1;
            value = value * 10 + (iso[i] - '0');
        }
        return value;
    };
    const long long year = num(0, 4);
    const long long month = num(5, 2);
    const long long day = num(8, 2);
    const long long hour = num(11, 2);
    const long long minute = num(14, 2);
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23
        || minute > 59)
        return std::nullopt;

    // 自 1970-01-01 的天数（civil algorithm）
    auto daysFromCivil = [](long long y, long long m, long long d) -> long long {
        y -= m <= 2;
        const long long era = (y >= 0 ? y : y - 399) / 400;
        const long long yoe = y - era * 400;
        const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    };
    return daysFromCivil(year, month, day) * 1440 + hour * 60 + minute;
}

std::string jsonList(const std::vector<std::string> &items)
{
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i)
            out += ",";
        out += "\"" + items[i] + "\"";
    }
    out += "]";
    return out;
}

// 中性原因假设（R2.2：不把"进度慢"等同于"不自律"）
const std::vector<std::string> kLagCauses = {
    "实际可投入时间或精力低于计划假设",
    "任务量或难度估计偏乐观",
    "前置知识不足导致进度受阻",
    "当前方法不适合这部分内容",
    "近期环境或现实事务发生变化",
    "本轮节奏安排需要调整（非自律问题）",
};
const std::vector<std::string> kLagInterventions = {
    "缩小本轮任务量", "调整 MEL 周期", "补充前置知识", "增加练习或测试",
    "更换学习资源", "调整任务顺序或时间比例", "暂停、降级或重新规划目标",
};
const std::vector<std::string> kInactivityCauses = {
    "可能已暂停但未记录", "近期时间或精力不足", "存在启动困难",
    "本轮安排与现实事务冲突", "目标或动机需要重新确认",
};
const std::vector<std::string> kInactivityInterventions = {
    "先了解原因，再决定是否调整", "缩小任务或降低启动门槛", "调整提醒与时段",
    "暂停并择机重新进入", "重新确认目标与路线",
};
const std::vector<std::string> kWeakCauses = {
    "练习或复述不足", "学习方法与该内容不匹配", "前置概念不牢固",
    "检测内容超出本轮学习范围", "临时状态影响发挥",
};
const std::vector<std::string> kWeakInterventions = {
    "安排复习与间隔练习", "换一种方法重新学习", "补充前置内容",
    "复核题目范围与判定", "改天再次检测确认",
};

} // namespace

std::optional<long long> isoToEpochMinutes(const std::string &iso)
{
    return parseIsoMinutes(iso);
}

DetectionUseCases::DetectionUseCases(StateRepository &states, MelRepository &mels,
                                     AssessmentRepository &assessments, UuidPort &uids,
                                     const Domain::Clock &clock)
    : m_states(states), m_mels(mels), m_assessments(assessments), m_uids(uids), m_clock(clock)
{}

Result<bool, ApplicationError> DetectionUseCases::appendDetection(
    const Domain::Uid &userId, const std::string &definitionCode,
    const Detection &detection, const std::string &nowIso,
    const std::string &idempotencyKey)
{
    // 幂等：同一定义仍有有效事件（TTL 内）则不再落一条
    if (m_states.latestValid(userId, definitionCode, nowIso))
        return Result<bool, ApplicationError>::success(false);
    if (m_states.existsIdempotencyKey(idempotencyKey))
        return Result<bool, ApplicationError>::success(false);

    const auto definition = m_states.findDefinitionByCode(definitionCode);
    if (!definition)
        return Result<bool, ApplicationError>::failure(
            {ErrorCode::NotFound, "detection definition missing: " + definitionCode, {},
             false});

    Domain::StateEvent event;
    event.uid = m_uids.next();
    event.userId = userId;
    event.definitionId = definition->uid;
    event.source = Domain::StateSource::SystemDerived;
    event.confidence = 0.8;
    event.observedAt = nowIso;
    event.validUntil = m_clock.utcIsoPlusMinutes(definition->defaultTtlMin);
    event.consentScope = "system_derived";
    event.idempotencyKey = idempotencyKey;
    event.valueJson = "{\"title\":\"" + detection.title + "\",\"causes\":"
                      + jsonList(detection.causes) + ",\"interventions\":"
                      + jsonList(detection.interventions) + ",\"basis\":\""
                      + detection.basis + "\"}";

    const auto saved = m_states.appendEvent(event);
    if (!saved.ok)
        return Result<bool, ApplicationError>::failure(saved.error);
    Audit::record({"system", {}, "detection.recorded", "state_event", event.uid.value(),
                   "{\"definition\":\"" + definitionCode + "\"}"});
    return Result<bool, ApplicationError>::success(true);
}

Result<int, ApplicationError> DetectionUseCases::detect(const Domain::Uid &userId,
                                                        const std::string &nowIso,
                                                        const DetectionConfig &config)
{
    if (!config.isValid())
        return Result<int, ApplicationError>::failure(
            {ErrorCode::Validation, "detection config invalid or missing basis source", {},
             false});

    const auto nowMinutes = parseIsoMinutes(nowIso);
    if (!nowMinutes)
        return Result<int, ApplicationError>::failure(
            {ErrorCode::Validation, "nowIso must be canonical UTC ISO", {}, false});

    int created = 0;

    // 1) 进度落后：活跃 MEL 的实际进度明显落后于时间进度（多原因假设，不贴标签）
    for (const auto &mel : m_mels.findActive(userId, 20)) {
        if (mel.state != Domain::MelState::Active)
            continue;
        const auto startMinutes = parseIsoMinutes(mel.plannedStartAt);
        const auto endMinutes = parseIsoMinutes(mel.plannedEndAt);
        if (!startMinutes || !endMinutes || *endMinutes <= *startMinutes)
            continue;
        const double window = static_cast<double>(*endMinutes - *startMinutes);
        const double elapsed =
            std::clamp(static_cast<double>(*nowMinutes - *startMinutes) / window, 0.0, 1.0);
        if (elapsed < 0.05)
            continue;   // 刚开局不检测

        double progressSum = 0.0;
        int required = 0;
        for (const auto &task : m_mels.tasksOf(mel.uid))
            if (task.required) {
                progressSum += task.progress;
                ++required;
            }
        if (required == 0)
            continue;
        const double actual = progressSum / required;
        if (actual < elapsed - config.progressLagTolerance) {
            Detection detection;
            detection.code = "subproblem_progress_lag";
            detection.title = "本轮进度落后于计划节奏";
            detection.causes = kLagCauses;
            detection.interventions = kLagInterventions;
            detection.basis = "计划节奏 " + std::to_string(elapsed) + "，实际 "
                              + std::to_string(actual) + "；阈值依据：" + config.basisSource;
            const auto appended = appendDetection(
                userId, detection.code, detection, nowIso,
                "detect:progress_lag:" + mel.uid.value() + ":" + nowIso);
            if (!appended)
                return Result<int, ApplicationError>::failure(appended.error());
            if (appended.value())
                ++created;
        }
    }

    // 2) 实际耗时明显超过预计
    for (const auto &mel : m_mels.findActive(userId, 20)) {
        int planned = 0;
        for (const auto &task : m_mels.tasksOf(mel.uid))
            planned += task.plannedEffortMin;
        if (planned <= 0)
            continue;
        const int actual = m_mels.actualMinutesOf(mel.uid);
        if (static_cast<double>(actual) > planned * config.effortRatioThreshold) {
            Detection detection;
            detection.code = "subproblem_time_overrun";
            detection.title = "实际耗时明显高于预计";
            detection.causes = {"任务难度或估时偏低", "方法效率低于预期",
                                "中途遇到额外障碍", "实际范围超出计划"};
            detection.interventions = {"重估任务耗时区间", "更换或补充方法",
                                       "拆分任务", "检查任务范围是否漂移"};
            detection.basis = "计划 " + std::to_string(planned) + " 分钟，实际 "
                              + std::to_string(actual) + " 分钟；阈值依据："
                              + config.basisSource;
            const auto appended = appendDetection(
                userId, detection.code, detection, nowIso,
                "detect:time_overrun:" + mel.uid.value() + ":" + nowIso);
            if (!appended)
                return Result<int, ApplicationError>::failure(appended.error());
            if (appended.value())
                ++created;
        }
    }

    // 3) 长时间没有执行记录（缺失上报不直接等同于停滞；先了解原因）
    const auto cutoffMinutes = *nowMinutes - config.inactivityMinutes;
    for (const auto &mel : m_mels.findActive(userId, 20)) {
        const auto lastAt = m_mels.lastProgressAtIso(mel.uid);
        if (lastAt) {
            const auto lastMinutes = parseIsoMinutes(*lastAt);
            if (lastMinutes && *lastMinutes >= cutoffMinutes)
                continue;   // 近期有执行
        }
        // 无记录或记录过久：仅当 MEL 已激活足够久才提示（避免刚创建就提示）
        const auto activated = parseIsoMinutes(mel.activatedAt ? *mel.activatedAt
                                                               : mel.createdAt);
        if (activated && *nowMinutes - *activated < config.inactivityMinutes)
            continue;
        Detection detection;
        detection.code = "subproblem_inactivity";
        detection.title = "一段时间没有执行记录";
        detection.causes = kInactivityCauses;
        detection.interventions = kInactivityInterventions;
        detection.basis = "最近执行：" + (lastAt ? *lastAt : "无记录") + "；观察周期："
                          + std::to_string(config.inactivityMinutes) + " 分钟；依据："
                          + config.basisSource;
        const auto appended = appendDetection(
            userId, detection.code, detection, nowIso,
            "detect:inactivity:" + mel.uid.value() + ":" + nowIso);
        if (!appended)
            return Result<int, ApplicationError>::failure(appended.error());
        if (appended.value())
            ++created;
    }

    // 4) 窗口内的"提示后仍不能"验收结果
    for (const auto &assessment : m_assessments.listForUser(userId, 10)) {
        if (assessment.status != Domain::AssessmentStatus::Scored)
            continue;
        // 验收时间以结果创建时间为准（无专列时用 scope 无关的最近尝试时间近似：
        // 结果表无时间列，保守起见仅在 scored 状态下检索 not_recalled）
        bool weak = false;
        for (const auto &attempt : m_assessments.attemptsOf(assessment.uid)) {
            for (const auto &result : m_assessments.resultsOfAttempt(attempt.uid)) {
                if (result.mastery == Domain::Mastery::NotRecalled) {
                    weak = true;
                    break;
                }
            }
            if (weak)
                break;
        }
        if (!weak)
            continue;
        Detection detection;
        detection.code = "subproblem_weak_assessment";
        detection.title = "验收中出现了提示后仍不能完成的内容";
        detection.causes = kWeakCauses;
        detection.interventions = kWeakInterventions;
        detection.basis = "验收 " + assessment.uid.value() + " 存在 not_recalled 结果；"
                          "窗口依据：" + config.basisSource;
        const auto appended = appendDetection(
            userId, detection.code, detection, nowIso,
            "detect:weak_assessment:" + assessment.uid.value() + ":" + nowIso);
        if (!appended)
            return Result<int, ApplicationError>::failure(appended.error());
        if (appended.value())
            ++created;
    }

    return Result<int, ApplicationError>::success(created);
}

Result<std::vector<DetectionUseCases::DetectionEvent>, ApplicationError>
DetectionUseCases::recentDetections(const Domain::Uid &userId, int limit)
{
    static const char *kCodes[] = {
        "subproblem_progress_lag", "subproblem_time_overrun",
        "subproblem_weak_assessment", "subproblem_inactivity",
        "subproblem_reported_prerequisite", "subproblem_reported_material",
    };
    std::vector<DetectionEvent> out;
    for (const auto &event : m_states.recentEvents(userId, limit * 2)) {
        bool matched = false;
        for (const char *code : kCodes)
            if (event.definitionCode == code) {
                matched = true;
                break;
            }
        if (!matched)
            continue;
        DetectionEvent detection;
        detection.definitionCode = event.definitionCode;
        detection.valueJson = event.valueJson;
        detection.observedAt = event.observedAt;
        out.push_back(std::move(detection));
        if (static_cast<int>(out.size()) >= limit)
            break;
    }
    return Result<std::vector<DetectionEvent>, ApplicationError>::success(std::move(out));
}

} // namespace PersonOS::Application
