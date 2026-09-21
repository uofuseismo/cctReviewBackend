#include <iostream>
#include <set>
#include <map>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <nlohmann/json.hpp>
#include "listener.hpp"
#include "ldap.hpp"
#include "callback.hpp"
#include "aqmsPostgresClient.hpp"
#include "cctPostgresService.hpp"
#include "postgresql.hpp"
#include "version.hpp"
#include "secretFile.hpp"

#define APPLICATION_NAME "cctReviewBackend"

struct ProgramOptions
{
    std::string applicationName{APPLICATION_NAME};
    int verbosity{3};

    boost::asio::ip::address address{boost::asio::ip::make_address("127.0.0.1")};
    std::filesystem::path documentRoot{"./"}; 
    int nThreads{1};
    unsigned short port{8000};
    bool helpOnly{false};

    std::string aqmsTestReadWriteUser;
    std::string aqmsTestReadWritePassword;
    std::string aqmsTestDatabaseName;
    std::string aqmsTestHost;
    uint16_t aqmsTestPort{5432};

    std::string aqmsProductionReadWriteUser;
    std::string aqmsProductionReadWritePassword;
    std::string aqmsProductionDatabaseName;
    std::string aqmsProductionHost;
    uint16_t aqmsProductionPort{5432};

    std::string cctReadWriteUser;
    std::string cctReadWritePassword;
    std::string cctDatabaseName;
    std::string cctHost;
    uint16_t cctPort{5432};


    std::string ldapHost;
    uint16_t ldapPort{636};
    std::string ldapOrganizationalUnit;
    std::string ldapDomainComponent;

    static ProgramOptions parseIniFile(const std::filesystem::path &iniFile)
    {   
        if (!std::filesystem::exists(iniFile))
        {
            throw std::invalid_argument(std::string{iniFile}
                                      + " does not exist");
        }
        ProgramOptions options;

        // Parse the initialization file
        boost::property_tree::ptree propertyTree;
        boost::property_tree::ini_parser::read_ini(iniFile, propertyTree);

        options.applicationName
            = propertyTree.get<std::string> ("General.applicationName",
                                             options.applicationName);
        if (options.applicationName.empty())
        {
            options.applicationName = APPLICATION_NAME;
        }
        options.verbosity
            = propertyTree.get<int> ("General.verbosity", options.verbosity);

        auto stringAddress
            = propertyTree.get<std::string> ("Beast.address", "127.0.0.1");
        if (stringAddress.empty())
        {
            throw std::invalid_argument("Beast address not set");
        }
        options.address = boost::asio::ip::make_address(stringAddress);

        options.port = propertyTree.get<uint16_t> ("Beast.port", options.port);
        if (options.port == 0)
        {
            throw std::invalid_argument("Port cannot be 0");
        }
    
        options.nThreads
            = propertyTree.get<int> ("Beast.numberOfThreads", options.nThreads);
        if (options.nThreads < 1)
        {
            throw std::invalid_argument("Number of threads must be positive");
        }

        // A setting that must be given, either inline or in a file.  An
        // empty value is the same as an absent one: an empty password is
        // never what was meant, and it would otherwise surface as a
        // connection failure rather than a configuration error.
        auto requireSecret
            = [&propertyTree](const std::string &inlineKey,
                              const std::string &fileKey) -> std::string
        {
            auto value = ::resolveSecret(propertyTree, inlineKey, fileKey);
            if (!value || value->empty())
            {
                throw std::invalid_argument("Set " + inlineKey + " or "
                                          + fileKey);
            }
            return *value;
        };
        // A setting that must be given inline - these are not secrets.
        auto requireString
            = [&propertyTree](const std::string &key) -> std::string
        {
            auto value = propertyTree.get_optional<std::string> (key);
            if (!value || value->empty())
            {
                throw std::invalid_argument("Set " + key);
            }
            return *value;
        };
        // A port must be non-zero; 0 asks the OS to pick one, which for a
        // service we connect to is never right.
        auto getPort
            = [&propertyTree](const std::string &key,
                              const uint16_t defaultPort) -> uint16_t
        {   
            auto port = propertyTree.get<uint16_t> (key, defaultPort);
            if (port == 0){throw std::invalid_argument(key + " cannot be 0");}
            return port;
        };


        options.ldapHost = requireSecret("LDAP.host", "LDAP.hostFile");
        options.ldapPort = getPort("LDAP.port", options.ldapPort);
        options.ldapOrganizationalUnit
            = requireString("LDAP.organizationalUnit");
        options.ldapDomainComponent
            = requireString("LDAP.domainComponent");
 

        options.aqmsTestReadWriteUser
            = requireSecret("AQMSTest.readWriteUser",
                            "AQMSTest.readWriteUserFile");
        options.aqmsTestReadWritePassword
            = requireSecret("AQMSTest.readWritePassword",
                            "AQMSTest.readWritePasswordFile");
        options.aqmsTestDatabaseName
            = requireSecret("AQMSTest.databaseName",
                            "AQMSTest.databaseNameFile");
        options.aqmsTestHost
            = requireSecret("AQMSTest.host",
                            "AQMSTest.hostFile");
        options.aqmsTestPort
            = getPort("AQMSTest.port",
                      options.aqmsTestPort);

        options.aqmsProductionReadWriteUser
            = requireSecret("AQMSProduction.readWriteUser",
                            "AQMSProduction.readWriteUserFile");
        options.aqmsProductionReadWritePassword
            = requireSecret("AQMSProduction.readWritePassword",
                            "AQMSProduction.readWritePasswordFile");
        options.aqmsProductionDatabaseName
            = requireSecret("AQMSProduction.databaseName",
                            "AQMSProduction.databaseNameFile");
        options.aqmsProductionHost
            = requireSecret("AQMSProduction.host",
                            "AQMSProduction.hostFile");
        options.aqmsProductionPort 
            = getPort("AQMSProduction.port",
                      options.aqmsProductionPort);

        options.cctReadWriteUser
            = requireSecret("CCTDB.readWriteUser",
                            "CCTDB.readWriteUserFile");
        options.cctReadWritePassword
            = requireSecret("CCTDB.readWritePassword",
                            "CCTDB.readWritePasswordFile");
        options.cctDatabaseName
            = requireSecret("CCTDB.databaseName",
                            "CCTDB.databaseNameFile");
        options.cctHost
            = requireSecret("CCTDB.host",
                            "CCTDB.hostFile");
        options.cctPort 
            = getPort("CCTDB.port",
                      options.cctPort);

        return options;
   }
};

/*
/// @brief Parses the command line options.
[[nodiscard]] ::ProgramOptions parseCommandLineOptions(int argc, char *argv[])
{
    ::ProgramOptions result;
    boost::program_options::options_description desc(
R"""(
The cctReviewService is the API for the CCT review frontend.
Example usage:
    cctReviewService --address=127.0.0.1 --port=8080 --document_root=./ --n_threads=1
Allowed options)""");
    desc.add_options()
        ("help",    "Produces this help message")
        ("address", boost::program_options::value<std::string> ()->default_value("0.0.0.0"),
                    "The address at which to bind")
        ("port",    boost::program_options::value<uint16_t> ()->default_value(80),
                    "The port on which to bind")
        ("document_root", boost::program_options::value<std::string> ()->default_value("./"),
                    "The document root in case files are served")
        ("n_threads", boost::program_options::value<int> ()->default_value(1),
                     "The number of threads");
    boost::program_options::variables_map vm; 
    boost::program_options::store(
        boost::program_options::parse_command_line(argc, argv, desc), vm); 
    boost::program_options::notify(vm);
    if (vm.count("help"))
    {   
        std::cout << desc << std::endl;
        result.helpOnly = true;
        return result;
    }   
    if (vm.count("address"))
    {   
        auto address = vm["address"].as<std::string>();
        if (address.empty()){throw std::invalid_argument("Address is empty");}
        result.address = boost::asio::ip::make_address(address); 
    }   
    if (vm.count("port"))
    {   
        auto port = vm["port"].as<uint16_t> ();
        result.port = port;
    }
    if (vm.count("document_root"))
    {
        auto documentRoot = vm["document_root"].as<std::string>();
        if (documentRoot.empty()){documentRoot = "./";}
        if (!std::filesystem::exists(documentRoot))
        {
            throw std::runtime_error("Document root: " + documentRoot
                                   + " does not exist");
        }
        result.documentRoot = documentRoot;
    }
    if (vm.count("n_threads"))
    {
        auto nThreads = vm["n_threads"].as<int> ();
        if (nThreads < 1){throw std::invalid_argument("Number of threads must be positive");}
        result.nThreads = nThreads;
    }
    return result;
}
*/

/// @brief Parses the command line options.
[[nodiscard]] 
std::pair<std::string, bool> parseCommandLineOptions(int argc, char *argv[])
{
    std::string iniFile;
    boost::program_options::options_description desc(
R"""(
The cctReviewService is the API for the CCT Review frontend.
Example usage:
    cctReviewService --ini=/path/to/config.ini
Allowed options)""");
    desc.add_options()
        ("help", "Produces this help message")
        ("ini",  boost::program_options::value<std::string> (), 
                 "The initialization file for this executable");
    boost::program_options::variables_map vm; 
    boost::program_options::store(
        boost::program_options::parse_command_line(argc, argv, desc), vm); 
    boost::program_options::notify(vm);
    if (vm.count("help"))
    {    
        std::cout << desc << std::endl;
        return {iniFile, true};
    }   
    if (vm.count("ini"))
    {    
        iniFile = vm["ini"].as<std::string>();
        if (!std::filesystem::exists(iniFile))
        {
            throw std::runtime_error("Initialization file: " + iniFile
                                   + " does not exist");
        }
    }    
    return {iniFile, false};
}

std::shared_ptr<CCTService::CCTPostgresService> createCCTPostgresService(
    const ProgramOptions &options,
    const std::set<std::string> &schemas)
{
    if (schemas.empty()){throw std::runtime_error("No schemas!");}
    // Create pg connection
    auto connection = std::make_unique<CCTService::PostgreSQL> (); 
    connection->setUser(options.cctReadWriteUser);
    connection->setPassword(options.cctReadWritePassword);
    connection->setDatabaseName(options.cctDatabaseName);
    connection->setAddress(options.cctHost);
    connection->setPort(options.cctPort);
    connection->connect();
    if (!connection->isConnected())
    {
        throw std::runtime_error("Could not create CCT connection");
    }   
    // Create the service
    auto service
        = std::make_shared<CCTService::CCTPostgresService>
          (std::move(connection), schemas);
    service->start();
    if (!service->isRunning())
    {
        throw std::runtime_error("Could not start service");
    }   
    return service;
}

/*
std::shared_ptr<CCTService::CCTPostgresService> createCCTPostgresService(
    const std::set<std::string> &schemas)
{
    if (schemas.empty()){throw std::runtime_error("No schemas!");}
    // Create pg connection
    auto connection = std::make_unique<CCTService::PostgreSQL> ();
    connection->setUser(std::getenv("CCT_READ_WRITE_USER"));
    connection->setPassword(std::getenv("CCT_READ_WRITE_PASSWORD"));
    connection->setDatabaseName(std::getenv("CCT_DATABASE_NAME"));
    connection->setAddress(std::getenv("CCT_DATABASE_ADDRESS"));
    connection->setPort(std::stoi(std::getenv("CCT_DATABASE_PORT")));
    connection->connect();
    if (!connection->isConnected())
    {   
        throw std::runtime_error("Could not create CCT connection");
    } 
    // Create the service
    auto service
        = std::make_shared<CCTService::CCTPostgresService>
          (std::move(connection), schemas);
    service->start();
    if (!service->isRunning())
    {
        throw std::runtime_error("Could not start service");
    } 
    return service;
}
*/

std::unique_ptr<CCTService::AQMSPostgresClient> createAQMSPostgresClient(
    const ProgramOptions &options,
    const std::string &schema)
{
    // Create pg connection
    auto connection = std::make_unique<CCTService::PostgreSQL> ();
    if (schema == "production")
    {
        connection->setUser(options.aqmsProductionReadWriteUser);
        connection->setPassword(options.aqmsProductionReadWritePassword);
        connection->setDatabaseName(options.aqmsProductionDatabaseName);
        connection->setAddress(options.aqmsProductionHost);
        connection->setPort(options.aqmsProductionPort);
    }
    else if (schema == "test")
    {
        connection->setUser(options.aqmsTestReadWriteUser);
        connection->setPassword(options.aqmsTestReadWritePassword);
        connection->setDatabaseName(options.aqmsTestDatabaseName);
        connection->setAddress(options.aqmsTestHost);
        connection->setPort(options.aqmsTestPort);
    }
    else
    {
        throw std::invalid_argument("Unhandled schema");
    }
    connection->connect();
    if (!connection->isConnected())
    {
        throw std::runtime_error("Could not create CCT connection for schema "
                               + schema);
    }
    // Create the service
    auto client
        = std::make_unique<CCTService::AQMSPostgresClient>
          (std::move(connection));
    return client;
}

/*
std::unique_ptr<CCTService::AQMSPostgresClient> createAQMSPostgresClient(
    const std::string &schema)
{
    // Create pg connection
    auto connection = std::make_unique<CCTService::PostgreSQL> (); 
    if (schema == "production")
    {
        connection->setUser(std::getenv("CCT_PRODUCTION_AQMS_READ_WRITE_USER"));
        connection->setPassword(std::getenv("CCT_PRODUCTION_AQMS_READ_WRITE_PASSWORD"));
        connection->setDatabaseName(std::getenv("CCT_PRODUCTION_AQMS_DATABASE_NAME"));
        connection->setAddress(std::getenv("CCT_PRODUCTION_AQMS_DATABASE_ADDRESS"));
        connection->setPort(std::stoi(std::getenv("CCT_PRODUCTION_AQMS_DATABASE_PORT")));
    }
    else if (schema == "test")
    {
        connection->setUser(std::getenv("CCT_TEST_AQMS_READ_WRITE_USER"));
        connection->setPassword(std::getenv("CCT_TEST_AQMS_READ_WRITE_PASSWORD"));
        connection->setDatabaseName(std::getenv("CCT_TEST_AQMS_DATABASE_NAME"));
        connection->setAddress(std::getenv("CCT_TEST_AQMS_DATABASE_ADDRESS"));
        connection->setPort(std::stoi(std::getenv("CCT_TEST_AQMS_DATABASE_PORT")));
    }
    else
    {
        throw std::invalid_argument("Unhandled schema");
    }
    connection->connect();
    if (!connection->isConnected())
    {   
        throw std::runtime_error("Could not create CCT connection");
    }   
    // Create the service
    auto client
        = std::make_unique<CCTService::AQMSPostgresClient>
          (std::move(connection));
    return client;
}
*/


/*
std::shared_ptr<CCTService::AQMSPostgresService> createAQMSPostgresService()
{

}
*/

int main(int argc, char* argv[])
{
    spdlog::info("Launching mlReviewBackend version "
               + CCTReview::Version::getVersionWithTag());

    std::filesystem::path iniFile;
    try
    {   
        auto [iniFileName, isHelp] = ::parseCommandLineOptions(argc, argv);
        if (isHelp){return EXIT_SUCCESS;}
        if (iniFileName.empty())
        {   
            throw std::runtime_error("No initialization file specified");
        }   
        iniFile = iniFileName;
    }
    catch (const std::exception &e)
    {
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }
    
    ::ProgramOptions programOptions;
    try
    {
        programOptions = ::ProgramOptions::parseIniFile(iniFile);
    }
    catch (const std::exception &e)
    {
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }

/*
    ::ProgramOptions programOptions;
    try
    {
        programOptions = parseCommandLineOptions(argc, argv);
        if (programOptions.helpOnly){return EXIT_SUCCESS;}
    }
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
        return EXIT_FAILURE;
    }
*/

    const std::set<std::string> schemas{"production", "test"};

    spdlog::info("Creating CCT LDAP authenticator...");
    std::shared_ptr<CCTService::IAuthenticator> ldapAuthenticator;
    try
    {
/*
        std::string ldapServerAddress{std::getenv("LDAP_HOST")};
        int ldapPort{std::stoi(std::getenv("LDAP_PORT"))};
        std::string ldapOrganizationUnit{std::getenv("LDAP_ORGANIZATION_UNIT")};
        std::string ldapDomainComponent{std::getenv("LDAP_DOMAIN_COMPONENT")};
*/
std::cout << programOptions.ldapOrganizationalUnit << std::endl;
std::cout << programOptions.ldapDomainComponent << std::endl;
        constexpr bool maintainConnection{false};
        ldapAuthenticator
            = std::make_shared<CCTService::LDAP> 
                (programOptions.ldapHost, //ServerAddress,
                 programOptions.ldapPort,
                 programOptions.ldapOrganizationalUnit,
                 programOptions.ldapDomainComponent,
                 CCTService::LDAP::Version::Three,
                 CCTService::LDAP::TLSVerifyClient::Allow,
                 maintainConnection);
        //ldapAuthenticator->authenticate("user", "password");
        //return 0;
    }
    catch (const std::exception &e)
    {
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }

    spdlog::info("Creating CCT database poller and service...");
    std::shared_ptr<CCTService::CCTPostgresService> cctPostgresService{nullptr};
    try
    {
        //cctPostgresService = ::createCCTPostgresService(schemas);
        cctPostgresService = ::createCCTPostgresService(programOptions, schemas);
    }
    catch (const std::exception &e)
    {
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }

    spdlog::info("Creating AQMS database clients...");
    auto aqmsClients 
        = std::make_shared<
             std::map<std::string, std::unique_ptr<CCTService::AQMSPostgresClient>>
          > ();
    for (const auto &schema : schemas)
    {
        try
        {
            //auto client = ::createAQMSPostgresClient(schema);
            auto client = ::createAQMSPostgresClient(programOptions, schema);
            aqmsClients->insert(
                std::move( std::pair{schema, std::move(client)}) );
        }
        catch (const std::exception &e)
        {
            spdlog::critical("Failed to create AQMS postgres client for schema "
                           + schema + ".  Failed with "
                           + std::string {e.what()});
            return EXIT_FAILURE;
        }
    }

    // Start making the Beast webserver
    const auto documentRoot
        = std::make_shared<std::string> (programOptions.documentRoot);

    // The IO context is required for all I/O
    boost::asio::io_context ioContext{programOptions.nThreads};
    // The SSL context is required, and holds certificates
    boost::asio::ssl::context context{boost::asio::ssl::context::tlsv12};


    CCTService::Callback callback{cctPostgresService,
                                  aqmsClients,
                                  ldapAuthenticator};

    // The io_context is required for all I/O
    //boost::asio::io_context ioContext{threads};

    // The SSL context is required, and holds certificates
    //boost::asio::ssl::context context{boost::asio::ssl::context::tlsv12};

    // This holds the self-signed certificate used by the server
    //::loadServerCertificate(context);

    // Create and launch a listening port
    spdlog::info("Launching HTTP listeners...");
    std::make_shared<CCTService::Listener>(
        ioContext,
        context,
        boost::asio::ip::tcp::endpoint{programOptions.address, programOptions.port},
        documentRoot,
        callback.getCallbackFunction())->run();

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> instances;
    instances.reserve(programOptions.nThreads - 1); 
    for (int i = programOptions.nThreads - 1; i > 0; --i)
    {
        instances.emplace_back([&ioContext]
                               {
                                   ioContext.run();
                               });
    }
    ioContext.run();
    return EXIT_SUCCESS;
}

