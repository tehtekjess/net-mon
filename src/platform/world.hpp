#pragma once
#include "core/model.hpp"
#include <filesystem>
#include <memory>
#include <vector>
namespace nm {
struct MapPoint {
    double longitude = 0, latitude = 0;
};
struct MapShape {
    std::string code, name;
    MapPoint label;
    std::vector<std::vector<MapPoint>> rings;
};
class WorldMap {
    std::vector<MapShape> shapes_;

  public:
    void load(const std::filesystem::path &path);
    const std::vector<MapShape> &shapes() const { return shapes_; }
    const MapShape *country(const std::string &code) const;
};
struct MapEndpoint {
    std::string ip;
    std::wstring title, subtitle, details;
    int severity = 0;
};
struct MapGroup {
    std::string code;
    std::wstring name, caption;
    MapPoint location;
    int severity = 0;
    std::vector<MapEndpoint> endpoints;
};
struct MapSnapshot {
    std::shared_ptr<const WorldMap> geometry;
    std::vector<MapGroup> groups;
    std::size_t mapped_ips = 0, local_ips = 0, unlocated_ips = 0, observations = 0;
};
// Country markers are representative label points, never IP-specific coordinates.
MapSnapshot prepare_map(const Json &records, std::shared_ptr<const WorldMap> geometry);
} // namespace nm
