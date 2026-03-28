"""
GenAI Declaration (Claude AI - Anthropic):
Approximately 60% of this file was written by Claude AI, including the plot functions
and report formatting. I wrote the evaluation logic, selected the metrics to track
(accuracy, recall, false negative rate), and decided what the output should look like.
"""

"""
Model evaluation — metrics, confusion matrix, and training plots.

Usage:
    python evaluate.py                          # uses best_model.zip
    python evaluate.py --model models/ember_dqn_final.zip
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')   # non-interactive backend (works without display)
import matplotlib.pyplot as plt
from pathlib import Path

from stable_baselines3 import DQN
from sklearn.metrics import (
    confusion_matrix, classification_report,
    ConfusionMatrixDisplay, roc_curve, auc
)

from data_loader import DataLoader
from env import AirQualityEnv

ROOT       = os.path.join(os.path.dirname(__file__), '..')
MODELS_DIR = os.path.join(ROOT, 'models')
RESULTS    = os.path.join(ROOT, 'results')
SYNTH_CSV  = os.path.join(ROOT, 'data', 'synthetic', 'synthetic_training_data.csv')


# ---------------------------------------------------------------------------
# Core evaluation
# ---------------------------------------------------------------------------

def evaluate_model(model: DQN, episodes: list, n_episodes: int = None) -> dict:
    """
    Run the trained model on episodes and collect predictions vs ground truth.

    Returns dict with keys: y_true, y_pred, rewards, false_neg_rate, false_pos_rate
    """
    env = AirQualityEnv(episodes, shuffle=False)

    if n_episodes is None:
        n_episodes = len(episodes)

    y_true, y_pred = [], []
    episode_rewards = []

    for ep_i in range(min(n_episodes, len(episodes))):
        obs, _ = env.reset()
        done   = False
        ep_reward = 0.0

        while not done:
            action, _ = model.predict(obs, deterministic=True)
            obs, reward, done, _, info = env.step(int(action))
            y_true.append(int(info['is_danger']))
            y_pred.append(int(action))
            ep_reward += reward

        episode_rewards.append(ep_reward)

    y_true = np.array(y_true)
    y_pred = np.array(y_pred)

    # Compute key metrics
    tp = np.sum((y_true == 1) & (y_pred == 1))
    tn = np.sum((y_true == 0) & (y_pred == 0))
    fp = np.sum((y_true == 0) & (y_pred == 1))
    fn = np.sum((y_true == 1) & (y_pred == 0))

    results = {
        'y_true':          y_true,
        'y_pred':          y_pred,
        'episode_rewards': episode_rewards,
        'tp': tp, 'tn': tn, 'fp': fp, 'fn': fn,
        'accuracy':        (tp + tn) / len(y_true) if len(y_true) > 0 else 0,
        'precision':       tp / (tp + fp) if (tp + fp) > 0 else 0,
        'recall':          tp / (tp + fn) if (tp + fn) > 0 else 0,  # sensitivity
        'false_neg_rate':  fn / (tp + fn) if (tp + fn) > 0 else 0,  # miss rate — CRITICAL
        'false_pos_rate':  fp / (fp + tn) if (fp + tn) > 0 else 0,  # false alarm rate
        'mean_ep_reward':  np.mean(episode_rewards),
    }
    results['f1'] = (
        2 * results['precision'] * results['recall'] /
        (results['precision'] + results['recall'])
        if (results['precision'] + results['recall']) > 0 else 0
    )
    return results


def print_report(results: dict):
    """Print a human-readable evaluation report."""
    print("\n" + "=" * 55)
    print("  EMBER AI — EVALUATION REPORT")
    print("=" * 55)
    print(f"  Total timesteps evaluated : {len(results['y_true']):,}")
    print(f"  Danger steps (ground truth): {results['y_true'].sum():,} "
          f"({results['y_true'].mean()*100:.1f}%)")
    print()
    print(f"  Accuracy          : {results['accuracy']*100:6.2f}%")
    print(f"  Precision         : {results['precision']*100:6.2f}%")
    print(f"  Recall (sensitivity): {results['recall']*100:6.2f}%")
    print(f"  F1 Score          : {results['f1']*100:6.2f}%")
    print()
    print(f"  !! False Negative Rate (missed danger) : {results['false_neg_rate']*100:6.2f}%")
    print(f"     False Positive Rate (false alarms)  : {results['false_pos_rate']*100:6.2f}%")
    print()
    print(f"  Mean episode reward: {results['mean_ep_reward']:+.1f}")
    print()
    print(f"  Confusion Matrix:")
    print(f"    TP={results['tp']:>5}  FP={results['fp']:>5}")
    print(f"    FN={results['fn']:>5}  TN={results['tn']:>5}")
    print("=" * 55)

    # Warn if false negative rate is too high (missing real danger is most critical)
    if results['false_neg_rate'] > 0.05:
        print(f"\n  ⚠  WARNING: False negative rate {results['false_neg_rate']*100:.1f}% > 5%")
        print("     The model is missing real danger events. Retrain with more data.")
    else:
        print(f"\n  ✓  False negative rate is acceptable ({results['false_neg_rate']*100:.1f}%)")


# ---------------------------------------------------------------------------
# Plots
# ---------------------------------------------------------------------------

def plot_confusion_matrix(results: dict, save_dir: str):
    cm = confusion_matrix(results['y_true'], results['y_pred'])
    fig, ax = plt.subplots(figsize=(5, 4))
    disp = ConfusionMatrixDisplay(
        confusion_matrix=cm,
        display_labels=['SAFE (alarm off)', 'DANGER (alarm on)']
    )
    disp.plot(ax=ax, colorbar=False, cmap='Blues')
    ax.set_title('Ember RL Agent — Confusion Matrix', fontsize=12)
    plt.tight_layout()
    path = os.path.join(save_dir, 'confusion_matrix.png')
    plt.savefig(path, dpi=150)
    plt.close()
    print(f"[Eval] Saved → {path}")


def plot_episode_rewards(results: dict, save_dir: str):
    rewards = results['episode_rewards']
    fig, ax = plt.subplots(figsize=(9, 4))
    ax.plot(rewards, alpha=0.5, linewidth=0.8, label='Episode reward')
    # Rolling mean
    window = max(len(rewards) // 20, 5)
    if len(rewards) >= window:
        roll = np.convolve(rewards, np.ones(window)/window, mode='valid')
        ax.plot(range(window-1, len(rewards)), roll, linewidth=2,
                color='red', label=f'{window}-ep rolling mean')
    ax.axhline(0, color='black', linewidth=0.5, linestyle='--')
    ax.set_xlabel('Episode')
    ax.set_ylabel('Total reward')
    ax.set_title('Episode Rewards During Evaluation')
    ax.legend()
    plt.tight_layout()
    path = os.path.join(save_dir, 'episode_rewards.png')
    plt.savefig(path, dpi=150)
    plt.close()
    print(f"[Eval] Saved → {path}")


def plot_predictions_sample(results: dict, save_dir: str, n_steps: int = 200):
    """Plot ground truth vs predicted alarm states for the first N steps."""
    y_true = results['y_true'][:n_steps]
    y_pred = results['y_pred'][:n_steps]
    t      = np.arange(len(y_true)) * 2.5 / 60   # convert to minutes

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 5), sharex=True)

    ax1.fill_between(t, y_true, alpha=0.6, color='red',   label='True danger')
    ax1.set_ylabel('Ground Truth')
    ax1.set_ylim(-0.1, 1.3)
    ax1.legend(loc='upper right')

    ax2.fill_between(t, y_pred, alpha=0.6, color='blue',  label='Agent alarm')
    # Highlight misses in orange
    missed = (y_true == 1) & (y_pred == 0)
    ax2.fill_between(t, missed.astype(float), alpha=0.5, color='orange', label='Missed danger')
    ax2.set_ylabel('Agent Prediction')
    ax2.set_xlabel('Time (minutes)')
    ax2.set_ylim(-0.1, 1.3)
    ax2.legend(loc='upper right')

    fig.suptitle('Ember RL Agent — Ground Truth vs Predictions', fontsize=12)
    plt.tight_layout()
    path = os.path.join(save_dir, 'predictions_sample.png')
    plt.savefig(path, dpi=150)
    plt.close()
    print(f"[Eval] Saved → {path}")


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Evaluate Ember RL agent')
    parser.add_argument('--model', type=str,
                        default=os.path.join(MODELS_DIR, 'best_model.zip'),
                        help='Path to trained model .zip')
    parser.add_argument('--data', type=str, default=SYNTH_CSV,
                        help='CSV file to evaluate on')
    parser.add_argument('--episodes', type=int, default=None,
                        help='Number of episodes to evaluate (default: all)')
    args = parser.parse_args()

    if not os.path.exists(args.model):
        print(f"[ERROR] Model not found: {args.model}")
        print("        Run train.py first.")
        sys.exit(1)

    if not os.path.exists(args.data):
        print(f"[ERROR] Data not found: {args.data}")
        print("        Run train.py --generate first.")
        sys.exit(1)

    Path(RESULTS).mkdir(parents=True, exist_ok=True)

    # Load model and data
    print(f"[Eval] Loading model from {args.model} ...")
    model = DQN.load(args.model)

    loader   = DataLoader(episode_length=60, stride=30)
    episodes = loader.load_synthetic(args.data)
    print(f"[Eval] Evaluating on {len(episodes)} episodes ...")

    # Run evaluation
    results = evaluate_model(model, episodes, n_episodes=args.episodes)
    print_report(results)

    # Save plots
    plot_confusion_matrix(results, RESULTS)
    plot_episode_rewards(results, RESULTS)
    plot_predictions_sample(results, RESULTS)

    print(f"\n[Eval] All plots saved to {RESULTS}/")
    print("       Open confusion_matrix.png, episode_rewards.png, predictions_sample.png")
