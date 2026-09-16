#ifndef CCT_BACKEND_SERVICE_EVENTS_HPP
#define CCT_BACKEND_SERVICE_EVENTS_HPP
#include <string>
#include <chrono>
#include <nlohmann/json.hpp>
namespace CCTService
{
struct Event
{
    nlohmann::json mLightWeightData;
    nlohmann::json mFullData;
    std::chrono::milliseconds mCreationTime
    {
        std::chrono::duration_cast<std::chrono::milliseconds>
        (
            std::chrono::high_resolution_clock::now().time_since_epoch()
        )
    };
};
class Events
{
public:
    Events() = default;
    ~Events() = default;
    void insert(const std::pair<std::string, Event> &event)
    {
        auto copy = event;
        insert(std::move(copy));
    }
    void insert(std::pair<std::string, Event> &&event)
    {
        if (contains(event.first))
        {
            throw std::invalid_argument(event.first + " already exists");
        }
        mEvents.insert(std::move(event));
    }
    void update(const std::pair<std::string, Event> &event)
    {
        auto copy = event;
        update(std::move(copy));
    }
    void update(std::pair<std::string, Event> &&event)
    {
        if (!contains(event.first))
        {
            insert(std::move(event));
            return;
        }
        mEvents[event.first] = std::move(event.second);
    }
    void generateHash()
    {
        if (mEvents.empty())
        {
            mHash = 0;
        }
        else
        {
            std::string jsonString = lightWeightDataToString(-1);
            mHash = std::hash<std::string> {}(jsonString);
        }
    }
    void clear() noexcept
    {
        mEvents.clear();
    }
    [[nodiscard]] bool contains(const std::string &identifier) const noexcept
    {
        return mEvents.contains(identifier);
    }
    [[nodiscard]] size_t size() const noexcept
    {
        return mEvents.size();
    }
    [[nodiscard]] std::string lightWeightDataToString(const int indent =-1) const
    {
        if (mEvents.empty()){return "";}
        nlohmann::json events;
        for (const auto &event : mEvents)
        {
            events.push_back(event.second.mLightWeightData);
        }
        return std::move(events.dump(indent));
    };
    [[nodiscard]]
    std::string heavyWeightDataToString(const std::string eventIdentifier,
                                        const int indent =-1) const
    {
        if (mEvents.empty()){return "";}
        auto idx = mEvents.find(eventIdentifier);
        if (idx != mEvents.end())
        {
            return idx->second.mFullData.dump(indent);
        }
        return "";
    }
    [[nodiscard]] size_t getHash() const noexcept
    {
        return mHash;
    }
    [[nodiscard]] Event at(const std::string &eventIdentifier) const
    {
        return mEvents.at(eventIdentifier);
    }
private:
    std::map<std::string, Event> mEvents;
    size_t mHash{0};
};
}
#endif
