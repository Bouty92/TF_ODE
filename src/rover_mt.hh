#ifndef ROVER_MT_HH
#define ROVER_MT_HH 

#include "rover.hh"
#include "model_tree.hh" // https://github.com/Bouty92/ModelTree


namespace robot
{


class Rover_1_mt : public Rover_1
{
	public:

	Rover_1_mt( ode::Environment& env, const Eigen::Vector3d& pose, const std::string yaml_file_path_1, const std::string yaml_file_path_2,
	            bool oblique_trees = false, unsigned int degree = 1, bool interaction_only = false );

	std::vector<double> GetState( const bool flip = false, const bool full = false ) const;
	inline std::vector<double> GetFullState( const bool flip = false ) const { return GetState( flip, true ); }

	void InferAction( const std::vector<double>& state, double& steering_rate, double& boggie_torque, const bool flip = false );

	int node_1, node_2;

	protected:

	virtual void _InternalControl( double delta_t );

	mt_ptr_t<double> _lmt_ptr_1, _lmt_ptr_2;
};


}

#endif
