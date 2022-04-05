from ray.rllib.agents.ppo import PPOTrainer
from ray.tune.registry import register_env
from testchamber import TestChamber 

def env_creator(config):
    return TestChamber(**config)

register_env('TestChamber', env_creator) 

config = {
    'env': 'TestChamber',
    'num_workers': 1,
    'framework': 'torch',
    #'_disable_preprocessor_api': True,
    'model': {
        'fcnet_hiddens': [32, 32],
        'fcnet_activation': 'elu',
        'conv_filters': [
            [32, [7, 7], 4],
            [32, [7, 7], 4],
            [32, [7, 7], 4],
            [32, [4, 4], 1],
        ],
        'conv_activation': 'elu',
        'post_fcnet_hiddens': [128, 64, 64],
        'post_fcnet_activation': 'elu'
    },
}

trainer = PPOTrainer(config=config)

for _ in range(3):
    print(trainer.train())

trainer.evaluate()
