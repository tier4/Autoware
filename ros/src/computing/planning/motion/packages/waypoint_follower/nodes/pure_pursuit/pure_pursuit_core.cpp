/*
 * Copyright 2015-2019 Autoware Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <pure_pursuit/pure_pursuit_core.h>

namespace waypoint_follower
{
// Constructor
PurePursuitNode::PurePursuitNode()
  : private_nh_("~")
  , pp_()
  , LOOP_RATE_(30)
  , is_waypoint_set_(false)
  , is_pose_set_(false)
  , is_velocity_set_(false)
  , is_config_set_(false)
  , current_linear_velocity_(0)
  , command_linear_velocity_(0)
  , direction_(1)
  , param_flag_(-1)
  , const_lookahead_distance_(4.0)
  , const_velocity_(5.0)
  , lookahead_distance_ratio_(2.0)
  , minimum_lookahead_distance_(6.0)
{
  initForROS();
  node_status_publisher_ptr_ = std::make_shared<autoware_health_checker::NodeStatusPublisher>(nh_,private_nh_);
  node_status_publisher_ptr_->ENABLE();
  // initialize for PurePursuit
  pp_.setLinearInterpolationParameter(is_linear_interpolation_);
}

// Destructor
PurePursuitNode::~PurePursuitNode()
{
}

void PurePursuitNode::initForROS()
{
  // ros parameter settings
  private_nh_.param("is_linear_interpolation", is_linear_interpolation_, bool(true));
  // ROS_INFO_STREAM("is_linear_interpolation : " << is_linear_interpolation_);
  private_nh_.param("publishes_for_steering_robot", publishes_for_steering_robot_, bool(false));
  nh_.param("vehicle_info/wheel_base", wheel_base_, double(2.7));
  private_nh_.param("feedforward_ratio", feedforward_ratio_, double(0.0));
  feedforward_ratio_ = std::max(std::min(feedforward_ratio_, 1.0), 0.0);
  private_nh_.param("feedforward_curvature_points_dist", feedforward_curvature_points_dist_, int(2));

  // setup subscriber
  sub1_ = nh_.subscribe("final_waypoints", 10, &PurePursuitNode::callbackFromWayPoints, this);
  sub2_ = nh_.subscribe("current_pose", 10, &PurePursuitNode::callbackFromCurrentPose, this);
  sub3_ = nh_.subscribe("config/waypoint_follower", 10, &PurePursuitNode::callbackFromConfig, this);
  sub4_ = nh_.subscribe("current_velocity", 10, &PurePursuitNode::callbackFromCurrentVelocity, this);

  // setup publisher
  pub1_ = nh_.advertise<geometry_msgs::TwistStamped>("twist_raw", 10);
  pub2_ = nh_.advertise<autoware_msgs::ControlCommandStamped>("ctrl_cmd", 10);
  pub11_ = nh_.advertise<visualization_msgs::Marker>("next_waypoint_mark", 0);
  pub12_ = nh_.advertise<visualization_msgs::Marker>("next_target_mark", 0);
  pub13_ = nh_.advertise<visualization_msgs::Marker>("search_circle_mark", 0);
  pub14_ = nh_.advertise<visualization_msgs::Marker>("line_point_mark", 0);  // debug tool
  pub15_ = nh_.advertise<visualization_msgs::Marker>("trajectory_circle_mark", 0);
  pub16_ = nh_.advertise<std_msgs::Float32>("angular_gravity", 0);
  pub17_ = nh_.advertise<std_msgs::Float32>("deviation_of_current_position", 0);
  pub18_ = private_nh_.advertise<std_msgs::Float32>("fb_curvature", 0);
  pub19_ = private_nh_.advertise<std_msgs::Float32>("ff_curvature", 0);
  pub20_ = private_nh_.advertise<std_msgs::Float32>("total_curvature", 0);
  pub21_ = nh_.advertise<visualization_msgs::Marker>("expanded_waypoints_mark", 0);
  // pub7_ = nh.advertise<std_msgs::Bool>("wf_stat", 0);
}

void PurePursuitNode::run()
{
  ROS_INFO_STREAM("pure pursuit start");
  ros::Rate loop_rate(LOOP_RATE_);
  while (ros::ok())
  {
    ros::spinOnce();

    if (!is_pose_set_ || !is_waypoint_set_ || !is_velocity_set_ || !is_config_set_)
    {
      ROS_WARN("Necessary topics are not subscribed yet ... ");
      loop_rate.sleep();
      continue;
    }

    pp_.setLookaheadDistance(computeLookaheadDistance());
    pp_.setMinimumLookaheadDistance(minimum_lookahead_distance_);

    double kappa_fb = 0.0;
    bool can_get_curvature = pp_.canGetCurvature(&kappa_fb);


    double kappa_ff = 0.0;
    pp_.calculateNearestCurvature(feedforward_curvature_points_dist_, &kappa_ff);

    double kappa = feedforward_ratio_ * kappa_ff + (1.0 - feedforward_ratio_) * kappa_fb;

    publishTwistStamped(can_get_curvature, kappa);
    publishControlCommandStamped(can_get_curvature, kappa);
    node_status_publisher_ptr_->NODE_ACTIVATE();
    node_status_publisher_ptr_->CHECK_RATE("/topic/rate/vehicle/slow",8,5,1,"topic vehicle_cmd publish rate low.");
    // for visualization with Rviz
    pub11_.publish(displayNextWaypoint(pp_.getPoseOfNextWaypoint()));
    pub13_.publish(displaySearchRadius(pp_.getCurrentPose().position, pp_.getLookaheadDistance()));
    pub12_.publish(displayNextTarget(pp_.getPoseOfNextTarget()));
    pub15_.publish(displayTrajectoryCircle(
        waypoint_follower::generateTrajectoryCircle(pp_.getPoseOfNextTarget(), pp_.getCurrentPose())));
    std_msgs::Float32 angular_gravity_msg;
    angular_gravity_msg.data = computeAngularGravity(computeCommandVelocity(), kappa);
    pub16_.publish(angular_gravity_msg);
    pub21_.publish(displayExpandWaypoints(pp_.getCurrentWaypoints(), expand_size_));

    publishDeviationCurrentPosition(pp_.getCurrentPose().position, pp_.getCurrentWaypoints());

    is_pose_set_ = false;
    is_velocity_set_ = false;
    is_waypoint_set_ = false;

    // debug
    std_msgs::Float32 ff_k, fb_k, total_k;
    ff_k.data = kappa_ff;
    fb_k.data = kappa_fb;
    total_k.data = kappa;
    pub18_.publish(fb_k);
    pub19_.publish(ff_k);
    pub20_.publish(total_k);

    loop_rate.sleep();
  }
}

void PurePursuitNode::publishTwistStamped(const bool& can_get_curvature, const double& kappa) const
{
  geometry_msgs::TwistStamped ts;
  ts.header.stamp = ros::Time::now();
  ts.twist.linear.x = can_get_curvature ? computeCommandVelocity() : 0;
  ts.twist.angular.z = can_get_curvature ? kappa * ts.twist.linear.x : 0;
  node_status_publisher_ptr_->CHECK_MAX_VALUE("/value/twist",ts.twist.linear.x,2.2,3.3,4.4,"linear twist_cmd is too high");
  pub1_.publish(ts);
}

void PurePursuitNode::publishControlCommandStamped(const bool& can_get_curvature, const double& kappa) const
{
  if (!publishes_for_steering_robot_)
    return;

  autoware_msgs::ControlCommandStamped ccs;
  ccs.header.stamp = ros::Time::now();
  ccs.cmd.linear_velocity = can_get_curvature ? fabs(computeCommandVelocity()) : 0;
  ccs.cmd.linear_acceleration = can_get_curvature ? computeCommandAccel() : 0;
  ccs.cmd.steering_angle = can_get_curvature ? convertCurvatureToSteeringAngle(wheel_base_, kappa) : 0;

  pub2_.publish(ccs);
}

double PurePursuitNode::computeLookaheadDistance() const
{
  if (param_flag_ == enumToInteger(Mode::dialog))
    return const_lookahead_distance_;

  double maximum_lookahead_distance = current_linear_velocity_ * 10;
  double ld = current_linear_velocity_ * lookahead_distance_ratio_;

  return ld < minimum_lookahead_distance_ ? minimum_lookahead_distance_ :
                                            ld > maximum_lookahead_distance ? maximum_lookahead_distance : ld;
}

double PurePursuitNode::computeCommandVelocity() const
{
  const int sgn = (command_linear_velocity_ < 0) ? -1 : 1;
  if (param_flag_ == enumToInteger(Mode::dialog))
    return sgn * kmph2mps(const_velocity_);

  return command_linear_velocity_;
}

double PurePursuitNode::computeCommandAccel() const
{
  const geometry_msgs::Pose current_pose = pp_.getCurrentPose();
  const geometry_msgs::Pose target_pose = pp_.getCurrentWaypoints().at(1).pose.pose;

  // v^2 - v0^2 = 2ax
  const double x =
      std::hypot(current_pose.position.x - target_pose.position.x, current_pose.position.y - target_pose.position.y);
  const double v0 = current_linear_velocity_;
  const double v = computeCommandVelocity();
  const double a = (v * v - v0 * v0) / (2 * x);
  return a;
}

double PurePursuitNode::computeAngularGravity(double velocity, double kappa) const
{
  const double gravity = 9.80665;
  return (velocity * velocity) / (1.0 / kappa * gravity);
}

void PurePursuitNode::callbackFromConfig(const autoware_config_msgs::ConfigWaypointFollowerConstPtr &config)
{
  param_flag_ = config->param_flag;
  const_lookahead_distance_ = config->lookahead_distance;
  const_velocity_ = config->velocity;
  lookahead_distance_ratio_ = config->lookahead_ratio;
  minimum_lookahead_distance_ = config->minimum_lookahead_distance;
  is_config_set_ = true;
}

void PurePursuitNode::publishDeviationCurrentPosition(const geometry_msgs::Point &point,
                                                      const std::vector<autoware_msgs::Waypoint> &waypoints) const
{
  // Calculate the deviation of current position from the waypoint approximate line

  if (waypoints.size() < 3)
  {
    return;
  }

  double a, b, c;
  double linear_flag_in =
      getLinearEquation(waypoints.at(2).pose.pose.position, waypoints.at(1).pose.pose.position, &a, &b, &c);

  std_msgs::Float32 msg;
  msg.data = getDistanceBetweenLineAndPoint(point, a, b, c);

  pub17_.publish(msg);
}

void PurePursuitNode::callbackFromCurrentPose(const geometry_msgs::PoseStampedConstPtr& msg)
{
  pp_.setCurrentPose(msg);
  is_pose_set_ = true;
}

void PurePursuitNode::callbackFromCurrentVelocity(const geometry_msgs::TwistStampedConstPtr& msg)
{
  current_linear_velocity_ = msg->twist.linear.x;
  pp_.setCurrentVelocity(current_linear_velocity_);
  is_velocity_set_ = true;
}

void PurePursuitNode::callbackFromWayPoints(const autoware_msgs::LaneConstPtr &msg)
{
  command_linear_velocity_ = (!msg->waypoints.empty()) ? msg->waypoints.at(0).twist.twist.linear.x : 0;
  if (msg->waypoints.size() > 2)
  {
    geometry_msgs::Point closest_rlt = calcRelativeCoordinate(
      msg->waypoints.at(2).pose.pose.position, msg->waypoints.at(1).pose.pose);
    direction_ = (closest_rlt.x > 0.0) ? 1 : (closest_rlt.x < 0.0) ? -1 : direction_;
  }
  autoware_msgs::Lane expanded_lane(*msg);
  expand_size_ = -expanded_lane.waypoints.size();
  connectVirtualLastWaypoints(&expanded_lane, direction_);
  expand_size_ += expanded_lane.waypoints.size();
  pp_.setCurrentWaypoints(expanded_lane.waypoints);
  is_waypoint_set_ = true;
}

void PurePursuitNode::connectVirtualLastWaypoints(autoware_msgs::Lane* lane, int direction)
{
  if (lane->waypoints.empty())
  {
    return;
  }
  static double interval = 1.0;
  const geometry_msgs::Pose& pn = lane->waypoints.back().pose.pose;
  if (lane->waypoints.size() > 2)
  {
    const int prev = lane->waypoints.size() - 2;
    const geometry_msgs::Pose& pn_prev = lane->waypoints[prev].pose.pose;
    interval = getPlaneDistance(pn.position, pn_prev.position);
  }

  autoware_msgs::Waypoint virtual_last_waypoint;
  virtual_last_waypoint.pose.pose.orientation = pn.orientation;
  virtual_last_waypoint.twist.twist.linear.x = 0.0;

  geometry_msgs::Point virtual_last_point_rlt;
  for (double dist = minimum_lookahead_distance_; dist > 0.0; dist -= interval)
  {
    virtual_last_point_rlt.x += interval * direction;
    virtual_last_waypoint.pose.pose.position = calcAbsoluteCoordinate(virtual_last_point_rlt, pn);
    lane->waypoints.emplace_back(virtual_last_waypoint);
  }
}

double convertCurvatureToSteeringAngle(const double& wheel_base, const double& kappa)
{
  return atan(wheel_base * kappa);
}

}  // waypoint_follower
