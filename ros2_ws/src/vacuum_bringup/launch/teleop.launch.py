"""期中用：序列埠橋接 + 網頁遙控。
ros2 launch vacuum_bringup teleop.launch.py port:=/dev/ttyACM0 web_port:=8080
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    port = LaunchConfiguration('port')
    web_port = LaunchConfiguration('web_port')
    return LaunchDescription([
        DeclareLaunchArgument('port', default_value='/dev/ttyACM0'),
        DeclareLaunchArgument('web_port', default_value='8080'),
        Node(package='vacuum_bridge', executable='bridge', output='screen',
             parameters=[{'port': port}]),
        Node(package='vacuum_teleop', executable='teleop', output='screen',
             parameters=[{'web_port': web_port}]),
    ])
