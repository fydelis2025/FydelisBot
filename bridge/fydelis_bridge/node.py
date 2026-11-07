from __future__ import annotations

import logging
import threading

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from sensor_msgs.msg import JointState
from std_msgs.msg import Bool

from .controller import JointController
from .server import CommandServer

log = logging.getLogger(__name__)


class FydelisBridge(Node):
    def __init__(self) -> None:
        super().__init__("fydelis_bridge")

        self.declare_parameter("listen_host", "127.0.0.1")
        self.declare_parameter("listen_port", 9090)
        self.declare_parameter("publish_rate", 50.0)
        self.declare_parameter("max_joint_velocity", 2.0)
        self.declare_parameter("command_timeout", 1.0)
        self.declare_parameter("idle_timeout", 30.0)
        self.declare_parameter("max_command_rate", 250.0)
        self.declare_parameter("max_clients", 4)
        self.declare_parameter(
            "joint_names",
            ["neck_joint", "right_shoulder_joint", "left_shoulder_joint",
             "right_hip_joint", "left_hip_joint"],
        )
        self.declare_parameter("joint_min", [-1.57, -3.14, -3.14, -1.57, -1.57])
        self.declare_parameter("joint_max", [1.57, 3.14, 3.14, 1.57, 1.57])

        names = list(self.get_parameter("joint_names").value)
        lower = list(self.get_parameter("joint_min").value)
        upper = list(self.get_parameter("joint_max").value)

        self.controller = JointController(
            names=names, lower=lower, upper=upper,
            max_velocity=float(self.get_parameter("max_joint_velocity").value),
            command_timeout=float(self.get_parameter("command_timeout").value),
        )

        qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE)
        self.publisher = self.create_publisher(JointState, "/joint_states", qos)
        self.estop_pub = self.create_publisher(Bool, "~/estop", 10)

        host = self.get_parameter("listen_host").value
        port = int(self.get_parameter("listen_port").value)

        try:
            self.server = CommandServer(
                (host, port), self.controller,
                idle_timeout=float(self.get_parameter("idle_timeout").value),
                max_command_rate=float(self.get_parameter("max_command_rate").value),
                max_clients=int(self.get_parameter("max_clients").value),
            )
        except OSError as exc:
            raise RuntimeError(f"não foi possível abrir {host}:{port} — {exc}") from exc

        self._server_thread = threading.Thread(
            target=self.server.serve_forever, name="fydelis-tcp", daemon=True
        )
        self._server_thread.start()

        self._last_tick_ns = 0
        self._reported_stale = False
        self._last_estop = False

        rate = float(self.get_parameter("publish_rate").value)
        if rate <= 0.0:
            raise ValueError("publish_rate tem de ser > 0")
        self.create_timer(1.0 / rate, self._tick)

        self.get_logger().info(
            f"FydelisBridge à escuta em {host}:{port} | {len(names)} juntas | {rate:.0f} Hz"
        )
        if host not in ("127.0.0.1", "localhost", "::1"):
            self.get_logger().warn(
                f"a escutar em {host} — canal de controlo SEM autenticação. "
                "Use apenas em rede fidedigna ou por túnel SSH."
            )

    # ------------------------------------------------------------------

    def _tick(self) -> None:
        now = self.get_clock().now()
        now_ns = now.nanoseconds
        dt = 0.0 if self._last_tick_ns == 0 else (now_ns - self._last_tick_ns) * 1e-9
        self._last_tick_ns = now_ns
        dt = min(max(dt, 0.0), 0.25)

        estop, stale = self.controller.step(dt)
        positions, velocities = self.controller.snapshot()

        msg = JointState()
        msg.header.stamp = now.to_msg()
        msg.name = list(self.controller.names)
        msg.position = positions
        msg.velocity = velocities
        msg.effort = [0.0] * len(positions)
        self.publisher.publish(msg)

        if stale != self._reported_stale:
            self._reported_stale = stale
            if stale:
                self.get_logger().warn("sem comandos — juntas congeladas (watchdog)")
            else:
                self.get_logger().info("comandos retomados")

        if estop != self._last_estop:
            self._last_estop = estop
            self.estop_pub.publish(Bool(data=estop))

    def destroy_node(self) -> bool:
        if hasattr(self, "server"):
            self.server.shutdown_server()
        return super().destroy_node()


def main(args=None) -> None:
    logging.basicConfig(level=logging.INFO, format="[%(levelname)s] %(name)s: %(message)s")
    rclpy.init(args=args)
    node = None
    try:
        node = FydelisBridge()
        rclpy.spin(node)
    except (KeyboardInterrupt, RuntimeError) as exc:
        if isinstance(exc, RuntimeError):
            logging.error("%s", exc)
    finally:
        if node is not None:
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()

