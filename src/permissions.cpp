#include <string>
#include <stdexcept>
#include "permissions.hpp"

using namespace CCTService;

std::string CCTService::permissionsToString(const Permissions permissions)
{
    if (permissions == Permissions::None)
    {   
        return "none";
    }   
    else if (permissions == Permissions::ReadOnly)
    {   
        return "read-only";
    }   
    else if (permissions == Permissions::ReadWrite)
    {   
        return "read-write";
    }   
    else
    {   
        throw std::runtime_error("Unhandled permissions");
    }   
}

