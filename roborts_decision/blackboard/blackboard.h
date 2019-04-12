/****************************************************************************
 *  Copyright (C) 2019 RoboMaster.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 ***************************************************************************/
#ifndef ROBORTS_DECISION_BLACKBOARD_H
#define ROBORTS_DECISION_BLACKBOARD_H

#include <actionlib/client/simple_action_client.h>
#include <tf/tf.h>
#include <tf/transform_listener.h>
#include <ros/ros.h>
#include <ros/ros.h>
#include <nav_msgs/Odometry.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Twist.h>

//Chassis
#include "roborts_msgs/TwistAccel.h"

//Gimbal
#include "roborts_msgs/GimbalAngle.h"
#include "roborts_msgs/GimbalRate.h"
#include "roborts_msgs/GimbalMode.h"
#include "roborts_msgs/ShootCmd.h"
#include "roborts_msgs/FricWhl.h"

//Referee System
#include "roborts_msgs/BonusStatus.h"
#include "roborts_msgs/GameResult.h"
#include "roborts_msgs/GameStatus.h"
#include "roborts_msgs/GameSurvivor.h"
#include "roborts_msgs/ProjectileSupply.h"
#include "roborts_msgs/RobotBonus.h"
#include "roborts_msgs/RobotDamage.h"
#include "roborts_msgs/RobotHeat.h"
#include "roborts_msgs/RobotShoot.h"
#include "roborts_msgs/RobotStatus.h"
#include "roborts_msgs/SupplierStatus.h"
#include "roborts_msgs/ArmorDetectionAction.h"

#include "io/io.h"
#include "../proto/decision.pb.h"
#include "costmap/costmap_interface.h"

namespace roborts_decision
{

enum class ArmorAttacked
{
  NONE = 0,
  FRONT = 1,
  LEFT = 2,
  BACK = 4,
  RIGHT = 8
};

enum class RobotDetected
{
  NONE = 0,
  FRONT = 1,
  LEFT = 2,
  BACK = 4,
  RIGHT = 8
};

class Blackboard
{
public:
  typedef std::shared_ptr<Blackboard> Ptr;
  typedef roborts_costmap::CostmapInterface CostMap;
  typedef roborts_costmap::Costmap2D CostMap2D;
  explicit Blackboard(const std::string &proto_file_path) : enemy_detected_(false),
                                                            armor_detection_actionlib_client_("armor_detection_node_action", true)
  {

    tf_ptr_ = std::make_shared<tf::TransformListener>(ros::Duration(10));

    std::string map_path = ros::package::getPath("roborts_costmap") +
                           "/config/costmap_parameter_config_for_decision.prototxt";
    costmap_ptr_ = std::make_shared<CostMap>("decision_costmap", *tf_ptr_,
                                             map_path);
    charmap_ = costmap_ptr_->GetCostMap()->GetCharMap();

    costmap_2d_ = costmap_ptr_->GetLayeredCostmap()->GetCostMap();

    roborts_decision::DecisionConfig decision_config;
    roborts_common::ReadProtoFromTextFile(proto_file_path, &decision_config);

    if (!decision_config.simulate())
    {

      armor_detection_actionlib_client_.waitForServer();

      ROS_INFO("Armor detection module has been connected!");

      armor_detection_goal_.command = 1;
      armor_detection_actionlib_client_.sendGoal(armor_detection_goal_,
                                                 actionlib::SimpleActionClient<roborts_msgs::ArmorDetectionAction>::SimpleDoneCallback(),
                                                 actionlib::SimpleActionClient<roborts_msgs::ArmorDetectionAction>::SimpleActiveCallback(),
                                                 boost::bind(&Blackboard::ArmorDetectionFeedbackCallback, this, _1));

      game_status_sub_ = referee_nh_.subscribe<roborts_msgs::GameStatus>("game_status", 30, &Blackboard::GameStatusCallback, this);
      game_survival_sub_ = referee_nh_.subscribe<roborts_msgs::GameSurvivor>("game_survivor", 30, &Blackboard::GameSurvivorCallback, this);
      bonus_status_sub_ = referee_nh_.subscribe<roborts_msgs::BonusStatus>("field_bonus_status", 30, &Blackboard::BonusStatusCallback, this);
      supplier_status_sub_ = referee_nh_.subscribe<roborts_msgs::SupplierStatus>("field_supplier_status", 30, &Blackboard::SupplierStatusCallback, this);
      robot_status_sub_ = referee_nh_.subscribe<roborts_msgs::RobotStatus>("robot_status", 30, &Blackboard::RobotStatusCallback, this);
      robot_heat_sub_ = referee_nh_.subscribe<roborts_msgs::RobotHeat>("robot_heat", 30, &Blackboard::RobotHeatCallback, this);
      robot_bonus_sub_ = referee_nh_.subscribe<roborts_msgs::RobotBonus>("robot_bonus", 30, &Blackboard::RobotBonusCallback, this);
      robot_damage_sub_ = referee_nh_.subscribe<roborts_msgs::RobotDamage>("robot_damage", 30, &Blackboard::RobotDamageCallback, this);
      robot_shoot_sub_ = referee_nh_.subscribe<roborts_msgs::RobotShoot>("robot_shoot", 30, &Blackboard::RobotShootCallback, this);
    }

    ros::NodeHandle nh;
    robot_pose_pub_ = nh.advertise<geometry_msgs::PoseStamped>("/robot_pose", 2);
  }

  ~Blackboard() = default;

  // 反馈比赛状态数据
  void GameStatusCallback(const roborts_msgs::GameStatusConstPtr &game_status_msg)
  {
    game_status_.game_status = game_status_msg -> game_status;
    game_status_.remaining_time = game_status_msg -> remaining_time;
    //game_status_.PRE_MATCH = game_status_msg -> PRE_MATCH;
    //game_status_.SETUP = game_status_msg -> SETUP;
    //game_status_.INIT = game_status_msg -> INIT;
    //game_status_.FIVE_SEC_CD = game_status_msg -> FIVE_SEC_CD;
    //game_status_.ROUND = game_status_msg -> ROUND;
    //game_status_.CALCULATION = game_status_msg -> CALCULATION;
  }

  // 反馈比赛结果数据
  void GameResultCallback(const roborts_msgs::GameResultConstPtr &game_result_msg)
  {
    game_result_.result = game_status_msg -> result;
    //game_result_.DRAW = game_status_msg -> DRAW;
    //game_result_.RED_WIN = game_status_msg -> RED_WIN;
    //game_result_.BLUE_WIN = game_status_msg -> BLUE_WIN;
  }

  // 反馈场上双方存活机器人状态数据
  void GameSurvivorCallback(const roborts_msgs::GameSurvivorConstPtr &game_survivor_msg)
  {
    game_survival_.red3 = game_survivor_msg -> red3;
    game_survival_.red4 = game_survivor_msg -> red4;
    game_survival_.blue3 = game_survivor_msg -> blue3;
    game_survival_.blue4 = game_survivor_msg -> blue4;
  }

  // 反馈buff区状态数据, 获取buff前应该先判断buff状态
  void BonusStatusCallback(const roborts_msgs::BonusStatusConstPtr &bonus_status_msg)
  {
    //bonus_status_.UNOCCUPIED = bonus_status_msg -> UNOCCUPIED
    //bonus_status_.BEING_OCCUPIED = bonus_status_msg -> BEING_OCCUPIED
    //bonus_status_.OCCUPIED = bonus_status_msg -> OCCUPIED
    bonus_status_.red_bonus = bonus_status_msg -> red_bonus
    bonus_status_.blue_bonus = bonus_status_msg -> blue_bonus
  }

  // 反馈补给站状态, 补给前应该先判断补给站状态
  void SupplierStatusCallback(const roborts_msgs::SupplierStatusConstPtr &supplier_status_msg)
  {
    //supplier_status_.CLOSE = supplier_status_msg -> CLOSE;
    //supplier_status_.PREPARING = supplier_status_msg -> PREPARING;
    //supplier_status_.SUPPLYING = supplier_status_msg -> SUPPLYING;
    supplier_status_.status = supplier_status_msg -> status;
  }

  // 反馈机器人状态
  void RobotStatusCallback(const roborts_msgs::RobotStatusConstPtr &robot_status_msg)
  {
    robot_status_.id = robot_status_msg -> id;
    robot_status_.level = robot_status_msg -> level;
    robot_status_.remain_hp = robot_status_msg -> remain_hp;
    robot_status_.max_hp = robot_status_msg -> max_hp;
    robot_status_.heat_cooling_limit = robot_status_msg -> heat_cooling_limit;
    robot_status_.heat_cooling_rate = robot_status_msg -> heat_cooling_rate;
    robot_status_.gimbal_output = robot_status_msg -> gimbal_output;
    robot_status_.chassis_output = robot_status_msg -> chassis_output;
    robot_status_.shooter_output = robot_status_msg -> shooter_output;
  }

  //反馈射击热量信息
  void RobotHeatCallback(const roborts_msgs::RobotHeatConstPtr &robot_heat_msg)
  {
    robot_heat_.chassis_volt = robot_heat_msg -> chassis_volt;
    robot_heat_.chassis_current = robot_heat_msg -> chassis_current;
    robot_heat_.chassis_power = robot_heat_msg -> chassis_power;
    robot_heat_.chassis_power_buffer = robot_heat_msg -> chassis_power_buffer;
    robot_heat_.shooter_heat = robot_heat_msg -> shooter_heat;
  }

  // 反馈获取buff信息
  void RobotBonusCallback(const roborts_msgs::RobotBonusConstPtr &robot_bonus_msg)
  {
    robot_bonus_.bonus = robot_bonus_msg -> bonus;
    //bonus_aviliable_ = false;
  }

  // 反馈机器人遭受伤害信息
  void RobotDamageCallback(const roborts_msgs::RobotDamageConstPtr &robot_damage_msg)
  {
    robot_damage_.damage_type = robot_damage_msg -> damage_type;
    robot_damage_.damage_source = robot_damage_msg -> damage_source;

    if (int(robot_damage_.damage_type) != 0)
    {
      damage_armor_forward_count = 0;
      damage_armor_backward_count = 0
      damage_armor_left_count = 0;
      damage_armor_right_count = 0;
    }

    switch (int(robot_damage_.damage_source))
    {
    case 0:
      ++damage_armor_forward_count;
      break;
    case 1:
      ++damage_armor_backward_count;
      break;
    case 2:
      ++damage_armor_left_count;
      break;
    case 3:
      ++damage_armor_right_count;
      break;
    }
  }

  //  反馈机器人射击信息
  void RobotShootCallback(const roborts_msgs::RobotShootConstPtr &robot_shoot_msg)
  {
    robot_shoot_.frequency = robot_shoot_msg -> frequency;
    robot_shoot_.speed = robot_shoot_msg -> speed;
  }

  // Enemy
  void ArmorDetectionFeedbackCallback(const roborts_msgs::ArmorDetectionFeedbackConstPtr &feedback)
  {
    if (feedback->detected)
    {
      enemy_detected_ = true;
      ROS_INFO("Find Enemy!");

      tf::Stamped<tf::Pose> tf_pose, global_tf_pose;
      geometry_msgs::PoseStamped camera_pose_msg, global_pose_msg;
      camera_pose_msg = feedback->enemy_pos;

      double distance = std::sqrt(camera_pose_msg.pose.position.x * camera_pose_msg.pose.position.x +
                                  camera_pose_msg.pose.position.y * camera_pose_msg.pose.position.y);
      double yaw = atan(camera_pose_msg.pose.position.y / camera_pose_msg.pose.position.x);

      //camera_pose_msg.pose.position.z=camera_pose_msg.pose.position.z;
      tf::Quaternion quaternion = tf::createQuaternionFromRPY(0,
                                                              0,
                                                              yaw);
      camera_pose_msg.pose.orientation.w = quaternion.w();
      camera_pose_msg.pose.orientation.x = quaternion.x();
      camera_pose_msg.pose.orientation.y = quaternion.y();
      camera_pose_msg.pose.orientation.z = quaternion.z();
      poseStampedMsgToTF(camera_pose_msg, tf_pose);

      tf_pose.stamp_ = ros::Time(0);
      try
      {
        tf_ptr_->transformPose("map", tf_pose, global_tf_pose);
        tf::poseStampedTFToMsg(global_tf_pose, global_pose_msg);

        if (GetDistance(global_pose_msg, enemy_pose_) > 0.2 || GetAngle(global_pose_msg, enemy_pose_) > 0.2)
        {
          enemy_pose_ = global_pose_msg;
        }
      }
      catch (tf::TransformException &ex)
      {
        ROS_ERROR("tf error when transform enemy pose from camera to map");
      }
    }
    else
    {
      enemy_detected_ = false;
    }
  }

  geometry_msgs::PoseStamped GetEnemy() const
  {
    return enemy_pose_;
  }

  bool IsEnemyDetected() const
  {
    ROS_INFO("%s: %d", __FUNCTION__, (int)enemy_detected_);
    return enemy_detected_;
  }

  /*---------------------------------- Tools ------------------------------------------*/

  double GetDistance(const geometry_msgs::PoseStamped &pose1,
                     const geometry_msgs::PoseStamped &pose2)
  {
    const geometry_msgs::Point point1 = pose1.pose.position;
    const geometry_msgs::Point point2 = pose2.pose.position;
    const double dx = point1.x - point2.x;
    const double dy = point1.y - point2.y;
    return std::sqrt(dx * dx + dy * dy);
  }

  double GetAngle(const geometry_msgs::PoseStamped &pose1,
                  const geometry_msgs::PoseStamped &pose2)
  {
    const geometry_msgs::Quaternion quaternion1 = pose1.pose.orientation;
    const geometry_msgs::Quaternion quaternion2 = pose2.pose.orientation;
    tf::Quaternion rot1, rot2;
    tf::quaternionMsgToTF(quaternion1, rot1);
    tf::quaternionMsgToTF(quaternion2, rot2);
    return rot1.angleShortestPath(rot2);
  }

  const geometry_msgs::PoseStamped GetRobotMapPose()
  {
    UpdateRobotPose();
    return robot_map_pose_;
  }

  const std::shared_ptr<CostMap> GetCostMap()
  {
    return costmap_ptr_;
  }

  const CostMap2D *GetCostMap2D()
  {
    return costmap_2d_;
  }

  const unsigned char *GetCharMap()
  {
    return charmap_;
  }

  ArmorAttacked GetArmorAttacked() const
  {
    if (ros::Time::now() - last_armor_attacked__time_ > ros::Duration(0.1))
    {
      return ArmorAttacked::NONE;
    }
    else
    {
      return armor_attacked_;
    }
  }
  RobotDetected GetRobotDetected() const
  {
    if (ros::Time::now() - last_robot_detected_time_ > ros::Duration(0.2))
    {
      return RobotDetected::NONE;
    }
    else
    {
      return robot_detected_;
    }
  }

private:
  void UpdateRobotPose()
  {
    tf::Stamped<tf::Pose> robot_tf_pose;
    robot_tf_pose.setIdentity();

    robot_tf_pose.frame_id_ = "base_link";
    robot_tf_pose.stamp_ = ros::Time();
    try
    {
      geometry_msgs::PoseStamped robot_pose;
      tf::poseStampedTFToMsg(robot_tf_pose, robot_pose);
      tf_ptr_->transformPose("map", robot_pose, robot_map_pose_);
    }
    catch (tf::LookupException &ex)
    {
      ROS_ERROR("Transform Error looking up robot pose: %s", ex.what());
    }
  }
  //! tf
  std::shared_ptr<tf::TransformListener> tf_ptr_;

  //! Enemy info
  actionlib::SimpleActionClient<roborts_msgs::ArmorDetectionAction> armor_detection_actionlib_client_;
  roborts_msgs::ArmorDetectionGoal armor_detection_goal_;
  geometry_msgs::PoseStamped enemy_pose_;
  bool enemy_detected_;

  //! cost map
  std::shared_ptr<CostMap> costmap_ptr_;
  CostMap2D *costmap_2d_;
  unsigned char *charmap_;

  //! robot map pose
  geometry_msgs::PoseStamped robot_map_pose_;

  //! referee handle
  ros::NodeHandle referee_nh_;
  //! referee system subscriber
  ros::Subscriber game_status_sub_;
  ros::Subscriber game_survival_sub_;
  ros::Subscriber robot_hurt_data_sub_;
  ros::Subscriber bonus_status_sub_;
  ros::Subscriber supplier_status_sub_;
  ros::Subscriber robot_status_sub_;
  ros::Subscriber robot_heat_sub_;
  ros::Subscriber robot_bonus_sub_;
  ros::Subscriber robot_damage_sub_;
  ros::Subscriber robot_shoot_sub_;
  
  //! robot self info
  ros::Publisher robot_pose_pub_;
  ros::Publisher enemy_pose_pub_;
  ros::Publisher robot_status_pub_;

  // 保存最近10次受伤的信息,用于判断受伤的装甲板
  std::queue<roborts_msgs::RobotDamageConstPtr> robot_wounded_msg_queue_;
  // 用于返回攻击的装甲板的的位置
  ArmorAttacked armor_attacked_;
  ros::Time last_armor_attacked__time_;

  //保存最近10次发现敌人的信息,用于判断敌人最可能的位置
  // std::queue<roborts_msgs::> robot_detected_msg_queue_;
  RobotDetected robot_detected_;
  ros::Time last_robot_detected_time_;

  // BounsAviliable
  bool bouns_aviliable_;
  
  roborts_msgs::GameStatus game_status_;
  roborts_msgs::GameResult game_result_;
  roborts_msgs::GameSurvivor game_survival_;
  roborts_msgs::BonusStatus bonus_status_;
  roborts_msgs::SupplierStatus supplier_status_;
  roborts_msgs::RobotStatus robot_status_;
  roborts_msgs::RobotHeat robot_heat_;
  roborts_msgs::RobotBonus robot_bonus_;
  roborts_msgs::RobotDamage robot_damage_;
  roborts_msgs::RobotShoot robot_shoot_;

  int damage_armor_forward_count = 0;
  int damage_armor_backward_count = 0;
  int damage_armor_left_count = 0;
  int damage_armor_right_count = 0;
};
} //namespace roborts_decision
#endif //ROBORTS_DECISION_BLACKBOARD_H
