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
        'post_fcnet_activation': 'elu'
    },
    'train_batch_size': 64,
    'sgd_minibatch_size': 64,
}

results = tune.run(
    'PPO',
    config=config,
    stop={'training_iteration': 3},
    verbose=3
)
