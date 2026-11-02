#include "platform/world.hpp"
#include "platform/windows.hpp"
#include "core/ip.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace nm {
void WorldMap::load(const std::filesystem::path &path) {
    const auto json = Json::parse(read_text(path, 2 * 1024 * 1024));
    if (json.at("version") != 1 || json.at("projection") != "equirectangular" ||
        !json.at("countries").is_array() || json.at("countries").size() > 300)
        throw std::runtime_error("Invalid world map");
    std::vector<MapShape> shapes;
    std::size_t vertices = 0;
    const auto point = [](double lon, double lat) {
        if (!std::isfinite(lon) || !std::isfinite(lat) || std::abs(lon) > 180.01 ||
            std::abs(lat) > 90)
            throw std::runtime_error("Invalid map coordinate");
        return MapPoint{lon, lat};
    };
    for (const auto &entry : json.at("countries")) {
        MapShape shape;
        shape.code = entry.at("code").get<std::string>();
        shape.name = entry.at("name").get<std::string>();
        if ((!shape.code.empty() && (shape.code.size() != 2 ||
                                     !std::all_of(shape.code.begin(), shape.code.end(),
                                                  [](char c) { return c >= 'A' && c <= 'Z'; }))) ||
            shape.name.size() > 200)
            throw std::runtime_error("Invalid map country");
        shape.label =
            point(entry.at("longitude").get<double>(), entry.at("latitude").get<double>());
        if (!entry.at("rings").is_array())
            throw std::runtime_error("Invalid map polygons");
        for (const auto &ring : entry.at("rings")) {
            if (!ring.is_array() || ring.size() < 4 || ring.size() > 20000)
                throw std::runtime_error("Invalid map polygon");
            std::vector<MapPoint> points;
            for (const auto &p : ring) {
                if (!p.is_array() || p.size() != 2 || ++vertices > 100000)
                    throw std::runtime_error("World map exceeds vertex limit");
                points.push_back(point(p[0].get<double>(), p[1].get<double>()));
            }
            shape.rings.push_back(std::move(points));
        }
        shapes.push_back(std::move(shape));
    }
    if (shapes.empty())
        throw std::runtime_error("World map is empty");
    shapes_ = std::move(shapes);
}
const MapShape *WorldMap::country(const std::string &code) const {
    if (code.empty())
        return nullptr;
    const auto it = std::find_if(shapes_.begin(), shapes_.end(),
                                 [&](const auto &shape) { return shape.code == code; });
    return it == shapes_.end() ? nullptr : &*it;
}
MapSnapshot prepare_map(const Json &records, std::shared_ptr<const WorldMap> geometry) {
    MapSnapshot result;
    result.geometry = std::move(geometry);
    result.observations = records.size();
    struct Endpoint {
        std::set<std::string> apps, ports;
        int severity = 0;
        std::size_t observations = 0;
    };
    std::map<std::string, std::map<std::string, Endpoint>> countries;
    std::set<std::string> local, unlocated;
    for (const auto &row : records) {
        const auto ip = row.value("remote_ip", "");
        if (ip.empty())
            continue;
        if (is_local_network(ip)) {
            local.insert(ip);
            continue;
        }
        if (!address_kind(ip).empty()) {
            unlocated.insert(ip);
            continue;
        }
        const auto code = row.value("country_code", "");
        if (!result.geometry || !result.geometry->country(code)) {
            unlocated.insert(ip);
            continue;
        }
        auto &endpoint = countries[code][ip];
        endpoint.apps.insert(row.value("app", "Unknown process"));
        endpoint.ports.insert(std::to_string(row.value("remote_port", 0)) +
                              (row.value("protocol", 0) == 6    ? "/TCP"
                               : row.value("protocol", 0) == 17 ? "/UDP"
                                                                : "/other"));
        const auto classification = row.value("classification", "unknown");
        const int severity = classification == "bad" || classification == "threat"          ? 3
                             : classification == "suspicious" || classification == "review" ? 2
                             : classification == "good"                                     ? 0
                                                                                            : 1;
        endpoint.severity = std::max(endpoint.severity, severity);
        ++endpoint.observations;
    }
    for (const auto &[code, ips] : countries) {
        const auto *shape = result.geometry->country(code);
        MapGroup group;
        group.code = code;
        group.name = wide(shape->name);
        group.location = shape->label;
        for (const auto &[ip, value] : ips) {
            MapEndpoint endpoint;
            endpoint.ip = ip;
            endpoint.title = wide(ip);
            endpoint.severity = value.severity;
            std::string ports, apps, names;
            for (const auto &port : value.ports) {
                if (!ports.empty())
                    ports += ", ";
                ports += port;
            }
            for (const auto &app : value.apps) {
                apps += app + "\r\n";
                if (!names.empty())
                    names += ", ";
                names += utf8(std::filesystem::path(wide(app)).filename().wstring());
            }
            endpoint.subtitle = wide(names + "  |  " + ports);
            endpoint.details =
                wide(ip + "\r\nCountry estimate: " + shape->name + "\r\nPorts: " + ports +
                     "\r\nProcesses:\r\n" + apps + "\r\n" + std::to_string(value.observations) +
                     " observations in the current view.");
            group.severity = std::max(group.severity, endpoint.severity);
            group.endpoints.push_back(std::move(endpoint));
        }
        group.caption = std::to_wstring(group.endpoints.size()) +
                        (group.endpoints.size() == 1 ? L" destination IP" : L" destination IPs");
        result.mapped_ips += group.endpoints.size();
        result.groups.push_back(std::move(group));
    }
    std::sort(result.groups.begin(), result.groups.end(), [](const auto &a, const auto &b) {
        return a.endpoints.size() != b.endpoints.size() ? a.endpoints.size() > b.endpoints.size()
                                                        : a.name < b.name;
    });
    result.local_ips = local.size();
    result.unlocated_ips = unlocated.size();
    return result;
}
} // namespace nm
