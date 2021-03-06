#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include "A_star_ros.h"

#include <pluginlib/class_list_macros.h>
//register this planner as a BaseGlobalPlanner plugin
PLUGINLIB_EXPORT_CLASS(AstarPlannerROS, nav_core::BaseGlobalPlanner)

int value;
int mapSize;
bool* OGM;
int width_global;
int height_global;
int goal_global;
cells::cells() {
}
cells::cells(cells * parent, int x, float g, float h)
{
this->parent=parent;
this->x=x;
this->g=g;
this->h=h;
//f=this->g+this->h;
}




//Default Constructor
AstarPlannerROS::AstarPlannerROS()
{

}
AstarPlannerROS::AstarPlannerROS(ros::NodeHandle &nh)
{
  ROSNodeHandle = nh;

}

AstarPlannerROS::AstarPlannerROS(std::string name, costmap_2d::Costmap2DROS* costmap_ros)
{
  initialize(name, costmap_ros);
}

void AstarPlannerROS::initialize(std::string name, costmap_2d::Costmap2DROS* costmap_ros)
{

  if (!initialized_)
  {
    costmap_ros_ = costmap_ros;
    costmap_ = costmap_ros_->getCostmap();

    ros::NodeHandle private_nh("~/" + name); //not sure where this is being used

    originX = costmap_->getOriginX();
    originY = costmap_->getOriginY();

	width = costmap_->getSizeInCellsX();
	width_global=width;
	height = costmap_->getSizeInCellsY();
	height_global=height;
	resolution = costmap_->getResolution();
	mapSize = width*height;
    //tBreak = 1+1/(mapSize);
	value =0;


	OGM = new bool [mapSize];
    for (unsigned int iy = 0; iy < costmap_->getSizeInCellsY(); iy++)
    {
      for (unsigned int ix = 0; ix < costmap_->getSizeInCellsX(); ix++)
      {
        unsigned int cost = static_cast<int>(costmap_->getCost(ix, iy));
        //cout<<cost;
        if (cost == 0)
          OGM[iy*width+ix]=true;
        else
          OGM[iy*width+ix]=false;
      }
    }

    ROS_INFO("RAstar planner initialized successfully");
    initialized_ = true;
  }
  else
    ROS_WARN("This planner has already been initialized... doing nothing");
}

bool AstarPlannerROS::makePlan(const geometry_msgs::PoseStamped& start, const geometry_msgs::PoseStamped& goal,
                             std::vector<geometry_msgs::PoseStamped>& plan)
{

  if (!initialized_)
  {
    ROS_ERROR("The planner has not been initialized, please call initialize() to use the planner");
    return false;
  }

  ROS_DEBUG("Got a start: %.2f, %.2f, and a goal: %.2f, %.2f", start.pose.position.x, start.pose.position.y,
            goal.pose.position.x, goal.pose.position.y);

  plan.clear();

  if (goal.header.frame_id != costmap_ros_->getGlobalFrameID())
  {
    ROS_ERROR("This planner as configured will only accept goals in the %s frame, but a goal was sent in the %s frame.",
              costmap_ros_->getGlobalFrameID().c_str(), goal.header.frame_id.c_str());
    return false;
  }

  tf::Stamped < tf::Pose > goal_tf;
  tf::Stamped < tf::Pose > start_tf;

  poseStampedMsgToTF(goal, goal_tf);
  poseStampedMsgToTF(start, start_tf);

  // convert the start and goal positions

  float startX = start.pose.position.x;
  float startY = start.pose.position.y;

  float goalX = goal.pose.position.x;
  float goalY = goal.pose.position.y;

  getCorrdinate(startX, startY);   //for getting the position relative to origin
  getCorrdinate(goalX, goalY);

  int startCell;
  int goalCell;

  if (isCellInsideMap(startX, startY) && isCellInsideMap(goalX, goalY))
  {
    startCell = convertToCellIndex(startX, startY);   // wht is the difference between cell index and coordinate

    goalCell = convertToCellIndex(goalX, goalY);
    goal_global=goalCell;
  }
  else
  {
    ROS_WARN("the start or goal is out of the map");
    return false;
  }

  /////////////////////////////////////////////////////////

  // call global planner

  if (isStartAndGoalCellsValid(startCell, goalCell)){

   std::vector<int> bestPath;
	bestPath.clear();

    bestPath = AstarPlanner(startCell, goalCell);

//if the global planner find a path
    if ( bestPath.size()>0)
    {

// convert the path

      for (int i = 0; i < bestPath.size(); i++)
      {

        float x = 0.0;
        float y = 0.0;

        int index = bestPath[i];

        convertToCoordinate(index, x, y);

        geometry_msgs::PoseStamped pose = goal;

        pose.pose.position.x = x;
        pose.pose.position.y = y;
        pose.pose.position.z = 0.0;

        pose.pose.orientation.x = 0.0;
        pose.pose.orientation.y = 0.0;
        pose.pose.orientation.z = 0.0;
        pose.pose.orientation.w = 1.0;

        plan.push_back(pose);
      }


	float path_length = 0.0;

	std::vector<geometry_msgs::PoseStamped>::iterator it = plan.begin();

	geometry_msgs::PoseStamped last_pose;
	last_pose = *it;
	it++;
	for (; it!=plan.end(); ++it) {
	   path_length += hypot(  (*it).pose.position.x - last_pose.pose.position.x,
		                 (*it).pose.position.y - last_pose.pose.position.y );
	   last_pose = *it;
	}
	std::cout <<"The global path length: "<< path_length<< " meters"<<std::endl;
      //publish the plan

      return true;

    }

    else
    {
      ROS_WARN("The planner failed to find a path, choose other goal position");
      return false;
    }

  }

  else
  {
    ROS_WARN("Not valid start or goal");
    return false;
  }

}
void AstarPlannerROS::getCorrdinate(float& x, float& y)
{

  x = x - originX;
  y = y - originY;

}

int AstarPlannerROS::convertToCellIndex(float x, float y)
{

  int cellIndex;

  float newX = x / resolution;
  float newY = y / resolution;

  cellIndex = getCellIndex(newY, newX);

  return cellIndex;
}

void AstarPlannerROS::convertToCoordinate(int index, float& x, float& y)
{

  x = getCellColID(index) * resolution;

  y = getCellRowID(index) * resolution;

  x = x + originX;
  y = y + originY;

}

bool AstarPlannerROS::isCellInsideMap(float x, float y)
{
  bool valid = true;

  if (x > (width * resolution) || y > (height * resolution))
    valid = false;

  return valid;
}

std::vector<int> AstarPlannerROS::AstarPlanner(int StartCell, int GoalCell) //make this class inherit for cells
{
  cells* node= new cells(NULL,StartCell,0,CostToGoal(StartCell));
  open.push(node);
  open_list[node->x]=node->g+node->h;
  bool status =false;
  std::vector<int> bestpath;
  while(!open.empty() and !status )
  {
  std::vector<cells*> neighbour_list_;
  cells* q = open.top();
  open.pop();
  neighbour_list_ = Neighbours8(q);
  for(int i=0;i<neighbour_list_.size();i++)
  {
  open.push(neighbour_list_[i]);
  }
  for(int i=0;i<neighbour_list_.size();i++)
  {
  if (neighbour_list_[i]->x == GoalCell)
  {   status=true;
   bestpath=ConstructPath(neighbour_list_[i]);
      break;}
  if (open_list.find(neighbour_list_[i]->x) != open_list.end()){
  if (open_list[neighbour_list_[i]->x] < neighbour_list_[i]->g+neighbour_list_[i]->h)
  continue;
  }
  if(close_list.find(neighbour_list_[i]->x) != close_list.end()){
 if (close_list[neighbour_list_[i]->x] < neighbour_list_[i]->g+neighbour_list_[i]->h)
  continue;
 }
    open.push(neighbour_list_[i]);
    open_list[neighbour_list_[i]->x] = neighbour_list_[i]->g+neighbour_list_[i]->h;

  }
  close_list[q->x]=q->g+q->h;
  }

//else
   // std::cout<<"path not found";

}
//void CheckForClosedList(std::vector<cells*> & Closed , cells * Obj )



std::vector<int>AstarPlannerROS::ConstructPath(cells * FinalCell)
{
    std::vector<int> bestpath;
    while(FinalCell->x!=NULL)
    {
        bestpath.push_back(FinalCell->x);
        FinalCell=FinalCell->parent;

    }
return bestpath;
}

float AstarPlannerROS::CostToGoal(int StartCell)
{
int GoalCell=goal_global;
int x_n=getCellRowID(StartCell);
int y_n=getCellColID(StartCell);
int x_g=getCellRowID(GoalCell);
int y_g=getCellColID(GoalCell);

float a = (x_g-x_n)^2-(y_g-y_n)^2;
return sqrt(a);
}
std::vector<cells*> AstarPlannerROS :: Neighbours8(cells *q)
{ std::vector<cells*> output;
int x_1=getCellRowID(q->x);
int y_1=getCellColID(q->x);
int x_neigh,y_neigh;
for (int i=0;i<2;i++)
{
for (int j=0;j<2;j++)
{ x_neigh=x_1+i;
  y_neigh=y_1+j;
  int ind=getCellIndex(x_neigh,y_neigh);
  if (x_neigh< width_global and y_neigh < height_global and OGM[ind])
  { cells* node = new  cells(q,getCellIndex(x_neigh,y_neigh),(q->g)+1,CostToGoal(getCellIndex(x_neigh,y_neigh)));
  output.push_back(node);
  }
}
}

for (int i=1;i> -2; i=i -2)
{
 for (int j=1;j> -2; j=j -2)
{
x_neigh=x_1+i;
  y_neigh=y_1+j;
  int ind= getCellIndex(x_neigh,y_neigh);
  if (x_neigh< width_global and y_neigh < height_global and OGM[ind]) //see if OGM is true for free space
  { cells* node = new  cells(q,getCellIndex(x_neigh,y_neigh),q->g+1.414,CostToGoal(getCellIndex(x_neigh,y_neigh)));
  output.push_back(node);
  }
}
}
return output;
}

