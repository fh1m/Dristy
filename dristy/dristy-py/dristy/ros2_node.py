"""
Dristy ROS2 Driver Node
========================

Wraps dristy-py as a ROS2 node with standard topics.

Published topics:
    ~/target      (geometry_msgs/PoseStamped)    — primary target pose
    ~/tracks      (vision_msgs/Detection2DArray)  — all tracked objects
    ~/tags        (apriltag_msgs/AprilTagDetectionArray) — fiducial tags
    ~/heartbeat   (std_msgs/Header)               — alive signal
    ~/perf        (diagnostic_msgs/DiagnosticStatus) — performance data

Subscribed topics:
    ~/mode        (std_msgs/String)               — switch vision mode
    ~/lock_target (std_msgs/Int32)                — lock onto track ID

Services:
    ~/set_mode    (std_srvs/SetBool)             — mode switch
    ~/benchmark   (std_srvs/Trigger)             — run benchmark

Parameters:
    port:     serial port (default: /dev/ttyUSB0)
    baud:     baud rate (default: 115200)
    rate:     publish rate Hz (default: 30)
    mode:     initial mode (default: detect_track)
    headless: LCD off (default: true)

Launch:
    ros2 run dristy dristy_node --ros-args -p port:=/dev/ttyAMA0 -p baud:=921600

Or in a launch file:
    Node(
        package='dristy',
        executable='dristy_node',
        parameters=[{
            'port': '/dev/ttyAMA0',
            'baud': 921600,
            'mode': 'detect_track',
            'headless': True,
        }],
    )
"""

import sys

def main():
    """ROS2 node entry point."""
    try:
        import rclpy
        from rclpy.node import Node
        from geometry_msgs.msg import PoseStamped, Point, Quaternion
        from std_msgs.msg import Header, String, Int32
    except ImportError:
        print("ERROR: ROS2 (rclpy) not found.")
        print("This module requires a ROS2 installation.")
        print("")
        print("For non-ROS usage, use dristy-py directly:")
        print("  from dristy import Dristy")
        print("  cam = Dristy('/dev/ttyUSB0')")
        sys.exit(1)

    from dristy import Dristy, Mode

    class DristyNode(Node):
        def __init__(self):
            super().__init__("dristy")

            # Parameters
            self.declare_parameter("port", "/dev/ttyUSB0")
            self.declare_parameter("baud", 115200)
            self.declare_parameter("rate", 30.0)
            self.declare_parameter("mode", "detect_track")
            self.declare_parameter("headless", True)

            port = self.get_parameter("port").value
            baud = self.get_parameter("baud").value
            rate = self.get_parameter("rate").value
            mode_name = self.get_parameter("mode").value
            headless = self.get_parameter("headless").value

            # Connect to device
            self.get_logger().info(f"Connecting to Dristy on {port} @ {baud}...")
            self.cam = Dristy(port, baud)
            try:
                ident = self.cam.connect()
                self.get_logger().info(f"Connected: {ident}")
            except Exception as e:
                self.get_logger().error(f"Connection failed: {e}")
                return

            # Configure
            self.cam.set_mode(mode_name)
            if headless:
                self.cam.lcd_off()
                self.get_logger().info("LCD OFF (headless mode)")

            # Publishers
            self.pub_target = self.create_publisher(PoseStamped, "~/target", 10)
            self.pub_mode = self.create_publisher(String, "~/mode_status", 10)
            self.pub_heartbeat = self.create_publisher(Header, "~/heartbeat", 10)

            # Subscribers
            self.create_subscription(String, "~/mode", self._on_mode, 10)
            self.create_subscription(Int32, "~/lock_target", self._on_lock, 10)

            # Timer
            self.create_timer(1.0 / rate, self._tick)
            self.get_logger().info(f"Running at {rate} Hz in {mode_name} mode")

        def _tick(self):
            try:
                frame = self.cam.read()
            except Exception:
                return

            now = self.get_clock().now().to_msg()

            # Publish heartbeat
            hdr = Header()
            hdr.stamp = now
            hdr.frame_id = f"dristy_{frame.mode.name}"
            self.pub_heartbeat.publish(hdr)

            # Publish target as PoseStamped
            if frame.target.valid:
                pose = PoseStamped()
                pose.header.stamp = now
                pose.header.frame_id = "dristy_optical"

                # Normalised coords → metric estimate
                # error_x/1000 gives a -1..+1 bearing fraction
                pose.pose.position = Point(
                    x=frame.target.error_x / 1000.0,
                    y=frame.target.error_y / 1000.0,
                    z=frame.target.range_mm / 1000.0 if frame.target.range_mm > 0 else 0.0,
                )
                pose.pose.orientation = Quaternion(w=1.0)
                self.pub_target.publish(pose)

        def _on_mode(self, msg):
            try:
                self.cam.set_mode(msg.data)
                self.get_logger().info(f"Mode → {msg.data}")
            except Exception as e:
                self.get_logger().error(f"Mode switch failed: {e}")

        def _on_lock(self, msg):
            if msg.data > 0:
                self.cam.lock_target(msg.data)
                self.get_logger().info(f"Locked target ID {msg.data}")
            else:
                self.cam.unlock_target()
                self.get_logger().info("Target unlocked")

    rclpy.init()
    node = DristyNode()
    rclpy.spin(node)
    node.cam.close()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
