#include "application/usecases/domain/StateContextBuilder.h"

namespace PersonOS::Application {

namespace {

// 最近一次事件（不区分是否仍有效）→ 用于 last_known 判定
std::optional<Domain::StateEvent> latestAny(StateRepository &states, const Domain::Uid &userId,
                                            const std::string &definitionCode)
{
    // recentEvents 按时间倒序；过滤出该定义的第一条即可
    for (const auto &event : states.recentEvents(userId, 200))
        if (event.definitionCode == definitionCode) {
            Domain::StateEvent full;
            full.uid = event.uid;
            full.userId = userId;
            full.valueJson = event.valueJson;
            full.observedAt = event.observedAt;
            full.validUntil = event.validUntil;
            full.idempotencyKey = event.idempotencyKey;
            return full;
        }
    return std::nullopt;
}

} // namespace

StateContextBuilder::StateContextBuilder(StateRepository &states, const Domain::Clock &clock)
    : m_states(states), m_clock(clock)
{}

Result<std::vector<StateContextBuilder::Item>, ApplicationError> StateContextBuilder::items(
    const Domain::Uid &userId, const std::string &asOfIso)
{
    std::vector<Item> out;
    for (const auto &definition : m_states.allDefinitions()) {
        Item item;
        item.code = definition.code;
        item.freshness = "unknown";
        if (const auto current = m_states.latestValid(userId, definition.code, asOfIso)) {
            item.freshness = "current";
            item.valueJson = current->valueJson;
            item.observedAt = current->observedAt;
            item.validUntil = current->validUntil;
        } else if (const auto last = latestAny(m_states, userId, definition.code)) {
            item.freshness = "last_known";
            item.valueJson = last->valueJson;
            item.observedAt = last->observedAt;
            item.validUntil = last->validUntil;
        }
        out.push_back(std::move(item));
    }
    return Result<std::vector<Item>, ApplicationError>::success(std::move(out));
}

Result<std::string, ApplicationError> StateContextBuilder::build(const Domain::Uid &userId,
                                                                 const std::string &asOfIso)
{
    const auto itemList = items(userId, asOfIso);
    if (!itemList)
        return Result<std::string, ApplicationError>::failure(itemList.error());

    std::string json = "{\"as_of\":\"" + asOfIso + "\",\"items\":[";
    for (size_t i = 0; i < itemList.value().size(); ++i) {
        const Item &item = itemList.value()[i];
        if (i)
            json += ",";
        json += "{\"code\":\"" + item.code + "\",\"freshness\":\"" + item.freshness
                + "\",\"value\":" + (item.valueJson.empty() ? "{}" : item.valueJson)
                + ",\"observed_at\":\"" + item.observedAt + "\",\"valid_until\":\""
                + item.validUntil + "\"}";
    }
    json += "]}";
    return Result<std::string, ApplicationError>::success(std::move(json));
}

} // namespace PersonOS::Application
