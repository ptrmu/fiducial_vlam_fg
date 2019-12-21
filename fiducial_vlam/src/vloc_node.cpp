
#include <iomanip>

#include "rclcpp/rclcpp.hpp"

#include "fiducial_math.hpp"
#include "map.hpp"
#include "observation.hpp"
#include "vloc_context.hpp"

#include "cv_bridge/cv_bridge.h"
#include "nav_msgs/msg/odometry.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.h"
#include "tf2_msgs/msg/tf_message.hpp"

namespace fiducial_vlam
{

  static void annotate_image_with_marker_axes(
    std::shared_ptr<cv_bridge::CvImage> &color_marked,
    const TransformWithCovariance &t_map_camera,
    const std::vector<TransformWithCovariance> &t_map_markers,
    const CameraInfo & camera_info,
    FiducialMath &fm)
  {
    // Annotate the image by drawing axes on each marker that was used for the location
    // calculation. This calculation uses the average t_map_camera and the t_map_markers
    // to figure out where the axes should be. This is different from the t_camera_marker
    // that was solved for above.

    // Cache a transform.
    auto tf_t_camera_map = t_map_camera.transform().inverse();

    // Loop through the ids of the markers visible in this image
    for (int i = 0; i < t_map_markers.size(); i += 1) {
      auto &t_map_marker = t_map_markers[i];

      if (t_map_marker.is_valid()) {
        // Calculalte t_camera_marker and draw the axis.
        auto t_camera_marker = TransformWithCovariance(tf_t_camera_map * t_map_marker.transform());
        fm.annotate_image_with_marker_axis(color_marked, t_camera_marker, camera_info);
      }
    }
  }

  static std::vector<TransformWithCovariance> markers_t_map_cameras(
    const Observations &observations,
    const CameraInfo &camera_info,
    Map &map,
    FiducialMath &fm)
  {
    std::vector<TransformWithCovariance> t_map_cameras;

    for (auto &observation : observations.observations()) {
      Observations single_observation{};
      single_observation.add(observation);
      auto t_map_camera = fm.solve_t_map_camera(single_observation, camera_info, map);
      if (t_map_camera.is_valid()) {
        t_map_cameras.emplace_back(t_map_camera);
      }
    }

    return t_map_cameras;
  }


// ==============================================================================
// VlocNode class
// ==============================================================================

  class VlocNode : public rclcpp::Node
  {
    VlocContext cxt_{};
    FiducialMathContext fm_cxt_{};
    FiducialMath fm_;
    std::unique_ptr<Map> map_{};
    std::unique_ptr<CameraInfo> camera_info_{};
    std::unique_ptr<sensor_msgs::msg::CameraInfo> camera_info_msg_{};
    std_msgs::msg::Header::_stamp_type last_image_stamp_{};

    rclcpp::Publisher<fiducial_vlam_msgs::msg::Observations>::SharedPtr observations_pub_{};
    rclcpp::Publisher<geometry_msgs::msg::PoseWithCovarianceStamped>::SharedPtr camera_pose_pub_{};
    rclcpp::Publisher<geometry_msgs::msg::PoseWithCovarianceStamped>::SharedPtr base_pose_pub_{};
    rclcpp::Publisher<tf2_msgs::msg::TFMessage>::SharedPtr tf_message_pub_{};
    rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr camera_odometry_pub_{};
    rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr base_odometry_pub_{};
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_marked_pub_{};

    rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr camera_info_sub_;
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_raw_sub_;
    rclcpp::Subscription<fiducial_vlam_msgs::msg::Map>::SharedPtr map_sub_;

    void validate_parameters()
    {
      cxt_.t_camera_base_ = TransformWithCovariance(TransformWithCovariance::mu_type{
        cxt_.t_camera_base_x_, cxt_.t_camera_base_y_, cxt_.t_camera_base_z_,
        cxt_.t_camera_base_roll_, cxt_.t_camera_base_pitch_, cxt_.t_camera_base_yaw_});
    }

    void load_parameters()
    {
#undef CXT_MACRO_MEMBER
#define CXT_MACRO_MEMBER(n, t, d) CXT_MACRO_LOAD_PARAMETER((*this), cxt_, n, t, d)
      CXT_MACRO_INIT_PARAMETERS(VLOC_ALL_PARAMS, validate_parameters)


#undef CXT_MACRO_MEMBER
#define CXT_MACRO_MEMBER(n, t, d) CXT_MACRO_PARAMETER_CHANGED(cxt_, n, t)
      CXT_MACRO_REGISTER_PARAMETERS_CHANGED((*this), VLOC_ALL_PARAMS, validate_parameters)

      RCLCPP_INFO(get_logger(), "VmapNode Parameters");

#undef CXT_MACRO_MEMBER
#define CXT_MACRO_MEMBER(n, t, d) CXT_MACRO_LOG_PARAMETER(RCLCPP_INFO, get_logger(), cxt_, n, t, d)
      VLOC_ALL_PARAMS
    }

    void validate_fm_parameters()
    {}

    void load_fm_parameters()
    {
#undef CXT_MACRO_MEMBER
#define CXT_MACRO_MEMBER(n, t, d) CXT_MACRO_LOAD_PARAMETER((*this), fm_cxt_, n, t, d)
      CXT_MACRO_INIT_PARAMETERS(FM_ALL_PARAMS, validate_fm_parameters)

#undef CXT_MACRO_MEMBER
#define CXT_MACRO_MEMBER(n, t, d) CXT_MACRO_PARAMETER_CHANGED(fm_cxt_, n, t)
      CXT_MACRO_REGISTER_PARAMETERS_CHANGED((*this), FM_ALL_PARAMS, validate_fm_parameters)

      RCLCPP_INFO(get_logger(), "FiducialMath Parameters");

#undef CXT_MACRO_MEMBER
#define CXT_MACRO_MEMBER(n, t, d) CXT_MACRO_LOG_PARAMETER(RCLCPP_INFO, get_logger(), fm_cxt_, n, t, d)
      FM_ALL_PARAMS
    }


  public:
    VlocNode(const rclcpp::NodeOptions &options) :
      Node("vloc_node", options), fm_(fm_cxt_)
    {
      RCLCPP_INFO(get_logger(), "Using opencv %d.%d.%d", CV_VERSION_MAJOR, CV_VERSION_MINOR, CV_VERSION_REVISION);

      // Get parameters from the command line
      load_parameters();

      // Set up parameter for FiducialMath
      load_fm_parameters();

      // ROS publishers. Initialize after parameters have been loaded.
      observations_pub_ = create_publisher<fiducial_vlam_msgs::msg::Observations>(
        cxt_.fiducial_observations_pub_topic_, 16);

      if (cxt_.publish_camera_pose_) {
        camera_pose_pub_ = create_publisher<geometry_msgs::msg::PoseWithCovarianceStamped>(
          cxt_.camera_pose_pub_topic_, 16);
      }
      if (cxt_.publish_base_pose_) {
        base_pose_pub_ = create_publisher<geometry_msgs::msg::PoseWithCovarianceStamped>(
          cxt_.base_pose_pub_topic_, 16);
      }
      if (cxt_.publish_tfs_) {
        tf_message_pub_ = create_publisher<tf2_msgs::msg::TFMessage>(
          "/tf", 16);
      }
      if (cxt_.publish_camera_odom_) {
        camera_odometry_pub_ = create_publisher<nav_msgs::msg::Odometry>(
          cxt_.camera_odometry_pub_topic_, 16);
      }
      if (cxt_.publish_base_odom_) {
        base_odometry_pub_ = create_publisher<nav_msgs::msg::Odometry>(
          cxt_.base_odometry_pub_topic_, 16);
      }
      if (cxt_.publish_image_marked_) {
        image_marked_pub_ = create_publisher<sensor_msgs::msg::Image>(
          cxt_.image_marked_pub_topic_, 16);
      }

      // ROS subscriptions
      auto camera_info_qos = cxt_.sub_camera_info_best_effort_not_reliable_ ?
                             rclcpp::QoS{rclcpp::SensorDataQoS()} :
                             rclcpp::QoS{rclcpp::ServicesQoS()};
      camera_info_sub_ = create_subscription<sensor_msgs::msg::CameraInfo>(
        cxt_.camera_info_sub_topic_,
        camera_info_qos,
        [this](const sensor_msgs::msg::CameraInfo::UniquePtr msg) -> void
        {
          if (!camera_info_) {
            camera_info_ = std::make_unique<CameraInfo>(*msg);
            // Save the info message because we pass it along with the observations.
            camera_info_msg_ = std::make_unique<sensor_msgs::msg::CameraInfo>(*msg);
          }
        });

      image_raw_sub_ = create_subscription<sensor_msgs::msg::Image>(
        cxt_.image_raw_sub_topic_,
        rclcpp::ServicesQoS(rclcpp::KeepLast(1)),
        [this](sensor_msgs::msg::Image::UniquePtr msg) -> void
        {
#undef SHOW_ADDRESS
#ifdef SHOW_ADDRESS
          static int count = 0;
          RCLCPP_INFO(get_logger(), "%d, %p", count++, reinterpret_cast<std::uintptr_t>(msg.get()));
#endif

          // the stamp to use for all published messages derived from this image message.
          auto stamp{msg->header.stamp};

          if (!camera_info_) {
            RCLCPP_DEBUG(get_logger(), "Ignore image message because no camera_info has been received yet.");

          } else if ((stamp.nanosec == 0l && stamp.sec == 0l) || stamp == last_image_stamp_) {
            RCLCPP_DEBUG(get_logger(), "Ignore image message because stamp is zero or the same as the previous.");

          } else {
            // rviz doesn't like it when time goes backward when a bag is played again.
            // The stamp_msgs_with_current_time_ parameter can help this by replacing the
            // image message time with the current time.
            stamp = cxt_.stamp_msgs_with_current_time_ ? builtin_interfaces::msg::Time(now()) : stamp;
            process_image(std::move(msg), stamp);
          }

          last_image_stamp_ = stamp;
        });

      map_sub_ = create_subscription<fiducial_vlam_msgs::msg::Map>(
        cxt_.fiducial_map_sub_topic_,
        16,
        [this](const fiducial_vlam_msgs::msg::Map::UniquePtr msg) -> void
        {
          map_ = std::make_unique<Map>(*msg);
        });

      (void) camera_info_sub_;
      (void) image_raw_sub_;
      (void) map_sub_;
      RCLCPP_INFO(get_logger(), "vloc_node ready");
    }

  private:
    void process_image(sensor_msgs::msg::Image::UniquePtr image_msg, std_msgs::msg::Header::_stamp_type stamp)
    {
      // Convert ROS to OpenCV
      cv_bridge::CvImagePtr gray{cv_bridge::toCvCopy(*image_msg, "mono8")};

      // If we are going to publish an annotated image, make a copy of
      // the original message image. If no annotated image is to be published,
      // then just make an empty image pointer. The routines need to check
      // that the pointer is valid before drawing into it.
      cv_bridge::CvImagePtr color_marked;
      if (cxt_.publish_image_marked_ &&
          count_subscribers(cxt_.image_marked_pub_topic_) > 0) {
        // The toCvShare only makes ConstCvImage because they don't want
        // to modify the original message data. I want to modify the original
        // data so I create another CvImage that is not const and steal the
        // image data.
        std::shared_ptr<void const> tracked_object;
        auto const_color_marked = cv_bridge::toCvShare(*image_msg, tracked_object);
        cv::Mat mat_with_msg_data = const_color_marked->image; // opencv does not copy the image data on assignment
        color_marked = cv_bridge::CvImagePtr{
          new cv_bridge::CvImage{const_color_marked->header,
                                 const_color_marked->encoding,
                                 mat_with_msg_data}};
      }

      // Detect the markers in this image and create a list of
      // observations.
      auto observations = fm_.detect_markers(gray, color_marked);

      // If there is a map, find t_map_marker for each detected
      // marker. The t_map_markers has an entry for each element
      // in observations. If the marker wasn't found in the map, then
      // the t_map_marker entry has is_valid() as false.
      // Debugging hint: If the markers in color_marked are not outlined
      // in green, then they haven't been detected. If the markers in
      // color_marked are outlined but they have no axes drawn, then vmap_node
      // is not running or has not been able to find the starting node.
      if (map_) {
        TransformWithCovariance t_map_camera;

        // Only try to determine the location if markers were detected.
        if (observations.size()) {

//        RCLCPP_INFO(get_logger(), "%i observations", observations.size());
//        for (auto &obs : observations.observations()) {
//          RCLCPP_INFO(get_logger(),
//                      " Marker %i, p0[%8.3f, %8.3f], p1[%8.3f, %8.3f], p2[%8.3f, %8.3f], p3[%8.3f, %8.3f]",
//                      obs.id(),
//                      obs.x0(), obs.y0(), obs.x1(), obs.y1(),
//                      obs.x2(), obs.y2(), obs.x3(), obs.y3()
//          );
//        }

          // Find the camera pose from the observations.
          t_map_camera = fm_.solve_t_map_camera(observations, *camera_info_, *map_);

          if (t_map_camera.is_valid()) {

            // If annotated images have been requested, then add the annotations now.
            if (color_marked) {
              auto t_map_markers = map_->find_t_map_markers(observations);
              annotate_image_with_marker_axes(color_marked, t_map_camera, t_map_markers, *camera_info_, fm_);
            }

            // Find the transform from the base of the robot to the map. Also include the covariance.
            // Note: the covariance values are with respect to the map frame so both t_map_camera and
            // t_map_base have the same covariance.
            TransformWithCovariance t_map_base{
              t_map_camera.transform() * cxt_.t_camera_base_.transform(),
              t_map_camera.cov()};

            // Publish the camera an/or base pose in the map frame
            if (cxt_.publish_camera_pose_) {
              auto pose_msg = to_PoseWithCovarianceStamped_msg(t_map_camera, stamp, cxt_.map_frame_id_);
              // add some fixed variance for now.
              add_fixed_covariance(pose_msg.pose);
              camera_pose_pub_->publish(pose_msg);
            }
            if (cxt_.publish_base_pose_) {
              auto pose_msg = to_PoseWithCovarianceStamped_msg(t_map_base, stamp, cxt_.map_frame_id_);
              // add some fixed variance for now.
              add_fixed_covariance(pose_msg.pose);
              base_pose_pub_->publish(pose_msg);
            }

            // Publish odometry of the camera and/or the base.
            if (cxt_.publish_camera_odom_) {
              auto odom_msg = to_odom_message(stamp, cxt_.camera_frame_id_, t_map_camera);
              add_fixed_covariance(odom_msg.pose);
              camera_odometry_pub_->publish(odom_msg);
            }
            if (cxt_.publish_base_odom_) {
              auto odom_msg = to_odom_message(stamp, cxt_.base_frame_id_, t_map_base);
              add_fixed_covariance(odom_msg.pose);
              base_odometry_pub_->publish(odom_msg);
            }

            // Also publish the camera's tf
            if (cxt_.publish_tfs_) {
              auto tf_message = to_tf_message(stamp, t_map_camera, t_map_base);
              tf_message_pub_->publish(tf_message);
            }

            // if requested, publish the camera tf as determined from each marker.
            if (cxt_.publish_tfs_per_marker_) {
              auto t_map_cameras = markers_t_map_cameras(observations, *camera_info_, *map_, fm_);
              auto tf_message = to_markers_tf_message(stamp, observations, t_map_cameras);
              if (!tf_message.transforms.empty()) {
                tf_message_pub_->publish(tf_message);
              }
            }

            // Publish the observations
            auto observations_msg = observations.to_msg(stamp, image_msg->header.frame_id, *camera_info_msg_);
            observations_pub_->publish(observations_msg);
          }
        }
      }

      // Publish an annotated image if requested. Even if there is no map.
      if (color_marked) {
        // The marking has been happening on the original message.
        // Republish it now.
        image_marked_pub_->publish(std::move(image_msg));
      }
    }

    nav_msgs::msg::Odometry to_odom_message(std_msgs::msg::Header::_stamp_type stamp,
                                            const std::string &child_frame_id,
                                            const TransformWithCovariance &t)
    {
      nav_msgs::msg::Odometry odom_message;

      odom_message.header.stamp = stamp;
      odom_message.header.frame_id = cxt_.map_frame_id_;
      odom_message.child_frame_id = child_frame_id;
      odom_message.pose = to_PoseWithCovariance_msg(t);
      return odom_message;
    }

    tf2_msgs::msg::TFMessage to_tf_message(std_msgs::msg::Header::_stamp_type stamp,
                                           const TransformWithCovariance &t_map_camera,
                                           const TransformWithCovariance &t_map_base)
    {
      tf2_msgs::msg::TFMessage tf_message;

      geometry_msgs::msg::TransformStamped msg;
      msg.header.stamp = stamp;
      msg.header.frame_id = cxt_.map_frame_id_;

      // The camera_frame_id parameter is non-empty to publish the camera tf.
      // The base_frame_id parameter is non-empty to publish the base tf.
      if (!cxt_.camera_frame_id_.empty()) {
        msg.child_frame_id = cxt_.camera_frame_id_;
        msg.transform = tf2::toMsg(t_map_camera.transform());
        tf_message.transforms.emplace_back(msg);
      }
      if (!cxt_.base_frame_id_.empty()) {
        msg.child_frame_id = cxt_.base_frame_id_;
        msg.transform = tf2::toMsg(t_map_base.transform());
        tf_message.transforms.emplace_back(msg);
      }

      return tf_message;
    }

    tf2_msgs::msg::TFMessage to_markers_tf_message(
      std_msgs::msg::Header::_stamp_type stamp,
      const Observations &observations,
      const std::vector<TransformWithCovariance> &t_map_cameras)
    {
      tf2_msgs::msg::TFMessage tf_message;

      geometry_msgs::msg::TransformStamped msg;
      msg.header.stamp = stamp;
      msg.header.frame_id = cxt_.map_frame_id_;

      for (int i = 0; i < observations.size(); i += 1) {
        auto &observation = observations.observations()[i];
        auto &t_map_camera = t_map_cameras[i];

        if (t_map_camera.is_valid()) {

          if (!cxt_.camera_frame_id_.empty()) {
            std::ostringstream oss_child_frame_id;
            oss_child_frame_id << cxt_.camera_frame_id_ << "_m" << std::setfill('0') << std::setw(3)
                               << observation.id();
            msg.child_frame_id = oss_child_frame_id.str();
            msg.transform = tf2::toMsg(t_map_camera.transform());
            tf_message.transforms.emplace_back(msg);
          }
        }
      }

      return tf_message;
    }

    void add_fixed_covariance(geometry_msgs::msg::PoseWithCovariance &pwc)
    {
      return; // don't change covariance.
      // A hack for now.
      // Seeing how rviz2 interprets these values, allows me to confirm which columns represent
      // which variables.
      pwc.covariance[0] = 96e-3; // along fixed x axis
      pwc.covariance[7] = 24e-3; // along fixed y axis
      pwc.covariance[14] = 6e-3; // along fixed z axis
      pwc.covariance[21] = 36e-3; // Not quite sure how rotation works. ??
      pwc.covariance[28] = 12e-3; //
      pwc.covariance[35] = 4e-3; //
    }
  };

  std::shared_ptr<rclcpp::Node> vloc_node_factory(const rclcpp::NodeOptions &options)
  {
    return std::shared_ptr<rclcpp::Node>(new VlocNode(options));
  }
}

#include "rclcpp_components/register_node_macro.hpp"

RCLCPP_COMPONENTS_REGISTER_NODE(fiducial_vlam::VlocNode)
