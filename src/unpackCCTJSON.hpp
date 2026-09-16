#ifndef UNPACK_CCT_JSON_HPP
#define UNPACK_CCT_JSON_HPP
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <cmath>
namespace 
{

/// @brief This is a convenience function to unpack the JSON data
///        stored in the Postgres CCT database and provide what is
///        necessary to the frontend.
/// @param[in] json   The JSON data to unpack.
/// @param[in] eventIdentifier  The event identifier.
/// @result A light-weight representation of the JSON data that
///         can be used by the frontned.
[[nodiscard]]
nlohmann::json unpackCCTJSON(const nlohmann::json &json,
                             const std::string &eventIdentifier)
{
    constexpr double tol{1.e-5};
    nlohmann::json outputObject;
    nlohmann::json eventObject;
    // First let's set some basics
    outputObject.emplace("eventIdentifier", eventIdentifier);
    const auto &measuredMwDetails = json["measuredMwDetails"][eventIdentifier];
    auto originTime
        = measuredMwDetails["datetime"].template get<std::string> ();
    if (originTime.back() == 'Z'){originTime.pop_back();}
    auto latitude
        = measuredMwDetails["latitude"].template get<double> (); 
    auto longitude
        = measuredMwDetails["longitude"].template get<double> ();
    auto depth
        = measuredMwDetails["depth"].template get<double> ();
    auto likelyPoorlyConstrained
        = measuredMwDetails["likelyPoorlyConstrained"].template get<bool> ();
    outputObject.emplace("originTime", originTime);
    outputObject.emplace("latitude", latitude);
    outputObject.emplace("longitude", longitude);
    outputObject.emplace("depth", depth);
    outputObject.emplace("likelyPoorlyConstrained", likelyPoorlyConstrained);
    // Now, we want to get the spectra b/c we can compute residuals from this
    const auto &fitSpectra = json["fitSpectra"][eventIdentifier];
    std::vector<double> fitFrequencies;
    std::vector<double> fitValues;
    bool haveUQ1{false};
    bool haveUQ2{false};
    nlohmann::json spectralObject;
    for (const auto &type : fitSpectra)
    {
        if (!type.contains("type")){continue;}
        if (type["type"] != "FIT" &&
            type["type"] != "UQ1" &&
            type["type"] != "UQ2")
        {
            continue;
        }
        bool isFit = type["type"] == "FIT";
        auto frequencies = nlohmann::json::array ();
        auto values = nlohmann::json::array ();
        for (const auto &item : type["spectraXY"])
        {
            auto frequency = std::pow(10, item["x"].template get<double> ());
            auto value = item["y"].template get<double> ();
            frequencies.push_back(frequency);
            values.push_back(value);
            if (isFit)
            {
                fitFrequencies.push_back(frequency);
                fitValues.push_back(value);
            }
        }
        nlohmann::json spectralXY;
        spectralXY.emplace("frequencies", std::move(frequencies));
        spectralXY.emplace("values", std::move(values));
        if (type["type"] == "FIT")
        {
            spectralObject.emplace("fit", std::move(spectralXY));
        }
        // I think uq1 is 1 std then we get a lower and upper
        else if (type["type"] == "UQ1")
        {
            if (haveUQ1)
            {
                spectralObject.emplace("bruneLowerBound-1", std::move(spectralXY));
            }
            else
            {
                spectralObject.emplace("bruneUpperBound-1", std::move(spectralXY));
            }
            haveUQ1 = true;
        }
        // I think uq2 is 2 std then we get a lower and upper
        else if (type["type"] == "UQ2")
        {
            if (haveUQ2)
            {
                spectralObject.emplace("bruneLowerBound-2", std::move(spectralXY));
            }
            else
            {
                spectralObject.emplace("bruneUpperBound-2", std::move(spectralXY));
            }
            haveUQ2 = true;
        }
    }
    outputObject.emplace("spectralFit", std::move(spectralObject) );
    // Finally we get the station measurements which is annoying
    struct MeasurementDetails
    {
        double centerFrequency{0};
        double value{0};
    };
    std::map<std::string, std::string> identifierToStationMap;
    std::map<std::string, std::vector<MeasurementDetails>>
         identifierToMeasurementMap; 
    const auto &spectraMeasurements = json["spectraMeasurements"];
    for (const auto &spectraMeasurement : spectraMeasurements[eventIdentifier])
    {
        // If there's no waveform, stream, id then I can't map the response
        // values to antyhing
        if (!spectraMeasurement.contains("waveform")){continue;}
        const auto &waveform = spectraMeasurement["waveform"];
        const auto &stream = waveform["stream"];
        std::string identifier;
        // We got a station - let's add it and lift its identiifer!
        if (stream.contains("station"))
        {
            identifier = stream["@id"];
            const auto &station = stream["station"];
            auto name = station["networkName"].template get<std::string> ()
                      + "."
                      + station["stationName"].template get<std::string> ();
            if (!identifierToStationMap.contains(identifier))
            {
                spdlog::debug("Adding " + name + " to the map");
                identifierToStationMap.insert(std::pair {identifier, name});
            } 
            else
            {
                spdlog::warn("Already have an identifier for: " + name);
            }
        }
        else
        {
            identifier = waveform["stream"];
        }
        // Lift the value
        if (spectraMeasurement.contains("pathAndSiteCorrected"))
        {
            auto value
                = spectraMeasurement["pathAndSiteCorrected"].template
                  get<double> (); 
            auto centerFrequency 
                = 0.5*(waveform["lowFrequency"].template get<double> ()
                     + waveform["highFrequency"].template get<double> ()); 
            MeasurementDetails details{centerFrequency, value}; 
            if (identifierToMeasurementMap.contains(identifier))
            {
                auto idx = identifierToMeasurementMap.find(identifier);
                idx->second.push_back(details);
            }
            else
            {
                identifierToMeasurementMap.insert
                (
                    std::pair {identifier,
                               std::vector<MeasurementDetails> {details}
                              }
                ); 
            }
        }
    }
    // Now let's simplify this hot mess so have a map of (station, values)
    if (identifierToMeasurementMap.size() !=
        identifierToStationMap.size())
    {
        spdlog::warn("Number of stream identifiers and stations differs");
    }
    // Now build an output object for every station / measurements
    auto stationMeasurements = nlohmann::json::array ();
    for (auto &spectraMeasurementsPair : identifierToMeasurementMap)
    {
        auto idx = identifierToStationMap.find(spectraMeasurementsPair.first);
        if (idx != identifierToStationMap.end())
        {
            auto station = idx->second;
            std::sort(spectraMeasurementsPair.second.begin(),
                      spectraMeasurementsPair.second.end(), 
                      [](const auto &lhs, const auto &rhs)
                      {
                         return lhs.centerFrequency < rhs.centerFrequency;
                      });
            nlohmann::json stationObject;
            stationObject["station"] = station;
            auto measurements = nlohmann::json::array ();
            for (const auto &measurement : spectraMeasurementsPair.second)
            {
                nlohmann::json observation;
                observation["centerFrequency"] = measurement.centerFrequency;
                observation["value"] = measurement.value;
                // Get a residual
                double residual{0};
                bool found{false};
                for (int i = 0; i < static_cast<int> (fitFrequencies.size()); ++i)
                {
                    if (std::abs(fitFrequencies[i]
                               - measurement.centerFrequency) < tol)
                    {
                        found = true;
                        residual = measurement.value - fitValues.at(i);
                        break;
                    }
                }
                if (found)
                {
                    observation["residual"] = residual;
                }
                else
                {
                    spdlog::warn("Couldn't find residual for " + station
                               + " at frequency "
                               + std::to_string(measurement.centerFrequency));
                    observation["residual"] = nullptr;
                } 
                measurements.push_back( std::move(observation) );
            }
            stationObject["measurements"] = std::move(measurements);
            stationMeasurements.push_back(std::move(stationObject));
        }
    }
    outputObject.emplace("stationMeasurements", std::move(stationMeasurements));
    return outputObject;
}

}
#endif
