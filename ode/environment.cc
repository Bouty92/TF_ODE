/*
** Copyright (C) 2014 Arthur BOUTON
** Copyright (C) 2010 mandor
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <Eigen/Geometry>

#include "environment.hh"
#include "object.hh"


namespace ode
{


  void Environment::_init(bool add_ground, double _angle)
  {
     //create world
    _world_id = dWorldCreate();
     //init gravity
    dWorldSetGravity(_world_id, 0, 0, -cst::g);
     //space
    _space_id = dHashSpaceCreate(0);
     //ground
    if (add_ground)
    {
      angle=_angle;
      Eigen::Vector3d normal(0, sin(-angle), cos(-angle));
      Eigen::Quaternion<double> q1, q2;
      q1 = Eigen::AngleAxis<double>(_pitch, Eigen::Vector3d::UnitX());
      q2 = Eigen::AngleAxis<double>(_roll, Eigen::Vector3d::UnitY());
      normal = q2 * q1 * normal;
      _ground = dCreatePlane(_space_id, normal.x(), normal.y(), normal.z(), _z);
		dGeomSetData( _ground, new collision_feature( "ground" ) );
    }
     //contact group1
    _contactgroup = dJointGroupCreate(0);

    //dWorldSetContactMaxCorrectingVel(_world_id, 100);

    //dWorldSetContactSurfaceLayer(_world_id, 0.001);
  }
  void Environment::_collision(dGeomID o1, dGeomID o2)
  {
	contact_type type = HARD;

	collision_feature* o1_collision_feature = (collision_feature*) dGeomGetData( o1 );
	collision_feature* o2_collision_feature = (collision_feature*) dGeomGetData( o2 );

	if ( o1_collision_feature != NULL && o1_collision_feature->callback )
		o1_collision_feature->callback( o2_collision_feature );

	if ( o2_collision_feature != NULL && o2_collision_feature->callback )
		o2_collision_feature->callback( o1_collision_feature );


	if ( o1_collision_feature == NULL && o2_collision_feature == NULL )
		return;
	else if ( o1_collision_feature != NULL && o2_collision_feature != NULL )
	{
		if ( o1_collision_feature->type == DISABLED || o2_collision_feature->type == DISABLED )
			return;
		else if ( strcmp( o1_collision_feature->group, o2_collision_feature->group ) == 0 )
			return;
		else if ( o1_collision_feature->type == SOFT || o2_collision_feature->type == SOFT )
			type = SOFT;
	}
	else if ( o1_collision_feature != NULL )
	{
		if ( *o1_collision_feature->group == '\0' )
			return;
		else if ( o1_collision_feature->type == DISABLED )
			return;
		else if ( o1_collision_feature->type == SOFT )
			type = SOFT;
	}
	else
	{
		if ( *o2_collision_feature->group == '\0' )
			return;
		else if ( o2_collision_feature->type == DISABLED )
			return;
		else if ( o2_collision_feature->type == SOFT )
			type = SOFT;
	}
		

    static const int N = 10;
    int i, n;
    dContact contact[N];
    n = dCollide(o1, o2, N, &contact[0].geom, sizeof(dContact));

    if (n > 0)
    {
      for (i = 0; i < n; i++)
      {
        contact[i].surface.mode = dContactApprox1;
        //contact[i].surface.mode = dContactApprox1 | dContactSlip1 | dContactSlip2;
        contact[i].surface.mu = _mu;
        //contact[i].surface.slip1 = 0.5;
        //contact[i].surface.slip2 = 0.5;

		if ( type == SOFT )
		{
			contact[i].surface.mode |= dContactSoftCFM | dContactSoftERP;
			//contact[i].surface.soft_cfm = 0.02;
			//contact[i].surface.soft_erp = 0.5;
			contact[i].surface.soft_cfm = 0.005;
			contact[i].surface.soft_erp = 0.4;
			//contact[i].surface.soft_cfm = 0.01;
			//contact[i].surface.soft_erp = 0.8;
		}

        dJointID c = dJointCreateContact( get_world(), get_contactgroup(), &contact[i] );
        dJointAttach( c, dGeomGetBody( contact[i].geom.g1 ), dGeomGetBody( contact[i].geom.g2 ) );


        // grass
        // dBodyID obj = 0;
        // if (g1 && o1 == _ground) // g2 is our object
        //   obj = dGeomGetBody(o2);
        // else if (g2 && o2 == _ground)
        //   obj = dGeomGetBody(o1);
        // if (obj)
        //   {
        //      dVector3 vel;
        //      dBodyGetPointVel(obj,
        //                       contact[i].geom.pos[0],
        //                       contact[i].geom.pos[1],
        //                       contact[i].geom.pos[2],
        //                       vel);
        //      dBodyAddForce(obj, -vel[0]*200, -vel[1]*200, 0);
        //      std::cout<<"vel:"<<vel[0]<<std::endl;
        //   }
      }
    }
  }


}
