#!/usr/bin/python3
""" 
Deep Deterministic Policy Gradient (DDPG) [cf. http://arxiv.org/pdf/1509.02971v2.pdf (DeepMind)]

Author: Arthur Bouton [arthur.bouton@gadz.org]
"""
#import warnings
#warnings.simplefilter( action='ignore', category=FutureWarning )

import tensorflow as tf
import numpy as np
from collections import deque
import random
import sys
import os
from protect_loop import Protect_loop
from tqdm import trange


def actor_network_def( name, states, a_dim, action_scale=None, summaries=False ) :

	s_dim = states.get_shape().as_list()[1]

	with tf.variable_scope( name ) :

		with tf.variable_scope( 'layer1' ) :
			n_units_1 = 400
			wmax = 1/np.sqrt( s_dim )
			bmax = 1/np.sqrt( s_dim )
			w1 = tf.get_variable( 'w', [s_dim, n_units_1], tf.float32, tf.initializers.random_uniform( -wmax, wmax ) )
			b1 = tf.get_variable( 'b', [n_units_1], tf.float32, tf.initializers.random_uniform( -bmax, bmax ) )
			o1 = tf.add( tf.matmul( states, w1 ), b1 )
			a1 = tf.nn.relu( o1 )

		with tf.variable_scope( 'layer2' ) :
			n_units_2 = 300
			wmax = 1/np.sqrt( n_units_1 )
			bmax = 1/np.sqrt( n_units_1 )
			w2 = tf.get_variable( 'w', [n_units_1, n_units_2], tf.float32, tf.initializers.random_uniform( -wmax, wmax ) )
			b2 = tf.get_variable( 'b', [n_units_2], tf.float32, tf.initializers.random_uniform( -bmax, bmax ) )
			o2 = tf.add( tf.matmul( a1, w2 ), b2 )
			a2 = tf.nn.relu( o2 )

		with tf.variable_scope( 'layer3' ) :
			wmax = 0.003
			bmax = 0.003
			w3 = tf.get_variable( 'w', [n_units_2, a_dim], tf.float32, tf.initializers.random_uniform( -wmax, wmax ) )
			b3 = tf.get_variable( 'b', [a_dim], tf.float32, tf.initializers.random_uniform( -bmax, bmax ) )
			o3 = tf.add( tf.matmul( a2, w3 ), b3 )
			action = tf.nn.tanh( o3 )
			#action = o3
			#action = tf.clip_by_value( o3, -1, 1 )

		if action_scale is not None :
			action_scale = tf.constant( action_scale, tf.float32, name='action_scale' )
			action = tf.multiply( action, action_scale, 'scale_actions' )

		if summaries :
			tf.summary.histogram( 'layer1/weights', w1 )
			tf.summary.histogram( 'layer1/bias', b1 )
			tf.summary.histogram( 'layer2/weights', w2 )
			tf.summary.histogram( 'layer2/bias', b2 )
			tf.summary.histogram( 'layer3/weights', w3 )
			tf.summary.histogram( 'layer3/bias', b3 )

		return action, tf.get_collection( tf.GraphKeys.TRAINABLE_VARIABLES, scope=tf.get_variable_scope().name )


def critic_network_def( name, states, actions, action_scale=None, summaries=False ) :

	s_dim = states.get_shape().as_list()[1]
	a_dim = actions.get_shape().as_list()[1]

	with tf.variable_scope( name ) :

		if action_scale is not None :
			action_scale = tf.constant( action_scale, tf.float32, name='action_scale' )
			actions = tf.divide( actions, action_scale, 'scale_actions' )

		with tf.variable_scope( 'layer1' ) :
			n_units_1 = 400
			wmax = 1/np.sqrt( s_dim )
			bmax = 1/np.sqrt( s_dim )
			w1 = tf.get_variable( 'w', [s_dim, n_units_1], tf.float32, tf.initializers.random_uniform( -wmax, wmax ) )
			b1 = tf.get_variable( 'b', [n_units_1], tf.float32, tf.initializers.random_uniform( -bmax, bmax ) )
			o1 = tf.add( tf.matmul( states, w1 ), b1 )
			a1 = tf.nn.relu( o1 )

		with tf.variable_scope( 'layer2' ) :
			n_units_2 = 300
			wmax = 1/np.sqrt( n_units_1 + a_dim )
			bmax = 1/np.sqrt( n_units_1 + a_dim )
			w2 = tf.get_variable( 'w', [n_units_1 + a_dim, n_units_2], tf.float32, tf.initializers.random_uniform( -wmax, wmax ) )
			b2 = tf.get_variable( 'b', [n_units_2], tf.float32, tf.initializers.random_uniform( -bmax, bmax ) )
			o2 = tf.add( tf.matmul( tf.concat( [ a1, actions ], 1 ), w2 ), b2 )
			a2 = tf.nn.relu( o2 )

		with tf.variable_scope( 'layer3' ) :
			wmax = 0.003
			bmax = 0.003
			w3 = tf.get_variable( 'w', [n_units_2, 1], tf.float32, tf.initializers.random_uniform( -wmax, wmax ) )
			b3 = tf.get_variable( 'b', [1], tf.float32, tf.initializers.random_uniform( -bmax, bmax ) )
			Q_value = tf.add( tf.matmul( a2, w3 ), b3 )

		if summaries :
			tf.summary.histogram( 'layer1/weights', w1 )
			tf.summary.histogram( 'layer1/bias', b1 )
			tf.summary.histogram( 'layer2/weights', w2 )
			tf.summary.histogram( 'layer2/bias', b2 )
			tf.summary.histogram( 'layer3/weights', w3 )
			tf.summary.histogram( 'layer3/bias', b3 )

		return Q_value, tf.get_collection( tf.GraphKeys.TRAINABLE_VARIABLES, scope=tf.get_variable_scope().name )


class DDPG() :

	def __init__( self, s_dim, a_dim, state_scale=None, action_scale=None,
	              gamma=0.99, tau=0.001, buffer_size=1000000, minibatch_size=64, actor_lr=0.0001, critic_lr=0.001, beta_L2=0,
				  actor_def=actor_network_def, critic_def=critic_network_def,
				  summary_dir=None, seed=None, single_thread=False, sess=None ) :
		"""
		s_dim: Dimension of the state space
		a_dim: Dimension of the action space
		state_scale: A scalar or a vector to normalize the state
		action_scale: A scalar or a vector to scale the actions
		gamma: Discount factor of the reward
		tau: Soft target update factor
		buffer_size: Maximal size of the replay buffer
		minibatch_size: Size of each minibatch
		actor_lr: Learning rate of the actor network
		critic_lr: Learning rate of the critic network
		beta_L2: Ridge regularization coefficient
		actor_def: Function defining the actor network
		critic_def: Function defining the critic network
		summary_dir: Directory where to save summaries
		seed: Random seed for the initialization of random generators
		single_thread: Force the execution on a single core in order to have a deterministic behavior
		sess: A TensorFlow session already initialized
		"""

		self.s_dim = s_dim
		self.a_dim = a_dim
		self.gamma = gamma
		self.minibatch_size = minibatch_size
		self.summaries = summary_dir is not None

		self.n_iter = 0

		# Instantiation of the replay buffer:
		self.replay_buffer = deque( maxlen=buffer_size )
		random.seed( seed )

		######################
		# Building the graph #
		######################

		# Set the graph-level random seed:
		tf.set_random_seed( seed )

		self.states = tf.placeholder( tf.float32, [None, self.s_dim], 'States' )
		self.actions = tf.placeholder( tf.float32, [None, self.a_dim], 'Actions' )

		# Scaling of the inputs:
		if state_scale is not None :
			state_scale = tf.constant( state_scale, tf.float32, name='state_scale' )
			scaled_states = tf.divide( self.states, state_scale, 'scale_states' )
		else :
			scaled_states = self.states

		# Declaration of the actor and the critic networks:
		self.mu_actions, actor_params = actor_def( 'Actor', scaled_states, self.a_dim, action_scale, self.summaries )
		tf.identity( self.mu_actions, name="Actor_Output" )
		self.Q_value, critic_params = critic_def( 'Critic', scaled_states, self.actions, action_scale, self.summaries )

		# Declaration of the target networks:
		with tf.variable_scope( 'Target_Networks' ) :
			target_mu_actions, target_actor_params = actor_def( 'Target_Actor', scaled_states, self.a_dim, action_scale )
			self.target_Q_value, target_critic_params = critic_def( 'Target_Critic', scaled_states, target_mu_actions, action_scale )

		# Update of the target network parameters:
		with tf.variable_scope( 'update_target_networks' ) :
			sync_target_networks = [ tP.assign( P ) for P, tP in zip( actor_params + critic_params, target_actor_params + target_critic_params ) ]
			self.update_target_actor = [ tP.assign( P*tau + tP*( 1 - tau ) ) for P, tP in zip( actor_params, target_actor_params ) ]
			self.update_target_critic = [ tP.assign( P*tau + tP*( 1 - tau ) ) for P, tP in zip( critic_params, target_critic_params ) ]

		# Backpropagation in the critic network of the target errors:
		self.y = tf.placeholder( tf.float32, [None, 1], 'Targets' )
		self.L = tf.losses.mean_squared_error( self.y, self.Q_value )
		if beta_L2 > 0 :
			L2 = beta_L2*tf.reduce_mean( [ tf.nn.l2_loss( v ) for v in critic_params if 'w' in v.name ] )
			self.L += L2
		critic_optimizer = tf.train.AdamOptimizer( critic_lr )
		self.train_critic = critic_optimizer.minimize( self.L, name='critic_backprop' )

		# Application of the deterministic policy gradient to the actor network:
		gradQ_a = tf.gradients( self.Q_value, self.actions, name='gradQ_a' )
		gradQ_a_N = tf.divide( gradQ_a[0], tf.constant( minibatch_size, tf.float32, name='minibatch_size' ), 'normalize_over_batch' )
		policy_gradients = tf.gradients( self.mu_actions, actor_params, -gradQ_a_N, name='policy_gradients' )
		self.train_actor = tf.train.AdamOptimizer( actor_lr ).apply_gradients( zip( policy_gradients, actor_params ), name='apply_policy_gradients' )

		#######################
		# Setting the session #
		#######################

		if sess is not None :
			self.sess = sess
		else :
			if single_thread :
				sess_config = tf.ConfigProto( intra_op_parallelism_threads=1, inter_op_parallelism_threads=1 )
				self.sess = tf.Session( config=sess_config )
			else :
				self.sess = tf.Session()

		# Initialize variables and saver:
		self.sess.run( tf.global_variables_initializer() )
		self.sess.run( sync_target_networks )
		self.saver = tf.train.Saver()

		# Create the summaries:
		if self.summaries :

			self.wb_summary_op = tf.summary.merge_all()

			self.reward_eval = tf.placeholder( tf.float32, name='reward_eval' )
			reward_summary = tf.summary.scalar( 'Reward', self.reward_eval )
			self.reward_summary_op = tf.summary.merge( [ reward_summary ] )

			L_summary = tf.summary.scalar( 'L', self.L )
			critic_gradients = critic_optimizer.compute_gradients( self.L, critic_params )
			cg_summary = []
			cg_summary.append( tf.summary.scalar( 'critic_gradients/layer1/weights', tf.norm( critic_gradients[0][0] ) ) )
			cg_summary.append( tf.summary.scalar( 'critic_gradients/layer1/bias', tf.norm( critic_gradients[1][0] ) ) )
			self.critic_summary_op = tf.summary.merge( [ L_summary ] + cg_summary )

			ag_summary = []
			ag_summary.append( tf.summary.scalar( 'actor_gradients/layer1/weights', tf.norm( policy_gradients[0] ) ) )
			ag_summary.append( tf.summary.scalar( 'actor_gradients/layer1/bias', tf.norm( policy_gradients[1] ) ) )
			self.actor_summary_op = tf.summary.merge( ag_summary )

			self.writer = tf.summary.FileWriter( summary_dir, self.sess.graph )

	def reward_summary( self, reward ) :

		if self.summaries :
			self.writer.add_summary( self.sess.run( self.reward_summary_op, {self.reward_eval: reward} ), self.n_iter )

	def sample_batch( self, batch_size ) :

		batch = random.sample( self.replay_buffer, min( len( self.replay_buffer ), batch_size ) )

		s_batch = np.array( [ _[0] for _ in batch ] )
		a_batch = np.array( [ _[1] if np.shape( _[1] ) else [ _[1] ] for _ in batch ] )
		r_batch = np.array( [ _[2] for _ in batch ] )
		t_batch = np.array( [ _[3] for _ in batch ] )
		s2_batch = np.array( [ _[4] for _ in batch ] )

		return s_batch, a_batch, r_batch, t_batch, s2_batch

	def train( self, iterations=1 ) :

		Lt = 0

		for _ in trange( iterations, desc='Training the networks', leave=False ) :

			self.n_iter += 1

			# Randomly pick samples in the replay buffer:
			s, a, r, t, s2 = self.sample_batch( self.minibatch_size )

			# Predict the future discounted rewards with the target critic network:
			target_q = self.sess.run( self.target_Q_value, {self.states: s2} )

			# Compute the targets for the Q-value:
			y = []
			for i in range( self.minibatch_size ) :
				if t[i] :
					y.append( np.array([ r[i] ]) )
				else :
					y.append( r[i] + self.gamma*target_q[i] )

			# Optimize the critic network according to the targets:
			if self.summaries :
				L, _, critic_summary = self.sess.run( [ self.L, self.train_critic, self.critic_summary_op ], {self.states: s, self.actions: a, self.y: y} )
				self.writer.add_summary( critic_summary, self.n_iter )
			else :
				L, _ = self.sess.run( [ self.L, self.train_critic ], {self.states: s, self.actions: a, self.y: y} )
			Lt += L

			# Apply the sample gradient to the actor network:
			mu_a = self.sess.run( self.mu_actions, {self.states: s} )
			if self.summaries :
				_, actor_summary = self.sess.run( [ self.train_actor, self.actor_summary_op ], {self.states: s, self.actions: mu_a} )
				self.writer.add_summary( actor_summary, self.n_iter )
			else :
				self.sess.run( self.train_actor, {self.states: s, self.actions: mu_a} )

			# Update the target networks:
			self.sess.run( self.update_target_actor )
			self.sess.run( self.update_target_critic )

		if self.summaries :
			self.writer.add_summary( self.sess.run( self.wb_summary_op ), self.n_iter )
			#self.writer.flush()

		return Lt/iterations
	
	def get_action( self, s ) :
		mu_a = self.sess.run( self.mu_actions, {self.states: s[np.newaxis, :] if s.ndim < 2 else s} )
		return mu_a.squeeze()
	
	def get_Q_value( self, s, a ) :
		Q_value = self.sess.run( self.Q_value, {self.states: s[np.newaxis, :] if s.ndim < 2 else s,
		                                        self.actions: a[:, np.newaxis] if isinstance( a, np.ndarray ) and a.ndim < 2 else a} )
		return Q_value.squeeze()
	
	def get_V_value( self, s ) :
		mu_a = self.sess.run( self.mu_actions, {self.states: s[np.newaxis, :] if s.ndim < 2 else s} )
		V_value = self.sess.run( self.Q_value, {self.states: s[np.newaxis, :] if s.ndim < 2 else s, self.actions: mu_a} )
		return V_value.squeeze()
	
	def save( self, filename ) :
		self.saver.save( self.sess, filename )
	
	def load( self, filename ) :
		self.saver.restore( self.sess, filename )

	def __enter__( self ) :
		return self

	def __exit__( self, type, value, traceback ) :
		self.sess.close()


if __name__ == '__main__' :

	from pendulum import Pendulum


	# Identifier name for the data:
	data_id = 'test1'

	script_name = os.path.splitext( os.path.basename( __file__ ) )[0]

	# Name of the file where to store network parameters:
	backup_file = './training_data/' + script_name + '_' + data_id

	# Parameters for the training:
	ENV = Pendulum # A class defining the environment
	EP_LEN = 100 # Number of steps for one episode
	EP_MAX = 1000 # Maximal number of episodes for the training
	ITER_PER_EP = 200 # Number of training iterations between each episode
	S_DIM = 2 # Dimension of the state space
	A_DIM = 1 # Dimension of the action space
	STATE_SCALE = [ np.pi, 2*np.pi ] # A scalar or a vector to normalize the state
	ACTION_SCALE = None # A scalar or a vector to scale the actions
	GAMMA = 0.99 # Discount factor of the reward
	TAU = 0.001 # Soft target update factor
	BUFFER_SIZE = 1000000 # Maximal size of the replay buffer
	MINIBATCH_SIZE = 64 # Size of each minibatch
	ACTOR_LR = 0.0001 # Learning rate of the actor network
	CRITIC_LR = 0.001 # Learning rate of the critic network
	BETA_L2 = 1e-6 # Ridge regularization coefficient
	#SUMMARY_DIR = '/tmp/' + script_name + '/' + data_id # Directory where to save summaries
	SUMMARY_DIR = None # No summaries
	SEED = None # Random seed for the initialization of all random generators
	SINGLE_THREAD = False # Force the execution on a single core in order to have a deterministic behavior

	with DDPG( S_DIM, A_DIM, STATE_SCALE, ACTION_SCALE, GAMMA, TAU, BUFFER_SIZE, MINIBATCH_SIZE, ACTOR_LR, CRITIC_LR, BETA_L2,
	           summary_dir=SUMMARY_DIR, seed=SEED, single_thread=SINGLE_THREAD ) as ddpg :


		if len( sys.argv ) == 1 or sys.argv[1] != 'eval' :

			if len( sys.argv ) > 1 and sys.argv[1] == 'load' :
				if len( sys.argv ) > 2 :
					ddpg.load( sys.argv[2] )
				else :
					ddpg.load( backup_file )


			np.random.seed( SEED )

			training_env = ENV()
			eval_env = ENV()

			n_ep = 0
			Li = 0

			import time
			start = time.time()

			with Protect_loop() as interruption :

				while not interruption() and n_ep < EP_MAX :

					s = training_env.reset()
					expl = False

					for _ in range( EP_LEN ) :

						# Choose an action:
						if np.random.rand() < 0.1 :
							if expl :
								expl = False
							else :
								expl = True
								a = np.random.uniform( -1, 1, A_DIM )
						if not expl :
							a = ddpg.get_action( s )

						# Do one step:
						s2, r, terminal, _ = training_env.step( a )

						# Scale the reward:
						#r = r/10

						# Store the experience in the replay buffer:
						ddpg.replay_buffer.append(( s, a, r, terminal, s2 ))

						# When there is enough samples, train the networks (on-line):
						#if len( ddpg.replay_buffer ) > ddpg.minibatch_size :
							#Li = ddpg.train()
						
						if terminal or interruption() :
							break

						s = s2

					if interruption() :
						break

					n_ep += 1

					# When there is enough samples, train the networks (off-line):
					if len( ddpg.replay_buffer ) >= ddpg.minibatch_size :
						Li = ddpg.train( ITER_PER_EP )


					# Evaluate the policy:
					if n_ep % 1 == 0 :
						s = eval_env.reset( store_data=True )
						for t in range( EP_LEN ) :
							s, _, done, _ = eval_env.step( ddpg.get_action( s ) )
							if done : break
						print( 'It %i | Ep %i | Li %+8.4f | ' % ( ddpg.n_iter, n_ep, Li ), end='', flush=True )
						eval_env.print_eval()
						sys.stdout.flush()
						ddpg.reward_summary( eval_env.get_Rt() )


			end = time.time()
			print( 'Elapsed time: %.3f' % ( end - start ) )

			answer = input( '\nSave network parameters as ' + backup_file + '? (y) ' )
			if answer.strip() == 'y' :
				ddpg.save( backup_file )
				print( 'Parameters saved.' )
			else :
				answer = input( 'Where to store network parameters? (leave empty to discard data) ' )
				if answer.strip() :
					ddpg.save( answer )
					print( 'Parameters saved as %s.' % answer )
				else :
					print( 'Data discarded.' )

		else :


			ddpg.load( backup_file )


			test_env = ENV( 180, store_data=True )
			s = test_env.get_obs()
			for t in range( EP_LEN ) :
				s, _, done, _ = test_env.step( ddpg.get_action( s ) )
				if done : break
			print( 'Trial result: ', end='' )
			test_env.print_eval()
			test_env.plot3D( ddpg.get_action, ddpg.get_V_value )
			test_env.plot_trial()
			test_env.show()
