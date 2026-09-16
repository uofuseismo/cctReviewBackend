#include <iostream>
#include <cmath>
#include <limits>
#include <chrono>
#include <atomic>
#include <string>
#include <thread>
#include <mutex>
#include <spdlog/spdlog.h>
#include <soci/soci.h>
#include "cctPostgresService.hpp"
#include "postgresql.hpp"
#include "events.hpp"
#include "unpackCCTJSON.hpp"

using namespace CCTService;

class CCTPostgresService::CCTPostgresServiceImpl
{
public:
    CCTPostgresServiceImpl(
        std::unique_ptr<PostgreSQL> &&connection,
        const std::set<std::string> &schemas)
    {
        if (connection == nullptr)
        {
            throw std::invalid_argument("Connection is NULL");
        }
        if (!connection->isConnected())
        {
            throw std::invalid_argument("Not connected");
        }
        if (schemas.empty()){throw std::invalid_argument("No schemas set");}
        mSchemas = schemas;
        mConnection = std::move(connection);
        for (const auto &schema : mSchemas)
        {
            mEventsMap.insert( std::pair {schema, Events{}} );
        }
        for (const auto &schema : mSchemas)
        {
            initialQuery(schema);
        }
    }
    ~CCTPostgresServiceImpl()
    {
        stop();
    }
    [[nodiscard]] bool isRunning() const noexcept
    {
        return mRunning;
    }
    void initialQuery(const std::string &schema)
    {
        spdlog::debug("Querying events from " + schema + "...");
        if (!mConnection->isConnected())
        {
            spdlog::warn("Reconnecting to CCT postgres");
            mConnection->connect();
        }
        if (!mConnection->isConnected())
        {
            spdlog::warn("CCT postgres connection broken");
            return;
        }
        auto session
             = reinterpret_cast<soci::session *> (mConnection->getSession());
        soci::rowset<soci::row> rows
            = (session->prepare <<
                "SELECT identifier, CAST(mw_data AS TEXT), cct_magnitude, cct_magnitude_type, authoritative_magnitude, authoritative_magnitude_type, review_status, creation_mode, EXTRACT(epoch FROM last_update) FROM "
              + schema + ".event ORDER BY load_date DESC LIMIT 50");
        double newestUpdate = std::numeric_limits<double>::lowest();
        for (soci::rowset<soci::row>::const_iterator it = rows.begin();
             it != rows.end();
             ++it)
        {
            const auto &row = *it;
            try
            {
                auto identifier = row.get<long long> (0);
                auto sIdentifier = std::to_string(identifier);
                auto json = nlohmann::json::parse(row.get<std::string> (1));
                auto eventDetails
                    = ::unpackCCTJSON(json, std::to_string(identifier));
                eventDetails.emplace("cctMagnitude",
                                     row.get<double> (2));
                eventDetails.emplace("cctMagnitudeType",
                                     row.get<std::string> (3));
                eventDetails.emplace("authoritativeMagnitude",
                                     row.get<double> (4));
                eventDetails.emplace("authoritativeMagnitudeType",
                                     row.get<std::string> (5));
                eventDetails.emplace("reviewStatus",
                                     row.get<std::string> (6));
                eventDetails.emplace("creationMode",
                                     row.get<std::string> (7));
                double lastUpdate = row.get<double> (8);
                newestUpdate = std::max(lastUpdate, newestUpdate);
                Event event{eventDetails, json};
                {
                std::scoped_lock lock(mMutex);
                mEventsMap.at(schema).insert(std::pair {sIdentifier, std::move(event)});
                }
            }
            catch (const std::exception &e)
            {
                spdlog::warn("Failed to unpack event; failed with: "
                           + std::string {e.what()});
            }
        }
        mEventsMap.at(schema).generateHash();
        mLastUpdateMap.insert(std::pair {schema, newestUpdate});
        spdlog::debug("Done querying events for schema " + schema);
    }
    void updateQuery(const std::string &schema)
    {
        spdlog::debug("Performing update query from " + schema + "...");
        if (!mConnection->isConnected())
        {
            spdlog::warn("Reconnecting to CCT postgres");
            mConnection->connect();
        }
        if (!mConnection->isConnected())
        {
            spdlog::critical("CCT postgres connection broken");
            return;
        }
        if (!mLastUpdateMap.contains(schema))
        {
            throw std::runtime_error("Can't find last update time for " + schema);
        }
        double newestUpdate = mLastUpdateMap[schema];
        auto session
             = reinterpret_cast<soci::session *> (mConnection->getSession());
        soci::rowset<soci::row> rows
            = (session->prepare <<
                "SELECT identifier, CAST(mw_data AS TEXT), cct_magnitude, cct_magnitude_type, authoritative_magnitude, authoritative_magnitude_type, review_status, creation_mode, EXTRACT(epoch FROM last_update) FROM "
              + schema + ".event "
              + " WHERE " + schema + ".event.last_update > TO_TIMESTAMP(" + std::to_string(newestUpdate) + ") "
              + " ORDER BY load_date DESC");
        bool updated{false};
        //std::cout << std::setprecision(16) << schema << " " << newestUpdate << std::endl;
        for (soci::rowset<soci::row>::const_iterator it = rows.begin();
             it != rows.end();
             ++it)
        {
            const auto &row = *it;
            try
            {
                auto identifier = row.get<long long> (0);
                auto sIdentifier = std::to_string(identifier);
                auto json = nlohmann::json::parse(row.get<std::string> (1));
                auto eventDetails
                    = ::unpackCCTJSON(json, std::to_string(identifier));
                eventDetails.emplace("cctMagnitude",
                                     row.get<double> (2));
                eventDetails.emplace("cctMagnitudeType",
                                     row.get<std::string> (3));
                eventDetails.emplace("authoritativeMagnitude",
                                     row.get<double> (4));
                eventDetails.emplace("authoritativeMagnitudeType",
                                     row.get<std::string> (5));
                eventDetails.emplace("reviewStatus",
                                     row.get<std::string> (6));
                eventDetails.emplace("creationMode",
                                     row.get<std::string> (7));
                double lastUpdate = row.get<double> (8);
                //Event event{eventDetails, json};
                std::pair<std::string, Event>
                    valueToAddOrInsert{sIdentifier, 
                                       Event {eventDetails, json}};
                {
                std::scoped_lock lock(mMutex);
                if (!mEventsMap.at(schema).contains(sIdentifier))
                {
                    spdlog::info("Adding " + sIdentifier);
                    mEventsMap.at(schema).insert(std::move(valueToAddOrInsert));
                }
                else
                {
                    spdlog::info("Updating " + sIdentifier);
                    mEventsMap[schema].update(std::move(valueToAddOrInsert));
                }
                mEventsMap.at(schema).generateHash();
                }
                newestUpdate = std::max(lastUpdate, newestUpdate);
                updated = true;
            }
            catch (const std::exception &e)
            {
                spdlog::warn("Failed to unpack event; failed with: "
                           + std::string {e.what()});
            }
        }
        if (updated)
        {
            mLastUpdateMap[schema] = newestUpdate;
        }
    }
    void start()
    {
        stop();
        setRunning(true);
        mThread = std::thread(&CCTPostgresServiceImpl::run, this);
    }
    void run()
    {
        while (isRunning())
        {
            auto now 
                = std::chrono::duration_cast<std::chrono::seconds> (
                    std::chrono::system_clock::now().time_since_epoch());
            if (now > mLastQuery + mQueryInterval)
            {
                // Perform update query
                for (const auto &schema : mSchemas)
                {
                    try
                    {
                        updateQuery(schema); 
                    }
                    catch (const std::exception &e)
                    {
                        spdlog::error("Failed to perform update query on " + schema
                                    + "; failed with "
                                    + std::string {e.what()});
                    }
                }
                mLastQuery = now;
            }
            std::this_thread::sleep_for(std::chrono::seconds (1));
            
        }
    }
    void setRunning(bool running)
    {
        mRunning = running;
    }
    void stop()
    {
        setRunning(false);
        if (mThread.joinable()){mThread.join();}
    }
    /// Have event?
    [[nodiscard]] bool haveEvent(const std::string &schema,
                                 const std::string &event) const noexcept
    {
        if (!mEventsMap.contains(schema)){return false;}
        std::scoped_lock lock(mMutex);
        return mEventsMap.at(schema).contains(event); 
    } 
    /// Lightweight data to string
    [[nodiscard]] std::string lightWeightDataToString(const std::string &schema,
                                                      const int indent) const
    {
        std::scoped_lock lock(mMutex);
        return mEventsMap.at(schema).lightWeightDataToString(indent);
    }   
    /// Query for envelope data
    [[nodiscard]] std::string envelopeDataToString(const std::string &schema,
                                                   const std::string &eventIdentifier,
                                                   const int indent) const
    {
        std::string result;
        auto session
             = reinterpret_cast<soci::session *> (mConnection->getSession());
        *session << "SELECT CAST(envelope_data AS TEXT) FROM "
                  + schema + ".event "
                  + " WHERE " + schema + ".event.identifier = " + eventIdentifier
                  + " LIMIT 1;",
                  soci::into(result);
        if (!result.empty())
        {
            try
            {
                auto envelopeJsonData = nlohmann::json::parse(result);
                result = envelopeJsonData.dump(indent); 
            }
            catch (const std::exception &e)
            {
                spdlog::warn("Failed to unpack data for " + eventIdentifier);
                result.clear();
            }
        }
        return result; 
    }
    /// Heavyweight data to string
    [[nodiscard]] std::string heavyWeightDataToString(const std::string &schema,
                                                      const std::string &eventIdentifier,
                                                      const int indent) const
    {
        std::scoped_lock lock(mMutex);
        return mEventsMap.at(schema).heavyWeightDataToString(eventIdentifier, indent);
    }
    /// Accept or reject event
    [[nodiscard]] bool acceptRejectEvent(const std::string &schema,
                                         const std::string &eventIdentifier,
                                         const std::string &reviewStatus)
    {
        bool success{true};
        auto nowMuS
           = std::chrono::duration_cast<std::chrono::microseconds> (
             std::chrono::high_resolution_clock::now().time_since_epoch());
        auto lastUpdate = static_cast<double> (nowMuS.count())*1.e-6;
        std::string query = "UPDATE "
                          + schema + ".event SET (review_status, last_update) = (:review_status, TO_TIMESTAMP(:last_update)) WHERE "
                          + schema + ".event.identifier=:identifier";
        {
        std::scoped_lock lock(mMutex);
        if (!mConnection->isConnected())
        {
            spdlog::warn("Reconnecting to CCT postgres");
            mConnection->connect();
        }
        if (!mConnection->isConnected())
        {
            spdlog::critical("CCT postgres connection broken");
            return false;
        }
        auto session
             = reinterpret_cast<soci::session *> (mConnection->getSession());
        soci::statement statement
             = (session->prepare << query,
                                    soci::use(reviewStatus),
                                    soci::use(lastUpdate),
                                    soci::use(eventIdentifier));
        try
        {
            statement.execute();
            success = true;
        }
        catch (const std::exception &e)
        {
            spdlog::critical(e.what());
            success = false;
        }
        }
        // Won't matter if we fail - but let's try to update the event data
        try
        {
            updateQuery(schema);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Failed to perform update query after accept/reject: "
                       + std::string {e.what()});
        }
        return success;
    }
    /// Accept event
    [[nodiscard]] bool acceptEvent(const std::string &schema,
                                   const std::string &eventIdentifier)
    {
        return acceptRejectEvent(schema, eventIdentifier, "A");
    }
    /// Reject event
    [[nodiscard]] bool rejectEvent(const std::string &schema,
                                   const std::string &eventIdentifier)
    {
        return acceptRejectEvent(schema, eventIdentifier, "R");
    }
    [[nodiscard]] size_t getCurrentHash(const std::string &schema) const
    {
        if (!mEventsMap.contains(schema)){return 0;}
        std::scoped_lock lock(mMutex);
        return mEventsMap.at(schema).getHash();
    }
    /// Get event
    [[nodiscard]] Event getEvent(const std::string &schema,
                                 const std::string &eventIdentifier) const
    {
        std::scoped_lock lock(mMutex);
        return mEventsMap.at(schema).at(eventIdentifier);
    }
//private:
    mutable std::mutex mMutex;
    std::unique_ptr<PostgreSQL> mConnection{nullptr};
    std::thread mThread;
    std::set<std::string> mSchemas;
    std::map<std::string, Events> mEventsMap;
    std::map<std::string, double> mLastUpdateMap;
    std::chrono::seconds mLastQuery{0};
    std::chrono::seconds mQueryInterval{1*60};
    std::atomic<bool> mRunning{false};
    //double mLastUpdate{std::numeric_limits<double>::lowest()};
};

/// Constructor
CCTPostgresService::CCTPostgresService(
    std::unique_ptr<PostgreSQL> &&connection,
    const std::set<std::string> &schemas) :
    pImpl(std::make_unique<CCTPostgresServiceImpl> (std::move(connection), schemas))
{
}

/// Destructor
CCTPostgresService::~CCTPostgresService() = default;

/// Running?
bool CCTPostgresService::isRunning() const noexcept
{
    return pImpl->isRunning();
}

/// Stop
void CCTPostgresService::stop()
{
    pImpl->stop();
}

/// Start
void CCTPostgresService::start()
{
    if (pImpl->mConnection == nullptr)
    {
        throw std::runtime_error("Connection not set");
    }
    spdlog::info("Starting CCT database poller");
    pImpl->start();
}

/// Have event?
bool CCTPostgresService::haveEvent(const std::string &schema,
                                   const std::string &event) const noexcept
{
    return pImpl->haveEvent(schema, event); 
}

/// Schemas
std::set<std::string> CCTPostgresService::getSchemas() const noexcept
{
    return pImpl->mSchemas;
}

/// Have schema?
bool CCTPostgresService::haveSchema(const std::string &schema) const noexcept
{
    return pImpl->mSchemas.contains(schema);
}

/// Get event reference
Event CCTPostgresService::getEvent(
    const std::string &schema, const std::string &identifier) const 
{
    if (!haveSchema(schema))
    {
        throw std::invalid_argument("Schema " + schema + " does not exist");
    }
    if (!haveEvent(schema, identifier))
    {
        throw std::invalid_argument(identifier
                                  + " does not exist in " + schema);
    }
    return pImpl->getEvent(schema, identifier);
}

/// Lightweight data
std::string CCTPostgresService::lightWeightDataToString(
    const std::string &schema,
    const int indent) const
{
    return pImpl->lightWeightDataToString(schema, indent);
}

/// Heavyweight data
std::string CCTPostgresService::heavyWeightDataToString(
    const std::string &schema,
    const std::string &identifier,
    const int indent) const
{
    return pImpl->heavyWeightDataToString(schema, identifier, indent);
}

/// Envelope data
std::string CCTPostgresService::envelopeDataToString(
    const std::string &schema,
    const std::string &identifier,
    const int indent) const
{
    if (!haveSchema(schema))
    {
        throw std::invalid_argument("Schema " + schema + " does not exist");
    }
    if (!haveEvent(schema, identifier))
    {
        throw std::invalid_argument("Event " + identifier
                                  + " does not exist in schema " + schema);
    }
    return pImpl->envelopeDataToString(schema, identifier, indent);
}


/// Accept event
void CCTPostgresService::acceptEvent(
    const std::string &schema,
    const std::string &identifier)
{
    if (!haveSchema(schema))
    {
        throw std::invalid_argument("Schema " + schema + " does not exist");
    }
    if (!haveEvent(schema, identifier))
    { 
        throw std::invalid_argument("Event " + identifier
                                  + " does not exist in schema " + schema);
    }
    if (!pImpl->acceptEvent(schema, identifier))
    {
        throw std::runtime_error("Failed to accept event " + identifier);
    }
}

/// Reject event
void CCTPostgresService::rejectEvent(
    const std::string &schema,
    const std::string &identifier)
{
    if (!haveSchema(schema))
    {
        throw std::invalid_argument("Schema " + schema + " does not exist");
    }
    if (!haveEvent(schema, identifier))
    {
        throw std::invalid_argument("Event " + identifier
                                  + " does not exist in schema " + schema);
    }
    if (!pImpl->rejectEvent(schema, identifier))
    {
        throw std::runtime_error("Failed to reject event " + identifier);
    }
}

size_t CCTPostgresService::getCurrentHash(const std::string &schema) const
{
    return pImpl->getCurrentHash(schema);
}
