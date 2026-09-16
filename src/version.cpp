#include <string>
#include "version.hpp"

using namespace CCTReview;

int Version::getMajor() noexcept
{
    return CCTReviewBackend_MAJOR;
}

int Version::getMinor() noexcept
{
    return CCTReviewBackend_MINOR;
}

int Version::getPatch() noexcept
{
    return CCTReviewBackend_PATCH;
}

//NOLINTBEGIN(bugprone-easily-swappable-parameters)
bool Version::isAtLeast(const int major, const int minor,
                        const int patch) noexcept
//NOLINTEND(bugprone-easily-swappable-parameters)
{
    if (CCTReviewBackend_MAJOR < major){return false;}
    if (CCTReviewBackend_MAJOR > major){return true;}
    if (CCTReviewBackend_MINOR < minor){return false;}
    if (CCTReviewBackend_MINOR > minor){return true;}
    if (CCTReviewBackend_PATCH < patch){return false;}
    return true;
}

std::string Version::getVersion() noexcept
{
    std::string version{CCTReviewBackend_VERSION};
    return version;
}

std::string Version::getTag() noexcept
{
    std::string tag{CCTReviewBackend_GITTAG};
    return tag;
}

std::string Version::getVersionWithTag() noexcept
{
    auto tag = Version::getTag();
    if (tag.empty())
    {
        return Version::getVersion();
    }
    else
    {
        return Version::getVersion() + "-" + tag;
    }
}
