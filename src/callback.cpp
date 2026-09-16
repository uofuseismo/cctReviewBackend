#include <string>
#include <map>
#include <cmath>
#include <iostream>
#include <functional>
#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <GeographicLib/Geodesic.hpp>
#include <GeographicLib/Constants.hpp>
#include "cctPostgresService.hpp"
#include "aqmsPostgresClient.hpp"
#include "callback.hpp"
#include "permissions.hpp"
#include "events.hpp"
#include "exceptions.hpp"
#include "authenticator.hpp"
#include "aqms.hpp"
#include "base64.hpp"

using namespace CCTService;

namespace
{

void distaz(const GeographicLib::Geodesic &geodesic,
            const std::pair<double, double> &sourceLatitudeAndLongitude,
            const std::pair<double, double> &stationLatitudeAndLongitude,
            double &greatCircleDistance,
            double &distance,
            double &azimuth,
            double &backAzimuth)
{
    try 
    {   
        // These will throw
        auto sourceLatitude = sourceLatitudeAndLongitude.first;
        auto sourceLongitude = sourceLatitudeAndLongitude.second;
        auto stationLatitude = stationLatitudeAndLongitude.first;
        auto stationLongitude = stationLatitudeAndLongitude.second;
        // Do calculation
        //GeographicLib::Geodesic geodesic{GeographicLib::Constants::WGS84_a(),
        //                                 GeographicLib::Constants::WGS84_f()};
        greatCircleDistance
            = geodesic.Inverse(sourceLatitude, sourceLongitude,
                               stationLatitude, stationLongitude,
                               distance, azimuth, backAzimuth);
        // Translate azimuth from [-180,180] to [0,360].
        if (azimuth < 0){azimuth = azimuth + 360;}
        // Translate azimuth from [-180,180] to [0,360] then convert to a
        // back-azimuth by subtracting 180, i.e., +180.
        backAzimuth = backAzimuth + 180;
    }   
    catch (const std::exception &e) 
    {   
        throw std::runtime_error("Vincenty failed with: "
                               + std::string {e.what()});
    }   
}

void computeDistanceAndAzimuth(
    const std::pair<double, double> &sourceLatitudeAndLongitude,
    const std::map<std::string, std::pair<double, double>> &stationLocations,
    double &closestDistanceKM,
    double &gap)
{
    // Nothign to do
    if (stationLocations.empty())
    {
        closestDistanceKM =-1;
        gap =-1;
        return;
    }
    // Compute distance and azimuth for every station
    GeographicLib::Geodesic geodesic{GeographicLib::Constants::WGS84_a(),
                                    GeographicLib::Constants::WGS84_f()};
    closestDistanceKM = std::numeric_limits<double>::max();
    std::vector<double> backAzimuths;
    for (const auto &stationLocation : stationLocations)
    {
        double greatCircleDistance, distanceMeters, azimuth, backAzimuth;
        // Throws - if any station fails then the results are shoddy
        // so this function fails
        ::distaz(geodesic, 
                 sourceLatitudeAndLongitude,
                 stationLocation.second,
                 greatCircleDistance,
                 distanceMeters,
                 azimuth,
                 backAzimuth);
        double distanceKM = distanceMeters*1.e-3;
        closestDistanceKM = std::min(closestDistanceKM, distanceKM);
        backAzimuths.push_back(backAzimuth);
    }
    // Compute the gap
    gap =-1;
    if (!backAzimuths.empty())
    {
        gap = 360; // One station
        if (backAzimuths.size() > 1)
        {
            gap = 0;
            std::sort(backAzimuths.begin(), backAzimuths.end());
            backAzimuths.push_back(backAzimuths[0] + 360);
            for (int i = 0; i < static_cast<int> (backAzimuths.size() - 1); ++i) 
            {
                gap = std::max(gap, backAzimuths.at(i + 1) - backAzimuths.at(i));
            }
        }
    }
}

/*
std::optional<int> getNumberOfStations(
    const ::Event &event, const std::string &identifier)
{
    if (event.mFullData.contains("measuredMwDetails"))
    {
        const auto &measuredMwDetails 
            = event.mFullData["measuredMwDetails"]; 
        if (measuredMwDetails.contains(identifier))
        {
            const auto &measuredMwDetailsForEvent
                = measuredMwDetails[identifier];
            if (measuredMwDetailsForEvent.contains("stationCount"))
            {
                auto nStations
                    = measuredMwDetailsForEvent["stationCount"].template
                      get<int> ();
                if (nStations > 0){return std::optional<int> (nStations);}
            }
        }
    }
    return std::nullopt;
}
*/

void getMagnitudeDistanceAzimuthAndNumberOfStations(
    const ::Event &event, const std::string &identifier,
    double &magnitude,
    double &closestDistanceKM,
    double &azimuthalGap,
    int &nStations,
    int &nObservations)
{
    magnitude =-10;
    closestDistanceKM =-1;
    azimuthalGap =-1;
    nStations =-1;
    nObservations =-1;
    std::map<std::string, std::pair<double, double>> stationLocations; 
    std::pair<double, double> eventLocation;
    bool haveEventLocation{false};
    if (event.mFullData.contains("measuredMwDetails"))
    {
        const auto &measuredMwDetails 
            = event.mFullData["measuredMwDetails"]; 
        if (measuredMwDetails.contains(identifier))
        {
            const auto &measuredMwDetailsForEvent
                = measuredMwDetails[identifier];
            if (!measuredMwDetailsForEvent.contains("mw"))
            {
                throw std::runtime_error("mw not set");
            }
            magnitude
                 = measuredMwDetailsForEvent["mw"].template get<double> ();
            if (measuredMwDetailsForEvent.contains("stationCount"))
            {
                nStations
                    = measuredMwDetailsForEvent["stationCount"].template
                      get<int> (); 
            }
            if (measuredMwDetailsForEvent.contains("latitude") &&
                measuredMwDetailsForEvent.contains("longitude"))
            {
                auto latitude = measuredMwDetailsForEvent["latitude"].template get<double> ();
                auto longitude = measuredMwDetailsForEvent["longitude"].template get<double> ();
                if (latitude >= -90 && latitude <= 90)
                {
                    eventLocation = std::pair {latitude, longitude};
                    //std::cout << latitude << " " << longitude << std::endl;
                    haveEventLocation = true;
                }
            }
        }
    }
    int observationCounter{0};
    if (event.mFullData.contains("spectraMeasurements"))
    {
        const auto &spectraMeasurements
            = event.mFullData["spectraMeasurements"];
        if (spectraMeasurements.contains(identifier))
        {
            for (const auto &measurement :
                 spectraMeasurements[identifier])
            {
                if (measurement.contains("pathAndSiteCorrected"))
                {
                    if (measurement["pathAndSiteCorrected"].is_number())
                    {
                        observationCounter = observationCounter + 1;
                    }
                }
                if (measurement.contains("waveform"))
                {
                    const auto &waveform = measurement["waveform"];
                    if (waveform.contains("stream"))
                    {
                        const auto &stream = waveform["stream"];
                        if (stream.contains("station"))
                        {
                            const auto &station = stream["station"];
                            if (station.contains("latitude") &&
                                station.contains("longitude") &&
                                station.contains("networkName") &&
                                station.contains("stationName"))
                            {
                                auto name = station["networkName"].template get<std::string> ()
                                          + "."
                                          + station["stationName"].template get<std::string> ();
                                auto latitude = station["latitude"].template get<double> ();
                                auto longitude = station["longitude"].template get<double> ();
                                if (!stationLocations.contains(name) &&
                                    (latitude >=-90 && latitude <= 90))
                                {
                                    stationLocations.insert(std::pair {name, std::pair {latitude, longitude}});
                                    // std::cout << name <<  " " << latitude << " " << longitude << std::endl;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (observationCounter >= 0){nObservations = observationCounter;}
    // Warning - I guess the mwMeasured details are authorative?
    if (nStations != stationLocations.size())
    {
        spdlog::warn("Number of stations differs form stationLocations.size()");
    }
    // Throws - but these aren't essential things so eat the error
    if (haveEventLocation)
    {
        try
        {
            ::computeDistanceAndAzimuth(eventLocation,
                                        stationLocations,
                                        closestDistanceKM,
                                        azimuthalGap);
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Failed to get distance/azimuth because " 
                       + std::string {e.what()});
        }
    }
    else
    {
        spdlog::warn("Could not extract event location from JSON");
    }
}

}

class Callback::CallbackImpl
{
public:
    /// @brief Authenticates the user with the given user name and
    ///        password stored in a base64 representation.  
    /// @result The response to propagate back to the client and
    ///         the user's credentials.
    /// @throws InvalidPermissionException if the user cannot be authenticated.
    [[nodiscard]] std::pair<nlohmann::json, IAuthenticator::Credentials>
        authenticate(const std::string &userNameAndPasswordHex) const
    {
        nlohmann::json jsonResponse; 
        IAuthenticator::Credentials credentials;
        auto authorizationValue
            = base64::from_base64(userNameAndPasswordHex);
        auto splitIndex = authorizationValue.find(":");
        if (splitIndex != authorizationValue.npos)
        {
            std::string userName
                = authorizationValue.substr(0, splitIndex);
            std::string password;
            if (splitIndex < authorizationValue.length() - 1)
            {
                 password
                     = authorizationValue.substr(splitIndex + 1,
                                                 authorizationValue.length());
            }
            if (!mAuthenticator->authenticate(userName, password))
            {
                throw InvalidPermissionException("Could not authenticate "
                                               + userName);
            }

            spdlog::debug("Sending authentication response...");
            auto tempCredentials = mAuthenticator->getCredentials(userName); 
            if (tempCredentials)
            {
                credentials = std::move(*tempCredentials);
                jsonResponse["status"] = "success";
                jsonResponse["jsonWebToken"] = credentials.token;
                jsonResponse["permissions"]
                    = CCTService::permissionsToString(credentials.permissions);
            }
            else
            {
                spdlog::warn("Somehow failed to return credentials");
                throw std::runtime_error("Internal error");
            }
        }
        else
        {
            throw BadRequestException(
               "Basic authorization field could not be parsed");
        }
        return std::pair {jsonResponse, credentials};
    }
    /// @brief Authorizes a user given with the provided JWT.
    /// @param[in] jsonWebToken  The JSON web token.
    /// @result The user's credentials.
    /// @throws InvalidPermissionException if the user is not authorized.
    [[nodiscard]] IAuthenticator::Credentials 
        authorize(const std::string jsonWebToken) const
    {
        IAuthenticator::Credentials credentials;
        spdlog::debug("Created: " + jsonWebToken);
        try
        {
             credentials = mAuthenticator->authorize(jsonWebToken);
             spdlog::debug("Callback authorized " + credentials.user);
        }
        catch (const std::exception &e)
        {
             spdlog::debug("Could not authenticated bearer token: "
                         + std::string {e.what()});
             throw InvalidPermissionException(
                 "Could not authorize bearer token");
        }
        return credentials;
    }
///private:
    std::function<std::string (const boost::beast::http::header
                               <
                                   true,
                                   boost::beast::http::basic_fields<std::allocator<char>>
                               > &,
                               const std::string &,
                               const boost::beast::http::verb)> mCallbackFunction;
    std::shared_ptr<CCTPostgresService> mCCTPostgresService{nullptr};
    std::shared_ptr<
       std::map<std::string, std::unique_ptr<CCTService::AQMSPostgresClient>>
    > mAQMSClients{nullptr};
    std::shared_ptr<IAuthenticator> mAuthenticator{nullptr};
    std::string mAuthority{"UU"};
    std::string mSubSource{"cct"};
    std::string mAlgorithm{"LLNLCCT"};
};

/// @brief Constructor.
Callback::Callback(
    std::shared_ptr<CCTPostgresService> &cctEventsService,
    std::shared_ptr<
       std::map<std::string, std::unique_ptr<CCTService::AQMSPostgresClient>>
    > &aqmsClients,
    std::shared_ptr<IAuthenticator> &authenticator
    ) :
    pImpl(std::make_unique<CallbackImpl> ())
{
    if (cctEventsService == nullptr)
    {
        throw std::invalid_argument("CCT events service is NULL");
    }
    if (!cctEventsService->isRunning())
    {
        throw std::runtime_error("CCT event service is not running");
    }
    if (aqmsClients == nullptr)
    {
        throw std::invalid_argument("AQMS clients map is NULL");
    }
    if (authenticator == nullptr)
    {
        throw std::invalid_argument("Authenticator is NULL");
    }
    auto schemas = cctEventsService->getSchemas();
    for (const auto &schema : schemas)
    {
        if (!aqmsClients->contains(schema))
        {
            throw std::invalid_argument(
                schema + " does not have an AQMS postgres client");
        }
    }
    pImpl->mCallbackFunction
        = std::bind(&Callback::operator(),
                    this,
                    std::placeholders::_1,
                    std::placeholders::_2,
                    std::placeholders::_3);
    pImpl->mCCTPostgresService = cctEventsService;
    pImpl->mAQMSClients = aqmsClients;
    pImpl->mAuthenticator = authenticator;
}

/// @brief Destructor.
Callback::~Callback() = default;

/// @brief Actually processes the requests.
std::string Callback::operator()(
    const boost::beast::http::header
    <
        true,
        boost::beast::http::basic_fields<std::allocator<char>>
    > &requestHeader,
    const std::string &message,
    const boost::beast::http::verb httpRequestType) const
{
    // First thing is we authenticate/authorize the user
    IAuthenticator::Credentials credentials;
    auto authorizationIndex = requestHeader.find("Authorization");
    if (authorizationIndex != requestHeader.end())
    {
        std::vector<std::string> authorizationField;
        boost::split(authorizationField,
                     requestHeader["Authorization"],
                     boost::is_any_of(" \t\n"));
        if (authorizationField.size() < 2)
        {
            throw BadRequestException("Authorization field incorrect size");
        }
        if (authorizationField.at(0) == "Basic")
        {
            spdlog::debug("Basic authentication");
            auto [jsonResponse, temporaryCredentials] 
                = pImpl->authenticate(authorizationField.at(1));
            return jsonResponse.dump();
        }
        else if (authorizationField.at(0) == "Bearer")
        {
            spdlog::debug("Bearer authorization");
            credentials = pImpl->authorize(authorizationField.at(1));
        }
        else
        {
            throw UnimplementedException("Unhandled authorization type: "
                                       + authorizationField.at(0));
        }
    }
    else
    {
        throw BadRequestException("Authorization header field not defined");
    }
    // If the credentials are insufficient then we're out of here
    if (credentials.permissions == Permissions::None)
    {
        throw InvalidPermissionException(
            "Insufficient permissions to make request");
    }
    if (credentials.permissions == Permissions::ReadOnly &&
        (httpRequestType != boost::beast::http::verb::put ||
         httpRequestType != boost::beast::http::verb::post))
    {
        throw InvalidPermissionException(
            "Insufficient permissions to put/post");
    }

    // I only know how to handle these types of requests
    if (httpRequestType != boost::beast::http::verb::get &&
        httpRequestType != boost::beast::http::verb::put &&
        httpRequestType != boost::beast::http::verb::post)
    {   
        throw std::runtime_error("Unhandled http request verb");
    }

    // We're to the message part -> is the parsable?
    if (message.empty())
    {
        throw BadRequestException("Empty request");
    }
    nlohmann::json result;
    nlohmann::json object;
    try
    {
        object = nlohmann::json::parse(message);
    }   
    catch (const std::exception &e)
    {
        throw std::runtime_error("Could not parse JSON request");
    }
    if (!object.contains("requestType"))
    {
        throw BadRequestException("requestType not set in JSON request");
    }
    auto requestType = object["requestType"].template get<std::string> ();
    spdlog::info("Received request type " + requestType
               + " from " + credentials.user);
    // Schema requests
    if (requestType == "availableSchemas")
    {
        nlohmann::json result;
        result["status"] = "success";
        result["request"] = requestType;
        result["availableSchemas"] = pImpl->mCCTPostgresService->getSchemas();
        return result.dump();
    }

    // Lightweight CCT data
    if (requestType == "hash")
    {
        if (!object.contains("schema"))
        {
            throw BadRequestException("schema not set in JSON request");
        }
        auto schema = object["schema"].template get<std::string> (); 
        if (!pImpl->mCCTPostgresService->haveSchema(schema))
        {
            throw BadRequestException("Invalid schema: " + schema);
        }
        auto hash = pImpl->mCCTPostgresService->getCurrentHash(schema);
        nlohmann::json result;
        result["status"] = "success";
        result["request"] = requestType;
        result["hash"] = hash;
        return result.dump();
    }
    else if (requestType == "cctData")
    {
        if (!object.contains("schema"))
        {
            throw BadRequestException("schema not set in JSON request");
        }
        spdlog::debug("Performing lightweight data request for "
                    + credentials.user);
        auto schema = object["schema"].template get<std::string> ();
        if (!pImpl->mCCTPostgresService->haveSchema(schema))
        {
            spdlog::error(schema + " does not exist");
            throw BadRequestException("Invalid schema: " + schema);
        }
        nlohmann::json result;
        std::string cctData
            = pImpl->mCCTPostgresService->lightWeightDataToString(schema, -1); 
        //std::cout << cctData << std::endl;
        result["status"] = "success";
        result["request"] = requestType;
        result["events"] = std::move(cctData);
        return result.dump();
    }
    else if (requestType == "eventData")
    {
        if (!object.contains("schema"))
        {
            throw BadRequestException("schema not set in JSON request");
        }
        if (!object.contains("eventIdentifier"))
        {
            throw BadRequestException(
                "eventIdentifier not set in JSON request");
        }
        spdlog::debug("Performing heavyweight data request for "
                    + credentials.user);
        auto eventIdentifier
            = object["eventIdentifier"].template get<std::string> ();
        if (eventIdentifier.empty())
        {
            throw BadRequestException("Event identifier is empty");
        }
        auto schema = object["schema"].template get<std::string> (); 
        if (!pImpl->mCCTPostgresService->haveSchema(schema))
        {
            throw BadRequestException("Invalid schema: " + schema);
        }
        nlohmann::json result;
        std::string eventData;
        try
        {
            eventData
               = pImpl->mCCTPostgresService->heavyWeightDataToString(
                     schema, eventIdentifier, -1);
        }
        catch (const std::exception &e)
        {
            throw BadRequestException("Invalid event identifier: "
                                    + eventIdentifier); 
        }
        result["status"] = "success";
        result["request"] = requestType;
        result["eventIdentifier"] = eventIdentifier;
        result["data"] = std::move(eventData);
        return result.dump();
    }
    else if (requestType == "envelopeData")
    {
        if (!object.contains("schema"))
        {
            throw BadRequestException("schema not set in JSON request");
        }
        if (!object.contains("eventIdentifier"))
        {
            throw BadRequestException(
                "eventIdentifier not set in JSON request");
        }
        spdlog::debug("Performing envelope data request for "
                    + credentials.user);
        auto eventIdentifier
            = object["eventIdentifier"].template get<std::string> (); 
        if (eventIdentifier.empty())
        {
            throw BadRequestException("Event identifier is empty");
        }
        auto schema = object["schema"].template get<std::string> (); 
        if (!pImpl->mCCTPostgresService->haveSchema(schema))
        {
            throw BadRequestException("Invalid schema: " + schema);
        }
        nlohmann::json result;
        std::string envelopeData;
        try
        {
            envelopeData
               = pImpl->mCCTPostgresService->envelopeDataToString(
                     schema, eventIdentifier, -1);
        }
        catch (const std::exception &e)
        {
            throw BadRequestException("Invalid event identifier: "
                                    + eventIdentifier);
        }
        result["status"] = "success";
        result["request"] = requestType;
        result["eventIdentifier"] = eventIdentifier;
        result["data"] = std::move(envelopeData);
        return result.dump();
    }
    else if (requestType == "accept")
    {
        if (!object.contains("schema"))
        {
            throw BadRequestException("schema not set in JSON request");
        }
        if (!object.contains("eventIdentifier"))
        {
            throw BadRequestException(
                "eventIdentifier not set in JSON request");
        }
        spdlog::debug("Performing accept for request for "
                    + credentials.user);
        auto eventIdentifier
            = object["eventIdentifier"].template get<std::string> (); 
        if (eventIdentifier.empty())
        {
            throw BadRequestException("Event identifier is empty");
        }
        auto schema = object["schema"].template get<std::string> (); 
        if (!pImpl->mCCTPostgresService->haveSchema(schema))
        {
            throw BadRequestException("Invalid schema: " + schema);
        }
        std::string status{"failure"};
        std::string reason;
        if (!pImpl->mCCTPostgresService->haveEvent(schema, eventIdentifier))
        {
            reason = eventIdentifier + " does not exist";
            spdlog::error(reason);
        }
        else
        {
            spdlog::info("Accepting magnitude for " + eventIdentifier
                       + " on " + schema + " schema");
            try
            {
//if (schema == "test")
//{
                auto eventDetails
                    = pImpl->mCCTPostgresService->getEvent(schema,
                                                           eventIdentifier);
                //auto nStations
                //    = ::getNumberOfStations(eventDetails, eventIdentifier);
                int nStations{-1};
                int nObservations{-1};
                double magnitude{-10};
                double closestDistanceKM{-1};
                double azimuthalGap{-1};
                try
                {
                    ::getMagnitudeDistanceAzimuthAndNumberOfStations(
                        eventDetails, eventIdentifier,
                        magnitude,
                        closestDistanceKM, 
                        azimuthalGap,
                        nStations,
                        nObservations);
                }
                catch (const std::exception &e)
                {
                     closestDistanceKM =-1;
                     azimuthalGap =-1;
                     spdlog::warn(e.what());
                }
                // Figure out the necessary AQMS details
                auto originIdentifier
                   = pImpl->mAQMSClients->at(schema)
                          ->getPreferredOriginIdentifier(eventIdentifier);
                CCTService::NetMag networkMagnitude;
                //networkMagnitude.setIdentifier(); // Can be set during insert
                networkMagnitude.setOriginIdentifier(originIdentifier);
                networkMagnitude.setMagnitude(magnitude);
                networkMagnitude.setMagnitudeType("w");
                networkMagnitude.setAuthority(pImpl->mAuthority);
                networkMagnitude.setReviewFlag(NetMag::ReviewFlag::Human);
                if (nStations >= 0)
                {
                    networkMagnitude.setNumberOfStations(nStations);
                }
                if (nObservations >= 0)
                {
                    networkMagnitude.setNumberOfObservations(nObservations);
                }
                if (azimuthalGap >= 0 && azimuthalGap < 360)
                {
                    networkMagnitude.setGap(azimuthalGap);
                }
                if (closestDistanceKM >= 0)
                {
                    networkMagnitude.setDistance(closestDistanceKM);
                }
                // Update or insert?
                auto existingMagnitudeIdentifier
                    = pImpl->mAQMSClients->at(schema)
                           ->getMwCodaMagnitudeIdentifier(eventIdentifier);
                if (existingMagnitudeIdentifier)
                {
                    spdlog::info("Will attempt to update Mw,coda magnitude for "
                               + eventIdentifier + " for user " + credentials.user);
                    networkMagnitude.setIdentifier(*existingMagnitudeIdentifier);
                    constexpr bool updatePrefMag{false};
                    pImpl->mAQMSClients->at(schema)
                         ->updateNetworkMagnitude(credentials.user,
                                                  eventIdentifier,
                                                  networkMagnitude,
                                                  updatePrefMag);
                }
                else
                {
                    constexpr bool updatePrefMag{false};
                    spdlog::info("Will attempt to create Mw,coda magnitude for "
                               + eventIdentifier + " for user " + credentials.user);
                    pImpl->mAQMSClients->at(schema)
                         ->insertNetworkMagnitude(credentials.user,
                                                  eventIdentifier,
                                                  networkMagnitude,
                                                  updatePrefMag);
                }
//spdlog::info(originIdentifier);
                pImpl->mCCTPostgresService->acceptEvent(schema, eventIdentifier);
                status = "success";
//}
//else
//{
// spdlog::critical("update for prod");
//}
                status = "success"; 
            }
            catch (const std::exception &error)
            {
                spdlog::warn("Failed to accept event " + eventIdentifier
                           + " because " + error.what());
                reason = "Server error";
                status = "failure";
            }
        }
        nlohmann::json result;
        result["status"] = status;
        result["request"] = requestType;
        result["eventIdentifier"] = eventIdentifier;
        if (!reason.empty()){result["reason"] = reason;}
        return result.dump();
    }
    else if (requestType == "reject")
    {
        if (!object.contains("schema"))
        {
            throw BadRequestException("schema not set in JSON request");
        }
        if (!object.contains("eventIdentifier"))
        {
            throw BadRequestException(
                "eventIdentifier not set in JSON request");
        }
        spdlog::debug("Performing reject request for "
                    + credentials.user);
        auto eventIdentifier
            = object["eventIdentifier"].template get<std::string> (); 
        if (eventIdentifier.empty())
        {
            throw BadRequestException("Event identifier is empty");
        }
        auto schema = object["schema"].template get<std::string> (); 
        if (!pImpl->mCCTPostgresService->haveSchema(schema))
        {
            throw BadRequestException("Invalid schema: " + schema);
        }
        std::string status{"failure"};
        std::string reason;
        if (!pImpl->mCCTPostgresService->haveEvent(schema, eventIdentifier))
        {
            reason = eventIdentifier + " does not exist";
            spdlog::error(reason);
        }
        else
        {
            spdlog::info("Rejecting magnitude for " + eventIdentifier
                       + " in schema " + schema);
            try
            {
//if (schema == "test")
//{
                // Delete
                auto mwCodaMagnitudeExists
                    = pImpl->mAQMSClients->at(schema)
                           ->mwCodaMagnitudeExists(eventIdentifier);
                if (mwCodaMagnitudeExists)
                {
                    spdlog::info("Will attempt to delete Mw,coda magnitude for "
                               + eventIdentifier + " for user " + credentials.user);
                    pImpl->mAQMSClients->at(schema)
                         ->deleteNetworkMagnitude(credentials.user,
                                                  eventIdentifier);
                }
                pImpl->mCCTPostgresService->rejectEvent(schema, eventIdentifier);
                status = "success";
//}
//else
//{
// spdlog::critical("update reject for prod");
//status = "success";
//}
            }
            catch (const std::exception &error)
            {
                spdlog::warn("Failed to reject event " + eventIdentifier
                           + " because " + error.what());
                reason = "Server error";
                status = "failure";
            }
            status = "success";
        }   
        nlohmann::json result;
        result["status"] = status;
        result["request"] = requestType;
        result["eventIdentifier"] = eventIdentifier;
        if (!reason.empty()){result["reason"] = reason;}
        return result.dump();
    }
    throw BadRequestException("Unhandled request type: " + requestType);
}

/// @result A function pointer to the callback.
std::function<
    std::string (
        const boost::beast::http::header<
            true,
            boost::beast::http::basic_fields<std::allocator<char>> 
        > &,
        const std::string &,
        const boost::beast::http::verb
    )>
Callback::getCallbackFunction() const noexcept
{
    return pImpl->mCallbackFunction;
}
