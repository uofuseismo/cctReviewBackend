#include <string>
#include <map>
#include <chrono>
#include <mutex>
#include <optional>
#include <spdlog/spdlog.h>
#include <jwt-cpp/jwt.h>
#include <boost/asio/ip/host_name.hpp>
#include "authenticator.hpp"
#include "exceptions.hpp"

#define JWT_TYPE "JWT"
#define SERVICE_NAME ":cct-backend"

using namespace CCTService;

namespace
{
struct Credentials
{
    std::string jsonWebToken;
    std::string user;
    int64_t issuedAt;
    int64_t expiresAt;
    Permissions permissions{Permissions::None};
};

int64_t getNow()
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>
              (now.time_since_epoch()).count();
}

}

class IAuthenticator::IAuthenticatorImpl
{
public:
    [[nodiscard]] ::Credentials createCredentials(const std::string &user)
    {
        auto now = std::chrono::system_clock::now();
        auto expirationTime
            = jwt::date(now + mExpirationDuration); 
        std::string token = jwt::create()
                                .set_type(JWT_TYPE)
                                .set_issued_at(now)
                                .set_expires_at(expirationTime)
                                .set_issuer(mIssuer)
                                .set_audience(user)
                                .sign(jwt::algorithm::hs256{mKey});
        auto nowInSeconds
            = std::chrono::duration_cast<std::chrono::seconds>
              (now.time_since_epoch()).count();
        ::Credentials result;  
        result.jsonWebToken = std::move(token);
        result.user = user;
        result.issuedAt = nowInSeconds;
        result.expiresAt = nowInSeconds + mExpirationDuration.count();
        result.permissions = Permissions::ReadWrite;
        //std::cout << result.issuedAt << " " << result.expiresAt << std::endl;
        //std::cout << result.jsonWebToken << std::endl;
        return result;
    }
    [[nodiscard]] bool verified(const std::string &token) const
    {
        auto verifier = jwt::verify()
            .with_issuer(mIssuer)
            .with_type(JWT_TYPE)
            .allow_algorithm(jwt::algorithm::hs256{mKey});
        auto decodedToken = jwt::decode(token);
        try
        {
            verifier.verify(decodedToken);
            return true;
        }
        catch (const std::exception &e)
        {
            spdlog::info(e.what());
            return false;
        }
    }
    void add(const std::string &user, const ::Credentials &credentials)
    {
        if (user.empty()){throw std::invalid_argument("User is empty");}
        if (credentials.jsonWebToken.empty())
        {
            throw std::invalid_argument("JSON web token is empty");
        }
        // Add or overwrite it
        std::lock_guard<std::mutex> lockGuard(mMutex);
        auto userIndex = mUserKeysMap.find(user);
        if (userIndex == mUserKeysMap.end())
        {
            mUserKeysMap.insert(std::pair {user, credentials});
        }
        else
        {
            userIndex->second = credentials;
        }   
    }
    void add(const std::string &user)
    {
        add(user, createCredentials(user));
    }
    std::optional<IAuthenticator::Credentials> find(const std::string &user) const
    {
        IAuthenticator::Credentials credentials;
        credentials.user = user;
        std::lock_guard<std::mutex> lockGuard(mMutex);
        auto idx = mUserKeysMap.find(user);
        if (idx != mUserKeysMap.end())
        {
            credentials.permissions = idx->second.permissions;
            return std::optional<IAuthenticator::Credentials> {credentials};
        }
        return std::nullopt; 
    }
    [[nodiscard]] std::optional<IAuthenticator::Credentials>
        getCredentials(const std::string &user) const noexcept
    {
        IAuthenticator::Credentials result;
        result.user = user;
        std::lock_guard<std::mutex> lockGuard(mMutex);
        auto idx = mUserKeysMap.find(user);
        if (idx != mUserKeysMap.end())
        {
            result.token = idx->second.jsonWebToken;
            result.permissions = idx->second.permissions;
            return std::optional<IAuthenticator::Credentials> {result};
        }
        return std::nullopt;
    }
    mutable std::mutex mMutex;
    std::map<std::string, ::Credentials> mUserKeysMap;
    std::string mIssuer{boost::asio::ip::host_name() + SERVICE_NAME};
    std::string mKey{boost::asio::ip::host_name()
                     + ":"
                     + std::to_string(getNow())
                    };
    std::chrono::seconds mExpirationDuration{86400};
};

/// Constructor
IAuthenticator::IAuthenticator() :
    pImpl(std::make_unique<IAuthenticatorImpl> ())
{
}

void IAuthenticator::add(const std::string &user)
{
    if (user.empty()){throw std::invalid_argument("User is empty");}
    pImpl->add(user); 
}

void IAuthenticator::remove(const std::string &user)
{
    if (pImpl->mUserKeysMap.contains(user))
    {
        pImpl->mUserKeysMap.erase(user);
    } 
}

IAuthenticator::Credentials IAuthenticator::authorize(
    const std::string &jsonWebToken) const
{
    // First thing - we verify the token is legit and lift the user
    std::string user;
    try
    {
        auto decodedToken = jwt::decode(jsonWebToken);
        if (!decodedToken.has_audience())
        {
            throw std::invalid_argument("User not found in token");
        }
        if (!decodedToken.has_expires_at())
        {
            throw std::invalid_argument("Key expiration not set");
        }
        auto verifier = jwt::verify()
                           .with_type(JWT_TYPE)
                           .with_issuer(pImpl->mIssuer)
                           .allow_algorithm(jwt::algorithm::hs256{pImpl->mKey})
                           .leeway(1U);
        verifier.verify(decodedToken);
        auto audience = decodedToken.get_audience();
        if (audience.size() != 1)
        {
            spdlog::critical("Audience has wrong size");
        }
        user = *audience.begin(); //std::string (*audience.begin());
    }
    catch (const jwt::error::token_verification_exception &e)
    {
        spdlog::debug("Token cannot be verified because "
                    + std::string {e.what()});
        throw std::invalid_argument("Token cannot be verified because "
                                  + std::string {e.what()});
    }
    catch (const std::exception &e) 
    {
        spdlog::debug("Token malformed because " + std::string {e.what()});
        throw std::invalid_argument("Malformed token");
    }
    if (user.empty()){throw std::invalid_argument("User name is empty");}
 
    // Okay the token is legit and we have a user -> are they good to go?
    auto credentials = pImpl->find(user);
    if (credentials)
    {
        spdlog::info("Authorized " + user);
        return *credentials;
    }
    else
    {
        throw InvalidPermissionException("Could not authorize " + user);
    }
}

/*
bool IAuthenticator::isValidated(const std::string &jwtToken) const
{
    auto decodedToken = jwt::decode(jwtToken);
    auto user = decodedToken.get_audience();
    // Verify the token
    auto verifier = jwt::verify()
                        .with_type(JWT_TYPE)
                        .with_issuer(pImpl->mIssuer)
                        .allow_algorithm(jwt::algorithm::hs256{pImpl->mKey});
    verifier.verify(decodedToken);
    return true;
}
*/

std::optional<IAuthenticator::Credentials>
IAuthenticator::getCredentials(const std::string &user) const
{
    return pImpl->getCredentials(user);
}

/// Destructor
IAuthenticator::~IAuthenticator() = default;
