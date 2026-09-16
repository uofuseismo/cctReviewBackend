#ifndef AQMS_HPP
#define AQMS_HPP
#include <string>
#include <optional>

#define MAGNITUDE_TYPE "w"
#define AUTHORITY "UU"
#define MAGNITUDE_SUBSOURCE "cct"
#define MAGNITUDE_ALGORITHM "LLNLCCT"

/// Do not commit in epref.insertNetMag; will happen later
#define EPREF_INSERTNETMAG_COMMIT 0

/// Do not bypass magpref rules on epref.setprefmag_magtype
#define EPREF_SETPREFMAG_MAGTYPE_BYPASS_MAGPREF_RULES 0 
/// Do not bump the event version in epref.setprefmag_magtype
#define EPREF_SETPREFMAG_MAGTYPE_BUMP_EVENT_VERSION 0
/// Do not commit in epref.setprefmag_magtype; will happen later
#define EPREF_SETPREFMAG_MAGTYPE_COMMIT 0

/// Do not commit in magpref.setPrefMagOfEvent
#define MAGPREF_SETPREFMAGOFEVENT_COMMIT 0 

namespace CCTService
{

class NetMag
{
public:
    enum class ReviewFlag
    {
        Automatic,
        Human
    };
public:
    NetMag() = default;
    NetMag(const NetMag &netmag) = default;
    NetMag(NetMag &&netmag) noexcept = default;
    NetMag& operator=(const NetMag &netmag) = default;
    NetMag& operator=(NetMag &&netmag) noexcept = default;

    ~NetMag() = default;

    void setIdentifier(const int64_t identifier) noexcept
    {
        mIdentifier = identifier; 
        mHaveIdentifier = true; 
    }
    [[nodiscard]] int64_t getIdentifier() const
    {
        if (!haveIdentifier()){throw std::runtime_error("Identifier not set");}
        return mIdentifier;
    }
    [[nodiscard]] bool haveIdentifier() const noexcept
    {
        return mHaveIdentifier;
    }

    void setOriginIdentifier(const int64_t identifier) noexcept
    {
        mOriginIdentifier = identifier; 
        mHaveOriginIdentifier = true; 
    }
    [[nodiscard]] int64_t getOriginIdentifier() const
    {
        if (!haveOriginIdentifier())
        {
            throw std::runtime_error("Origin identifier not set");
        }
        return mOriginIdentifier;
    }
    [[nodiscard]] bool haveOriginIdentifier() const noexcept
    {
        return mHaveOriginIdentifier;
    }

    void setMagnitude(double magnitude)
    {
        if (magnitude >= 10 || magnitude <=-10)
        {
            throw std::invalid_argument("Magnitude must be in range [-10,10]");
        }
        mMagnitude = magnitude;
        mHaveMagnitude = true;
    }
    [[nodiscard]] double getMagnitude() const
    {
        if (!haveMagnitude()){throw std::runtime_error("Magnitude not set");}
        return mMagnitude;
    }
    [[nodiscard]] bool haveMagnitude() const noexcept
    {
        return mHaveMagnitude;
    }

    void setMagnitudeType(const std::string &type)
    {
        if (type.empty()){throw std::invalid_argument("Magnitude is empty");}
        mMagnitudeType = type;
    }
    [[nodiscard]] std::string getMagnitudeType() const
    {
        if (!haveMagnitudeType())
        {
            throw std::runtime_error("Magnitude type not set");
        } 
        return mMagnitudeType;
    }
    [[nodiscard]] bool haveMagnitudeType() const noexcept
    {
        return !mMagnitudeType.empty();
    }

    void setAuthority(const std::string &authority)
    {
        if (authority.empty())
        {
            throw std::invalid_argument("Authority is empty");
        }
        mAuthority = authority;
    }
    [[nodiscard]] std::string getAuthority() const
    {
        if (!haveAuthority()){throw std::runtime_error("Authority not set");}
        return mAuthority;
    }
    [[nodiscard]] bool haveAuthority() const noexcept
    {
        return !mAuthority.empty();
    }

    void setSubSource(const std::string &subsource)
    {   
        if (subsource.empty())
        {
            throw std::invalid_argument("SubSource is empty");
        }
        mSubSource = subsource;
    }   
    [[nodiscard]] std::string getSubSource() const
    {   
        if (!haveSubSource())
        {
            throw std::runtime_error("SubSource not set");
        }
        return mSubSource;
    }   
    [[nodiscard]] bool haveSubSource() const noexcept
    {   
        return !mSubSource.empty();
    }

    void setReviewFlag(ReviewFlag reviewFlag) noexcept
    {
        mReviewFlag = reviewFlag;
    }
    [[nodiscard]] std::optional<ReviewFlag> getReviewFlag() const noexcept
    {
        return std::optional<ReviewFlag> (mReviewFlag);
    }

    void setNumberOfStations(int nStations)
    {
        if (nStations < 0)
        {
            throw std::invalid_argument("Number of stations must be non-negative");
        } 
        mStationCount = nStations;
    }
    [[nodiscard]] std::optional<int> getNumberOfStations() const noexcept
    {
        return (mStationCount >= 0) ? std::optional<int> (mStationCount) : std::nullopt;
    }

    void setNumberOfObservations(int nObservations)
    {
        if (nObservations < 0)
        {
            throw std::invalid_argument("Number of observations must be non-negative");
        }
        mObservationCount = nObservations;
    }
    [[nodiscard]] std::optional<int> getNumberOfObservations() const noexcept
    {
        return (mObservationCount >= 0) ?
                std::optional<int> (mObservationCount) : std::nullopt;
    }

    void setGap(double gap)
    {
        if (gap < 0 || gap >= 360)
        {
            throw std::invalid_argument("Gap must be in range [0,360)");
        }
        mGap = gap;
    }
    [[nodiscard]] std::optional<double> getGap() const noexcept
    {
        return (mGap >= 0) ? std::optional<double> (mGap) : std::nullopt;
    }

    void setDistance(double distance)
    {
        if (distance < 0)
        {
            throw std::invalid_argument("Distance must be non-negative");
        }
        mDistance = distance;
    }
    [[nodiscard]] std::optional<double> getDistance() const noexcept
    {
        return (mDistance >= 0) ?
               std::optional<double> (mDistance) : std::nullopt;
    }
   
    void setMagnitudeAlgorithm(const std::string &algorithm)
    {
        mMagnitudeAlgorithm = algorithm;
    }
    [[nodiscard]] std::optional<std::string> getMagnitudeAlgorithm() const noexcept
    {
        return !mMagnitudeAlgorithm.empty() ?
               std::optional<std::string> (mMagnitudeAlgorithm) :
               std::nullopt;
    }
private:
    std::string mSubSource{MAGNITUDE_SUBSOURCE}; 
    std::string mAuthority{AUTHORITY};
    std::string mMagnitudeType{MAGNITUDE_TYPE};
    std::string mMagnitudeAlgorithm{MAGNITUDE_ALGORITHM};
    int64_t mIdentifier{0};
    int64_t mOriginIdentifier{0};
    double mMagnitude{0};
    double mGap{-1};
    double mDistance{-1};
    int mStationCount{-1};
    int mObservationCount{-1};
    ReviewFlag mReviewFlag{ReviewFlag::Human};
    bool mHaveIdentifier{false};
    bool mHaveOriginIdentifier{false};
    bool mHaveMagnitude{false};
};
}
#endif
