#ifndef CCT_BACKEND_SERVICE_DATABASE_AQMS_POSTGRES_CLIENT_HPP
#define CCT_BACKEND_SERVICE_DATABASE_AQMS_POSTGRES_CLIENT_HPP
#include <memory>
#include <optional>
namespace CCTService
{
 class PostgreSQL;
 class NetMag;
}
namespace CCTService
{
/// @name AQMSPostgresClient "aqmsPostgreClient.hpp" "aqmsPostgresClient.hpp"
/// @brief Defines the interactivity with the AQMS PostgreSQL database.
/// @copyright Ben Baker (University of Utah) distributed under the MIT license. 
class AQMSPostgresClient
{
public:
    /// @name Constructors
    /// @{

    /// @brief Constructor.
    AQMSPostgresClient() = delete;
    /// @brief Creates the postgres service that queries the given schemas
    ///        from the given database connection.
    explicit AQMSPostgresClient(std::unique_ptr<PostgreSQL> &&connection);
    /// @}

    /// @brief Insert a network magnitude - this is an accept action.
    void insertNetworkMagnitude(const std::string &user, const std::string &eventIdentifier, const NetMag &networkMagnitude,
                                bool updatePrefMag);
    void insertNetworkMagnitude(const std::string &user, int64_t eventIdentifier, const NetMag &networkMagnitude,
                                bool updatePrefMag);
    /// @brief Updates a network magnitude - not sure how this happens
    ///        but it could.
    void updateNetworkMagnitude(const std::string &user, const std::string &eventIdentifier, const NetMag &networkMagnitude,
                                const bool updatePrefMag);
    void updateNetworkMagnitude(const std::string &user, int64_t eventIdentifier, const NetMag &networkMagnitude,
                                const bool updatePrefMag);
    /// @brief Delete a network magnitude.  This is for a cancel action.
    void deleteNetworkMagnitude(const std::string &user, const std::string &eventIdentifier);
    void deleteNetworkMagnitude(const std::string &user, int64_t eventIdentifier);
    [[nodiscard]] std::optional<int64_t> getMwCodaMagnitudeIdentifier(const std::string &eventIdentifier) const;
    [[nodiscard]] std::optional<int64_t> getMwCodaMagnitudeIdentifier(int64_t eventIdentifier) const;
    [[nodiscard]] bool mwCodaMagnitudeExists(const std::string &eventIdentifier) const;
    [[nodiscard]] bool mwCodaMagnitudeExists(int64_t eventIdentifier) const;
    [[nodiscard]] int64_t getPreferredOriginIdentifier(const std::string &eventIdentifier) const;
    [[nodiscard]] int64_t getPreferredOriginIdentifier(int64_t eventIdentifier) const;
    [[nodiscard]] std::optional<int64_t> getPreferredMagnitudeIdentifier(int64_t eventIdentifier) const;
    /// @name Destructors
    /// @{

    /// @brief Destructor.
    ~AQMSPostgresClient();
    /// @}

    AQMSPostgresClient(const AQMSPostgresClient &) = delete;
    AQMSPostgresClient(AQMSPostgresClient &&) noexcept = delete;
    AQMSPostgresClient& operator=(const AQMSPostgresClient &) = delete;
    AQMSPostgresClient& operator=(AQMSPostgresClient &&) noexcept = delete;
private:
    class AQMSPostgresClientImpl;
    std::unique_ptr<AQMSPostgresClientImpl> pImpl;
};
}
#endif
