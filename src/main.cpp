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

struct ProgramOptions
{
    boost::asio::ip::address address{boost::asio::ip::make_address("0.0.0.0")};
    std::filesystem::path documentRoot{"./"}; 
    int nThreads{1};
    unsigned short port{80};
    bool helpOnly{false};
};

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


/*
std::shared_ptr<CCTService::AQMSPostgresService> createAQMSPostgresService()
{

}
*/

int main(int argc, char* argv[])
{
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


    const std::set<std::string> schemas{"production", "test"};

    spdlog::info("Creating CCT LDAP authenticator...");
    std::shared_ptr<CCTService::IAuthenticator> ldapAuthenticator;
    try
    {
        std::string ldapServerAddress{std::getenv("LDAP_HOST")};
        int ldapPort{std::stoi(std::getenv("LDAP_PORT"))};
        std::string ldapOrganizationUnit{std::getenv("LDAP_ORGANIZATION_UNIT")};
        std::string ldapDomainComponent{std::getenv("LDAP_DOMAIN_COMPONENT")};
        constexpr bool maintainConnection{false};
        ldapAuthenticator
            = std::make_shared<CCTService::LDAP> 
                (ldapServerAddress,
                 ldapPort,
                 ldapOrganizationUnit,
                 ldapDomainComponent,
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
        cctPostgresService = ::createCCTPostgresService(schemas);
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
            auto client = ::createAQMSPostgresClient(schema);
            aqmsClients->insert(std::move( std::pair{schema, std::move(client)}));
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

