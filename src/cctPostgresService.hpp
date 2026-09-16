#ifndef CCT_BACKEND_SERVICE_DATABASE_CCT_POSTGRES_SERVICE_HPP
#define CCT_BACKEND_SERVICE_DATABASE_CCT_POSTGRES_SERVICE_HPP
#include <memory>
#include <set>
#include "events.hpp"
namespace CCTService
{
 class PostgreSQL;
}
namespace CCTService
{
/// @name CCTPostgreSQL "cctPostgreSQL.hpp" "cctPostgreSQL.hpp"
/// @brief Defines the interactivity with the CCT PostgreSQL database.
/// @copyright Ben Baker (University of Utah) distributed under the MIT license. 
class CCTPostgresService
{
public:
    /// @name Constructors
    /// @{

    /// @brief Constructor.
    CCTPostgresService() = delete;
    /// @brief Creates the postgres service that queries the given schemas
    ///        from the given database connection.
    CCTPostgresService(std::unique_ptr<PostgreSQL> &&connection,
                       const std::set<std::string> &schemas);
    /// @}

    /// @name Operators
    /// @{

    /// @brief Starts the poller service.
    void start();
    /// @result True indicates the service is running.
    [[nodiscard]] bool isRunning() const noexcept;
    /// @brief Stops the service.
    void stop();
 
    /// @result True indicates the schema exists.
    [[nodiscard]] bool haveSchema(const std::string &schema) const noexcept;
    /// @result The available schemas.
    [[nodiscard]] std::set<std::string> getSchemas() const noexcept; 
    /// @brief Accepts an event.
    void acceptEvent(const std::string &schema,
                     const std::string &eventIdentifier);
    /// @brief Rejects an event.
    void rejectEvent(const std::string &schema,
                     const std::string &eventIdentifier);
    /// @result A reverence to the events.
    //[[nodiscard]] const Events &getEventsReference(const std::string &schema) const;

    /// @result True indicates the event identifier exists in the schema.
    [[nodiscard]] bool haveEvent(const std::string &schema, const std::string &identifier) const noexcept;
    [[nodiscard]] Event getEvent(const std::string &schema, const std::string &identifier) const;
    [[nodiscard]] std::string lightWeightDataToString(const std::string &schema, int indent =-1) const;
    [[nodiscard]] std::string heavyWeightDataToString(const std::string &schema, const std::string &identifier, int indent =-1) const;
    [[nodiscard]] std::string envelopeDataToString(const std::string &schema, const std::string &identifier, int indent =-1) const;
    [[nodiscard]] size_t getCurrentHash(const std::string &schema) const;
    /// @name Destructors
    /// @{

    /// @brief Destructor.
    ~CCTPostgresService();
    /// @}

    CCTPostgresService(const CCTPostgresService &) = delete;
    CCTPostgresService& operator=(const CCTPostgresService &) = delete;
    CCTPostgresService(CCTPostgresService &&) noexcept = delete;
    CCTPostgresService& operator=(CCTPostgresService &&) noexcept = delete;
private:
    class CCTPostgresServiceImpl;
    std::unique_ptr<CCTPostgresServiceImpl> pImpl;
};
}
#endif
