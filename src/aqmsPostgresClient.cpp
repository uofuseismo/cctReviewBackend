#include <soci/soci.h>
#include <spdlog/spdlog.h>
#include "aqmsPostgresClient.hpp"
#include "postgresql.hpp"
#include "aqms.hpp"

using namespace CCTService;

namespace
{

/// Gets the next sequence value
int64_t getNextSequenceValue(soci::session &session,
                             const std::string &sequenceName = "magseq")
{
    if (sequenceName.empty())
    {
        throw std::invalid_argument("sequenceName is empty");
    }
    int64_t sequenceValue{-1};
    try
    {
        {
        soci::transaction tr(session);
        session << "SELECT sequence.getNext(:sequenceName, 1)",
                   soci::use(sequenceName),
                   soci::into(sequenceValue);
        tr.commit();
        }
    }
    catch (const std::exception &e)
    {
        spdlog::warn(e.what());
    }
    if (sequenceValue < 0)
    {
        throw std::runtime_error("Failed to get sequence value for "
                               + sequenceName);
    }
    return sequenceValue;
}

int64_t convertEventIdentifier(const std::string &eventIdentifier)
{
    if (eventIdentifier.empty())
    {   
        throw std::invalid_argument("Event identifier is empty");
    }   
    int64_t identifier{-1};
    try 
    {   
        identifier = std::stol(eventIdentifier);    
    }   
    catch (...)
    {   
        try
        {
            // Might be in form of uu8238239 so pop uu
            auto temporaryIdentifier = eventIdentifier;
            if (temporaryIdentifier.size() > 2)
            {
                temporaryIdentifier.erase(0, 2); 
            }
            identifier = std::stol(temporaryIdentifier);
        }
        catch (const std::exception &e) 
        {
            throw std::invalid_argument("Could not convert "
                                      + eventIdentifier + " to an integer");
        }
    }   
    if (identifier < 0)
    {   
        throw std::invalid_argument("Could not convert "
                                  + eventIdentifier + " to an integer");
    }   
    return identifier;
}

std::pair<int, soci::indicator>
    getNumberOfStations(const NetMag &networkMagnitude)
{
    int nStationsInsert{-1};
    auto nStations = networkMagnitude.getNumberOfStations();
    soci::indicator nStationsIndicator{soci::i_null};
    if (nStations)
    {
        nStationsInsert = *nStations;
        nStationsIndicator = soci::indicator::i_ok;
    }
    return std::pair {nStationsInsert, nStationsIndicator};
}

std::pair<int, soci::indicator>
    getNumberOfObservations(const NetMag &networkMagnitude)
{
    int nObservationsInsert{-1};
    auto nObservations = networkMagnitude.getNumberOfObservations();
    soci::indicator nObservationsIndicator{soci::i_null};
    if (nObservations)
    {
        nObservationsInsert = *nObservations;
        nObservationsIndicator = soci::indicator::i_ok;
    }
    return std::pair {nObservationsInsert, nObservationsIndicator};
}

std::pair<double, soci::indicator> getGap(const NetMag &networkMagnitude)
{
    double gapInsert{-1};
    auto gap = networkMagnitude.getGap();
    soci::indicator gapIndicator{soci::i_null};
    if (gap)
    {   
        gapInsert = *gap;
        gapIndicator = soci::indicator::i_ok;
    }   
    return std::pair {gapInsert, gapIndicator};
}

std::pair<double, soci::indicator>
    getDistance(const NetMag &networkMagnitude)
{
    double distanceInsert{-1};
    auto distance = networkMagnitude.getDistance();
    soci::indicator distanceIndicator{soci::i_null};
    if (distance)
    {
        distanceInsert = *distance;
        distanceIndicator = soci::indicator::i_ok;
    }
    return std::pair {distanceInsert, distanceIndicator};
}

std::pair<std::string, soci::indicator>
    getMagnitudeAlgorithm(const NetMag &networkMagnitude)
{
    std::string magAlgo;
    auto magnitudeAlgorithm = networkMagnitude.getMagnitudeAlgorithm();
    soci::indicator magnitudeAlgorithmIndicator{soci::i_null};
    if (magnitudeAlgorithm)
    {
        magAlgo = *magnitudeAlgorithm;
        magnitudeAlgorithmIndicator = soci::i_ok;
    }
    return std::pair {magAlgo, magnitudeAlgorithmIndicator};
}
                
std::pair<std::string, soci::indicator>
    getReviewFlag(const NetMag &networkMagnitude)
{
    auto reviewFlag = networkMagnitude.getReviewFlag();
    std::string stringReviewFlag;
    soci::indicator reviewFlagIndicator{soci::i_null};
    if (reviewFlag)
    {
        if (*reviewFlag == NetMag::ReviewFlag::Human)
        {       
            stringReviewFlag = "H";
        }
        else if (*reviewFlag == NetMag::ReviewFlag::Automatic)
        {
            stringReviewFlag = "A";
        }
        else
        {
            spdlog::warn("Unhandled review flag");
        }
    }
    if (!stringReviewFlag.empty()){reviewFlagIndicator = soci::i_ok;}
    return std::pair {stringReviewFlag, reviewFlagIndicator};
}

}

class AQMSPostgresClient::AQMSPostgresClientImpl
{
public:
    explicit AQMSPostgresClientImpl(std::unique_ptr<PostgreSQL> &&connection)
    {
        if (connection == nullptr)
        {
            throw std::invalid_argument("AQMS database connection is NULL");
        }
        mConnection = std::move(connection);
    }
    std::unique_ptr<PostgreSQL> mConnection{nullptr};
};

/// Constructor
AQMSPostgresClient::AQMSPostgresClient(
    std::unique_ptr<PostgreSQL> &&connection) :
    pImpl(std::make_unique<AQMSPostgresClientImpl> (std::move(connection)))
{
}

/// Insert the network magnitude
void AQMSPostgresClient::insertNetworkMagnitude(
    const std::string &user,
    const std::string &eventIdentifier,
    const NetMag &networkMagnitude,
    const bool updatePrefMag)
{
    auto identifier = ::convertEventIdentifier(eventIdentifier);
    insertNetworkMagnitude(user, identifier, networkMagnitude, updatePrefMag);
}

void AQMSPostgresClient::insertNetworkMagnitude(
    const std::string &user,
    const int64_t eventIdentifier,
    const NetMag &networkMagnitudeIn,
    const bool updatePrefMag)
{
    if (mwCodaMagnitudeExists(eventIdentifier))
    {
        throw std::invalid_argument("Mw,Coda netmag already exists for "
                                  + std::to_string (eventIdentifier));
    }

    // Make sure I have a magnitude identifier and a few other
    // things I can figure out on the fly
    auto session
        = reinterpret_cast<soci::session *> (pImpl->mConnection->getSession());
    auto networkMagnitude = networkMagnitudeIn;
    /*
    if (!networkMagnitude.haveIdentifier())
    {
        auto magnitudeIdentifier
            = ::getNextSequenceValue(*session, "magseq");
        networkMagnitude.setIdentifier(magnitudeIdentifier);
    }
    */
    if (!networkMagnitude.haveOriginIdentifier())
    {
        auto originIdentifier
            = getPreferredOriginIdentifier(eventIdentifier);
    }
    if (!networkMagnitude.haveMagnitudeType())
    {   
        networkMagnitude.setMagnitudeType(MAGNITUDE_TYPE);
    }
    if (!networkMagnitude.haveSubSource())
    {
        networkMagnitude.setSubSource(MAGNITUDE_SUBSOURCE);
    }
    if (!networkMagnitude.haveMagnitude())
    {
        throw std::invalid_argument("Magnitude not set");
    }
    if (!networkMagnitude.haveAuthority())
    {
        throw std::invalid_argument("Authority not set");
    }
    auto currentPreferredMagnitudeIdentifier
        = getPreferredMagnitudeIdentifier(eventIdentifier);
    if (currentPreferredMagnitudeIdentifier == std::nullopt)
    {
        spdlog::warn("Currently there is no preferred magnitude");
    }

    // Get values for insertNetMag function
    auto [nStationsInsert, nStationsIndicator]
        = ::getNumberOfStations(networkMagnitude);
    auto [nObservationsInsert, nObservationsIndicator]
        = ::getNumberOfObservations(networkMagnitude);
    auto [gapInsert, gapIndicator] = ::getGap(networkMagnitude);
    auto [distanceInsert, distanceIndicator] = ::getDistance(networkMagnitude);
    auto [magAlgo, magnitudeAlgorithmIndicator]
        = ::getMagnitudeAlgorithm(networkMagnitude);
    auto [stringReviewFlag, reviewFlagIndicator]
        = ::getReviewFlag(networkMagnitude);

    /*
    int nStationsInsert{-1};
    auto nStations = networkMagnitude.getNumberOfStations();
    soci::indicator nStationsIndicator{soci::i_null};
    if (nStations)
    {
        nStationsInsert = *nStations;
        nStationsIndicator = soci::indicator::i_ok;
    }

    int nObservationsInsert{-1};
    auto nObservations = networkMagnitude.getNumberOfObservations();
    soci::indicator nObservationsIndicator{soci::i_null};
    if (nObservations)
    {
        nObservationsInsert = *nObservations;
        nObservationsIndicator = soci::indicator::i_ok;
    }

    double gapInsert{-1};
    auto gap = networkMagnitude.getGap();
    soci::indicator gapIndicator{soci::i_null};
    if (gap)
    {
        gapInsert = *gap;
        gapIndicator = soci::indicator::i_ok;
    }

    double distanceInsert{-1};
    auto distance = networkMagnitude.getDistance();
    soci::indicator distanceIndicator{soci::i_null};
    if (distance)
    {
        distanceInsert = *distance;
        distanceIndicator = soci::indicator::i_ok;
    }

    std::string magAlgo;
    auto magnitudeAlgorithm = networkMagnitude.getMagnitudeAlgorithm();
    soci::indicator magnitudeAlgorithmIndicator{soci::i_null};
    if (magnitudeAlgorithm)
    {
        magAlgo = *magnitudeAlgorithm;
        magnitudeAlgorithmIndicator = soci::i_ok;
    }

    auto reviewFlag = networkMagnitude.getReviewFlag();
    std::string stringReviewFlag;
    soci::indicator reviewFlagIndicator{soci::i_null};
    if (reviewFlag)
    {
        if (*reviewFlag == NetMag::ReviewFlag::Human)
        {
            stringReviewFlag = "H";
        }
        else if (*reviewFlag == NetMag::ReviewFlag::Automatic)
        {
            stringReviewFlag = "A";
        }
        else
        {
            spdlog::warn("Unhandled review flag");
        }
    }
    if (!stringReviewFlag.empty()){reviewFlagIndicator = soci::i_ok;}
    */
    bool doPrefMagLogic{false};
    if (updatePrefMag)
    {
        doPrefMagLogic = true;
        spdlog::info("Will insert Mw,Coda netmag and update preferred magnitude");
    }
    else
    {
        if (currentPreferredMagnitudeIdentifier == std::nullopt)
        {
            doPrefMagLogic = true;
            spdlog::warn("No current preferred magnitude - must employ pref mag logic");
        }
        else
        {
            doPrefMagLogic = false;
            spdlog::info("Will insert Mw,Coda netmag without updating preferred magnitude; current pref mag is "
                       + std::to_string (*currentPreferredMagnitudeIdentifier));
        }
    } 
    // Let the commit fun begin
    {
    soci::transaction tr(*session);
    soci::indicator nullIndicator{soci::i_null};
    int64_t magnitudeIdentifier{0};
    const double uncertainty{0};
    const double quality{0};
    constexpr int commit{0};
    std::string insertNetMagQuery{
R"'''(
SELECT epref.insertNetMag(:orid, :mag, :type, :auth, :subsource, :magalgo, :nsta, :nobs, :uncertainty, :gap, :dist, :quality, :rflag, :commit)
)'''"
    };
    double roundedMagnitude
         = std::round(networkMagnitude.getMagnitude()*100)/100.0;
    auto originIdentifier = networkMagnitude.getOriginIdentifier();
    auto magnitudeType = networkMagnitude.getMagnitudeType();
    auto authority = networkMagnitude.getAuthority();
    auto subSource = networkMagnitude.getSubSource();
    int localCommit{1};
    *session << insertNetMagQuery,
                soci::use(originIdentifier), //networkMagnitude.getOriginIdentifier()),
                soci::use(roundedMagnitude), //networkMagnitude.getMagnitude()),
                soci::use(magnitudeType), //networkMagnitude.getMagnitudeType()),
                soci::use(authority), //networkMagnitude.getAuthority()),
                soci::use(subSource), //networkMagnitude.getSubSource()),
                soci::use(magAlgo, magnitudeAlgorithmIndicator),
                soci::use(nStationsInsert, nStationsIndicator),
                soci::use(nObservationsInsert, nObservationsIndicator),
                soci::use(uncertainty, nullIndicator),
                soci::use(gapInsert, gapIndicator),
                soci::use(distanceInsert, distanceIndicator),
                soci::use(quality, nullIndicator),
                soci::use(stringReviewFlag, reviewFlagIndicator),
                soci::use(localCommit),
                soci::into(magnitudeIdentifier);
    spdlog::info("insertNetMag returned magnitude identifier: "
               + std::to_string(magnitudeIdentifier));
    // TODO I think this is called by prefMagOfEvent function
/*
    std::string setPrefMagTypeQuery{
R"'''(
SELECT epref.setprefmag_magtype(:evid, :magid, :evtpref, :bump, :commit)
)'''"
    };
    const int bypassMagPrefRules{0}; // Don't bypass magpref rules
    const int bumpEventVersion{0}; // Don't bump event version
    int status{0};
    *session << setPrefMagTypeQuery,
                soci::use(eventIdentifier),
                soci::use(magnitudeIdentifier), //networkMagnitude.getIdentifier()),
                soci::use(bypassMagPrefRules), // Don't bypass magpref rules
                soci::use(bumpEventVersion), 
                soci::use(commit),
                soci::into(status); //  Commit happens later
    spdlog::info("Status from epref.setprefmag_magtype: " + std::to_string(status));
*/
    if (doPrefMagLogic)
    {
        std::string setPrefMagOfEventQuery{
R"''''(
SELECT magpref.setPrefMagOfEvent(:evid, :commit);
)''''"
        };
        int64_t prefMagStatus{-1};
        *session << setPrefMagOfEventQuery,
                    soci::use(eventIdentifier),
                    soci::use(localCommit),
                    soci::into(prefMagStatus);
        spdlog::info("Status from setPrefMagOfEvent: "
                   + std::to_string(prefMagStatus));

        /// Make sure this is in the event pref mag
        std::string setEventPrefMag{
R"''''(
INSERT INTO eventprefmag (evid, magtype, magid) VALUES (:evid, :magtype, :magid);
)''''"
        };
        *session << setEventPrefMag,
                    soci::use(eventIdentifier),
                    soci::use(magnitudeType),
                    soci::use(magnitudeIdentifier);
    }
    else
    {
        int64_t returnedMagnitudeIdentifier;
        std::string setExistingPrefMag{
 R"""(
SELECT magpref.setPrefMag(:evid, :magid, :commit); 
)"""    
        };
        *session << setExistingPrefMag,
                    soci::use(eventIdentifier),
                    soci::use(*currentPreferredMagnitudeIdentifier),
                    soci::use(commit),
                    soci::into(returnedMagnitudeIdentifier);
        spdlog::debug("Current prefmag "
                    + std::to_string(*currentPreferredMagnitudeIdentifier)
                    + " returned prefmag "
                    + std::to_string(returnedMagnitudeIdentifier));
        if (*currentPreferredMagnitudeIdentifier !=
            std::abs(returnedMagnitudeIdentifier))
        {
            spdlog::warn("Prefmag identifier changed from "
                       + std::to_string(*currentPreferredMagnitudeIdentifier)
                       + " to "
                       + std::to_string(returnedMagnitudeIdentifier));
        }

        /// Make sure this is in the event pref mag
        std::string setEventPrefMag{
R"''''(
INSERT INTO eventprefmag (evid, magtype, magid) VALUES (:evid, :magtype, :magid);
)''''"
        };   
        *session << setEventPrefMag,
                    soci::use(eventIdentifier),
                    soci::use(magnitudeType),
                    soci::use(magnitudeIdentifier);

        std::string setBumpEventVersion{
R"''''(
SELECT epref.bump_version(:evid);
)''''"
        };  
        int newVersion{-1};
        *session << setBumpEventVersion,
                    soci::use(eventIdentifier),
                    soci::into(newVersion);
        spdlog::info("Incremented event version to "
                   + std::to_string(newVersion));
    }

    std::string creditQuery{
R"'''(
INSERT INTO credit (id, tname, refer) VALUES (:id, :tname, :refer);
)'''"
    };
    std::string netmag{"NETMAG"};
    *session << creditQuery,
                soci::use(magnitudeIdentifier),
                soci::use(netmag), //std::string  {"NETMAG"}),
                soci::use(user);
    tr.commit();
    } // End transaction
}

/// Update
void AQMSPostgresClient::updateNetworkMagnitude(
    const std::string &user,
    const std::string &eventIdentifier,
    const NetMag &networkMagnitude,
    const bool updatePrefMag)
{
    auto identifier = ::convertEventIdentifier(eventIdentifier);
    updateNetworkMagnitude(user, identifier, networkMagnitude,
                           updatePrefMag);
}
 

void AQMSPostgresClient::updateNetworkMagnitude(
    const std::string &user,
    const int64_t eventIdentifier,
    const NetMag &networkMagnitudeIn,
    const bool updatePrefMag)
{
    auto networkMagnitude = networkMagnitudeIn;
    if (!mwCodaMagnitudeExists(eventIdentifier))
    {   
        throw std::invalid_argument("Mw,Coda netmag already exists for "
                                  + std::to_string (eventIdentifier));
    }

    auto magnitudeIdentifier = networkMagnitude.getIdentifier();
    if (!networkMagnitude.haveMagnitudeType())
    {
        networkMagnitude.setMagnitudeType(MAGNITUDE_TYPE);
    }
    if (!networkMagnitude.haveSubSource())
    {
        networkMagnitude.setSubSource(MAGNITUDE_SUBSOURCE);
    }
    if (!networkMagnitude.haveMagnitude())
    {
        throw std::invalid_argument("Magnitude not set");
    }
    if (!networkMagnitude.haveAuthority())
    {
        throw std::invalid_argument("Authority not set");
    }
    auto currentPreferredMagnitudeIdentifier
        = getPreferredMagnitudeIdentifier(eventIdentifier);
    if (currentPreferredMagnitudeIdentifier == std::nullopt)
    {   
        spdlog::warn("Currently there is no preferred magnitude");
    }   

    bool doPrefMagLogic{false};
    if (updatePrefMag)
    {
        doPrefMagLogic = true;
        spdlog::info("Will update Mw,Coda netmag and update preferred magnitude");
    }
    else
    {           
        if (currentPreferredMagnitudeIdentifier == std::nullopt)
        {       
            doPrefMagLogic = true;
            spdlog::warn("No current preferred magnitude - must employ pref mag logic in update");
        }
        else    
        {
            doPrefMagLogic = false;
            spdlog::info("Will update Mw,Coda netmag without updating preferred magnitude; current pref mag is "
                       + std::to_string (*currentPreferredMagnitudeIdentifier));
        }       
    }           

    // Get values for update - just overwrite the entire row 
    auto [nStationsInsert, nStationsIndicator]
        = ::getNumberOfStations(networkMagnitude);
    auto [nObservationsInsert, nObservationsIndicator]
        = ::getNumberOfObservations(networkMagnitude);
    auto [gapInsert, gapIndicator] = ::getGap(networkMagnitude);
    auto [distanceInsert, distanceIndicator] = ::getDistance(networkMagnitude);
    auto [magAlgo, magnitudeAlgorithmIndicator]
        = ::getMagnitudeAlgorithm(networkMagnitude);
    auto [stringReviewFlag, reviewFlagIndicator]
        = ::getReviewFlag(networkMagnitude);

    // Begin the transaction
    constexpr int commit{0};
    auto session
        = reinterpret_cast<soci::session *> (pImpl->mConnection->getSession());
    {   
    soci::transaction tr(*session);

    std::string updateNetMagQuery{
R"'''(
UPDATE NetMag SET (magnitude, magtype, auth, subsource, magalgo, nsta, nobs, gap, distance, rflag, lddate) = (:magnitude, :magtype, :auth, :subsource, :magalgo, :nsta, :nobs, :gap, :distance, :rflag, NOW()) WHERE magid = :magid;
)'''"
    };
    double roundedMagnitude
         = std::round(networkMagnitude.getMagnitude()*100)/100.0;
    auto magnitudeType = networkMagnitude.getMagnitudeType();
    auto authority = networkMagnitude.getAuthority();
    auto subSource = networkMagnitude.getSubSource();
    *session << updateNetMagQuery,
                //soci::use(networkMagnitude.getOriginIdentifier()),
                soci::use(roundedMagnitude), //networkMagnitude.getMagnitude()),
                soci::use(magnitudeType), //networkMagnitude.getMagnitudeType()),
                soci::use(authority), //networkMagnitude.getAuthority()),
                soci::use(subSource), //networkMagnitude.getSubSource()),
                soci::use(magAlgo, magnitudeAlgorithmIndicator),
                soci::use(nStationsInsert, nStationsIndicator),
                soci::use(nObservationsInsert, nObservationsIndicator),
                //soci::use(uncertainty, nullIndicator),
                soci::use(gapInsert, gapIndicator),
                soci::use(distanceInsert, distanceIndicator),
                //soci::use(quality, nullIndicator),
                soci::use(stringReviewFlag, reviewFlagIndicator),
                soci::use(magnitudeIdentifier);

    // Update the preferred magnitude 
    // TODO I think this is called by prefMagOfEvent function
/*
    std::string setPrefMagTypeQuery{
R"'''(
SELECT epref.setprefmag_magtype(:evid, :magid, :evtpref, :bump, :commit)
)'''"
    };
    const int bypassMagPrefRules{0}; // Don't bypass magpref rules
    const int bumpEventVersion{0}; // Don't bump event version
    *session << setPrefMagTypeQuery,
                soci::use(eventIdentifier),
                soci::use(magnitudeIdentifier), //networkMagnitude.getIdentifier()),
                soci::use(bypassMagPrefRules), // Don't bypass magpref rules
                soci::use(bumpEventVersion),
                soci::use(commit); //  Commit happens later
*/
    if (doPrefMagLogic)
    {
        int64_t prefMagResult{0};
        int localCommit{1};
        std::string setPrefMagOfEventQuery{
R"''''(
SELECT magpref.setPrefMagOfEvent(:evid, :commit)
)''''"
    };
        *session << setPrefMagOfEventQuery,
                    soci::use(eventIdentifier),
                    soci::use(localCommit),
                    soci::into(prefMagResult);
       spdlog::info("Update magpref.setPfefMagOfEvent result is " + std::to_string(prefMagResult));
    }
    else
    {   
        int64_t returnedMagnitudeIdentifier;
        std::string setExistingPrefMag{
 R"""(
SELECT magpref.setPrefMag(:evid, :magid, :commit); 
)"""    
        };
        *session << setExistingPrefMag,
                    soci::use(eventIdentifier),
                    soci::use(*currentPreferredMagnitudeIdentifier),
                    soci::use(commit),
                    soci::into(returnedMagnitudeIdentifier);
        spdlog::info("Updated current prefmag " 
                   + std::to_string(*currentPreferredMagnitudeIdentifier)
                   + " returned prefmag "
                   + std::to_string(returnedMagnitudeIdentifier));

        // Upsert it
        std::string setEventPrefMag{
R"''''(
INSERT INTO eventprefmag (evid, magtype, magid) VALUES (:evid, :magtype, :magid) ON CONFLICT DO NOTHING;
)''''"
        };
        *session << setEventPrefMag,
                    soci::use(eventIdentifier),
                    soci::use(magnitudeType),
                    soci::use(magnitudeIdentifier);

        std::string setBumpEventVersion{
R"''''(
SELECT epref.bump_version(:evid);
)''''"
        };
        int newVersion{-1};
        *session << setBumpEventVersion,
                    soci::use(eventIdentifier),
                    soci::into(newVersion);
        spdlog::info("Updated event version to "
                   + std::to_string(newVersion));
    }   

    std::string creditQuery{
R"'''(
INSERT INTO credit (id, tname, refer) VALUES (:id, :tname, :refer);
)'''"
    };
    std::string netmag{"NETMAG"};
    *session << creditQuery,
                soci::use(magnitudeIdentifier),
                soci::use(netmag), //std::string  {"NETMAG"}),
                soci::use(user);

    tr.commit();
    }

}

/// Delete operation
void AQMSPostgresClient::deleteNetworkMagnitude(
    const std::string &user,
    const std::string &eventIdentifier)
{
    auto identifier = ::convertEventIdentifier(eventIdentifier);
    deleteNetworkMagnitude(user, identifier);
}

void AQMSPostgresClient::deleteNetworkMagnitude(
    const std::string &user,
    const int64_t eventIdentifier)
{
    auto magnitudeIdentifier = getMwCodaMagnitudeIdentifier(eventIdentifier);
    if (!magnitudeIdentifier)
    {   
        spdlog::warn("Network magnitude does not exist; skipping");
        return;
    }
    auto currentPreferredMagnitudeIdentifier
        = getPreferredMagnitudeIdentifier(eventIdentifier);
    if (currentPreferredMagnitudeIdentifier == std::nullopt)
    {
        spdlog::warn("Currently there is no preferred magnitude");
    }
    bool changePrefMag{false};
   // bool deletePrefMag{false};
    if (currentPreferredMagnitudeIdentifier)
    {
        if (*currentPreferredMagnitudeIdentifier == *magnitudeIdentifier)
        {
            changePrefMag = true;
            //deletePrefMag = true;
            spdlog::warn("Mw,Coda is currently preferred - will delete it then use prefmag logic to update");
        }
        else
        {
            spdlog::info("Mw,Coda magid "
                       + std::to_string(*magnitudeIdentifier)
                       + " is not preferred - will simply delete it");
        }
    }
    else
    {
        changePrefMag = true;
        spdlog::warn("There exists no preferred magnitude - will attempt to update");
    }

    // Begin the transaction
    spdlog::info("Attempting to delete magnitude "
               + std::to_string (*magnitudeIdentifier) + " from NetMag.");
    constexpr int commit{0};
    auto session
        = reinterpret_cast<soci::session *> (pImpl->mConnection->getSession());
    {
    soci::transaction tr(*session);

    // Delete the magnitude
    std::string deleteNetMagQuery{
R"'''(
DELETE FROM NetMag WHERE magid = :magid AND magalgo = :algorithm
)'''"
    };
    std::string magnitudeAlgorithm{MAGNITUDE_ALGORITHM};
    *session << deleteNetMagQuery,
                soci::use(*magnitudeIdentifier),
                soci::use(magnitudeAlgorithm);

    // Delete it to be sure
    std::string deleteFromEventPrefMag{
R"'''(
DELETE FROM EventPrefMag WHERE evid = :evid AND magid = :magid;
)'''"
    };
    *session << deleteFromEventPrefMag,
                soci::use(eventIdentifier),
                soci::use(*magnitudeIdentifier);

/*
    // Delete the eventprefmag
    if (deletePrefMag)
    {
        std::string deleteEventPrefMagQuery{
R"'''(
DELETE FROM eventprefmag WHERE magid = :magid;
)'''"
        };  
        *session << deleteEventPrefMagQuery,
                    soci::use(*magnitudeIdentifier);
    }
*/

    // Try to update the preferred magnitude.  This is the last
    // statement so commit it.
    if (changePrefMag)
    {
        std::string magPrefQuery{
R"'''(
SELECT magpref.setPrefMagOfEventByPrefor(:evid, 1);
)'''"
        };
        *session << magPrefQuery,
                    soci::use(eventIdentifier);
    }
    else
    {
/*
        int64_t returnedMagnitudeIdentifier;
        std::string setExistingPrefMag{
 R"""(
SELECT magpref.setPrefMag(:evid, :magid, :commit); 
)"""    
        };
        *session << setExistingPrefMag,
                    soci::use(eventIdentifier),
                    soci::use(*currentPreferredMagnitudeIdentifier),
                    soci::use(commit),
                    soci::into(returnedMagnitudeIdentifier);
        spdlog::info("Current prefmag " 
                   + std::to_string(*currentPreferredMagnitudeIdentifier)
                   + " returned prefmag "
                   + std::to_string(returnedMagnitudeIdentifier));
*/
        std::string setBumpEventVersion{
R"''''(
SELECT epref.bump_version(:evid);
)''''"
        };
        int newVersion{-1};
        *session << setBumpEventVersion,
                    soci::use(eventIdentifier),
                    soci::into(newVersion);
        spdlog::info("Incremented event version to "
                   + std::to_string(newVersion));
    }

    tr.commit();
    }

}

/// Magnitude already exists?
std::optional<int64_t> AQMSPostgresClient::getMwCodaMagnitudeIdentifier(
    const std::string &eventIdentifier) const
{
    auto identifier = ::convertEventIdentifier(eventIdentifier);
    return getMwCodaMagnitudeIdentifier(identifier);
}

std::optional<int64_t> AQMSPostgresClient::getMwCodaMagnitudeIdentifier(
    const int64_t eventIdentifier) const
{
    if (!mwCodaMagnitudeExists(eventIdentifier))
    {
        return std::nullopt;
    }
    // Query is cumbersome but ensures we always operate on prefor for event
    int64_t magnitudeIdentifier{-1};
    std::string query{
R"'''(
SELECT netmag.magid FROM event
  INNER JOIN origin ON event.prefor = origin.orid
   INNER JOIN netmag ON netmag.orid = origin.orid
WHERE event.evid = :eventIdentifier AND netmag.magtype = :magtype AND netmag.magalgo = :magalgo LIMIT 1;
)'''"
    };  
    auto session
         = reinterpret_cast<soci::session *> (pImpl->mConnection->getSession());
    try
    {
        std::string magnitudeType{MAGNITUDE_TYPE};
        std::string magnitudeAlgorithm{MAGNITUDE_ALGORITHM};
        *session << query,
                    soci::use(eventIdentifier),
                    soci::use(magnitudeType), //std::string {MAGNITUDE_TYPE}),
                    soci::use(magnitudeAlgorithm), //std::string {MAGNITUDE_ALGORITHM}),
                    soci::into(magnitudeIdentifier);
    }
    catch (const std::exception &e)
    {
        spdlog::error("Mw,Coda magnitude identifier query failed with "
                    + std::string {e.what()});
        return false;
    }
    return (magnitudeIdentifier >= 0) ? 
           std::optional<int64_t> (magnitudeIdentifier) : std::nullopt;
}

bool AQMSPostgresClient::mwCodaMagnitudeExists(
    const std::string &eventIdentifier) const
{
    auto identifier = ::convertEventIdentifier(eventIdentifier);
    return mwCodaMagnitudeExists(identifier);
}

bool AQMSPostgresClient::mwCodaMagnitudeExists(
    const int64_t eventIdentifier) const
{
    if (pImpl->mConnection == nullptr)
    {   
        throw std::runtime_error("Connection is NULL");
    }   
    if (!pImpl->mConnection->isConnected())
    {   
        spdlog::warn("Reconnecting to AQMS postgres");
        pImpl->mConnection->connect();
    }   
    if (!pImpl->mConnection->isConnected())
    {   
         spdlog::critical("AQMS postgres connection broken");
         throw std::runtime_error("AQMS database connection broken");
    }
    // Query is cumbersome but ensures we operate on prefor for event
    std::string query{
R"'''(
SELECT COUNT(*) FROM event
  INNER JOIN origin ON event.prefor = origin.orid
   INNER JOIN netmag ON netmag.orid = origin.orid
WHERE event.evid = :eventIdentifier AND netmag.magtype = :magtype AND netmag.magalgo = :magalgo;
)'''"
    };
    int count{0};
    auto session
         = reinterpret_cast<soci::session *> (pImpl->mConnection->getSession());
    try
    {
        std::string magnitudeType{MAGNITUDE_TYPE};
        std::string magnitudeAlgorithm{MAGNITUDE_ALGORITHM};
        *session << query,
                    soci::use(eventIdentifier),
                    soci::use(magnitudeType), //std::string {MAGNITUDE_TYPE}),
                    soci::use(magnitudeAlgorithm), //std::string {MAGNITUDE_ALGORITHM}),
                    soci::into(count);
    }
    catch (const std::exception &e)
    {
        spdlog::error("Mw,Coda magnitude existence query failed with " 
                    + std::string {e.what()});
        return false;
    }
    return (count >= 1) ? true : false;
}

/// Get prefor 
int64_t AQMSPostgresClient::getPreferredOriginIdentifier(
    const std::string &eventIdentifier) const
{
    auto identifier = ::convertEventIdentifier(eventIdentifier);
    return getPreferredOriginIdentifier(identifier);
  
}

int64_t AQMSPostgresClient::getPreferredOriginIdentifier(
    const int64_t eventIdentifier) const
{
    if (pImpl->mConnection == nullptr)
    {
        throw std::runtime_error("Connection is NULL");
    }
    if (!pImpl->mConnection->isConnected())
    {
        spdlog::warn("Reconnecting to AQMS postgres");
        pImpl->mConnection->connect();
    }
    if (!pImpl->mConnection->isConnected())
    {
         spdlog::critical("AQMS postgres connection broken");
         throw std::runtime_error("AQMS database connection broken");
    }

    int64_t originIdentifier{-1};
    std::string query{
R"'''(
SELECT prefor FROM event WHERE evid=:identifier LIMIT 1;
)'''"
    };
    auto session
         = reinterpret_cast<soci::session *> (pImpl->mConnection->getSession());
    try
    {
        *session << query,
                    soci::use(eventIdentifier),
                    soci::into(originIdentifier);
    }
    catch (const std::exception &e)
    {
        spdlog::error("Preferred origin query failed with "
                    + std::string {e.what()});
    }
    if (originIdentifier ==-1)
    {
        throw std::runtime_error("Failed to get preferred origin identifier");
    }
    return originIdentifier;
}

std::optional<int64_t> AQMSPostgresClient::getPreferredMagnitudeIdentifier(
    const int64_t eventIdentifier) const
{
    if (pImpl->mConnection == nullptr)
    {   
        throw std::runtime_error("Connection is NULL");
    }   
    if (!pImpl->mConnection->isConnected())
    {   
        spdlog::warn("Reconnecting to AQMS postgres");
        pImpl->mConnection->connect();
    }   
    if (!pImpl->mConnection->isConnected())
    {   
         spdlog::critical("AQMS postgres connection broken");
         throw std::runtime_error("AQMS database connection broken");
    }   

    int64_t magnitudeIdentifier{-1};
    std::string query{
R"'''(
SELECT prefmag FROM event WHERE evid=:identifier LIMIT 1;
)'''"
    };
    soci::indicator indicator;
    auto session
         = reinterpret_cast<soci::session *> (pImpl->mConnection->getSession());
    try 
    {   
        *session << query,
                    soci::use(eventIdentifier),
                    soci::into(magnitudeIdentifier, indicator);
        if (indicator == soci::i_ok)
        {
            spdlog::debug("Found preferred magnitude identifier of "
                        + std::to_string(magnitudeIdentifier)); 
        }
        else if (indicator == soci::i_null)
        {
            spdlog::warn("Preferred magnitude not found");
            magnitudeIdentifier =-1;
        }
        else
        {
            spdlog::error("Unhandled case - setting magnitude identifier to -1");
            magnitudeIdentifier =-1;
        }
    }   
    catch (const std::exception &e) 
    {
        spdlog::error("Preferred origin query failed with "
                    + std::string {e.what()});
    }
    if (magnitudeIdentifier ==-1)
    {
        return std::nullopt;
        //throw std::runtime_error("Failed to get preferred magnitude identifier");
    }
    return std::optional<int64_t> (magnitudeIdentifier);
}

/// Destructor
AQMSPostgresClient::~AQMSPostgresClient() = default;
