/*
 * Author: naktamello
 */
#include <odrive_ros_control/odrive_hardware_interface.h>
#include <boost/format.hpp>
#include <boost/shared_ptr.hpp>
#include <string>

#include <odrive_ros_control/GetAxisError.h>
#include <odrive_ros_control/GetCurrentState.h>
#include <odrive_ros_control/SetRequestedState.h>

namespace odrive_hardware_interface
{

bool ODriveHardwareInterface::init(ros::NodeHandle& root_nh, ros::NodeHandle& robot_hw_nh)
{
  nh_ = std::make_shared<ros::NodeHandle>(robot_hw_nh);
  if (!nh_->getParam("/odrive/hardware_interface/joints", joint_names_))
  {
    ROS_FATAL_STREAM_NAMED("odrive_ros_control", "You must load joint names to '/odrive/hardware_interface/joints' on "
                                                 "param server.");
    ros::shutdown();
  }
  n_dof_ = joint_names_.size();
  joint_position_.assign(n_dof_, 0.0);
  joint_velocity_.assign(n_dof_, 0.0);
  joint_effort_.assign(n_dof_, 0.0);
  joint_position_command_.assign(n_dof_, 0.0);
  joint_velocity_command_.assign(n_dof_, 0.0);
  joint_effort_command_.assign(n_dof_, 0.0);
  // scaled values
  hardware_position_.assign(n_dof_, 0.0);
  hardware_velocity_.assign(n_dof_, 0.0);
  hardware_position_command_.assign(n_dof_, 0.0);
  hardware_velocity_command_.assign(n_dof_, 0.0);
  if (nh_->getParam("/odrive/hardware_interface/multiplier", multiplier_))
  {
    if (multiplier_.size() != joint_names_.size())
    {
      ROS_FATAL_STREAM_NAMED("odrive_ros_control", "multiplier must have same number of elements as joints!");
      ros::shutdown();
    }
    ROS_DEBUG_STREAM("ODriveHardwareInterface setting up multiplier");
    use_multiplier_ = true;
  }

  for (std::size_t i = 0; i < n_dof_; ++i)
  {
    joint_state_interface_.registerHandle(hardware_interface::JointStateHandle(joint_names_[i], &joint_position_[i],
                                                                               &joint_velocity_[i], &joint_effort_[i]));
    posvel_joint_interface_.registerHandle(hardware_interface::PosVelJointHandle(
        joint_state_interface_.getHandle(joint_names_[i]), &joint_position_command_[i], &joint_velocity_command_[i]));
  }

  registerInterface(&joint_state_interface_);
  registerInterface(&posvel_joint_interface_);

  start();
  ROS_INFO_STREAM_NAMED("hardware_interface", "ODriveHardwareInterface loaded");
  return true;
}

void ODriveHardwareInterface::read(const ros::Time& time, const ros::Duration& period)
{
  if (use_multiplier_)
  {
    command_transport_->receive(hardware_position_, hardware_velocity_);
    apply_multiplier(hardware_position_, joint_position_, true);
    apply_multiplier(hardware_velocity_, joint_velocity_, true);
  }
  else
  {
    command_transport_->receive(joint_position_, joint_velocity_);
  }
}

void ODriveHardwareInterface::write(const ros::Time& time, const ros::Duration& period)
{
  if (use_multiplier_)
  {
    apply_multiplier(joint_position_command_, hardware_position_command_, false);
    apply_multiplier(joint_velocity_command_, hardware_velocity_command_, false);
    command_transport_->send(hardware_position_command_, hardware_velocity_command_);
  }
  else
  {
    command_transport_->send(joint_position_command_, joint_velocity_command_);
  }
}

void ODriveHardwareInterface::apply_multiplier(std::vector<double>& src, std::vector<double>& dst, bool divide)
{
  if (!divide)
  {
    std::transform(src.begin(), src.end(), multiplier_.begin(), dst.begin(), std::multiplies<double>());
  }
  else
  {
    std::transform(src.begin(), src.end(), multiplier_.begin(), dst.begin(), std::divides<double>());
  }
}


std::string ODriveHardwareInterface::get_transport_plugin()
{
  std::string interface;
  if (!nh_->getParam(odrive_ros_control::transport::param_prefix + "interface", interface))
  {
    ROS_FATAL_STREAM_NAMED("odrive_ros_control", "you must define the interface type on the param server");
    ros::shutdown();
  };
  std::transform(interface.begin(), interface.end(), interface.begin(), ::tolower);
  if (interface == "uart")
    return "odrive_ros_control/UartTransport";
  else if (interface == "can")
    return "odrive_ros_control/CanTransport";
  else
  {
    ROS_FATAL_STREAM_NAMED("odrive_ros_control", "unknown transport interface: " << interface);
    ros::shutdown();
  }
}

void ODriveHardwareInterface::start()
{
  try
  {
    // TODO robot namespace

    transport_loader_.reset(new pluginlib::ClassLoader<odrive_ros_control::transport::CommandTransport>(
        "odrive_ros_control", "odrive_ros_control::transport::CommandTransport"));
    command_transport_ =
        transport_loader_->createInstance(std::forward<std::string>(ODriveHardwareInterface::get_transport_plugin()));
    command_transport_->init_transport(nh_, "", joint_names_);
    ROS_DEBUG_STREAM("CommandTransport loaded");
  }
  catch (pluginlib::LibraryLoadException& ex)
  {
    ROS_FATAL_STREAM_NAMED("odrive_ros_control", "Failed to load odrive transport plugin: " << ex.what());
    ros::shutdown();
  }
}
}
PLUGINLIB_EXPORT_CLASS(odrive_hardware_interface::ODriveHardwareInterface, hardware_interface::RobotHW)