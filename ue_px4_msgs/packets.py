"""
UDP数据包解析
用于解析UE5发送的控制指令和多机控制指令
"""

import struct
from typing import Optional, Dict, Any, Tuple
import time


class UEDataPacket:
    """UE5发送的UDP数据包解析

    数据结构 (24字节，小端序):
    struct FDroneSocketData {
        double Timestamp;  // 8 bytes (Unix timestamp, seconds)
        float X;          // 4 bytes (UE5 X coordinate, cm)
        float Y;          // 4 bytes (UE5 Y coordinate, cm)
        float Z;          // 4 bytes (UE5 Z coordinate, cm)
        int32 Mode;       // 4 bytes (0=hover, 1=move)
    };
    """

    STRUCT_FORMAT = "<dfffI"  # 小端序: double, float, float, float, uint32
    DATA_SIZE = 24  # 字节

    @staticmethod
    def parse(data: bytes) -> Optional[Dict[str, Any]]:
        """解析UDP二进制数据"""
        if len(data) != UEDataPacket.DATA_SIZE:
            return None

        try:
            timestamp, x, y, z, mode = struct.unpack(UEDataPacket.STRUCT_FORMAT, data)

            return {
                'timestamp': timestamp,
                'x': x,  # UE5 X (cm)
                'y': y,  # UE5 Y (cm)
                'z': z,  # UE5 Z (cm)
                'mode': int(mode),  # 0=hover, 1=move
                'received_time': time.time()
            }
        except Exception:
            return None


class MultiUEDataPacket:
    """多无人机UE5数据包 (32字节)

    struct MultiDroneData {
        double timestamp;      // 8 bytes
        float x;              // 4 bytes
        float y;              // 4 bytes
        float z;              // 4 bytes
        uint32 mode;          // 4 bytes
        uint32 drone_mask;    // 4 bytes (位掩码选择无人机)
        uint32 sequence;      // 4 bytes (序列号)
    };
    """

    STRUCT_FORMAT = "<dfffIII"  # 小端序: double, float x3, uint32 x3
    DATA_SIZE = 32  # 字节

    @staticmethod
    def parse(data: bytes) -> Optional[Dict[str, Any]]:
        """解析多无人机UDP数据包"""
        if len(data) < 24:
            return None

        try:
            if len(data) >= 32:
                # 32字节完整格式
                timestamp, x, y, z, mode, drone_mask, sequence = struct.unpack(MultiUEDataPacket.STRUCT_FORMAT, data)
            else:
                # 24字节旧格式（向后兼容）
                timestamp, x, y, z, mode = struct.unpack(UEDataPacket.STRUCT_FORMAT, data)
                drone_mask = 0x07  # 默认选择所有3架无人机
                sequence = 0

            return {
                'timestamp': timestamp,
                'x': x,
                'y': y,
                'z': z,
                'mode': int(mode),
                'drone_mask': int(drone_mask),
                'sequence': int(sequence)
            }
        except Exception:
            return None
