#!/usr/bin/python
import pandas as pd
import numpy as np
from gmr import GMM
import pickle
import sys
import matplotlib.pyplot as plt
from tqdm import tqdm

import warnings
warnings.filterwarnings( action='ignore', module='pandas', category=UserWarning )


gmm_id = 'gmm_t2e5_k200_kmeans'
#gmm_id = 'gmm_t2e5_k500'

gmm_file = gmm_id + '.pkl'

trial_file = 'trial2.dat'

model_data_file_list = []
model_data_file_list.append( 'transitions_2e5.dat' )



forces = [ 'Front $f_x$', 'Front $f_y$', 'Front $f_z$',
		   'Front $\\tau_x$','Front $\\tau_y$', 'Front $\\tau_z$',
		   'Rear $f_x$', 'Rear $f_y$', 'Rear $f_z$',
		   'Rear $\\tau_x$', 'Rear $\\tau_y$', 'Rear $\\tau_z$' ]
state = [ 'Direction', 'Steering angle', 'Roll angle', 'Pitch Angle', 'Boggie angle' ] + forces
actions = [ 'Steering rate', 'Boggie torque' ]

state_1_full = [ name + ' s1' for name in state ]
state_2_full = [ name + ' s2' for name in state ]
columns = state_1_full + actions + state_2_full

features_to_drop = [ 'Rear $f_x$', 'Rear $f_y$', 'Rear $f_z$' ]

state = [ feature for feature in state if feature not in features_to_drop ]
state_1 = [ name + ' s1' for name in state ]
state_2 = [ name + ' s2' for name in state ]

dim_s = len( state )
dim_a = len( actions )
dim_t = 2*dim_s + dim_a



if len( sys.argv ) > 1 and ( sys.argv[1] == 'train' or sys.argv[1] == 'score' ) :

	#######################
	# Load the model data #
	#######################

	df_list = []
	for f in model_data_file_list :
		df_list.append( pd.read_csv( f, sep=' ', header=None, names=columns ) )
	df = pd.concat( df_list )

	# Convert every field to np.float64 and replace strings by np.nan:
	df = df.apply( pd.to_numeric, errors='coerce' )

	# Remove rows that contain at least one NaN:
	df = df.dropna()

	# Shuffle the rows:
	#df = df.sample( frac=1 )

	# Reset the index:
	df.reset_index( drop=True, inplace=True )

	# Symmetrize the states and actions around the steering angle:
	variables_to_flip = [ 'Direction', 'Steering angle', 'Roll angle', 'Boggie angle' ] + forces[1::2]
	variables_to_flip = [ name + ' s1' for name in variables_to_flip ]
	variables_to_flip = variables_to_flip + actions
	df.loc[ df[ 'Steering angle s1' ] < 0, variables_to_flip ] *= -1

	variables_to_flip = [ 'Direction', 'Steering angle', 'Roll angle', 'Boggie angle' ] + forces[1::2]
	variables_to_flip = [ name + ' s2' for name in variables_to_flip ]
	df.loc[ df[ 'Steering angle s2' ] < 0, variables_to_flip ] *= -1

	# Drop some features:
	df = df.drop( [ name + ' s1' for name in features_to_drop ], axis=1 )
	df = df.drop( [ name + ' s2' for name in features_to_drop ], axis=1 )


	data = df.to_numpy()

	print( 'Number of transitions in the data: %d' % data.shape[0] )


	if sys.argv[1] == 'train' :

		#######################
		# Train the GMM model #
		#######################

		n_components = 500
		gmm = GMM( n_components=n_components )
		print( 'Training the model with %d gaussian units' % n_components )
		gmm.from_samples( data, init='', plot_title=gmm_id, n_iter=100, savefig=True )
		print( 'Model ready' )
		plt.show()

		# Save the model:
		with open( gmm_file, 'wb' ) as f :
			pickle.dump( gmm, f )


	elif sys.argv[1] == 'score' :

		# Load the model:
		with open( gmm_file, 'rb' ) as f :
			gmm = pickle.load( f )

		######################
		# Evaluate the model #
		######################

		N_test = 20
		i_test = np.random.choice( range( len( data ) ), N_test, replace=False )
		X_data = df.drop( actions, axis=1 ).to_numpy()[i_test,:]
		Y_data = df[ actions ].to_numpy()[i_test,:]

		X_indices = np.array( list( range( dim_s ) ) + list( range( dim_s + dim_a, dim_t ) ) )
		Y_pred = gmm.predict( X_indices, X_data )

		abs_errors = abs( Y_pred - Y_data ).mean( axis=0 )
		print( 'Absolute errors: %g %g' % tuple( abs_errors ) )
		print( Y_data )
		print( Y_pred )


	exit( 0 )


# Load the model:
with open( gmm_file, 'rb' ) as f :
	gmm = pickle.load( f )


#######################
# Load the trial data #
#######################

if len( sys.argv ) > 2 :
	trial_file = sys.argv[2]

df = pd.read_csv( trial_file, sep=' ', header=None, names=columns )

# Convert every field to np.float64 and replace strings by np.nan:
df = df.apply( pd.to_numeric, errors='coerce' )

# Remove rows that contain at least one NaN:
df = df.dropna()

# Reset the index:
df.reset_index( drop=True, inplace=True )
#df = df.iloc[:3]

# Symmetrize the states and actions around the steering angle:
variables_to_flip = [ 'Direction', 'Steering angle', 'Roll angle', 'Boggie angle' ] + forces[1::2]
variables_to_flip = [ name + ' s1' for name in variables_to_flip ]
variables_to_flip = variables_to_flip + actions
df.loc[ df[ 'Steering angle s1' ] < 0, variables_to_flip ] *= -1

variables_to_flip = [ 'Direction', 'Steering angle', 'Roll angle', 'Boggie angle' ] + forces[1::2]
variables_to_flip = [ name + ' s2' for name in variables_to_flip ]
df.loc[ df[ 'Steering angle s2' ] < 0, variables_to_flip ] *= -1

# Drop some features:
df = df.drop( [ name + ' s1' for name in features_to_drop ], axis=1 )
df = df.drop( [ name + ' s2' for name in features_to_drop ], axis=1 )


s_sprime_indices = np.array( list( range( dim_s ) ) + list( range( dim_s + dim_a, dim_t ) ) )
s_a_indices = np.array( range( dim_s + dim_a ) )

delta_a = []
grad_a = []
delta_s_norm = []
for _, samp in tqdm( df.iterrows(), total=len( df ), leave=False ) :
	s_a = samp[ state_1 + actions ].to_numpy()[np.newaxis,:]
	s_sprime = samp[ state_1 + state_2 ].to_numpy()
	sprime = samp[ state_2 ].to_numpy()

	expected_sprime = gmm.predict( s_a_indices, s_a )
	grad_a_sprime = gmm.condition_derivative( s_sprime_indices, s_sprime )[:,dim_s:]

	delta_a.append( grad_a_sprime.dot( ( expected_sprime - sprime ).T ) )

	#if 'grad_a_prev' in globals() :
		#grad_a.append( grad_a_sprime - grad_a_prev )
	#grad_a_prev = grad_a_sprime
	grad_a.append( grad_a_sprime )
	delta_s_norm.append( np.linalg.norm( expected_sprime - sprime ) )


fig, ax = plt.subplots( 2, 1, sharex=True )
fig.canvas.set_window_title( 'Policy corrections' )

ax[0].set_title( 'Trial result' )
ax[0].plot( df[ actions ] )
ax[0].legend( [ 'Steering rate', 'Boggie torque' ] )

ax[1].set_title( 'Corrections' )
ax[1].plot( [ a[0] for a in delta_a ] )
ax[1].plot( [ a[1] for a in delta_a ] )
ax[1].legend( [ 'Steering rate', 'Boggie torque' ] )


fig, ax = plt.subplots( 3, 1, sharex=True )
fig.canvas.set_window_title( 'Gradient and state discrepancy' )

ax[0].set_title( 'Gradient of the steering rate' )
for i in range( len( grad_a[0][0] ) ) :
	ax[0].plot( [ g[0][i] for g in grad_a ] )
ax[0].legend( list( range( len( grad_a[0][0] ) ) ) )

ax[1].set_title( 'Gradient of the boggie torque' )
for i in range( len( grad_a[0][1] ) ) :
	ax[1].plot( [ g[1][i] for g in grad_a ] )
ax[1].legend( list( range( len( grad_a[0][1] ) ) ) )

ax[2].set_title( 'Delta s norm' )
ax[2].plot( delta_s_norm )


plt.show()
