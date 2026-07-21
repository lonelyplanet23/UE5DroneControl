#!/usr/bin/env python3
"""
Jetson ROS2 camera -> H.264 RTSP publisher with SfM metadata capture.

This process is intentionally independent from jetson_bridge.py:
  - it only subscribes to ROS2 output topics;
  - it never opens /dev/video* directly;
  - it never opens the PX4 serial port or the bridge UDP ports;
  - it publishes no PX4 command topics.

Default LAN topology:
  Jetson:   192.168.10.1
  MediaMTX: 192.168.10.30 (the UE/Windows computer)
  RTSP:     rtsp://192.168.10.30:8554/drone-1
  UE page:  http://192.168.10.30:8889/drone-1

The program records every ROS image callback in frames.csv. GPS fields remain
empty when PX4 has no valid global fix. When bracketing GPS samples are
available, latitude/longitude/altitude are linearly interpolated on a unified
ROS clock for the image frame. Original PX4 timestamps are retained in the
CSV. Local pose is matched from the nearest VehicleOdometry sample.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import dataclasses
import json
import logging
import math
import pathlib
import queue
import re
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import deque
from datetime import datetime
from typing import Callable, Deque, Generic, Optional, Sequence, TypeVar


LOGGER = logging.getLogger("jetson_video_stream")


# Keep pure helpers importable on developer machines without ROS2/GStreamer.
try:
    import gi

    gi.require_version("Gst", "1.0")
    from gi.repository import Gst  # type: ignore
except (ImportError, ValueError):
    Gst = None  # type: ignore

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import (
        DurabilityPolicy,
        HistoryPolicy,
        QoSProfile,
        ReliabilityPolicy,
    )
    from px4_msgs.msg import VehicleGlobalPosition, VehicleOdometry
    from sensor_msgs.msg import CameraInfo, Image
except ImportError:
    rclpy = None  # type: ignore
    Node = object  # type: ignore
    QoSProfile = None  # type: ignore
    ReliabilityPolicy = None  # type: ignore
    DurabilityPolicy = None  # type: ignore
    HistoryPolicy = None  # type: ignore
    VehicleGlobalPosition = None  # type: ignore
    VehicleOdometry = None  # type: ignore
    CameraInfo = None  # type: ignore
    Image = None  # type: ignore


T = TypeVar("T")


def normalize_topic_prefix(value: str) -> str:
    value = value.strip()
    if not value or value == "/":
        return ""
    return "/" + value.strip("/")


def prefixed_topic(prefix: str, topic: str) -> str:
    normalized = normalize_topic_prefix(prefix)
    return normalized + "/" + topic.strip("/")


def sanitize_stream_path(value: str) -> str:
    value = value.strip().strip("/")
    if not value:
        raise ValueError("stream path must not be empty")
    if not re.fullmatch(r"[A-Za-z0-9._/-]+", value):
        raise ValueError(
            "stream path may only contain letters, numbers, '.', '_', '-' and '/'"
        )
    return value


def gst_quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def ros_stamp_to_ns(stamp) -> int:
    return int(stamp.sec) * 1_000_000_000 + int(stamp.nanosec)


def finite_or_none(value) -> Optional[float]:
    try:
        converted = float(value)
    except (TypeError, ValueError):
        return None
    return converted if math.isfinite(converted) else None


@dataclasses.dataclass(frozen=True)
class GpsSample:
    timestamp_us: int
    timestamp_sample_us: int
    latitude: float
    longitude: float
    altitude_amsl_m: float
    altitude_ellipsoid_m: float
    eph_m: float
    epv_m: float
    lat_lon_valid: bool
    alt_valid: bool
    dead_reckoning: bool
    # ROS clock time captured when this PX4 sample reached this node. PX4's
    # timestamp_sample is normally boot-relative and cannot be compared with a
    # camera Header timestamp directly.
    sync_timestamp_us: int = 0


@dataclasses.dataclass(frozen=True)
class OdomSample:
    timestamp_us: int
    timestamp_sample_us: int
    pose_frame: int
    position: tuple[float, float, float]
    quaternion_wxyz: tuple[float, float, float, float]
    velocity_frame: int
    velocity: tuple[float, float, float]
    sync_timestamp_us: int = 0


@dataclasses.dataclass(frozen=True)
class GpsMatch:
    sample: GpsSample
    interpolated: bool
    before_timestamp_us: int
    after_timestamp_us: int
    nearest_offset_ms: float


class TimedSampleCache(Generic[T]):
    def __init__(self, timestamp_getter: Callable[[T], int], maxlen: int = 512):
        self._timestamp_getter = timestamp_getter
        self._samples: Deque[T] = deque(maxlen=maxlen)
        self._lock = threading.Lock()

    def add(self, sample: T) -> None:
        with self._lock:
            if self._samples and self._timestamp_getter(sample) < self._timestamp_getter(
                self._samples[-1]
            ):
                ordered = list(self._samples)
                ordered.append(sample)
                ordered.sort(key=self._timestamp_getter)
                self._samples = deque(ordered[-self._samples.maxlen :], maxlen=self._samples.maxlen)
            else:
                self._samples.append(sample)

    def snapshot(self) -> list[T]:
        with self._lock:
            return list(self._samples)


def _interpolate_value(a: float, b: float, ratio: float) -> float:
    return a + (b - a) * ratio


def _sync_timestamp_us(sample) -> int:
    return int(sample.sync_timestamp_us or sample.timestamp_sample_us)


def match_gps(
    samples: Sequence[GpsSample],
    target_timestamp_us: int,
    max_age_us: int,
) -> Optional[GpsMatch]:
    if not samples:
        return None

    timestamps = [_sync_timestamp_us(sample) for sample in samples]
    index = bisect.bisect_left(timestamps, target_timestamp_us)
    before = samples[index - 1] if index > 0 else None
    after = samples[index] if index < len(samples) else None

    if before is not None and after is not None:
        before_sync_us = _sync_timestamp_us(before)
        after_sync_us = _sync_timestamp_us(after)
        before_age = target_timestamp_us - before_sync_us
        after_age = after_sync_us - target_timestamp_us
        interval = after_sync_us - before_sync_us
        if (
            before_age >= 0
            and after_age >= 0
            and before_age <= max_age_us
            and after_age <= max_age_us
            and interval > 0
            and before.lat_lon_valid
            and after.lat_lon_valid
        ):
            ratio = before_age / interval
            altitude_valid = before.alt_valid and after.alt_valid
            sample = GpsSample(
                timestamp_us=round(
                    _interpolate_value(
                        before.timestamp_us, after.timestamp_us, ratio
                    )
                ),
                timestamp_sample_us=round(
                    _interpolate_value(
                        before.timestamp_sample_us,
                        after.timestamp_sample_us,
                        ratio,
                    )
                ),
                latitude=_interpolate_value(before.latitude, after.latitude, ratio),
                longitude=_interpolate_value(before.longitude, after.longitude, ratio),
                altitude_amsl_m=(
                    _interpolate_value(
                        before.altitude_amsl_m, after.altitude_amsl_m, ratio
                    )
                    if altitude_valid
                    else math.nan
                ),
                altitude_ellipsoid_m=(
                    _interpolate_value(
                        before.altitude_ellipsoid_m,
                        after.altitude_ellipsoid_m,
                        ratio,
                    )
                    if altitude_valid
                    else math.nan
                ),
                eph_m=_interpolate_value(before.eph_m, after.eph_m, ratio),
                epv_m=_interpolate_value(before.epv_m, after.epv_m, ratio),
                lat_lon_valid=True,
                alt_valid=altitude_valid,
                dead_reckoning=before.dead_reckoning or after.dead_reckoning,
                sync_timestamp_us=target_timestamp_us,
            )
            return GpsMatch(
                sample=sample,
                interpolated=True,
                before_timestamp_us=before.timestamp_sample_us,
                after_timestamp_us=after.timestamp_sample_us,
                nearest_offset_ms=min(before_age, after_age) / 1000.0,
            )

    candidates = [sample for sample in (before, after) if sample is not None]
    nearest = min(
        candidates,
        key=lambda sample: abs(_sync_timestamp_us(sample) - target_timestamp_us),
    )
    offset_us = _sync_timestamp_us(nearest) - target_timestamp_us
    if abs(offset_us) > max_age_us:
        return None
    return GpsMatch(
        sample=nearest,
        interpolated=False,
        before_timestamp_us=before.timestamp_sample_us if before else 0,
        after_timestamp_us=after.timestamp_sample_us if after else 0,
        nearest_offset_ms=offset_us / 1000.0,
    )


def match_nearest_odom(
    samples: Sequence[OdomSample],
    target_timestamp_us: int,
    max_age_us: int,
) -> tuple[Optional[OdomSample], Optional[float]]:
    if not samples:
        return None, None
    timestamps = [_sync_timestamp_us(sample) for sample in samples]
    index = bisect.bisect_left(timestamps, target_timestamp_us)
    candidates = []
    if index > 0:
        candidates.append(samples[index - 1])
    if index < len(samples):
        candidates.append(samples[index])
    nearest = min(
        candidates,
        key=lambda sample: abs(_sync_timestamp_us(sample) - target_timestamp_us),
    )
    offset_us = _sync_timestamp_us(nearest) - target_timestamp_us
    if abs(offset_us) > max_age_us:
        return None, None
    return nearest, offset_us / 1000.0


GST_FORMAT_BY_ROS_ENCODING = {
    "rgb8": ("RGB", 3),
    "bgr8": ("BGR", 3),
    "rgba8": ("RGBA", 4),
    "bgra8": ("BGRA", 4),
    "mono8": ("GRAY8", 1),
    "yuyv": ("YUY2", 2),
    "yuv422_yuy2": ("YUY2", 2),
}


@dataclasses.dataclass
class FrameTracking:
    frame_index: int
    image_timestamp_ns: int
    timestamp_source: str
    received_monotonic: float
    width: int
    height: int
    encoding: str
    step: int
    source_gap_ms: Optional[float]
    estimated_source_drops: int
    sync_timestamp_us: int = 0
    sync_basis: str = "ros_receive_time"
    stream_accepted: bool = False
    stream_generation: int = 0
    stream_pts_ns: int = 0
    stream_state: str = "queued"
    stream_error: str = ""
    stream_done: threading.Event = dataclasses.field(
        default_factory=threading.Event, repr=False
    )


@dataclasses.dataclass(frozen=True)
class FramePacket:
    tracking: FrameTracking
    data: bytes


@dataclasses.dataclass(frozen=True)
class StreamConfig:
    rtsp_url: str
    bitrate: int
    fps: int
    keyframe_interval: int
    reconnect_seconds: float
    record_local: bool
    mission_dir: pathlib.Path
    segment_minutes: float


def build_pipeline_description(
    config: StreamConfig,
    gst_format: str,
    width: int,
    height: int,
    generation: int,
) -> str:
    common = (
        "appsrc name=video_source is-live=true block=false format=time "
        "do-timestamp=false "
        f"caps=video/x-raw,format={gst_format},width={width},height={height},"
        f"framerate={config.fps}/1 "
        "! queue max-size-buffers=8 max-size-bytes=0 max-size-time=0 "
        "! videoconvert n-threads=2 "
        "! video/x-raw,format=I420 "
        "! nvvidconv "
        "! video/x-raw(memory:NVMM),format=NV12 "
        f"! nvv4l2h264enc bitrate={config.bitrate} control-rate=1 "
        f"iframeinterval={config.keyframe_interval} "
        f"idrinterval={config.keyframe_interval} insert-sps-pps=true "
        "! video/x-h264,stream-format=byte-stream,alignment=au,profile=baseline "
        "! h264parse config-interval=-1 "
    )
    rtsp_branch = (
        "queue max-size-buffers=15 max-size-bytes=0 max-size-time=500000000 "
        "leaky=downstream "
        f"! rtspclientsink location={gst_quote(config.rtsp_url)} protocols=tcp"
    )
    if not config.record_local:
        return common + "! " + rtsp_branch

    record_pattern = config.mission_dir / (
        f"video_g{generation:03d}_%05d.mkv"
    )
    segment_ns = max(1, int(config.segment_minutes * 60.0 * 1_000_000_000))
    record_branch = (
        "queue max-size-buffers=60 max-size-bytes=0 max-size-time=0 "
        "! h264parse "
        f"! splitmuxsink location={gst_quote(str(record_pattern))} "
        f"muxer-factory=matroskamux max-size-time={segment_ns} "
        "async-finalize=true"
    )
    return common + "! tee name=encoded encoded. ! " + rtsp_branch + " encoded. ! " + record_branch


class GstStreamPipeline:
    REQUIRED_ELEMENTS = (
        "appsrc",
        "queue",
        "videoconvert",
        "nvvidconv",
        "nvv4l2h264enc",
        "h264parse",
        "rtspclientsink",
    )

    def __init__(self, config: StreamConfig):
        self.config = config
        self.pipeline = None
        self.appsrc = None
        self.width = 0
        self.height = 0
        self.encoding = ""
        self.generation = 0
        self.first_timestamp_ns = 0
        self.next_retry_monotonic = 0.0
        self.state = "stopped"
        self.last_error = ""

    def _check_runtime(self) -> None:
        if Gst is None:
            raise RuntimeError(
                "PyGObject/GStreamer Python bindings are unavailable"
            )
        required = list(self.REQUIRED_ELEMENTS)
        if self.config.record_local:
            required.extend(("tee", "splitmuxsink", "matroskamux"))
        missing = [
            name
            for name in required
            if Gst.ElementFactory.find(name) is None
        ]
        if missing:
            detail = ", ".join(missing)
            raise RuntimeError(
                f"missing GStreamer element(s): {detail}; install "
                "gstreamer1.0-rtsp and verify Jetson multimedia plugins"
            )

    def _start(self, frame: FrameTracking) -> None:
        self._check_runtime()
        format_info = GST_FORMAT_BY_ROS_ENCODING.get(frame.encoding.lower())
        if format_info is None:
            raise RuntimeError(f"unsupported ROS image encoding: {frame.encoding}")

        self.stop()
        self.generation += 1
        description = build_pipeline_description(
            self.config,
            format_info[0],
            frame.width,
            frame.height,
            self.generation,
        )
        LOGGER.info("Starting GStreamer generation %d", self.generation)
        LOGGER.debug("GStreamer pipeline: %s", description)
        self.pipeline = Gst.parse_launch(description)
        self.appsrc = self.pipeline.get_by_name("video_source")
        if self.appsrc is None:
            raise RuntimeError("GStreamer appsrc element was not created")
        result = self.pipeline.set_state(Gst.State.PLAYING)
        if result == Gst.StateChangeReturn.FAILURE:
            raise RuntimeError("GStreamer pipeline failed to enter PLAYING state")
        self.width = frame.width
        self.height = frame.height
        self.encoding = frame.encoding.lower()
        self.first_timestamp_ns = frame.image_timestamp_ns
        self.state = "connecting"
        self.last_error = ""

    def _poll_bus(self) -> None:
        if self.pipeline is None or Gst is None:
            return
        bus = self.pipeline.get_bus()
        while True:
            message = bus.pop_filtered(
                Gst.MessageType.ERROR
                | Gst.MessageType.EOS
                | Gst.MessageType.STATE_CHANGED
            )
            if message is None:
                break
            if message.type == Gst.MessageType.ERROR:
                error, debug = message.parse_error()
                self.last_error = str(error)
                if debug:
                    self.last_error += f" ({debug})"
                self.state = "reconnecting"
                LOGGER.error("GStreamer error: %s", self.last_error)
                self.stop(keep_error=True)
                self.next_retry_monotonic = (
                    time.monotonic() + self.config.reconnect_seconds
                )
                break
            if message.type == Gst.MessageType.EOS:
                self.last_error = "unexpected end of stream"
                self.state = "reconnecting"
                self.stop(keep_error=True)
                self.next_retry_monotonic = (
                    time.monotonic() + self.config.reconnect_seconds
                )
                break
            if (
                message.type == Gst.MessageType.STATE_CHANGED
                and message.src == self.pipeline
            ):
                _old, new, _pending = message.parse_state_changed()
                if new == Gst.State.PLAYING:
                    self.state = "streaming"

    @staticmethod
    def _tight_frame_data(frame: FramePacket) -> bytes:
        format_info = GST_FORMAT_BY_ROS_ENCODING.get(
            frame.tracking.encoding.lower()
        )
        if format_info is None:
            raise RuntimeError(
                f"unsupported ROS image encoding: {frame.tracking.encoding}"
            )
        expected_step = frame.tracking.width * format_info[1]
        if frame.tracking.step == expected_step:
            return frame.data
        if frame.tracking.step < expected_step:
            raise RuntimeError(
                f"image step {frame.tracking.step} is smaller than expected "
                f"{expected_step}"
            )
        rows = []
        for row in range(frame.tracking.height):
            offset = row * frame.tracking.step
            rows.append(frame.data[offset : offset + expected_step])
        return b"".join(rows)

    def push(self, packet: FramePacket) -> tuple[bool, int, int, str, str]:
        if Gst is None:
            return False, 0, 0, "unavailable", "GStreamer unavailable"
        self._poll_bus()
        tracking = packet.tracking
        geometry_changed = (
            self.pipeline is not None
            and (
                tracking.width != self.width
                or tracking.height != self.height
                or tracking.encoding.lower() != self.encoding
            )
        )
        if geometry_changed:
            LOGGER.warning(
                "Image format changed from %dx%d %s to %dx%d %s; restarting",
                self.width,
                self.height,
                self.encoding,
                tracking.width,
                tracking.height,
                tracking.encoding,
            )
            self.stop()

        if self.pipeline is None:
            if time.monotonic() < self.next_retry_monotonic:
                return (
                    False,
                    self.generation,
                    0,
                    "reconnecting",
                    self.last_error,
                )
            try:
                self._start(tracking)
            except Exception as exc:
                self.last_error = str(exc)
                self.state = "reconnecting"
                self.stop(keep_error=True)
                self.next_retry_monotonic = (
                    time.monotonic() + self.config.reconnect_seconds
                )
                LOGGER.error("Unable to start GStreamer: %s", exc)
                return False, self.generation, 0, self.state, self.last_error

        try:
            payload = self._tight_frame_data(packet)
            buffer = Gst.Buffer.new_allocate(None, len(payload), None)
            buffer.fill(0, payload)
            pts = max(0, tracking.image_timestamp_ns - self.first_timestamp_ns)
            buffer.pts = pts
            buffer.dts = pts
            buffer.duration = int(1_000_000_000 / self.config.fps)
            flow_result = self.appsrc.emit("push-buffer", buffer)
            if flow_result != Gst.FlowReturn.OK:
                raise RuntimeError(f"appsrc push returned {flow_result.value_nick}")
            self._poll_bus()
            accepted = self.pipeline is not None
            return (
                accepted,
                self.generation,
                pts,
                self.state,
                self.last_error,
            )
        except Exception as exc:
            self.last_error = str(exc)
            self.state = "reconnecting"
            LOGGER.error("Frame push failed: %s", exc)
            self.stop(keep_error=True)
            self.next_retry_monotonic = (
                time.monotonic() + self.config.reconnect_seconds
            )
            return False, self.generation, 0, self.state, self.last_error

    def stop(self, keep_error: bool = False) -> None:
        if self.appsrc is not None:
            try:
                self.appsrc.emit("end-of-stream")
            except Exception:
                pass
        if self.pipeline is not None and Gst is not None:
            self.pipeline.set_state(Gst.State.NULL)
        self.pipeline = None
        self.appsrc = None
        self.width = 0
        self.height = 0
        self.encoding = ""
        self.first_timestamp_ns = 0
        if not keep_error:
            self.state = "stopped"
            self.last_error = ""


CSV_FIELDS = (
    "frame_index",
    "image_timestamp_ns",
    "timestamp_source",
    "sync_timestamp_us",
    "sync_basis",
    "source_gap_ms",
    "estimated_source_drops",
    "width",
    "height",
    "encoding",
    "stream_accepted",
    "stream_generation",
    "stream_pts_ns",
    "stream_state",
    "stream_error",
    "gps_available",
    "gps_interpolated",
    "gps_px4_timestamp_us",
    "gps_px4_timestamp_sample_us",
    "gps_before_timestamp_us",
    "gps_after_timestamp_us",
    "gps_nearest_offset_ms",
    "latitude",
    "longitude",
    "altitude_amsl_m",
    "altitude_ellipsoid_m",
    "eph_m",
    "epv_m",
    "lat_lon_valid",
    "alt_valid",
    "dead_reckoning",
    "odom_available",
    "odom_timestamp_us",
    "odom_offset_ms",
    "pose_frame",
    "local_north_m",
    "local_east_m",
    "local_down_m",
    "q_w",
    "q_x",
    "q_y",
    "q_z",
    "velocity_frame",
    "velocity_north_m_s",
    "velocity_east_m_s",
    "velocity_down_m_s",
)


def _csv_number(value) -> object:
    finite = finite_or_none(value)
    return "" if finite is None else finite


class MetadataWriter:
    def __init__(
        self,
        mission_dir: pathlib.Path,
        gps_cache: TimedSampleCache[GpsSample],
        odom_cache: TimedSampleCache[OdomSample],
        sync_wait_ms: float,
        max_gps_age_ms: float,
        max_odom_age_ms: float,
    ):
        self.mission_dir = mission_dir
        self.gps_cache = gps_cache
        self.odom_cache = odom_cache
        self.sync_wait_seconds = max(0.0, sync_wait_ms / 1000.0)
        self.max_gps_age_us = int(max_gps_age_ms * 1000.0)
        self.max_odom_age_us = int(max_odom_age_ms * 1000.0)
        self.tasks: queue.Queue[Optional[FrameTracking]] = queue.Queue()
        self.thread = threading.Thread(
            target=self._run, name="sfm-metadata", daemon=True
        )
        self.file = (mission_dir / "frames.csv").open(
            "w", encoding="utf-8", newline="", buffering=1
        )
        self.writer = csv.DictWriter(self.file, fieldnames=CSV_FIELDS)
        self.writer.writeheader()
        self.rows_written = 0

    def start(self) -> None:
        self.thread.start()

    def enqueue(self, tracking: FrameTracking) -> None:
        self.tasks.put(tracking)

    def _build_row(self, tracking: FrameTracking) -> dict[str, object]:
        target_us = tracking.sync_timestamp_us or tracking.image_timestamp_ns // 1000
        gps_match = match_gps(
            self.gps_cache.snapshot(), target_us, self.max_gps_age_us
        )
        odom, odom_offset_ms = match_nearest_odom(
            self.odom_cache.snapshot(), target_us, self.max_odom_age_us
        )

        row: dict[str, object] = {
            "frame_index": tracking.frame_index,
            "image_timestamp_ns": tracking.image_timestamp_ns,
            "timestamp_source": tracking.timestamp_source,
            "sync_timestamp_us": target_us,
            "sync_basis": tracking.sync_basis,
            "source_gap_ms": _csv_number(tracking.source_gap_ms),
            "estimated_source_drops": tracking.estimated_source_drops,
            "width": tracking.width,
            "height": tracking.height,
            "encoding": tracking.encoding,
            "stream_accepted": tracking.stream_accepted,
            "stream_generation": tracking.stream_generation,
            "stream_pts_ns": tracking.stream_pts_ns,
            "stream_state": tracking.stream_state,
            "stream_error": tracking.stream_error,
            "gps_available": gps_match is not None,
            "gps_interpolated": gps_match.interpolated if gps_match else False,
            "gps_px4_timestamp_us": (
                gps_match.sample.timestamp_us if gps_match else ""
            ),
            "gps_px4_timestamp_sample_us": (
                gps_match.sample.timestamp_sample_us if gps_match else ""
            ),
            "gps_before_timestamp_us": (
                gps_match.before_timestamp_us if gps_match else ""
            ),
            "gps_after_timestamp_us": (
                gps_match.after_timestamp_us if gps_match else ""
            ),
            "gps_nearest_offset_ms": (
                _csv_number(gps_match.nearest_offset_ms) if gps_match else ""
            ),
            "latitude": "",
            "longitude": "",
            "altitude_amsl_m": "",
            "altitude_ellipsoid_m": "",
            "eph_m": "",
            "epv_m": "",
            "lat_lon_valid": False,
            "alt_valid": False,
            "dead_reckoning": False,
            "odom_available": odom is not None,
            "odom_timestamp_us": odom.timestamp_sample_us if odom else "",
            "odom_offset_ms": _csv_number(odom_offset_ms),
            "pose_frame": odom.pose_frame if odom else "",
            "local_north_m": _csv_number(odom.position[0]) if odom else "",
            "local_east_m": _csv_number(odom.position[1]) if odom else "",
            "local_down_m": _csv_number(odom.position[2]) if odom else "",
            "q_w": _csv_number(odom.quaternion_wxyz[0]) if odom else "",
            "q_x": _csv_number(odom.quaternion_wxyz[1]) if odom else "",
            "q_y": _csv_number(odom.quaternion_wxyz[2]) if odom else "",
            "q_z": _csv_number(odom.quaternion_wxyz[3]) if odom else "",
            "velocity_frame": odom.velocity_frame if odom else "",
            "velocity_north_m_s": _csv_number(odom.velocity[0]) if odom else "",
            "velocity_east_m_s": _csv_number(odom.velocity[1]) if odom else "",
            "velocity_down_m_s": _csv_number(odom.velocity[2]) if odom else "",
        }
        if gps_match is not None:
            gps = gps_match.sample
            row.update(
                {
                    "latitude": _csv_number(gps.latitude)
                    if gps.lat_lon_valid
                    else "",
                    "longitude": _csv_number(gps.longitude)
                    if gps.lat_lon_valid
                    else "",
                    "altitude_amsl_m": _csv_number(gps.altitude_amsl_m)
                    if gps.alt_valid
                    else "",
                    "altitude_ellipsoid_m": _csv_number(
                        gps.altitude_ellipsoid_m
                    )
                    if gps.alt_valid
                    else "",
                    "eph_m": _csv_number(gps.eph_m),
                    "epv_m": _csv_number(gps.epv_m),
                    "lat_lon_valid": gps.lat_lon_valid,
                    "alt_valid": gps.alt_valid,
                    "dead_reckoning": gps.dead_reckoning,
                }
            )
        return row

    def _run(self) -> None:
        while True:
            tracking = self.tasks.get()
            if tracking is None:
                break
            due = tracking.received_monotonic + self.sync_wait_seconds
            remaining = due - time.monotonic()
            if remaining > 0:
                time.sleep(remaining)
            tracking.stream_done.wait(timeout=0.5)
            self.writer.writerow(self._build_row(tracking))
            self.rows_written += 1

    def close(self) -> None:
        self.tasks.put(None)
        # Every received image must have one CSV row, so drain the pending
        # metadata queue before closing the file.
        self.thread.join()
        self.file.flush()
        self.file.close()


def update_backend_video_url(
    backend_base_url: str,
    drone_id: str,
    video_url: str,
    timeout_seconds: float = 3.0,
) -> dict:
    endpoint = (
        backend_base_url.rstrip("/")
        + "/api/drones/"
        + urllib.parse.quote(str(drone_id), safe="")
    )
    payload = json.dumps({"video_url": video_url}).encode("utf-8")
    request = urllib.request.Request(
        endpoint,
        data=payload,
        method="PUT",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
        body = response.read().decode("utf-8")
        return json.loads(body) if body else {}


def write_json_atomic(path: pathlib.Path, payload: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    temporary.replace(path)


class JetsonVideoStreamNode(Node):  # type: ignore[misc]
    def __init__(self, args: argparse.Namespace):
        node_suffix = re.sub(r"[^A-Za-z0-9_]", "_", str(args.drone_id))
        super().__init__(f"jetson_video_stream_{node_suffix}")
        self.args = args
        self.stop_event = threading.Event()
        self.frame_queue: queue.Queue[Optional[FramePacket]] = queue.Queue(
            maxsize=args.frame_queue_size
        )
        self.gps_cache = TimedSampleCache[GpsSample](
            _sync_timestamp_us
        )
        self.odom_cache = TimedSampleCache[OdomSample](
            _sync_timestamp_us
        )
        self.frame_index = 0
        self.last_image_timestamp_ns = 0
        self.stream_queue_drops = 0
        self.source_drop_estimate = 0

        mission_name = (
            f"mission_{datetime.now().strftime('%Y%m%d_%H%M%S')}_"
            f"drone_{args.drone_id}"
        )
        self.mission_dir = pathlib.Path(args.record_dir).expanduser() / mission_name
        self.mission_dir.mkdir(parents=True, exist_ok=False)

        stream_config = StreamConfig(
            rtsp_url=args.rtsp_url,
            bitrate=args.bitrate,
            fps=args.fps,
            keyframe_interval=args.keyframe_interval,
            reconnect_seconds=args.reconnect_seconds,
            record_local=args.record_local,
            mission_dir=self.mission_dir,
            segment_minutes=args.segment_minutes,
        )
        self.stream_pipeline = GstStreamPipeline(stream_config)
        self.metadata_writer = MetadataWriter(
            mission_dir=self.mission_dir,
            gps_cache=self.gps_cache,
            odom_cache=self.odom_cache,
            sync_wait_ms=args.sync_wait_ms,
            max_gps_age_ms=args.max_gps_age_ms,
            max_odom_age_ms=args.max_odom_age_ms,
        )
        self.metadata_writer.start()
        self.stream_thread = threading.Thread(
            target=self._stream_loop, name="rtsp-stream", daemon=True
        )
        self.stream_thread.start()

        sensor_qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )
        image_qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=2,
        )

        self.create_subscription(Image, args.image_topic, self._on_image, image_qos)
        self.create_subscription(
            CameraInfo, args.camera_info_topic, self._on_camera_info, image_qos
        )
        self.create_subscription(
            VehicleGlobalPosition, args.gps_topic, self._on_gps, sensor_qos
        )
        self.create_subscription(
            VehicleOdometry, args.odometry_topic, self._on_odometry, sensor_qos
        )
        self.create_timer(5.0, self._log_status)

        self._camera_info_signature = None
        self._backend_registered = False
        self._backend_thread = None
        if args.update_backend:
            self._backend_thread = threading.Thread(
                target=self._backend_registration_loop,
                name="backend-video-url",
                daemon=True,
            )
            self._backend_thread.start()

        self._write_mission_summary(completed=False)
        self.get_logger().info(
            f"RTSP target: {args.rtsp_url}; UE page: {args.video_url}"
        )
        self.get_logger().info(f"Mission directory: {self.mission_dir}")
        self.get_logger().info(
            "Read-only ROS subscriptions started; jetson_bridge.py can run concurrently"
        )

    def _write_mission_summary(self, completed: bool) -> None:
        write_json_atomic(
            self.mission_dir / "mission.json",
            {
                "drone_id": str(self.args.drone_id),
                "jetson_ip": self.args.jetson_ip,
                "mediamtx_host": self.args.mediamtx_host,
                "rtsp_url": self.args.rtsp_url,
                "video_url": self.args.video_url,
                "image_topic": self.args.image_topic,
                "camera_info_topic": self.args.camera_info_topic,
                "gps_topic": self.args.gps_topic,
                "odometry_topic": self.args.odometry_topic,
                "fps": self.args.fps,
                "bitrate": self.args.bitrate,
                "record_local": self.args.record_local,
                "completed": completed,
                "frames_received": self.frame_index,
                "metadata_rows": self.metadata_writer.rows_written,
                "stream_queue_drops": self.stream_queue_drops,
                "estimated_source_drops": self.source_drop_estimate,
            },
        )

    def _on_gps(self, message) -> None:
        timestamp_us = int(message.timestamp)
        timestamp_sample_us = int(message.timestamp_sample or timestamp_us)
        sync_timestamp_us = int(self.get_clock().now().nanoseconds // 1000)
        self.gps_cache.add(
            GpsSample(
                timestamp_us=timestamp_us,
                timestamp_sample_us=timestamp_sample_us,
                latitude=float(message.lat),
                longitude=float(message.lon),
                altitude_amsl_m=float(message.alt),
                altitude_ellipsoid_m=float(message.alt_ellipsoid),
                eph_m=float(message.eph),
                epv_m=float(message.epv),
                lat_lon_valid=bool(message.lat_lon_valid),
                alt_valid=bool(message.alt_valid),
                dead_reckoning=bool(message.dead_reckoning),
                sync_timestamp_us=sync_timestamp_us,
            )
        )

    def _on_odometry(self, message) -> None:
        timestamp_us = int(message.timestamp)
        timestamp_sample_us = int(message.timestamp_sample or timestamp_us)
        sync_timestamp_us = int(self.get_clock().now().nanoseconds // 1000)
        self.odom_cache.add(
            OdomSample(
                timestamp_us=timestamp_us,
                timestamp_sample_us=timestamp_sample_us,
                pose_frame=int(message.pose_frame),
                position=tuple(float(value) for value in message.position[:3]),
                quaternion_wxyz=tuple(float(value) for value in message.q[:4]),
                velocity_frame=int(message.velocity_frame),
                velocity=tuple(float(value) for value in message.velocity[:3]),
                sync_timestamp_us=sync_timestamp_us,
            )
        )

    def _on_camera_info(self, message) -> None:
        payload = {
            "timestamp_ns": ros_stamp_to_ns(message.header.stamp),
            "frame_id": message.header.frame_id,
            "width": int(message.width),
            "height": int(message.height),
            "distortion_model": message.distortion_model,
            "d": [float(value) for value in message.d],
            "k": [float(value) for value in message.k],
            "r": [float(value) for value in message.r],
            "p": [float(value) for value in message.p],
        }
        # Acquisition timestamps change on every CameraInfo publication. Only
        # rewrite the calibration file when actual calibration data changes.
        signature = json.dumps(
            {key: value for key, value in payload.items() if key != "timestamp_ns"},
            sort_keys=True,
        )
        if signature != self._camera_info_signature:
            self._camera_info_signature = signature
            write_json_atomic(self.mission_dir / "camera_info.json", payload)
            if not payload["k"] or payload["k"][0] == 0.0:
                self.get_logger().warning(
                    "CameraInfo reports an uncalibrated camera (K[0] == 0)"
                )

    def _on_image(self, message) -> None:
        received_ros_ns = int(self.get_clock().now().nanoseconds)
        image_timestamp_ns = ros_stamp_to_ns(message.header.stamp)
        timestamp_source = "camera_header"
        if image_timestamp_ns <= 0:
            image_timestamp_ns = received_ros_ns
            timestamp_source = "ros_receive_time"

        # Prefer the camera acquisition timestamp when it is clearly in this
        # node's ROS clock domain. Otherwise use callback receipt time. PX4
        # boot-relative timestamps are kept separately and never compared
        # directly with camera timestamps.
        if abs(image_timestamp_ns - received_ros_ns) <= 5_000_000_000:
            sync_timestamp_us = image_timestamp_ns // 1000
            sync_basis = "camera_header_ros_clock"
        else:
            sync_timestamp_us = received_ros_ns // 1000
            sync_basis = "ros_receive_time_clock_mismatch"

        self.frame_index += 1
        gap_ms = None
        estimated_drops = 0
        if self.last_image_timestamp_ns > 0:
            gap_ns = image_timestamp_ns - self.last_image_timestamp_ns
            gap_ms = gap_ns / 1_000_000.0
            expected_ns = 1_000_000_000 / self.args.fps
            if gap_ns > expected_ns * 1.5:
                estimated_drops = max(0, round(gap_ns / expected_ns) - 1)
                self.source_drop_estimate += estimated_drops
        self.last_image_timestamp_ns = image_timestamp_ns

        tracking = FrameTracking(
            frame_index=self.frame_index,
            image_timestamp_ns=image_timestamp_ns,
            timestamp_source=timestamp_source,
            received_monotonic=time.monotonic(),
            width=int(message.width),
            height=int(message.height),
            encoding=str(message.encoding),
            step=int(message.step),
            source_gap_ms=gap_ms,
            estimated_source_drops=estimated_drops,
            sync_timestamp_us=sync_timestamp_us,
            sync_basis=sync_basis,
        )
        packet = FramePacket(tracking=tracking, data=bytes(message.data))
        try:
            self.frame_queue.put_nowait(packet)
        except queue.Full:
            self.stream_queue_drops += 1
            tracking.stream_state = "dropped"
            tracking.stream_error = "stream_queue_full"
            tracking.stream_done.set()
        self.metadata_writer.enqueue(tracking)

    def _stream_loop(self) -> None:
        while True:
            packet = self.frame_queue.get()
            if packet is None:
                break
            tracking = packet.tracking
            accepted, generation, pts, state, error = self.stream_pipeline.push(
                packet
            )
            tracking.stream_accepted = accepted
            tracking.stream_generation = generation
            tracking.stream_pts_ns = pts
            tracking.stream_state = state
            tracking.stream_error = error
            tracking.stream_done.set()
        self.stream_pipeline.stop()

    def _backend_registration_loop(self) -> None:
        while not self.stop_event.is_set() and not self._backend_registered:
            try:
                result = update_backend_video_url(
                    self.args.backend_base_url,
                    str(self.args.backend_drone_id),
                    self.args.video_url,
                )
                self._backend_registered = bool(result.get("updated", False))
                if self._backend_registered:
                    self.get_logger().info(
                        f"Backend video_url updated: {self.args.video_url}"
                    )
                    return
                self.get_logger().warning(
                    f"Backend response did not confirm update: {result}"
                )
            except (urllib.error.URLError, TimeoutError, ValueError) as exc:
                self.get_logger().warning(f"Backend video_url update failed: {exc}")
            self.stop_event.wait(self.args.backend_retry_seconds)

    def _log_status(self) -> None:
        self.get_logger().info(
            f"video frames={self.frame_index} "
            f"queue_drops={self.stream_queue_drops} "
            f"source_drop_estimate={self.source_drop_estimate} "
            f"stream={self.stream_pipeline.state} "
            f"generation={self.stream_pipeline.generation} "
            f"gps_samples={len(self.gps_cache.snapshot())} "
            f"odom_samples={len(self.odom_cache.snapshot())}"
        )

    def shutdown(self) -> None:
        if self.stop_event.is_set():
            return
        self.stop_event.set()
        while True:
            try:
                dropped = self.frame_queue.get_nowait()
                if dropped is not None:
                    dropped.tracking.stream_state = "dropped"
                    dropped.tracking.stream_error = "shutdown_queue_flush"
                    dropped.tracking.stream_done.set()
            except queue.Empty:
                break
        self.frame_queue.put_nowait(None)
        self.stream_thread.join(timeout=8.0)
        if self.stream_thread.is_alive():
            self.get_logger().warning(
                "Stream worker did not stop in 8 seconds; forcing pipeline stop"
            )
            self.stream_pipeline.stop()
            self.stream_thread.join(timeout=2.0)
        self.metadata_writer.close()
        if self._backend_thread is not None:
            self._backend_thread.join(timeout=1.0)
        self._write_mission_summary(completed=True)
        self.get_logger().info("Video pipeline, recorder and metadata writer stopped")


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="ROS2 camera -> Jetson H.264 -> MediaMTX RTSP with SfM metadata"
    )
    parser.add_argument("--drone-id", default="1")
    parser.add_argument("--backend-drone-id", default=None)
    parser.add_argument("--jetson-ip", default="192.168.10.1")
    parser.add_argument("--mediamtx-host", default="192.168.10.30")
    parser.add_argument("--rtsp-port", type=int, default=8554)
    parser.add_argument("--webrtc-port", type=int, default=8889)
    parser.add_argument("--stream-path", default=None)
    parser.add_argument(
        "--topic-prefix",
        default="",
        help="PX4 topic prefix only, for example /px4_1 or /uav_0",
    )
    parser.add_argument("--image-topic", default="/image_raw")
    parser.add_argument("--camera-info-topic", default="/camera_info")
    parser.add_argument(
        "--gps-topic", default="/fmu/out/vehicle_global_position"
    )
    parser.add_argument(
        "--odometry-topic", default="/fmu/out/vehicle_odometry"
    )
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--bitrate", type=int, default=8_000_000)
    parser.add_argument("--keyframe-interval", type=int, default=30)
    parser.add_argument("--reconnect-seconds", type=float, default=2.0)
    parser.add_argument("--frame-queue-size", type=int, default=8)
    parser.add_argument("--record-dir", default="./sfm_captures")
    parser.add_argument(
        "--record-local",
        action=argparse.BooleanOptionalAction,
        default=True,
    )
    parser.add_argument("--segment-minutes", type=float, default=10.0)
    parser.add_argument("--sync-wait-ms", type=float, default=200.0)
    parser.add_argument("--max-gps-age-ms", type=float, default=1000.0)
    parser.add_argument("--max-odom-age-ms", type=float, default=100.0)
    parser.add_argument(
        "--backend-base-url", default="http://192.168.10.30:8080"
    )
    parser.add_argument(
        "--update-backend",
        action=argparse.BooleanOptionalAction,
        default=True,
    )
    parser.add_argument("--backend-retry-seconds", type=float, default=5.0)
    parser.add_argument(
        "--log-level", choices=("DEBUG", "INFO", "WARNING", "ERROR"), default="INFO"
    )
    return parser


def finalize_arguments(args: argparse.Namespace) -> argparse.Namespace:
    if args.fps <= 0:
        raise ValueError("fps must be positive")
    if args.bitrate <= 0:
        raise ValueError("bitrate must be positive")
    if args.keyframe_interval <= 0:
        raise ValueError("keyframe interval must be positive")
    if args.frame_queue_size <= 0:
        raise ValueError("frame queue size must be positive")
    stream_path = args.stream_path or f"drone-{args.drone_id}"
    args.stream_path = sanitize_stream_path(stream_path)
    args.backend_drone_id = args.backend_drone_id or args.drone_id
    prefix = normalize_topic_prefix(args.topic_prefix)
    if prefix:
        args.gps_topic = prefixed_topic(prefix, args.gps_topic)
        args.odometry_topic = prefixed_topic(prefix, args.odometry_topic)
    args.rtsp_url = (
        f"rtsp://{args.mediamtx_host}:{args.rtsp_port}/{args.stream_path}"
    )
    args.video_url = (
        f"http://{args.mediamtx_host}:{args.webrtc_port}/{args.stream_path}"
    )
    return args


def validate_runtime() -> None:
    if rclpy is None:
        raise RuntimeError(
            "ROS2 Python packages are unavailable; source /opt/ros/humble/setup.bash "
            "and the PX4 workspace install/setup.bash"
        )
    if Gst is None:
        raise RuntimeError(
            "GStreamer Python bindings are unavailable; install python3-gi and "
            "gir1.2-gstreamer-1.0"
        )


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_argument_parser()
    try:
        args = finalize_arguments(parser.parse_args(argv))
        logging.basicConfig(
            level=getattr(logging, args.log_level),
            format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        )
        validate_runtime()
        Gst.init(None)
        # Custom application arguments were already consumed by argparse.
        rclpy.init(args=[])
        node = JetsonVideoStreamNode(args)
        try:
            rclpy.spin(node)
        except KeyboardInterrupt:
            pass
        finally:
            node.shutdown()
            node.destroy_node()
            if rclpy.ok():
                rclpy.shutdown()
        return 0
    except Exception as exc:
        LOGGER.error("Fatal error: %s", exc)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
