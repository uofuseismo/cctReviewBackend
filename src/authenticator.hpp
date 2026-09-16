#ifndef CCT_BACKEND_SERVICE_AUTHENTICATOR_HPP
#define CCT_BACKEND_SERVICE_AUTHENTICATOR_HPP
#include <string>
#include <optional>
#include "permissions.hpp"
namespace CCTService
{
/// @class IAuthenticator "authenticator.hpp"
/// @brief Defines a base class for an authenticator.
/// @copyright Ben Baker (University of Utah) distributed under the MIT license.
class IAuthenticator
{
public:
    /// @brief Defines authentication credentials.
    struct Credentials
    {
        std::string user; /*!< The user name. */
        std::string token; /*!< The JSON web token. */
        Permissions permissions{Permissions::None}; /*!< The user's permissions. */
    };
public:
    /// @brief Constructor.
    IAuthenticator();
    /// @brief Destructor.
    virtual ~IAuthenticator();
    /// @brief Adds the user.
    void add(const std::string &user);
    /// @brief Removes the user.
    void remove(const std::string &user);
    /// @brief Authorizes the user based on their token.
    [[nodiscard]] Credentials authorize(const std::string &jsonWebToken) const;
    /// @brief Gets the credentials of the user.
    [[nodiscard]] std::optional<Credentials> getCredentials(const std::string &user) const;
    /// @brief Upon successful authentication this will add the user and
    ///        password pair to the list of authenticated users.
    ///        Additionally, a JSON Web Token for the user will be obtainable
    ///        fomr \c getCredentials().
    /// @result True indicates the user with provided password was authenticated.
    [[nodiscard]] virtual bool authenticate(const std::string &user,
                                            const std::string &password) = 0;
private:
    class IAuthenticatorImpl;
    std::unique_ptr<IAuthenticatorImpl> pImpl;
};
}
#endif
