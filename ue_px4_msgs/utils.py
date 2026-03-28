"""
坐标转换工具
"""

import math
from typing import Tuple


class CoordinateConverter:
    """坐标系转换工具类"""

    UE5_TO_NED_SCALE = 0.01   # UE5厘米 -> NED米
    NED_TO_UE5_SCALE = 100.0  # NED米 -> UE5厘米

    @staticmethod
    def ue5_to_ned(ue_x: float, ue_y: float, ue_z: float) -> Tuple[float, float, float]:
        """
        UE5坐标系 (厘米) -> NED坐标系 (米)

        UE5: X=前(Forward), Y=右(Right), Z=上(Up) - 左手坐标系
        NED: X=北(North), Y=东(East), Z=下(Down) - 右手坐标系

        假设UE5的X轴指向北，Y轴指向东
        """
        ned_x = ue_x * CoordinateConverter.UE5_TO_NED_SCALE  # 前 -> 北
        ned_y = ue_y * CoordinateConverter.UE5_TO_NED_SCALE  # 右 -> 东
        ned_z = -ue_z * CoordinateConverter.UE5_TO_NED_SCALE  # 上 -> 下 (取负)

        return ned_x, ned_y, ned_z

    @staticmethod
    def ned_to_ue5(ned_x: float, ned_y: float, ned_z: float) -> Tuple[float, float, float]:
        """
        NED坐标系 (米) -> UE5坐标系 (厘米)
        """
        ue_x = ned_x * CoordinateConverter.NED_TO_UE5_SCALE  # 北 -> 前
        ue_y = ned_y * CoordinateConverter.NED_TO_UE5_SCALE  # 东 -> 右
        ue_z = -ned_z * CoordinateConverter.NED_TO_UE5_SCALE  # 下 -> 上 (取负)

        return ue_x, ue_y, ue_z


class GPSConverter:
    """GPS坐标转换工具"""

    # WGS84椭球体参数
    WGS84_A = 6378137.0  # 长半轴 (米)
    WGS84_E2 = 0.006694379990141  # 第一偏心率平方

    @staticmethod
    def haversine_distance(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
        """
        计算两个经纬度点之间的水平距离 (米)
        使用Haversine公式
        """
        R = 6371000  # 地球平均半径 (米)

        lat1_rad = math.radians(lat1)
        lon1_rad = math.radians(lon1)
        lat2_rad = math.radians(lat2)
        lon2_rad = math.radians(lon2)

        dlat = lat2_rad - lat1_rad
        dlon = lon2_rad - lon1_rad

        a = math.sin(dlat/2)**2 + math.cos(lat1_rad) * math.cos(lat2_rad) * math.sin(dlon/2)**2
        c = 2 * math.atan2(math.sqrt(a), math.sqrt(1-a))

        return R * c

    @staticmethod
    def gps_to_enu(lat: float, lon: float, alt: float,
                   lat0: float, lon0: float, alt0: float) -> Tuple[float, float, float]:
        """
        将GPS坐标转换为以(lat0, lon0, alt0)为原点的ENU局部坐标

        Args:
            lat, lon, alt: 当前GPS坐标 (度, 度, 米)
            lat0, lon0, alt0: 参考点（地图中心）GPS坐标

        Returns:
            (e, n, u): ENU坐标 (东, 北, 上)，单位: 米
        """
        # 计算与参考点的经纬度差
        dlat = lat - lat0
        dlon = lon - lon0
        dalt = alt - alt0

        # 将经纬度差转换为米（在纬度方向，1度约等于111319米）
        # 更精确的计算需要考虑纬度
        lat0_rad = math.radians(lat0)
        meters_per_deg_lat = 111319.0  # 纬度方向的米/度（在赤道）
        meters_per_deg_lon = 111319.0 * math.cos(lat0_rad)  # 经度方向的米/度

        n = dlat * meters_per_deg_lat   # 北方向 (纬度增加方向)
        e = dlon * meters_per_deg_lon  # 东方向 (经度增加方向)
        u = dalt                        # 上方向 (高度)

        return e, n, u

    @staticmethod
    def enu_to_ned(e: float, n: float, u: float) -> Tuple[float, float, float]:
        """
        ENU坐标 -> NED坐标

        ENU: 东-北-上 (East-North-Up)
        NED: 北-东-下 (North-East-Down)
        """
        ned_n = n   # ENU北 -> NED北 (相同)
        ned_e = e   # ENU东 -> NED东 (相同)
        ned_d = -u  # ENU上 -> NED下 (取负)

        return ned_n, ned_e, ned_d
