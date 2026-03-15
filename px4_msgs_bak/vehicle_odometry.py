from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional


@dataclass
class VehicleOdometry:
    timestamp: int = 0
    timestamp_sample: int = 0
    pose_frame: int = 0
    position: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    q: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0, 1.0])
    velocity_frame: int = 0
    velocity: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    angular_velocity: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    position_variance: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    orientation_variance: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    velocity_variance: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    reset_counter: int = 0
    quality: int = 0

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "VehicleOdometry":
        # Robust conversion with defaults
        def to_floats(v, length):
            if v is None:
                return [0.0] * length
            try:
                return [float(x) for x in v][:length] + [0.0] * max(0, length - len(v))
            except Exception:
                return [0.0] * length

        timestamp = int(data.get('timestamp', 0) or 0)
        timestamp_sample = int(data.get('timestamp_sample', 0) or 0)
        pose_frame = int(data.get('pose_frame', 0) or 0)
        position = to_floats(data.get('position'), 3)
        q = to_floats(data.get('q'), 4)
        velocity_frame = int(data.get('velocity_frame', 0) or 0)
        velocity = to_floats(data.get('velocity'), 3)
        angular_velocity = to_floats(data.get('angular_velocity'), 3)
        position_variance = to_floats(data.get('position_variance') or data.get('pose_covariance'), 3)
        orientation_variance = to_floats(data.get('orientation_variance'), 3)
        velocity_variance = to_floats(data.get('velocity_variance') or data.get('twist_covariance'), 3)
        reset_counter = int(data.get('reset_counter', 0) or 0)
        quality = int(data.get('quality', 0) or 0)

        return cls(
            timestamp=timestamp,
            timestamp_sample=timestamp_sample,
            pose_frame=pose_frame,
            position=position,
            q=q,
            velocity_frame=velocity_frame,
            velocity=velocity,
            angular_velocity=angular_velocity,
            position_variance=position_variance,
            orientation_variance=orientation_variance,
            velocity_variance=velocity_variance,
            reset_counter=reset_counter,
            quality=quality
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            'timestamp': self.timestamp,
            'timestamp_sample': self.timestamp_sample,
            'pose_frame': self.pose_frame,
            'position': self.position,
            'q': self.q,
            'velocity_frame': self.velocity_frame,
            'velocity': self.velocity,
            'angular_velocity': self.angular_velocity,
            'position_variance': self.position_variance,
            'orientation_variance': self.orientation_variance,
            'velocity_variance': self.velocity_variance,
            'reset_counter': self.reset_counter,
            'quality': self.quality,
        }
