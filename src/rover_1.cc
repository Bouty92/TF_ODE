#include "rover.hh"
#include "ode/box.hh"
#include "ode/wheel.hh"


#define RAD_TO_DEG 57.29577951308232
#define DEG_TO_RAD 0.017453292519943295


using namespace ode;
using namespace Eigen;


namespace robot
{


Rover_1::Rover_1( Environment& env, const Vector3d& pose ) :
				  _robot_speed( 0 ),
				  _steering_rate( 0 ),
				  _boggie_torque( 0 ),
                  _ic_period( 0 ),
                  _ic_clock( 0 ),
                  _ic_activated( true )
{
	// [ Rover's parameters ]

	//#define WHEELS_MAX_TORQUE dInfinity
	//#define WHEELS_MAX_TORQUE 3.38954 // N.m
	#define WHEELS_MAX_TORQUE 25.4 // N.m
	#define WHEELS_MAX_SPEED 7.4351 // rad/s
	//#define WHEELS_TORQUE_SPEED_RATIO 0.45588357923901496
	#define WHEELS_TORQUE_SPEED_RATIO 3.416228430014391
	#define STEERING_SERVOS_K 0.8

	// Stiffness and damping of force-torque sensors:
	//#define FORK_K_LIN 2e4
	//#define FORK_K_ANG 1e3
	//#define FORK_C_LIN 5e2
	//#define FORK_C_ANG 1.
	#define FORK_K_LIN Vector3d( 2e4, 2e4, 1e5 )
	#define FORK_K_ANG Vector3d( 1e3, 1e3, 1e3 )
	#define FORK_C_LIN Vector3d( 5e2, 5e2, 5e2 )
	#define FORK_C_ANG Vector3d( 1., 1., 1. )

	steering_max_vel = 15;
	boggie_max_torque = 20;

	wheelbase = 0.58;
	wheeltrack = 0.61;

	wheel_mass = 1.4 + 1; // Wheel + motor
	wheel_radius[0] = 0.105;
	wheel_radius[1] = 0.105;
	wheel_radius[2] = 0.105;
	wheel_radius[3] = 0.105;
	wheel_width = 0.1;
	wheel_def = 50;

	belly_elev = 0.33;

	front_mass = 4.55 + 2.2; // Body + battery
	front_length = 0.345;
	front_height = 0.225;
	front_width = 0.200;
	front_pos = Vector3d( front_length/2, 0, belly_elev + front_height/2 );

	rear_mass = 5;
	rear_length = 0.312;
	rear_height = 0.177;
	rear_width = 0.126;
	rear_pos = Vector3d( -rear_length/2, 0, belly_elev + rear_height/2 );

	hinge_pos = Vector3d( 0, 0, ( front_pos[2] + rear_pos[2] )/2 );
	steering_angle_max = 45;

	sea_elev = belly_elev + 0.12;
	sea_pos = Vector3d( -wheelbase/2, 0, sea_elev );
	boggie_angle_max = 45;

	boggie_mass = 1;
	boggie_length = 0.1;
	boggie_height = sea_elev - belly_elev;
	boggie_width = 0.1;
	boggie_pos = Vector3d( -wheelbase/2 + 0.035, 0, belly_elev + boggie_height/2 );

	fork_mass = 1;
	fork_length = 0.05;
	fork_height = 0.1;
	fork_width = 0.3;
	fork_elev = belly_elev - fork_height/2;


	robot_max_speed = WHEELS_MAX_SPEED/wheel_radius[0];
	for ( int i = 1 ; i < NBWHEELS ; i++ )
		robot_max_speed = std::min( robot_max_speed, WHEELS_MAX_SPEED*wheel_radius[i] );


	// [ Wheel positions ]

	Vector3d wheel_position[NBWHEELS];
	for ( int i = 0 ; i < NBWHEELS ; i++ )
		wheel_position[i] = Vector3d( ( i/2 ? -1 : 1 )*wheelbase/2, ( i%2 ? -1 : 1 )*wheeltrack/2, wheel_radius[i] );


	// [ Definition of the chassis ]

	_main_body = Object::ptr_t( new Box( env,
							             pose + front_pos,
							             front_mass,
							             front_length, front_width, front_height ) );
	_main_body->set_mesh( "../meshes/front.obj" );
	_bodies.push_back( _main_body );
	//_main_body->set_contact_type( DISABLED );

	ode::Object::ptr_t rear_body = Object::ptr_t( new Box( env,
						                                   pose + rear_pos,
						                                   rear_mass,
						                                   rear_length, rear_width, rear_height ) );
	rear_body->set_mesh( "../meshes/rear.obj" );
	_bodies.push_back( rear_body );
	//rear_body->set_contact_type( DISABLED );

	ode::Object::ptr_t boggie = Object::ptr_t( new Box( env,
							          pose + boggie_pos,
							          boggie_mass,
							          boggie_length, boggie_width, boggie_height ) );
	boggie->set_mesh( "../meshes/sea.obj" );
	_bodies.push_back( boggie );
	//boggie->set_contact_type( DISABLED );

	Vector3d front_fork_pos = pose + Vector3d( wheelbase/2, 0, fork_elev );
	_front_fork = Object::ptr_t( new Box( env,
							              front_fork_pos,
							              fork_mass,
							              fork_length, fork_width, fork_height ) );
	_front_fork->set_mesh( "../meshes/front_fork.obj" );
	_bodies.push_back( _front_fork );
	//_front_fork->set_contact_type( DISABLED );

	Vector3d rear_fork_pos = pose + Vector3d( -wheelbase/2, 0, fork_elev );
	_rear_fork = Object::ptr_t( new Box( env,
							             rear_fork_pos,
							             fork_mass,
							             fork_length, fork_width, fork_height ) );
	_rear_fork->set_mesh( "../meshes/rear_fork.obj" );
	_bodies.push_back( _rear_fork );
	//_rear_fork->set_contact_type( DISABLED );


	// [ Centre hinge joint ]

	Servo::ptr_t centre_hinge_servo( new Servo( env,
					                            *rear_body, *_main_body,
					                            hinge_pos,
					                            Vector3d( 0, 0, 1 ),
					                            STEERING_SERVOS_K,
					                            steering_max_vel*DEG_TO_RAD,
					                            -steering_angle_max*DEG_TO_RAD, steering_angle_max*DEG_TO_RAD ) );
	_servos.push_back( centre_hinge_servo );


	// [ Boggie joint ]

	_boggie_hinge = dJointCreateHinge( env.get_world(), 0 );
	dJointAttach( _boggie_hinge, boggie->get_body(), rear_body->get_body() );
	dJointSetHingeAxis( _boggie_hinge, 1, 0, 0 );
	dJointSetHingeAnchor( _boggie_hinge, sea_pos.x(), sea_pos.y(), sea_pos.z() );
	dJointSetHingeParam( _boggie_hinge, dParamLoStop, -boggie_angle_max*DEG_TO_RAD );
	dJointSetHingeParam( _boggie_hinge, dParamHiStop, boggie_angle_max*DEG_TO_RAD );


	// [ Force-torque sensors ]

	_front_ft_sensor = FT_sensor( _main_body.get(), _front_fork.get(), pose + Vector3d( wheelbase/2, 0, belly_elev ), FORK_K_LIN, FORK_K_ANG, FORK_C_LIN, FORK_C_ANG );
	_rear_ft_sensor = FT_sensor( boggie.get(), _rear_fork.get(), pose + Vector3d( -wheelbase/2, 0, belly_elev ), FORK_K_LIN, FORK_K_ANG, FORK_C_LIN, FORK_C_ANG );


	for ( int i = 0 ; i < NBWHEELS ; i++ )
	{
		// [ Definition of wheels ]

		_wheel[i] = Object::ptr_t( new ode::Wheel( env, pose + wheel_position[i], wheel_mass, wheel_radius[i], wheel_width, wheel_def ) );
		_wheel[i]->set_rotation( M_PI/2, 0, 0 );
		_bodies.push_back( _wheel[i] );
		_wheel[i]->set_contact_type( SOFT );
		_wheel[i]->set_color( 0.2, 0.2, 0.2 );
		

		// [ Definition of wheel motors ]

		Vector3d wheel_joint_pos = pose + wheel_position[i];

		_wheel_joint[i] = dJointCreateHinge( env.get_world(), 0 );
		dJointAttach( _wheel_joint[i], ( wheel_position[i].x() > 0 ? _front_fork->get_body() : _rear_fork->get_body() ), _wheel[i]->get_body() );
		dJointSetHingeAnchor( _wheel_joint[i], wheel_joint_pos.x(), wheel_joint_pos.y(), wheel_joint_pos.z() );
		dJointSetHingeAxis( _wheel_joint[i], 0, -1, 0 );

		dJointSetHingeParam( _wheel_joint[i], dParamFMax, WHEELS_MAX_TORQUE );
		//dJointSetHingeParam( _wheel_joint[i], dParamFMax, 0 );

		//dJointSetFeedback( _wheel_joint[i], &_wheel_feedback[i] );
	}


	// [ Initialisation of filters ]
	
	for ( int i = 0 ; i < NBWHEELS ; i++ )
		_torque_filter[i].init_bilinear( 0.001, 2*M_PI, 0.5, nullptr, _torque_output + i );
}


Vector3d Rover_1::GetPosition() const
{
	dVector3 center_pos;
	dBodyGetRelPointPos( _main_body->get_body(), -front_pos[0], -front_pos[1], -front_pos[2] + wheel_radius[0], center_pos );
	return Vector3d( center_pos[0], center_pos[1], center_pos[2] );
}


double Rover_1::GetDirection() const
{
	dVector3 vec;
	dBodyVectorToWorld( _main_body->get_body(), 1, 0, 0, vec );
	double direction = asin( vec[1] )*RAD_TO_DEG;
	if ( vec[0] < 0 )
		direction = ( vec[1] > 0 ? 1 : -1 )*180 - direction;
	//direction += GetSteeringTrueAngle()/2;
	return direction;
}


bool Rover_1::IsUpsideDown() const
{
	dVector3 vec;
	dBodyVectorToWorld( _main_body->get_body(), 0, 0, 1, vec );
	return ( vec[2] < 0 ? true : false );
}


double Rover_1::GetRollAngle() const
{
	dVector3 vec;
	dBodyVectorToWorld( _main_body->get_body(), 0, 1, 0, vec );
	return asin( vec[2] )*RAD_TO_DEG;
}


double Rover_1::GetPitchAngle() const
{
	dVector3 vec;
	dBodyVectorToWorld( _main_body->get_body(), 1, 0, 0, vec );
	return asin( -vec[2] )*RAD_TO_DEG;
}


void Rover_1::GetTiltRates( double& roll_rate, double& pitch_rate ) const
{
	const dReal* angular_vel = dBodyGetAngularVel( _main_body->get_body() );
	dVector3 vec;
	dBodyVectorFromWorld( _main_body->get_body(), angular_vel[0], angular_vel[1], angular_vel[2], vec );
	roll_rate = vec[0];
	pitch_rate = vec[1];
}


double Rover_1::GetBoggieAngle() const
{
	return dJointGetHingeAngle( _boggie_hinge )*RAD_TO_DEG;
}


void Rover_1::SetRobotSpeed( double speed )
{
	_robot_speed = std::min( std::max( -robot_max_speed, speed ), robot_max_speed );
}


void Rover_1::SetSteeringAngle( double angle )
{
	servos()[0]->set_angle( angle*DEG_TO_RAD )*RAD_TO_DEG;
}


double Rover_1::GetSteeringTrueAngle() const
{
	return servos()[0]->get_real_angle()*RAD_TO_DEG;
}


void Rover_1::SetSteeringRate( double rate )
{
	_steering_rate = std::min( std::max( -steering_max_vel, rate ), steering_max_vel );
}


double Rover_1::GetSteeringTrueRate() const
{
	return servos()[0]->get_real_vel()*RAD_TO_DEG;
}


void Rover_1::SetBoggieTorque( double torque )
{
	_boggie_torque = std::min( std::max( -boggie_max_torque, torque ), boggie_max_torque );
}


// [ Wheel control ]

void Rover_1::_UpdateWheelControl()
{
	double speed = _robot_speed;

	double gamma = servos()[0]->get_real_angle();
	double dgamma_dt = servos()[0]->get_real_vel();

	double diff[NBWHEELS];
	double trans[NBWHEELS];
	double speed_lim = speed;
	for ( int i = 0 ; i < NBWHEELS ; i++ )
	{
		diff[i] = ( i%2 ? -1 : 1 )*wheeltrack/wheelbase*tan( gamma/2 );
		trans[i] = ( i/2 ? -1 : 1 )*( -wheelbase*tan( gamma/2 ) + ( i%2 ? -1 : 1 )*wheeltrack )*dgamma_dt/4;

		if ( speed >= 0 )
			speed_lim = std::min( speed_lim, ( WHEELS_MAX_SPEED*wheel_radius[i] - trans[i] )/( 1 + diff[i] ) );
		else
			speed_lim = std::max( speed_lim, ( -WHEELS_MAX_SPEED*wheel_radius[i] - trans[i] )/( 1 + diff[i] ) );
	}
	for ( int i = 0 ; i < NBWHEELS ; i++ )
		_W[i] = ( speed_lim*( 1 + diff[i] ) + trans[i] )/wheel_radius[i];
}


void Rover_1::_ApplyWheelControl()
{
	for ( int i = 0 ; i < NBWHEELS ; i++ )
	{
		_W[i] = std::min( std::max( -WHEELS_MAX_SPEED, _W[i] ), WHEELS_MAX_SPEED );

		// Limit the torque according to the current wheel speed:
		double max_torque = WHEELS_MAX_TORQUE - WHEELS_TORQUE_SPEED_RATIO*fabs( dJointGetHingeAngleRate( _wheel_joint[i] ) );
		dJointSetHingeParam( _wheel_joint[i], dParamFMax, max_torque );
		dJointSetHingeParam( _wheel_joint[i], dParamVel, _W[i] );
	}
}


void Rover_1::_ApplySteeringControl()
{
	//_steering_rate = std::min( std::max( -steering_max_vel, _steering_rate ), steering_max_vel ); // Redundant with Servo::set_vel
	//_steering_rate = servos()[0]->set_vel( _steering_rate*DEG_TO_RAD )*RAD_TO_DEG;
	servos()[0]->set_vel( _steering_rate*DEG_TO_RAD )*RAD_TO_DEG;
}


void Rover_1::_ApplyBoggieControl()
{
	//_boggie_torque = std::min( std::max( -boggie_max_torque, _boggie_torque ), boggie_max_torque );
	//dJointAddHingeTorque( _boggie_hinge, _boggie_torque );
	dJointAddHingeTorque( _boggie_hinge, std::min( std::max( -boggie_max_torque, _boggie_torque ), boggie_max_torque ) );
}


void Rover_1::_UpdateTorqueFilters()
{
	for ( int i = 0 ; i < NBWHEELS ; i++ )
	{
		dVector3* t_abs = &_wheel_feedback[i].t1;
		dVector3 t_rel;
		dBodyVectorFromWorld( _wheel[i]->get_body(), *t_abs[0], *t_abs[1], *t_abs[2], t_rel );
		_torque_filter[i].update( -t_rel[2] );
	}
}


Matrix<double,4,3> Rover_1::GetFT300Torsors() const
{
	Matrix<double,4,3> ft_torsors;
	ft_torsors.row( 0 ) = *_front_ft_sensor.GetForces();
	ft_torsors.row( 1 ) = *_front_ft_sensor.GetTorques();
	ft_torsors.row( 2 ) = *_rear_ft_sensor.GetForces();
	ft_torsors.row( 3 ) = *_rear_ft_sensor.GetTorques();

	return ft_torsors;
}


void Rover_1::PrintFT300Torsors( bool endl ) const
{
	const Vector3d* list[] = { _front_ft_sensor.GetForces(), _front_ft_sensor.GetTorques(), _rear_ft_sensor.GetForces(), _rear_ft_sensor.GetTorques() };
	for ( const Vector3d* vec : list )
		for ( int j = 0 ; j < 3 ; j++ )
			printf( "%f ", vec->coeff( j ) );

	if ( endl )
	{
		printf( "\n" );
		fflush( stdout );
	}
}


void Rover_1::PrintWheelTorques( bool endl ) const
{
	for ( int i = 0 ; i < NBWHEELS ; i++ )
		printf( "%f ", _torque_output[i] );

	if ( endl )
	{
		printf( "\n" );
		fflush( stdout );
	}
}


void Rover_1::next_step( double dt )
{
	_front_ft_sensor.Update();
	_rear_ft_sensor.Update();
	//_UpdateTorqueFilters();

	_ic_clock += dt;
	if ( _ic_activated && _ic_clock >= _ic_period )
	{
		_InternalControl( _ic_clock );

		_ic_clock = 0;
	}

	_UpdateWheelControl();

	_ApplyWheelControl();
	_ApplySteeringControl();
	_ApplyBoggieControl();

	Robot::next_step( dt );
}


void Rover_1::_InternalControl( double delta_t )
{
	//PrintFT300Torsors();
	//PrintWheelTorques();

	//printf( "%f ", GetDirection() );
	printf( "%f %f ", _steering_rate, _boggie_torque );
	//printf( "%f %f ", GetSteeringTrueAngle(), GetBoggieAngle() );
	//printf( "%f %f ", GetRollAngle(), GetPitchAngle() );

	printf( "\n" );
	fflush( stdout );
}


Rover_1::~Rover_1()
{
	dJointDestroy( _boggie_hinge );

	for ( int i = 0 ; i < NBWHEELS ; i++ )
	{
		dJointDestroy( _wheel_joint[i] );
	}
}


}
