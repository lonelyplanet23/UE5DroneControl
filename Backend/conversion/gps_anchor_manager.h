#pragma once

#include "core/types.h"
#include <unordered_map>
#include <mutex>

/// GPS 锚点管理器
///
/// 职责：
/// 1. 首次收到无人机遥测时记录 GPS 锚点
/// 2. 断联重连时更新锚点
/// 3. 提供锚点查询接口（供 HTTP GET /anchor 和 WS event 使用）
class GpsAnchorManager {
public:
    /// 记录或更新无人机锚点
    /// @param drone_id  无人机 ID
    /// @param lat       纬度（度）
    /// @param lon       经度（度）
    /// @param alt       高度（米）
    /// @return true 如果是首次记录（PowerOn），false 如果是更新（Reconnect）
    bool SetAnchor(int drone_id, double lat, double lon, double alt);

    /// 获取无人机锚点
    GpsAnchor GetAnchor(int drone_id) const;

    /// 无人机是否有有效锚点
    bool HasAnchor(int drone_id) const;

    /// 清除无人机锚点（删除无人机时调用）
    void ClearAnchor(int drone_id);

    /// 获取当前锚点数量
    size_t Count() const;

private:
    mutable std::mutex  mutex_;
    std::unordered_map<int, GpsAnchor> anchors_;
};
