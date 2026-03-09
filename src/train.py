"""
DQN Training script for the Ember air quality RL agent.

Usage:
    # Generate data then train (recommended first run)
    python train.py --generate

    # Train using existing CSV files in data/
    python train.py

    # Train using real hardware CSV
    python train.py --hardware data/real/2026-02-22.csv

    # Train using Beijing dataset
    python train.py --beijing data/real/PRSA_Data_Aotizhongxin_20130301-20170228.csv
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

import argparse
import numpy as np
from pathlib import Path

from stable_baselines3 import DQN
from stable_baselines3.common.monitor import Monitor
from stable_baselines3.common.callbacks import (
    EvalCallback, CheckpointCallback, BaseCallback
)
from stable_baselines3.common.env_checker import check_env

from data_generator import SyntheticDataGenerator
from data_loader import DataLoader
from env import AirQualityEnv

# ---------------------------------------------------------------------------
# Paths (relative to project root)
# ---------------------------------------------------------------------------
ROOT        = os.path.join(os.path.dirname(__file__), '..')
SYNTH_DIR   = os.path.join(ROOT, 'data', 'synthetic')
REAL_DIR    = os.path.join(ROOT, 'data', 'real')
MODELS_DIR  = os.path.join(ROOT, 'models')
LOGS_DIR    = os.path.join(ROOT, 'results', 'logs')

# ---------------------------------------------------------------------------
# DQN hyperparameters — tuned for this task
# ---------------------------------------------------------------------------
DQN_PARAMS = {
    'learning_rate':            3e-4,
    'buffer_size':              50_000,
    'batch_size':               128,
    'gamma':                    0.95,        # discount — short-horizon problem
    'exploration_fraction':     0.30,        # 30% of training = random exploration
    'exploration_final_eps':    0.05,        # 5% random after exploration
    'train_freq':               4,
    'gradient_steps':           1,
    'target_update_interval':   500,
    'learning_starts':          1_000,
    'policy_kwargs':            dict(net_arch=[64, 64]),   # 2 hidden layers
    'verbose':                  1,
}


# ---------------------------------------------------------------------------
# Reward tracking callback
# ---------------------------------------------------------------------------

class RewardLoggerCallback(BaseCallback):
    """Logs mean episode reward every N steps."""

    def __init__(self, log_freq: int = 2_000):
        super().__init__()
        self.log_freq = log_freq
        self.episode_rewards = []
        self.current_reward  = 0.0

    def _on_step(self) -> bool:
        reward = self.locals.get('rewards', [0])[0]
        self.current_reward += reward

        if self.locals.get('dones', [False])[0]:
            self.episode_rewards.append(self.current_reward)
            self.current_reward = 0.0

        if self.n_calls % self.log_freq == 0 and self.episode_rewards:
            mean_r = np.mean(self.episode_rewards[-20:])
            print(f"  [Step {self.n_calls:>7,}]  Mean ep reward (last 20): {mean_r:+.1f}")

        return True


# ---------------------------------------------------------------------------
# Data loading helpers
# ---------------------------------------------------------------------------

def load_episodes_synthetic(n_clean: int = 300, n_pollution: int = 200) -> list:
    """Generate or load synthetic data, return episodes."""
    syn_csv = os.path.join(SYNTH_DIR, 'synthetic_training_data.csv')

    if not os.path.exists(syn_csv):
        print("[Train] Generating synthetic training data ...")
        gen = SyntheticDataGenerator(seed=42)
        gen.generate_dataset(n_clean=n_clean, n_pollution=n_pollution, save_dir=SYNTH_DIR)

    print("[Train] Loading synthetic data ...")
    loader = DataLoader(episode_length=60, stride=30)
    return loader.load_synthetic(syn_csv)


def load_episodes_hardware(csv_path: str) -> list:
    print(f"[Train] Loading hardware data from {csv_path} ...")
    loader = DataLoader(episode_length=60, stride=30)
    return loader.load_hardware(csv_path)


def load_episodes_beijing(csv_path: str) -> list:
    print(f"[Train] Loading Beijing dataset from {csv_path} ...")
    loader = DataLoader(episode_length=60, stride=30)
    return loader.load_beijing(csv_path)


def load_episodes_all_real() -> list:
    """Load everything from data/real/ automatically."""
    loader = DataLoader(episode_length=60, stride=30)
    return loader.load_all(REAL_DIR)


# ---------------------------------------------------------------------------
# Train / Evaluate split
# ---------------------------------------------------------------------------

def train_eval_split(episodes: list, eval_ratio: float = 0.15):
    """Split episodes into train and eval sets."""
    n        = len(episodes)
    n_eval   = max(int(n * eval_ratio), 5)
    n_train  = n - n_eval
    idx      = np.random.default_rng(0).permutation(n)
    train_ep = [episodes[i] for i in idx[:n_train]]
    eval_ep  = [episodes[i] for i in idx[n_train:]]
    print(f"[Train] Episodes — train: {len(train_ep)}  |  eval: {len(eval_ep)}")
    return train_ep, eval_ep


# ---------------------------------------------------------------------------
# Main training function
# ---------------------------------------------------------------------------

def train(episodes: list,
          total_timesteps: int = 200_000,
          model_name: str = 'ember_dqn') -> DQN:
    """
    Train a DQN agent on the provided episodes.

    Args:
        episodes       : list of episode dicts (from DataLoader)
        total_timesteps: total env steps to train for
        model_name     : filename stem for the saved model

    Returns:
        Trained DQN model
    """
    Path(MODELS_DIR).mkdir(parents=True, exist_ok=True)
    Path(LOGS_DIR).mkdir(parents=True, exist_ok=True)

    train_ep, eval_ep = train_eval_split(episodes)

    # Build environments
    train_env = Monitor(AirQualityEnv(train_ep, shuffle=True))
    eval_env  = Monitor(AirQualityEnv(eval_ep,  shuffle=False))

    # Validate environment structure
    check_env(train_env, warn=True)

    # DQN model
    model = DQN(
        policy='MlpPolicy',
        env=train_env,
        tensorboard_log=LOGS_DIR,
        **DQN_PARAMS,
    )

    # Callbacks
    reward_cb = RewardLoggerCallback(log_freq=2_000)

    eval_cb = EvalCallback(
        eval_env,
        best_model_save_path=MODELS_DIR,
        log_path=LOGS_DIR,
        eval_freq=5_000,
        n_eval_episodes=20,
        deterministic=True,
        render=False,
        verbose=0,
    )

    checkpoint_cb = CheckpointCallback(
        save_freq=20_000,
        save_path=MODELS_DIR,
        name_prefix=model_name,
        verbose=0,
    )

    # Train
    print(f"\n[Train] Starting DQN training for {total_timesteps:,} timesteps ...")
    print(f"        TensorBoard logs → {LOGS_DIR}")
    print(f"        Best model      → {MODELS_DIR}/best_model.zip\n")

    model.learn(
        total_timesteps=total_timesteps,
        callback=[reward_cb, eval_cb, checkpoint_cb],
        tb_log_name=model_name,
        progress_bar=True,
    )

    # Save final model
    final_path = os.path.join(MODELS_DIR, f'{model_name}_final')
    model.save(final_path)
    print(f"\n[Train] Training complete. Final model saved → {final_path}.zip")

    return model


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Train Ember RL air quality agent')
    parser.add_argument('--generate',  action='store_true',
                        help='Generate fresh synthetic training data before training')
    parser.add_argument('--hardware',  type=str, default=None,
                        help='Path to hardware CSV (from SD card)')
    parser.add_argument('--beijing',   type=str, default=None,
                        help='Path to Beijing PRSA dataset CSV')
    parser.add_argument('--steps',     type=int, default=200_000,
                        help='Total training timesteps (default: 200000)')
    args = parser.parse_args()

    # Collect all available data
    all_episodes = []

    # Always include synthetic data as baseline
    if args.generate or not os.path.exists(
            os.path.join(SYNTH_DIR, 'synthetic_training_data.csv')):
        print("[Train] Generating synthetic data ...")
        gen = SyntheticDataGenerator(seed=42)
        gen.generate_dataset(n_clean=300, n_pollution=200, save_dir=SYNTH_DIR)

    syn_csv = os.path.join(SYNTH_DIR, 'synthetic_training_data.csv')
    if os.path.exists(syn_csv):
        all_episodes.extend(load_episodes_synthetic())

    # Add Beijing dataset if provided
    if args.beijing:
        all_episodes.extend(load_episodes_beijing(args.beijing))

    # Add hardware data if provided
    if args.hardware:
        all_episodes.extend(load_episodes_hardware(args.hardware))

    # Auto-load anything in data/real/ if no explicit files given
    if not args.beijing and not args.hardware:
        real_eps = load_episodes_all_real()
        if real_eps:
            all_episodes.extend(real_eps)

    if not all_episodes:
        print("[ERROR] No episodes loaded. Run with --generate first.")
        sys.exit(1)

    print(f"[Train] Total episodes: {len(all_episodes)}")

    # Train
    trained_model = train(all_episodes, total_timesteps=args.steps)

    print("\n[Train] Done! To view training curves:")
    print(f"        tensorboard --logdir {LOGS_DIR}")
