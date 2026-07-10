import ast
import json
import math
import pathlib
import threading
import time
import unittest


SCRIPT_PATH = pathlib.Path(__file__).with_name("jetson_bridge.py")


class _Logger:
    def info(self, _message):
        pass

    def warning(self, _message):
        pass

    def error(self, _message):
        pass


def _load_protocol_code():
    """加载纯协议代码，避免测试机必须安装 ROS2/px4_msgs。"""
    tree = ast.parse(SCRIPT_PATH.read_text(encoding="utf-8"))
    parser = next(
        node for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == "parse_control_packet"
    )
    bridge = next(
        node for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == "JetsonBridge"
    )
    method_names = {
        "_accept_backend_session",
        "_control_fingerprint",
        "_stage_control_command",
        "_apply_control_command",
        "_prune_applied_commands",
    }
    gate_class = ast.ClassDef(
        name="ProtocolGate",
        bases=[],
        keywords=[],
        decorator_list=[],
        body=[
            node for node in bridge.body
            if isinstance(node, ast.FunctionDef) and node.name in method_names
        ],
    )
    namespace = {
        "json": json,
        "math": math,
        "time": time,
        "MAX_CONTROL_PACKET_BYTES": 4096,
        "MAX_ABS_TARGET_M": 5000.0,
        "CONTROL_PROTOCOL": "ue5_drone_control",
        "CONTROL_PROTOCOL_VERSION": 1,
        "COMMAND_CONFIRM_COUNT": 3,
        "COMMAND_CONFIRM_WINDOW_SEC": 2.5,
    }
    module = ast.Module(body=[parser, gate_class], type_ignores=[])
    module = ast.fix_missing_locations(module)
    exec(compile(module, str(SCRIPT_PATH), "exec"), namespace)
    return namespace["parse_control_packet"], namespace["ProtocolGate"]


parse_control_packet, ProtocolGate = _load_protocol_code()


def _message(
    repeat_index=1,
    sequence=100,
    command_id="cmd-100",
    session_id="backend-test",
):
    return {
        "protocol": "ue5_drone_control",
        "version": 1,
        "type": "control",
        "session_id": session_id,
        "command_id": command_id,
        "sequence": sequence,
        "drone_id": 1,
        "slot": 1,
        "mode": "move",
        "issued_at_unix_s": 1000.0,
        "sent_at_unix_s": 1000.1,
        "target": {
            "frame": "NED",
            "reference": "power_on_origin",
            "unit": "m",
            "north": 10.0,
            "east": 20.0,
            "down": -5.0,
        },
        "delivery": {"repeat_index": repeat_index, "repeat_total": 5},
    }


def _new_gate():
    gate = ProtocolGate()
    gate._active_backend_session = None
    gate._retired_backend_sessions = set()
    gate._highest_applied_sequence = 0
    gate._pending_commands = {}
    gate._applied_command_ids = {}
    gate._commands_applied = 0
    gate._last_applied_command = None
    gate._udp_rx_duplicate = 0
    gate._udp_rx_stale = 0
    gate._udp_rx_invalid = 0
    gate._last_setpoint = {
        "x": 0.0, "y": 0.0, "z": 0.0,
        "mode": "hold", "sequence": 0,
    }
    gate._setpoint_lock = threading.Lock()
    gate.get_logger = lambda: _Logger()
    return gate


class ControlProtocolTest(unittest.TestCase):
    def test_parser_accepts_explicit_power_on_relative_ned_meters(self):
        parsed = parse_control_packet(json.dumps(_message()).encode("utf-8"))
        self.assertEqual(parsed["x"], 10.0)
        self.assertEqual(parsed["y"], 20.0)
        self.assertEqual(parsed["z"], -5.0)

    def test_parser_rejects_wrong_coordinate_unit(self):
        message = _message()
        message["target"]["unit"] = "cm"
        with self.assertRaises(ValueError):
            parse_control_packet(json.dumps(message).encode("utf-8"))

    def test_three_unique_repeats_apply_once(self):
        gate = _new_gate()
        sender = ("192.168.30.100", 50123)
        for index in (1, 2):
            packet = parse_control_packet(
                json.dumps(_message(repeat_index=index)).encode("utf-8")
            )
            self.assertTrue(gate._accept_backend_session(packet))
            gate._stage_control_command(packet, sender, 10.0 + index * 0.1)
        self.assertEqual(gate._commands_applied, 0)

        packet = parse_control_packet(
            json.dumps(_message(repeat_index=3)).encode("utf-8")
        )
        gate._stage_control_command(packet, sender, 10.3)
        self.assertEqual(gate._commands_applied, 1)
        self.assertEqual(gate._last_setpoint["sequence"], 100)
        self.assertEqual(gate._last_setpoint["z"], -5.0)

    def test_duplicate_repeat_index_does_not_reach_threshold(self):
        gate = _new_gate()
        sender = ("192.168.30.100", 50123)
        for index in (1, 1, 2):
            packet = parse_control_packet(
                json.dumps(_message(repeat_index=index)).encode("utf-8")
            )
            gate._accept_backend_session(packet)
            gate._stage_control_command(packet, sender, 20.0 + index * 0.1)
        self.assertEqual(gate._commands_applied, 0)
        self.assertEqual(gate._udp_rx_duplicate, 1)

    def test_older_sequence_is_rejected_after_newer_command(self):
        gate = _new_gate()
        sender = ("192.168.30.100", 50123)
        for index in (1, 2, 3):
            packet = parse_control_packet(json.dumps(_message(
                repeat_index=index, sequence=101, command_id="cmd-101"
            )).encode("utf-8"))
            gate._accept_backend_session(packet)
            gate._stage_control_command(packet, sender, 30.0 + index * 0.1)
        old = parse_control_packet(json.dumps(_message(
            repeat_index=4, sequence=100, command_id="cmd-100"
        )).encode("utf-8"))
        gate._stage_control_command(old, sender, 31.0)
        self.assertEqual(gate._commands_applied, 1)
        self.assertEqual(gate._udp_rx_stale, 1)

    def test_same_command_id_with_conflicting_target_is_discarded(self):
        gate = _new_gate()
        sender = ("192.168.30.100", 50123)
        first = _message(repeat_index=1)
        conflict = _message(repeat_index=2)
        conflict["target"]["north"] = 999.0

        for message in (first, conflict):
            packet = parse_control_packet(json.dumps(message).encode("utf-8"))
            gate._accept_backend_session(packet)
            gate._stage_control_command(packet, sender, 40.0)

        self.assertEqual(gate._commands_applied, 0)
        self.assertEqual(gate._udp_rx_invalid, 1)
        self.assertNotIn("cmd-100", gate._pending_commands)

    def test_new_session_retires_old_session_and_restarts_ordering(self):
        gate = _new_gate()
        sender = ("192.168.30.100", 50123)

        old = parse_control_packet(json.dumps(_message(
            repeat_index=1,
            sequence=500,
            command_id="old-500",
            session_id="backend-old",
        )).encode("utf-8"))
        self.assertTrue(gate._accept_backend_session(old))
        gate._stage_control_command(old, sender, 50.0)

        for index in (1, 2, 3):
            new = parse_control_packet(json.dumps(_message(
                repeat_index=index,
                sequence=1,
                command_id="new-1",
                session_id="backend-new",
            )).encode("utf-8"))
            self.assertTrue(gate._accept_backend_session(new))
            gate._stage_control_command(new, sender, 50.0 + index * 0.1)

        self.assertEqual(gate._commands_applied, 1)
        self.assertEqual(gate._highest_applied_sequence, 1)
        self.assertFalse(gate._accept_backend_session(old))
        self.assertEqual(gate._udp_rx_stale, 1)


if __name__ == "__main__":
    unittest.main()
