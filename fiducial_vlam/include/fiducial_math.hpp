#ifndef FIDUCIAL_VLAM_FIDUCIAL_MATH_HPP
#define FIDUCIAL_VLAM_FIDUCIAL_MATH_HPP


#include <array>

#include "observation.hpp"
#include "transform_with_covariance.hpp"

#include "cv_bridge/cv_bridge.h"
#include "sensor_msgs/msg/camera_info.hpp"

namespace fiducial_vlam
{
// ==============================================================================
// CameraInfo class
// ==============================================================================

  class CameraInfo
  {
    class CvCameraInfo;

    std::shared_ptr<CvCameraInfo> cv_;

  public:
    CameraInfo();

    explicit CameraInfo(const sensor_msgs::msg::CameraInfo &camera_info);

    auto &cv() const
    { return cv_; }

    bool is_valid() const
    { return cv_ != nullptr; }
  };

// ==============================================================================
// FiducialMath class
// ==============================================================================

  class FiducialMath
  {
    class CvFiducialMath;

    std::shared_ptr<CvFiducialMath> cv_;

  public:
    FiducialMath(const CameraInfo &camera_info);

    FiducialMath(const sensor_msgs::msg::CameraInfo &camera_info_msg);

    TransformWithCovariance solve_t_camera_marker(const Observation &observation, double marker_length);

    Observations detect_markers(cv_bridge::CvImagePtr &color);

    void annotate_image_with_marker_axis(cv_bridge::CvImagePtr &color, const TransformWithCovariance &t_camera_marker);
  };
}

#endif //FIDUCIAL_VLAM_FIDUCIAL_MATH_HPP