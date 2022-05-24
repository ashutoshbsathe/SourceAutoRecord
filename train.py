import ray 
from ray import tune
from ray.tune.registry import register_env
from testchamber import TestChamber 

ray.init(num_gpus=1)

def env_creator(config):
    return TestChamber(**config)

register_env('TestChamber', env_creator) 

config = {
    'env': 'TestChamber',
    'num_workers': 1,
    'num_gpus': 1,
    'framework': 'torch',
    #'_disable_preprocessor_api': True,
    'model': {
        'fcnet_hiddens': [32, 32],
        'fcnet_activation': 'elu',
        'conv_filters': [
            [32, [7, 7], 4],
            [32, [7, 7], 4],
            [32, [7, 7], 4],
            [32, [3, 3], 1],
        ],
        'conv_activation': 'elu',
        'post_fcnet_hiddens': [128, 64, 64],
        'post_fcnet_activation': 'elu',
        'vf_share_layers': True,
    },
    'train_batch_size': 64,
    'rollout_fragment_length': 64,
    'sgd_minibatch_size': 64,
    'lr': 1e-5,
    'vf_clip_param': 100,
    'vf_loss_coeff': 0.1,
    'clip_param': 0.2,
    'grad_clip': 4,
}

results = tune.run(
    'PPO',
    config=config,
    stop={'training_iteration': 1000, 'episode_reward_mean': 64},
    verbose=3,
    checkpoint_at_end=True,
    checkpoint_freq=10,
    restore='~/ray_results/PPO/PPO_TestChamber_ee94b_00000_0_2022-04-06_12-35-25/checkpoint_000080/checkpoint-80'
)
