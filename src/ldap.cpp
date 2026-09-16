#include <iostream>
#include <cstdint>
#include <limits>
#include <spdlog/spdlog.h>
#include "ldap.hpp"
extern "C"
{
#include <ldap.h>
}

// https://www.middleware.vt.edu/ed/ldap/edauth-examples.html#cc-applications


class CCTService::LDAP::LDAPImpl
{
public:
    LDAPImpl(const std::string &serverAddress,
             const int port,
             const std::string &organizationalUnitName,
             const std::string &domainComponent,
             const CCTService::LDAP::Version ldapVersion,
             const bool maintainConnection)
    {
        if (serverAddress.empty())
        {
            throw std::invalid_argument("Server address is empty");
        }
        if (port < 0 || port > std::numeric_limits<uint16_t>::max())
        {
            std::invalid_argument("Port out of range");
        }
        mServerAddress = serverAddress + ":" + std::to_string(port);
        mDNSuffix = organizationalUnitName + "," + domainComponent;
        mVersion = ldapVersion;
        mMaintainConnection = maintainConnection;
    }
    ~LDAPImpl()
    {
        unbind();
    }
    void unbind()
    {
        if (mBound)
        {
            spdlog::debug("LDAPImpl::unbind: Unbinding LDAP connection");
            auto returnCode
                = ldap_unbind_ext(mLDAP, &mServerControl, &mClientControl);
            if (returnCode != LDAP_SUCCESS)
            {
                std::string error{ldap_err2string(returnCode)};
                spdlog::critical("LDAPImpl::unbind failed with: " + error);
            }
            mBound = false;
        }
    }
    /// @brief initializing the connection.
    void initialize()
    {
        unbind();
        spdlog::debug("LDAPImpl::initialize: Initializing connection to "
                    + mServerAddress);
        if (ldap_initialize(&mLDAP, mServerAddress.c_str()) != LDAP_SUCCESS)
        {
            mBound = false;
            spdlog::critical(
                "LDAPImpl::initialize: Failed to bind to LDAP server at: "
              + mServerAddress);
            return;
        }
        // Set the version
        auto version{LDAP_VERSION3};
        if (mVersion == Version::One)
        {
            version  = LDAP_VERSION1;
        }
        else if (mVersion == Version::Two)
        {
            version = LDAP_VERSION2;
        }
        else
        {
            version = LDAP_VERSION3;
        }
        auto returnCode = ldap_set_option(mLDAP,
                                          LDAP_OPT_PROTOCOL_VERSION,
                                          &version);
        if (returnCode != LDAP_SUCCESS)
        {
            spdlog::critical("LDAPImpl::Failed to set version");
            unbind();
            return;
        }
        mBound = true;
    }

    ::LDAP *mLDAP{nullptr};
    LDAPControl *mClientControl{nullptr};
    LDAPControl *mServerControl{nullptr};
    std::string mServerAddress;
    std::string mDNSuffix;
    Version mVersion{Version::Three};
    bool mMaintainConnection{false};
    bool mBound{false};
};

/// Construtor
CCTService::LDAP::LDAP(const std::string &serverAddress,
                       const int port,
                       const std::string &organizationalUnitName,
                       const std::string &domainComponent,
                       const CCTService::LDAP::Version ldapVersion,
                       const bool maintainConnection) :
    pImpl(std::make_unique<LDAPImpl> (serverAddress,
                                      port,
                                      organizationalUnitName,
                                      domainComponent,
                                      ldapVersion,
                                      maintainConnection))
{
    pImpl->initialize();
    if (!pImpl->mMaintainConnection){pImpl->unbind();}
}
 
/// Constructor
CCTService::LDAP::LDAP(const std::string &serverAddress,
                       const int port,
                       const std::string &organizationalUnitName,
                       const std::string &domainComponent,
                       const CCTService::LDAP::Version ldapVersion,
                       const CCTService::LDAP::TLSVerifyClient verify,
                       const bool maintainConnection) :
    pImpl(std::make_unique<LDAPImpl> (serverAddress,
                                      port,
                                      organizationalUnitName,
                                      domainComponent,
                                      ldapVersion,
                                      maintainConnection))
{ 
    constexpr int overwrite{1};
    if (verify == CCTService::LDAP::TLSVerifyClient::Never)
    {
        if (setenv("LDAPTLS_REQCERT", "NEVER", overwrite) != 0)
        {
            throw std::runtime_error(
               "LDAP: Failed to update LDAPTLS_REQCERT to NEVER");
        }
    }
    else if (verify == CCTService::LDAP::TLSVerifyClient::Allow)
    {
        if (setenv("LDAPTLS_REQCERT", "ALLOW", overwrite) != 0)
        {
            throw std::runtime_error(
               "LDAP: Failed to update LDAPTLS_REQCERT to ALLOW");
        } 
    }
    else if (verify == CCTService::LDAP::TLSVerifyClient::Try)
    {
        if (setenv("LDAPTLS_REQCERT", "TRY", overwrite) != 0)
        {
            throw std::runtime_error(
               "LDAP: Failed to update LDAPTLS_REQCERT to TRY");
        }
    }
    else if (verify == CCTService::LDAP::TLSVerifyClient::Demand)
    {
        if (setenv("LDAPTLS_REQCERT", "DEMAND", overwrite) != 0)
        {
            throw std::runtime_error(
               "LDAP: Failed to update LDAPTLS_REQCERT to DEMAND");
        }
    }
    pImpl->initialize();
    if (!pImpl->mMaintainConnection){pImpl->unbind();}
}

/// @result True indicates the LDAP authenticator is initialized.
[[nodiscard]] bool CCTService::LDAP::isInitialized() const noexcept
{
    return pImpl->mBound;
}

/// @result True indicates the user is permitted
bool CCTService::LDAP::authenticate(const std::string &user,
                                    const std::string &password)
{
    if (user.empty())
    {
        throw std::invalid_argument("User name must be specified");
    }
    auto temporaryPassword{password};
    struct berval *serverCredential{nullptr};
    auto dn = "uid=" + user + "," + pImpl->mDNSuffix;

    struct berval credential;
    credential.bv_val = temporaryPassword.data();
    credential.bv_len = temporaryPassword.size();

    // Verify my connection
    if (!pImpl->mMaintainConnection)
    {
        pImpl->initialize();
    }
    if (!pImpl->mBound)
    {
        throw std::runtime_error("No LDAP connection");
    }

    // Autenticate this user
    auto returnCode
        = ldap_sasl_bind_s(pImpl->mLDAP, dn.c_str(),
                           LDAP_SASL_SIMPLE,
                           &credential, NULL, NULL, &serverCredential);
    if (returnCode != LDAP_SUCCESS)
    {
        if (returnCode != LDAP_INVALID_CREDENTIALS)
        {
            std::string error{ldap_err2string(returnCode)};
            spdlog::warn("Could not bind to SASL; failed with: " + error);
        }
        else
        {
            spdlog::info("Rejected " + user);
        }
        return false;
    }
    else
    {
        spdlog::debug("LDAP::authenticate: Validated credentials for " + user);
    }
    if (!pImpl->mMaintainConnection){pImpl->unbind();}
    // LDAP says the user is okay - now add the user
    try
    {
        this->add(user);
    }
    catch (const std::exception &e)
    {
        spdlog::critical("LDAP::authenticate: Failed to add user; failed with: "
                       + std::string {e.what()});
        return false;
    }
    spdlog::info("LDAP::authenticate: Authenticated " + user);
    return true;
}

/// Disconnect
void CCTService::LDAP::unbind()
{
    pImpl->unbind();
}

/// Destructor
CCTService::LDAP::~LDAP() = default;
 

