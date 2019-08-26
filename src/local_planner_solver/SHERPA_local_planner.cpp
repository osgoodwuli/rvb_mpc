#include "SHERPA_local_planner.h"

using namespace std;

SherpaAckermannPlanner::SherpaAckermannPlanner(const std::string& yaml_file) 
                                              : BaseibvsController(yaml_file) {

  W_.setZero();
  WN_.setZero();
  input_.setZero();
  state_.setZero();
  reference_.setZero();
  referenceN_.setZero();
  acc_W = vel_W_prev = Eigen::Vector2d::Zero();
}

SherpaAckermannPlanner::~SherpaAckermannPlanner(){}

bool SherpaAckermannPlanner::setCommandPose(const nav_msgs::Odometry odom_msg){

  eigenOdometryFromMsg(odom_msg, &trajectory_point);  

  std::cerr << FBLU("Short Term Final State Set to: ") << trajectory_point.position_W.transpose() << 
               " " << trajectory_point.orientation_W_B.w() << " " << trajectory_point.orientation_W_B.vec().transpose() << "\n";  
        
  return true;
}

void SherpaAckermannPlanner::calculateRollPitchYawRateThrustCommands(trajectory_msgs::JointTrajectory& trajectory_pts)
{
    
  Eigen::Vector3d euler_angles;
  odometry.getEulerAngles(&euler_angles);

  /*  NON LINEAR CONTROLLER INITIAL STATE AND CONSTRAINTS  */
  for (size_t i = 0; i < ACADO_N; i++) {
    reference_.block(i, 0, 1, ACADO_NY) << trajectory_point.position_W(0), //0,
                                           trajectory_point.position_W(1), //0,
                                           //0.f, 0.f, 
                                           0.f, 0.f, 0.f, 0.f, 0.f, 0.f;
  }    

  referenceN_ << trajectory_point.position_W(0), 0.f, trajectory_point.position_W(1), 0.f;

  Eigen::Matrix<double, ACADO_NX, 1> x_0;
  x_0 << odometry.position_W(0), odometry.velocity_B(0), odometry.position_W(1), odometry.velocity_B(1), 0.f;

  Eigen::Map<Eigen::Matrix<double, ACADO_NX, 1>>(const_cast<double*>(acadoVariables.x0)) = x_0;
  Eigen::Map<Eigen::Matrix<double, ACADO_NY, ACADO_N>>(const_cast<double*>(acadoVariables.y)) = reference_.transpose();
  Eigen::Map<Eigen::Matrix<double, ACADO_NYN, 1>>(const_cast<double*>(acadoVariables.yN)) = referenceN_.transpose();
  
  acado_preparationStep();
  auto start = chrono::steady_clock::now();

  for(iter = 0; iter < NUM_STEPS; ++iter)
  {
    acado_feedbackStep( );

    if(acado_getKKT() < KKT_THRESHOLD)
      break;

    acado_preparationStep();
  }
  auto end = chrono::steady_clock::now();
  solve_time = chrono::duration_cast<chrono::microseconds>(end - start).count();

  if(verbosity_ == Verbosity_Level::VERBOSE){   
    std::cout << FBLU("Short Term time elapsed: ") << solve_time << "microsecs\n";
    std::cout << FBLU("Short Term N° Iteration Performed: ") << iter << "\n\n"; 
  } else if (verbosity_ == Verbosity_Level::FEMMINA_CAGACAZZI ) {
    std::cout << FBLU("Short Term time of one real-time iteration: ") <<  solve_time << " microsecs\n";
    std::cout << FBLU("Short Term N° Iteration Performed: ") << iter << "\n\n"; 
    std::cout << FBLU("Short Term Differential Variables: ") << "\n";
    printDifferentialVariables();
    std::cout << FBLU("Short Term Control Variables: ") << "\n";
    printControlVariables();
    std::cout << FBLU("Short Term Desired State: ") << "\n";
    std::cout << referenceN_.transpose() << "\n";
  }
                                        
  getTrajectoryVector(trajectory_pts);
  return;

}

void SherpaAckermannPlanner::getTrajectoryVector(trajectory_msgs::JointTrajectory& trajectory_pts){
  for (unsigned int i = 1; i < ACADO_N + 1; ++i) {
    trajectory_pts.points[0].positions[ACADO_NX/2 * i - 2 ] = acadoVariables.x[i*ACADO_NX];
    trajectory_pts.points[0].positions[ACADO_NX/2 * i - 2 + 1] = acadoVariables.x[i*ACADO_NX + 2];
    trajectory_pts.points[0].velocities[ACADO_NX/2 * i - 2] = acadoVariables.x[i*ACADO_NX + 1];
    trajectory_pts.points[0].velocities[ACADO_NX/2 * i - 2 + 1] = acadoVariables.x[i*ACADO_NX + 3];
  }
}

void SherpaAckermannPlanner::printDifferentialVariables(){
  int i, j;
  for (i = 0; i < ACADO_N + 1; ++i)
  {
    for (j = 0; j < ACADO_NX; ++j)
      std::cout << acadoVariables.x[i * ACADO_NX + j] << " ";
    std::cout << "\n";
  }
  std::cout << "\n\n";
}

void SherpaAckermannPlanner::printControlVariables(){
  int i, j;
  for (i = 0; i < ACADO_N; ++i)
  {
    for (j = 0; j < ACADO_NU; ++j)
      std::cout << acadoVariables.u[i * ACADO_NU + j] << " ";
    std::cout << "\n";
  }
  std::cout << "\n\n";
}

bool SherpaAckermannPlanner::InitializeController()
{
  
  acado_initializeSolver();

  W_(0,0) = 100;
  W_(1,1) = 100;
  W_(2,2) = 100;
  W_(3,3) = 100;
  W_(4,4) = 100;
  W_(5,5) = 100;
  W_(6,6) = 0;
  W_(7,7) = 0;
  W_(8,8) = 0;
  W_(9,9) = 0;
  W_(10,10) = 0;
  W_(11,11) = 0;

  WN_(0,0) = 1000;
  WN_(1,1) = 1000;
  WN_(2,2) = 1000;
  WN_(3,3) = 1000;

  
  std::cout << FBLU("Short Term controller W matrix: ") << "\n";    
  std::cout << W_.diagonal().transpose() << "\n" << "\n";
  std::cout << FBLU("Short Term controller WN matrix: ") << "\n";   
  std::cout << WN_.diagonal().transpose() << "\n" << "\n";
  
  Eigen::Map<Eigen::Matrix<double, ACADO_NY, ACADO_NY>>(const_cast<double*>(acadoVariables.W)) = W_.transpose();
  Eigen::Map<Eigen::Matrix<double, ACADO_NYN, ACADO_NYN>>(const_cast<double*>(acadoVariables.WN)) = WN_.transpose();
    
  for (size_t i = 0; i < ACADO_N; ++i) {
    
    acadoVariables.lbAValues[ACADO_NU * i] = 0.25;       // 1st Obstacle min distance
    acadoVariables.lbAValues[ACADO_NU * i + 1] = 0.25;   // 1st Obstacle min distance
    acadoVariables.lbAValues[ACADO_NU * i + 2] = 0.25;   // 1st Obstacle min distance
    acadoVariables.lbAValues[ACADO_NU * i + 3] = 0.25;   // 1st Obstacle min distance
    acadoVariables.lbAValues[ACADO_NU * i + 4] = 0.25;   // 1st Obstacle min distance
    acadoVariables.lbAValues[ACADO_NU * i + 5] = 0.25;   // 1st Obstacle min distance
    acadoVariables.ubAValues[ACADO_NU * i] = 10000;      // 1st Obstacle max distance (needed by the algorithm)
    acadoVariables.ubAValues[ACADO_NU * i + 1] = 10000;  // 1st Obstacle max distance (needed by the algorithm)
    acadoVariables.ubAValues[ACADO_NU * i + 2] = 10000;  // 1st Obstacle max distance (needed by the algorithm)
    acadoVariables.ubAValues[ACADO_NU * i + 3] = 10000;  // 1st Obstacle max distance (needed by the algorithm)
    acadoVariables.ubAValues[ACADO_NU * i + 4] = 10000;  // 1st Obstacle max distance (needed by the algorithm)
    acadoVariables.ubAValues[ACADO_NU * i + 5] = 10000;  // 1st Obstacle max distance (needed by the algorithm)

    acadoVariables.lbValues[ACADO_NU * i] = -0.5;       // min vel_x
    acadoVariables.lbValues[ACADO_NU * i + 1] = -0.5;   // min vel_y
    acadoVariables.ubValues[ACADO_NU * i] = 0.5;        // max vel_x
    acadoVariables.ubValues[ACADO_NU * i + 1] = 0.5;    // max vel_y              

  }

  std::cout << FBLU("Short Term controller Lower Bound Limits: ") << "\n"; 
  std::cout << acadoVariables.lbValues[0] << " " << acadoVariables.lbValues[1] << " " << "\n" << "\n";

  std::cout << FBLU("Short Term controller Upper Bound Limits: ") << "\n"; 
  std::cout << acadoVariables.ubValues[0] << " " << acadoVariables.ubValues[1] << " " << "\n" << "\n";

  for (int i = 0; i < ACADO_N + 1; i++) {
    acado_online_data_.block(i, 0, 1, ACADO_NOD) << 100.f, 100.f,     // 1st Obstacle x-y position
                                                    100.f, 100.f,     // 2st Obstacle x-y position
                                                    100.f, 100.f,     // 3st Obstacle x-y position
                                                    100.f, 100.f,     // 4st Obstacle x-y position
                                                    100.f, 100.f,     // 5st Obstacle x-y position
                                                    100.f, 100.f;     // 6st Obstacle x-y position
  }

  std::cout << FBLU("Short Term controller Online Data matrix: ") << "\n"; 
  std::cout << acado_online_data_.row(0) << "\n" << "\n";

  Eigen::Map<Eigen::Matrix<double, ACADO_NOD, ACADO_N + 1>>(const_cast<double*>(acadoVariables.od)) = acado_online_data_.transpose(); 
  restartSolver();   
}

void SherpaAckermannPlanner::restartSolver(){
  
  // Initialize the states and controls. 
  for (unsigned int i = 0; i < ACADO_NX * (ACADO_N + 1); ++i)  acadoVariables.x[ i ] = 0.0;
  for (unsigned int i = 0; i < ACADO_NU * ACADO_N; ++i)  acadoVariables.u[ i ] = 0.0;

  // Initialize the measurements/reference. 
  for (unsigned int i = 0; i < ACADO_NY * ACADO_N; ++i)  acadoVariables.y[ i ] = 0.0;
  for (unsigned int i = 0; i < ACADO_NYN; ++i)  acadoVariables.yN[ i ] = 0.0;

  #if ACADO_INITIAL_STATE_FIXED
    for (unsigned int i = 0; i < ACADO_NX; ++i) acadoVariables.x0[ i ] = 0.0;
  #endif    

  std::cout << FBLU("NY: ") << ACADO_NY << 
                FBLU(" NYN: ") << ACADO_NYN << 
                FBLU(" NX: ") << ACADO_NX << 
                FBLU(" NU: ") << ACADO_NU << 
                FBLU(" N: ") << ACADO_N << 
                FBLU(" NOD: ") << ACADO_NOD << "\n" << "\n";

}