#ifndef FIDUCIAL_VLAM_OBSERVATION_HPP
#define FIDUCIAL_VLAM_OBSERVATION_HPP

#include <vector>

#include "fiducial_vlam_msgs/msg/observations.hpp"

// todo remove this
//#include "opencv2/calib3d.hpp" // remove this when finished refactoring

namespace fiducial_vlam
{
// ==============================================================================
// Observation class
// ==============================================================================

  class Observation
  {
    // The id of the marker that we observed.
    int id_;

    double x0_, y0_;
    double x1_, y1_;
    double x2_, y2_;
    double x3_, y3_;

    // The 2D pixel coordinates of the corners in the image.
    // The corners need to be in the same order as is returned
    // from cv::aruco::detectMarkers().
//    std::vector<cv::Point2f> corners_f_image_;

  public:
//    // Todo remove this constructor when finished refactoring
//    Observation(int id, const std::vector<cv::Point2f> &corners_image_corner)
//      : id_(id),
//        x0_(corners_image_corner[0].x), y0_(corners_image_corner[0].y),
//        x1_(corners_image_corner[1].x), y1_(corners_image_corner[1].y),
//        x2_(corners_image_corner[2].x), y2_(corners_image_corner[2].y),
//        x3_(corners_image_corner[3].x), y3_(corners_image_corner[3].y)
//    {}

    Observation(int id,
                double x0, double y0,
                double x1, double y1,
                double x2, double y2,
                double x3, double y3)
      : id_(id),
        x0_(x0), y0_(y0),
        x1_(x1), y1_(y1),
        x2_(x2), y2_(y2),
        x3_(x3), y3_(y3)
    {}

    explicit Observation(const fiducial_vlam_msgs::msg::Observation &msg)
      : id_(msg.id),
        x0_(msg.x0), y0_(msg.y0),
        x1_(msg.x1), y1_(msg.y1),
        x2_(msg.x2), y2_(msg.y2),
        x3_(msg.x3), y3_(msg.y3)
    {}

    auto id() const
    { return id_; }

    auto x0() const
    { return x0_; }

    auto x1() const
    { return x1_; }

    auto x2() const
    { return x2_; }

    auto x3() const
    { return x3_; }

    auto y0() const
    { return y0_; }

    auto y1() const
    { return y1_; }

    auto y2() const
    { return y2_; }

    auto y3() const
    { return y3_; }

    // todo remove this
//    std::vector<cv::Point2f> corners_f_image() const
//    {
//      return std::vector<cv::Point2f>{
//        cv::Point2f(x0_, y0_),
//        cv::Point2f(x1_, y1_),
//        cv::Point2f(x2_, y2_),
//        cv::Point2f(x3_, y3_)};
//    };
  };

// ==============================================================================
// Observations class
// ==============================================================================

  class Observations
  {
    // The list of observations
    std::vector<Observation> observations_;

  public:
    Observations() = default;

    // todo remove this
//    Observations(const std::vector<int> &ids, const std::vector<std::vector<cv::Point2f>> &corners);

    explicit Observations(const fiducial_vlam_msgs::msg::Observations &msg);

    auto &observations()
    { return observations_; }

    auto size()
    { return observations_.size(); }

    fiducial_vlam_msgs::msg::Observations to_msg(const std_msgs::msg::Header &header_msg,
                                                 const sensor_msgs::msg::CameraInfo &camera_info_msg);
  };


}
#endif //FIDUCIAL_VLAM_OBSERVATION_HPP
