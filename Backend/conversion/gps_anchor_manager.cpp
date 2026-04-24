#include "gps_anchor_manager.h"
#include <spdlog/spdlog.h>

bool GpsAnchorManager::SetAnchor(int drone_id, double lat, double lon, double alt)
{
    std::lock_guard<std::mutex> lock(mutex_);

    bool is_new = (anchors_.find(drone_id) == anchors_.end());

    GpsAnchor anchor;
    anchor.drone_id  = drone_id;
    anchor.latitude  = lat;
    anchor.longitude = lon;
    anchor.altitude  = alt;
    anchor.valid     = true;
    anchors_[drone_id] = anchor;

    if (is_new) {
        spdlog::info("[GPS Anchor] Drone {} first anchor: ({:.6f}, {:.6f}, {:.2f}m)",
                     drone_id, lat, lon, alt);
    } else {
        spdlog::info("[GPS Anchor] Drone {} anchor updated: ({:.6f}, {:.6f}, {:.2f}m)",
                     drone_id, lat, lon, alt);
    }

    return is_new;
}

GpsAnchor GpsAnchorManager::GetAnchor(int drone_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = anchors_.find(drone_id);
    if (it != anchors_.end()) {
        return it->second;
    }
    return GpsAnchor{};
}

bool GpsAnchorManager::HasAnchor(int drone_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return anchors_.find(drone_id) != anchors_.end() && anchors_.at(drone_id).valid;
}

void GpsAnchorManager::ClearAnchor(int drone_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    anchors_.erase(drone_id);
    spdlog::info("[GPS Anchor] Drone {} anchor cleared", drone_id);
}

size_t GpsAnchorManager::Count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return anchors_.size();
}
