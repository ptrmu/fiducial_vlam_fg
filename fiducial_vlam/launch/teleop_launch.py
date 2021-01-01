import os
import sys

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import ExecuteProcess
from launch_ros.actions import Node

package_name = 'fiducial_vlam'
package_share_directory = get_package_share_directory(package_name);
package_launch_directory = os.path.join(package_share_directory, 'launch')
package_cfg_directory = os.path.join(package_share_directory, 'cfg')

map_filename = os.path.join(package_cfg_directory, 'fiducial_marker_locations_office.yaml')
rviz_config_filename = os.path.join(package_cfg_directory, package_name + '.rviz')

print ("map_filename: ", map_filename)
print ("rviz_config_filename: ", rviz_config_filename)


tello_ros_args = [{
    'drone_ip': '192.168.0.35',
    'command_port': 38065,
    'drone_port': 8889,
    'data_port': 8890,
    'video_port': 11111
}]

vloc_args = [{
    'use_sim_time': False,  # Use /clock if available
    'psl_publish_tfs': 1,  # Publish drone and camera /tf
    'stamp_msgs_with_current_time': 0,  # Use incoming message time, not now()
    # 'psl_base_frame_id': 'base_link' + suffix,
    'map_init_pose_z': -0.035,
    # 'psl_camera_frame_id': 'camera_link' + suffix,
    'psl_base_odometry_pub_topic': 'filtered_odom',
    'psl_sub_camera_info_best_effort_not_reliable': 1,
    'psl_publish_image_marked': 1,
}]

vmap_args = [{
    'use_sim_time': False,  # Use /clock if available
    'psm_publish_tfs': 1,  # Publish marker /tf
    'map_marker_length': 0.1778,  # Marker length
    'map_load_filename': map_filename,  # Load a pre-built map from disk
}]



def generate_launch_description():

    entities = [
        ExecuteProcess(cmd=['rviz2', '-d', rviz_config_filename], output='screen'),

        Node(package='tello_driver', executable='tello_driver_main', output='screen',
             parameters=tello_ros_args),

        Node(package='fiducial_vlam', executable='vloc_main', output='screen',
             parameters=vloc_args),
        Node(package='fiducial_vlam', executable='vmap_main', output='screen',
             parameters=vmap_args),

    ]

    return LaunchDescription(entities)

