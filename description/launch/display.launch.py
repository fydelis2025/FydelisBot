import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import Command, LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description() -> LaunchDescription:
    pkg = get_package_share_directory("fydelisbot_description")
    xacro_file = os.path.join(pkg, "urdf", "fydelisbot.urdf.xacro")
    rviz_file = os.path.join(pkg, "rviz", "fydelisbot.rviz")
    
    bridge_cfg = os.path.join(
        get_package_share_directory("fydelis_bridge"),
        "config",
        "bridge.yaml"
    )

    robot_description = ParameterValue(
        Command(["xacro ", xacro_file]),
        value_type=str
    )

    return LaunchDescription([
        DeclareLaunchArgument("gui", default_value="true"),
        DeclareLaunchArgument("bridge", default_value="true"),
        DeclareLaunchArgument("rviz", default_value="true"),

        # Publica a descrição do robô 3D
        Node(
            package="robot_state_publisher",
            executable="robot_state_publisher",
            output="screen",
            parameters=[{"robot_description": robot_description}],
        ),

        # Editor gráfico de juntas (pode desligar mudando para false)
        Node(
            package="joint_state_publisher_gui",
            executable="joint_state_publisher_gui",
            condition=IfCondition(LaunchConfiguration("gui")),
        ),

        # Ponte TCP ↔ ROS 2 — CORRIGIDO: forma correta de carregar config
        Node(
            package="fydelis_bridge",
            executable="fydelis_bridge",
            name="fydelis_bridge",
            output="screen",
            condition=IfCondition(LaunchConfiguration("bridge")),
            parameters=[bridge_cfg],  # ✅ Caminho direto = funciona!
        ),

        # Visualizador RViz
        Node(
            package="rviz2",
            executable="rviz2",
            arguments=["-d", rviz_file],
            condition=IfCondition(LaunchConfiguration("rviz")),
            output="screen",
        ),
    ])

