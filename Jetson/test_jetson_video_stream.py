import dataclasses
import importlib.util
import math
import pathlib
import sys
import unittest


SCRIPT_PATH = pathlib.Path(__file__).with_name("jetson_video_stream.py")
SPEC = importlib.util.spec_from_file_location("jetson_video_stream_under_test", SCRIPT_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class TopicAndArgumentsTest(unittest.TestCase):
    def test_topic_prefix_is_normalized(self):
        self.assertEqual(MODULE.normalize_topic_prefix("px4_1/"), "/px4_1")
        self.assertEqual(
            MODULE.prefixed_topic("/px4_1", "/fmu/out/vehicle_odometry"),
            "/px4_1/fmu/out/vehicle_odometry",
        )

    def test_default_lan_urls_and_camera_topics(self):
        parser = MODULE.build_argument_parser()
        args = MODULE.finalize_arguments(
            parser.parse_args(["--drone-id", "1", "--topic-prefix", "/px4_1"])
        )
        self.assertEqual(args.rtsp_url, "rtsp://192.168.10.30:8554/drone-1")
        self.assertEqual(args.video_url, "http://192.168.10.30:8889/drone-1")
        self.assertEqual(
            args.metadata_endpoint,
            "http://192.168.10.30:8080/api/video-metadata/batch",
        )
        self.assertFalse(args.record_local)
        self.assertFalse(args.local_metadata)
        self.assertTrue(args.upload_metadata)
        self.assertEqual(args.image_topic, "/image_raw")
        self.assertEqual(
            args.gps_topic, "/px4_1/fmu/out/vehicle_global_position"
        )

    def test_stream_path_rejects_shell_characters(self):
        with self.assertRaises(ValueError):
            MODULE.sanitize_stream_path("drone-1;shutdown")


class TimestampMatchingTest(unittest.TestCase):
    @staticmethod
    def _gps(timestamp_us, latitude, longitude, altitude=100.0, valid=True):
        return MODULE.GpsSample(
            timestamp_us=timestamp_us,
            timestamp_sample_us=timestamp_us,
            latitude=latitude,
            longitude=longitude,
            altitude_amsl_m=altitude,
            altitude_ellipsoid_m=altitude + 10.0,
            eph_m=1.0,
            epv_m=2.0,
            lat_lon_valid=valid,
            alt_valid=valid,
            dead_reckoning=False,
        )

    def test_gps_is_interpolated_to_frame_timestamp(self):
        samples = [
            self._gps(1_000_000, 30.0, 120.0, 100.0),
            self._gps(1_100_000, 30.2, 120.4, 104.0),
        ]
        match = MODULE.match_gps(samples, 1_050_000, max_age_us=200_000)
        self.assertIsNotNone(match)
        self.assertTrue(match.interpolated)
        self.assertAlmostEqual(match.sample.latitude, 30.1)
        self.assertAlmostEqual(match.sample.longitude, 120.2)
        self.assertAlmostEqual(match.sample.altitude_amsl_m, 102.0)

    def test_invalid_gps_is_retained_but_not_marked_valid(self):
        sample = self._gps(1_000_000, 0.0, 0.0, valid=False)
        match = MODULE.match_gps([sample], 1_010_000, max_age_us=20_000)
        self.assertIsNotNone(match)
        self.assertFalse(match.sample.lat_lon_valid)

    def test_ros_sync_clock_is_used_instead_of_px4_boot_clock(self):
        before = dataclasses.replace(
            self._gps(1_000_000, 30.0, 120.0),
            sync_timestamp_us=10_000_000,
        )
        after = dataclasses.replace(
            self._gps(1_100_000, 30.2, 120.4),
            sync_timestamp_us=10_100_000,
        )
        match = MODULE.match_gps(
            [before, after], 10_050_000, max_age_us=200_000
        )
        self.assertIsNotNone(match)
        self.assertTrue(match.interpolated)
        self.assertEqual(match.sample.timestamp_sample_us, 1_050_000)
        self.assertEqual(match.sample.sync_timestamp_us, 10_050_000)

    def test_stale_gps_is_not_attached_to_frame(self):
        sample = self._gps(1_000_000, 30.0, 120.0)
        self.assertIsNone(
            MODULE.match_gps([sample], 2_000_000, max_age_us=100_000)
        )

    def test_nearest_odometry_respects_max_age(self):
        odom = MODULE.OdomSample(
            timestamp_us=1_000_000,
            timestamp_sample_us=1_000_000,
            pose_frame=1,
            position=(1.0, 2.0, 3.0),
            quaternion_wxyz=(1.0, 0.0, 0.0, 0.0),
            velocity_frame=1,
            velocity=(0.0, 0.0, 0.0),
        )
        matched, offset_ms = MODULE.match_nearest_odom(
            [odom], 1_004_000, max_age_us=10_000
        )
        self.assertEqual(matched, odom)
        self.assertAlmostEqual(offset_ms, -4.0)
        self.assertEqual(
            MODULE.match_nearest_odom([odom], 1_020_000, max_age_us=10_000),
            (None, None),
        )


class PipelineDescriptionTest(unittest.TestCase):
    def test_pipeline_uses_hardware_encoder_rtsp_tcp_and_local_segments(self):
        config = MODULE.StreamConfig(
            rtsp_url="rtsp://192.168.10.30:8554/drone-1",
            bitrate=8_000_000,
            fps=30,
            keyframe_interval=30,
            reconnect_seconds=2.0,
            record_local=True,
            mission_dir=pathlib.Path("/sfm-test"),
            segment_minutes=10.0,
        )
        pipeline = MODULE.build_pipeline_description(
            config, "RGB", 1920, 1080, generation=3
        )
        self.assertIn("nvv4l2h264enc", pipeline)
        self.assertIn("rtspclientsink", pipeline)
        self.assertIn("protocols=tcp", pipeline)
        self.assertIn("splitmuxsink", pipeline)
        self.assertIn("video_g003_%05d.mkv", pipeline)
        self.assertIn("width=1920,height=1080", pipeline)

    def test_row_padding_is_removed_before_gstreamer(self):
        tracking = MODULE.FrameTracking(
            frame_index=1,
            image_timestamp_ns=1,
            timestamp_source="camera_header",
            received_monotonic=0.0,
            width=2,
            height=2,
            encoding="mono8",
            step=4,
            source_gap_ms=None,
            estimated_source_drops=0,
        )
        packet = MODULE.FramePacket(tracking, b"abXXcdYY")
        self.assertEqual(MODULE.GstStreamPipeline._tight_frame_data(packet), b"abcd")


class MetadataBatchUploaderTest(unittest.TestCase):
    def test_payload_contains_frame_timing_camera_and_server_stream_identity(self):
        uploader = MODULE.MetadataBatchUploader(
            endpoint="http://192.168.10.30:8080/api/video-metadata/batch",
            mission_id="mission_test_drone_1",
            drone_id="1",
            session_info={
                "stream_path": "drone-1",
                "rtsp_url": "rtsp://192.168.10.30:8554/drone-1",
                "video_url": "http://192.168.10.30:8889/drone-1",
            },
            batch_size=90,
            queue_size=100,
            flush_seconds=1.0,
            retry_seconds=2.0,
            request_timeout_seconds=3.0,
        )
        uploader.set_camera_info({"width": 1920, "height": 1080})
        payload = uploader._build_payload(
            [
                {
                    "frame_index": 1,
                    "stream_generation": 1,
                    "stream_pts_ns": 0,
                    "latitude": 30.0,
                }
            ],
            final=False,
        )
        self.assertEqual(payload["mission_id"], "mission_test_drone_1")
        self.assertEqual(payload["drone_id"], "1")
        self.assertEqual(payload["stream_path"], "drone-1")
        self.assertEqual(payload["camera_info"]["width"], 1920)
        self.assertEqual(payload["frames"][0]["stream_generation"], 1)

    def test_metadata_retry_queue_is_bounded(self):
        uploader = MODULE.MetadataBatchUploader(
            endpoint="http://localhost/unused",
            mission_id="mission_test",
            drone_id="1",
            session_info={},
            batch_size=1,
            queue_size=1,
            flush_seconds=1.0,
            retry_seconds=1.0,
            request_timeout_seconds=1.0,
        )
        self.assertTrue(uploader.enqueue({"frame_index": 1}))
        self.assertFalse(uploader.enqueue({"frame_index": 2}))
        self.assertEqual(uploader.snapshot()["dropped_frames"], 1)


if __name__ == "__main__":
    unittest.main()
