#include "rover_tf.hh"
#include "ode/box.hh"
#include "ode/wheel.hh"
#include <tensorflow/core/protobuf/meta_graph.pb.h>
#include "tensorflow/cc/framework/ops.h"
#include <random>


using namespace ode;
using namespace Eigen;
using namespace tensorflow;
namespace p = boost::python;


namespace robot
{


Rover_1_tf::Rover_1_tf( Environment& env, const Vector3d& pose, const char* path_to_tf_model, const int seed ) :
                        Rover_1( env, pose ),
						_total_reward( 0 ),
						_exploration( false )
{
	_last_pos = GetPosition();
	_was_flipped = GetSteeringTrueAngle() < 0;


	// Creation of the TensorFlow session:
	_tf_session_ptr = NewSession( SessionOptions() );
	if ( _tf_session_ptr == nullptr )
		throw std::runtime_error( "Could not create the TensorFlow session." );

	// Read in the protobuf graph:
	MetaGraphDef graph_def;
	TF_CHECK_OK( ReadBinaryProto( Env::Default(), std::string( path_to_tf_model ) += ".meta", &graph_def ) );

	// Add the graph to the session:
	TF_CHECK_OK( _tf_session_ptr->Create( graph_def.graph_def() ) );

	// Read weights from the saved checkpoint:
	tensorflow::Tensor checkpointPathTensor( DT_STRING, TensorShape() );
	checkpointPathTensor.scalar<std::string>()() = path_to_tf_model;
	TF_CHECK_OK( _tf_session_ptr->Run( {{graph_def.saver_def().filename_tensor_name(), checkpointPathTensor}}, {}, {graph_def.saver_def().restore_op_name()}, nullptr ) );
	

	// Initialization of the random number engine:
	if ( seed < 0 )
	{
		std::random_device rd;
		_rd_gen = std::mt19937( rd() );
	}
	else
		_rd_gen = std::mt19937( seed );
    _expl_dist = std::uniform_real_distribution<double>( 0., 1. );
    _ctrl_dist = std::uniform_real_distribution<double>( -1., 1. );
}


p::list Rover_1_tf::GetState( const bool flip ) const
{
	// Flip or not left and right in order to speed up learning by taking into account the robot's symmetry:
	int flip_coef = flip ? 1 : -1;

	p::list state;

	state.append( flip_coef*GetDirection() );
	state.append( flip_coef*GetSteeringTrueAngle() );
	state.append( flip_coef*GetRollAngle() );
	state.append( flip_coef*GetPitchAngle() );
	state.append( flip_coef*GetBoggieAngle() );
	for ( int i = 0 ; i < 4 ; i++ )
		for ( int j = 0 ; j < 3 ; j++ )
			state.append( ( ( i + j )%2 == 0 ? 1 : flip_coef )*_fork_output[i][j] );
	//for ( int i = 0 ; i < NBWHEELS ; i++ )
		//state.append( _torque_output[i] );

	return state;
}


void Rover_1_tf::InferAction( const p::list& state, double& steering_rate, double& boggie_torque, const bool flip ) const
{
	// Setup the inputs and outputs:
	tensorflow::Tensor state_tensor( tensorflow::DT_FLOAT, tensorflow::TensorShape( { 1, p::len( state ) } ) );
	for ( int i = 0 ; i < p::len( state ) ; i++ )
		state_tensor.flat<float>()( i ) = float( p::extract<float>( state[i] ) );

	std::vector<std::pair<string, tensorflow::Tensor>> inputs = { { "States", state_tensor } };
	std::vector<tensorflow::Tensor> outputs;

	// Run the session:
	TF_CHECK_OK( _tf_session_ptr->Run( inputs, { "Actor_Output" }, {}, &outputs ) );

	// Extract the outputs:
	steering_rate = ( flip ? -1 : 1 )*outputs[0].flat<float>()( 0 );
	boggie_torque = ( flip ? -1 : 1 )*outputs[0].flat<float>()( 1 );
}


double Rover_1_tf::_ComputeReward( double delta_t )
{
	Vector3d new_pos = GetPosition();
	Vector3d pos_diff = new_pos - _last_pos;

	// Reward the forward advance:
	//double reward = pos_diff[0]*fabs( pos_diff[0] ) + pos_diff[2]*fmax( 0., pos_diff[2] );
	double reward = pos_diff[0]*fabs( pos_diff[0] );
	// Scaling:
	//reward *= 1e3/( delta_t*delta_t );
	reward *= 30./( delta_t*delta_t );

	// Penalise the use of boggie torque:
	reward -= fabs( _boggie_torque )/boggie_max_torque*0.5;

	_last_pos = new_pos;
	return reward;
}


void Rover_1_tf::_InternalControl( double delta_t )
{
	// Get the reward obtained since last call:
	double reward = _ComputeReward( delta_t );
	_total_reward += reward;
#ifdef PRINT
		printf( "reward: %f\n", reward );
#endif

	// Flip the role of left and right if the steering angle is negative:
	bool flip = GetSteeringTrueAngle() < 0;

	// Get the current state of the robot:
	p::list new_state = GetState( flip );

	// Store the last experience, flip the actions according to the last state:
	if ( p::len( _last_state ) > 0 )
		_experience.append( p::make_tuple( _last_state,
		                                   p::make_tuple( ( _was_flipped ? -1 : 1 )*_steering_rate,( _was_flipped ? -1 : 1 )*_boggie_torque ),
										   reward,
										   false,
										   new_state ) );


	// Choose the next action:

	static bool explore;

	if ( _exploration && _expl_dist( _rd_gen ) > 0.7 )
	{
		if ( explore )
			explore = false;
		else
		{
			explore = true;
			_steering_rate = _ctrl_dist( _rd_gen )*steering_max_vel;
			_boggie_torque = _ctrl_dist( _rd_gen )*boggie_max_torque;
#ifdef PRINT
			printf( "EXPLO: %f %f\n", _steering_rate, _boggie_torque );
#endif
		}
	}
	if ( !_exploration || ! explore )
	{
		InferAction( new_state, _steering_rate, _boggie_torque, flip );
#ifdef PRINT
		printf( "INFER: %f %f\n", _steering_rate, _boggie_torque );
#endif
	}
#ifdef PRINT
	else
		printf( "EXPLO: %f %f (continue)\n", _steering_rate, _boggie_torque );
#endif

#ifdef PRINT
	fflush( stdout );
#endif

	_last_state = new_state;
	_was_flipped = flip;
}


Rover_1_tf::~Rover_1_tf()
{
	_tf_session_ptr->Close();
	delete _tf_session_ptr;
}


}
