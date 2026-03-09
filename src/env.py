"""
Custom Gymnasium environment for the Ember air quality RL agent.

State  : 16-feature normalised observation vector (Section 7.1)
Actions: 0 = ALARM OFF  |  1 = ALARM ON  (Section 7.2)
Reward : Exact reward function from Section 7.3

The environment replays episodes from a pre-loaded dataset.
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

import numpy as np
import gymnasium as gym
from gymnasium import spaces


# ---------------------------------------------------------------------------
# Action constants
# ---------------------------------------------------------------------------
ALARM_OFF = 0
ALARM_ON  = 1


class AirQualityEnv(gym.Env):
    """
    Gymnasium environment that steps through air quality episodes.

    Each episode is a fixed-length sequence of sensor readings.
    The agent observes the normalised 16-feature vector and decides
    whether to raise the alarm.

    Reward function (Section 7.3):
        True  positive  (danger,  alarm on)  : +10
        False negative  (danger,  alarm off) : -50   ← highest penalty
        True  negative  (safe,    alarm off) :  +1
        False positive  (safe,    alarm on)  :  -5
        Fast response   (danger onset)       : +15 bonus
        Fast recovery   (danger cleared)     :  +5 bonus
    """

    metadata = {'render_modes': []}

    # RL hyper-params (match alarm logic from Section 4.3)
    TRIGGER_THRESHOLD = 151    # AQI >= 151 → danger

    def __init__(self, episodes: list, shuffle: bool = True):
        """
        Args:
            episodes : list of episode dicts from DataLoader._split_episodes()
                       Each dict has keys: 'obs' (list of np.array), 'labels' (list of int)
            shuffle  : randomise episode order on each reset
        """
        super().__init__()

        assert len(episodes) > 0, "Must provide at least one episode"

        self.episodes = episodes
        self.shuffle  = shuffle

        # Gymnasium spaces
        self.observation_space = spaces.Box(
            low=0.0, high=1.0, shape=(16,), dtype=np.float32
        )
        self.action_space = spaces.Discrete(2)  # 0=off, 1=on

        # Episode state (initialised in reset)
        self._ep_idx      = 0
        self._step_idx    = 0
        self._prev_action = ALARM_OFF
        self._episode     = None
        self._ep_order    = list(range(len(episodes)))

    # -----------------------------------------------------------------------
    # Gymnasium API
    # -----------------------------------------------------------------------

    def reset(self, seed=None, options=None):
        super().reset(seed=seed)

        # Shuffle episode order when we wrap around
        if self._ep_idx >= len(self._ep_order):
            self._ep_idx = 0
            if self.shuffle:
                self.np_random.shuffle(self._ep_order)

        self._episode     = self.episodes[self._ep_order[self._ep_idx]]
        self._ep_idx     += 1
        self._step_idx    = 0
        self._prev_action = ALARM_OFF

        obs  = self._current_obs()
        info = {}
        return obs, info

    def step(self, action: int):
        action = int(action)

        is_danger   = bool(self._episode['labels'][self._step_idx])
        reward      = self._compute_reward(action, is_danger, self._prev_action)
        self._prev_action = action

        self._step_idx += 1
        done = self._step_idx >= self._episode['length']

        if not done:
            obs = self._current_obs()
        else:
            # Return last obs when done (required by Gymnasium)
            obs = self._episode['obs'][self._step_idx - 1].copy()
            # Inject current alarm state into feature 15
            obs[15] = float(action)

        truncated = False
        info = {
            'is_danger':   is_danger,
            'action':      action,
            'aqi_approx':  float(obs[14]) * 500,
        }
        return obs, reward, done, truncated, info

    def render(self):
        pass   # No rendering needed

    # -----------------------------------------------------------------------
    # Reward function (Section 7.3)
    # -----------------------------------------------------------------------

    def _compute_reward(self, action: int, is_danger: bool,
                        prev_action: int) -> float:
        reward = 0.0

        if is_danger and action == ALARM_ON:
            reward = 10.0                # True positive — correct detection

        elif is_danger and action == ALARM_OFF:
            reward = -50.0               # False negative — MISSED DANGER

        elif not is_danger and action == ALARM_OFF:
            reward = 1.0                 # True negative — correct silence

        elif not is_danger and action == ALARM_ON:
            reward = -5.0                # False positive — unnecessary alarm

        # Transition bonuses
        if is_danger and prev_action == ALARM_OFF and action == ALARM_ON:
            reward += 15.0              # Fast response bonus

        if not is_danger and prev_action == ALARM_ON and action == ALARM_OFF:
            reward += 5.0               # Fast recovery bonus

        return reward

    # -----------------------------------------------------------------------
    # Internal helpers
    # -----------------------------------------------------------------------

    def _current_obs(self) -> np.ndarray:
        obs = self._episode['obs'][self._step_idx].copy()
        obs[15] = float(self._prev_action)   # inject live alarm state
        return obs


# ---------------------------------------------------------------------------
# Quick sanity check
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    # Create a tiny fake episode for testing
    fake_obs    = [np.random.rand(16).astype(np.float32) for _ in range(60)]
    fake_labels = [0] * 30 + [1] * 20 + [0] * 10
    episodes    = [{'obs': fake_obs, 'labels': fake_labels, 'length': 60}]

    env = AirQualityEnv(episodes, shuffle=False)
    obs, _ = env.reset()
    print(f"Obs shape  : {obs.shape}")
    print(f"Action space: {env.action_space}")

    total_reward = 0
    done = False
    while not done:
        action = env.action_space.sample()
        obs, reward, done, _, info = env.step(action)
        total_reward += reward

    print(f"Episode total reward: {total_reward:.1f}")
    print("Environment OK")
