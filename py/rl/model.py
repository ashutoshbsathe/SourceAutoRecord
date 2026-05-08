"""
Model definitions: pure-Flax ViT-B/16, position projection, actor-critic.

Architecture:
    image (B,224,224,3) -> frozen ViT-B/16 -> CLS (B,768) -> LayerNorm -+
                                                                        |-> concat (B,1536) -> trunk -> heads
    position (B,3) -----> Dense(768) --------------------------> LayerNorm -+

The ViT is implemented from scratch in Flax and loads pretrained weights
from Google's official .npz checkpoint (no HuggingFace dependency).

Action heads are modular: swap IndependentActionHead for an autoregressive
variant by implementing the same interface (sample / log_prob / entropy).
"""

import os
import urllib.request
from typing import Dict, NamedTuple, Tuple

import jax
import jax.numpy as jnp
import numpy as np
import flax.linen as nn

# ──────────────────────────── Constants ───────────────────────────────────── #

IMAGENET_MEAN = jnp.array([0.485, 0.456, 0.406])
IMAGENET_STD = jnp.array([0.229, 0.224, 0.225])

VIT_B16_URL = "https://storage.googleapis.com/vit_models/imagenet21k/ViT-B_16.npz"
VIT_B16_CONFIG = dict(
    num_layers=12, num_heads=12, hidden_size=768, mlp_dim=3072, patch_size=16
)


# ═══════════════════════════ Pure-Flax ViT ════════════════════════════════ #


class MlpBlock(nn.Module):
    """Transformer MLP block (matching Google's checkpoint naming)."""

    mlp_dim: int = 3072

    @nn.compact
    def __call__(self, x):
        d = x.shape[-1]
        x = nn.Dense(self.mlp_dim, name="fc1")(x)
        x = nn.gelu(x)
        x = nn.Dense(d, name="fc2")(x)
        return x


class TransformerBlock(nn.Module):
    """Pre-LN transformer encoder block."""

    num_heads: int = 12
    mlp_dim: int = 3072

    @nn.compact
    def __call__(self, x):
        y = nn.LayerNorm(name="ln1")(x)
        y = nn.MultiHeadDotProductAttention(num_heads=self.num_heads, name="attn")(y, y)
        x = x + y

        y = nn.LayerNorm(name="ln2")(x)
        y = MlpBlock(self.mlp_dim, name="mlp")(y)
        x = x + y
        return x


def create_sinusoidal_positions(max_seq_len: int, embed_dim: int) -> jnp.ndarray:
    """Creates standard fixed sinusoidal positional embeddings."""
    position = np.arange(max_seq_len)[:, np.newaxis]
    div_term = np.exp(np.arange(0, embed_dim, 2) * -(np.log(10000.0) / embed_dim))
    pe = np.zeros((1, max_seq_len, embed_dim), dtype=np.float32)
    pe[0, :, 0::2] = np.sin(position * div_term)
    pe[0, :, 1::2] = np.cos(position * div_term)
    return jnp.array(pe)


class MemoryTransformerBlock(nn.Module):
    """Transformer block for RL sequence modeling (POMDP)."""

    num_heads: int = 8
    mlp_dim: int = 512

    @nn.compact
    def __call__(self, x, mask=None):
        y = nn.LayerNorm(name="ln1")(x)
        # Flax attention automatically converts boolean mask (False) to -inf
        # which is numerically stable before softmax.
        y = nn.MultiHeadDotProductAttention(num_heads=self.num_heads, name="attn")(
            y, y, mask=mask
        )
        x = x + y

        y = nn.LayerNorm(name="ln2")(x)
        y = MlpBlock(self.mlp_dim, name="mlp")(y)
        x = x + y
        return x


class ViTB16(nn.Module):
    """Vision Transformer B/16 — pure Flax, no external deps."""

    num_layers: int = 12
    num_heads: int = 12
    hidden_size: int = 768
    mlp_dim: int = 3072
    patch_size: int = 16
    image_size: int = 224

    @nn.compact
    def __call__(self, x):
        """Forward pass.

        Args:
            x: (B, H, W, C) float32, ImageNet-normalised, channels-last.
        Returns:
            (B, hidden_size) CLS token embedding.
        """
        B = x.shape[0]
        n_patches = (self.image_size // self.patch_size) ** 2  # 196

        # Patch embedding (Conv2D)
        x = nn.Conv(
            self.hidden_size,
            kernel_size=(self.patch_size, self.patch_size),
            strides=(self.patch_size, self.patch_size),
            padding="VALID",
            name="patch_embed",
        )(
            x
        )  # (B, 14, 14, 768)
        x = x.reshape(B, n_patches, self.hidden_size)

        # Prepend CLS token
        cls = self.param("cls_token", nn.initializers.zeros, (1, 1, self.hidden_size))
        x = jnp.concatenate(
            [jnp.broadcast_to(cls, (B, 1, self.hidden_size)), x], axis=1
        )

        # Position embeddings
        pos = self.param(
            "pos_embed", nn.initializers.zeros, (1, n_patches + 1, self.hidden_size)
        )
        x = x + pos

        # Transformer encoder
        for i in range(self.num_layers):
            x = TransformerBlock(
                num_heads=self.num_heads,
                mlp_dim=self.mlp_dim,
                name=f"block_{i}",
            )(x)

        x = nn.LayerNorm(name="final_ln")(x)
        return x[:, 0]  # CLS token


# ─────────── Pretrained weight loading from Google .npz ──────────────────── #


def _download_checkpoint(url: str, cache_dir: str = "~/.cache/vit_jax") -> str:
    """Download the ViT .npz checkpoint if not already cached."""
    cache_dir = os.path.expanduser(cache_dir)
    os.makedirs(cache_dir, exist_ok=True)
    filename = os.path.basename(url)
    local_path = os.path.join(cache_dir, filename)
    if os.path.exists(local_path):
        print(f"[ViT] Using cached checkpoint: {local_path}")
        return local_path
    print(f"[ViT] Downloading {url} ...")
    urllib.request.urlretrieve(url, local_path)
    print(f"[ViT] Saved to {local_path}")
    return local_path


def load_vit_params_from_npz(npz_path: str, num_layers: int = 12):
    """Convert Google's .npz checkpoint to our ViTB16 Flax param tree.

    Returns:
        {"params": { ... }}  ready for model.apply().
    """
    data = np.load(npz_path)

    params = {}

    # CLS token & position embeddings
    params["cls_token"] = data["cls"]
    params["pos_embed"] = data["Transformer/posembed_input/pos_embedding"]

    # Patch embedding (Conv)
    params["patch_embed"] = {
        "kernel": data["embedding/kernel"],
        "bias": data["embedding/bias"],
    }

    # Final LayerNorm
    params["final_ln"] = {
        "scale": data["Transformer/encoder_norm/scale"],
        "bias": data["Transformer/encoder_norm/bias"],
    }

    # Transformer blocks
    for i in range(num_layers):
        pf = f"Transformer/encoderblock_{i}"
        params[f"block_{i}"] = {
            "ln1": {
                "scale": data[f"{pf}/LayerNorm_0/scale"],
                "bias": data[f"{pf}/LayerNorm_0/bias"],
            },
            "attn": {
                "query": {
                    "kernel": data[f"{pf}/MultiHeadDotProductAttention_1/query/kernel"],
                    "bias": data[f"{pf}/MultiHeadDotProductAttention_1/query/bias"],
                },
                "key": {
                    "kernel": data[f"{pf}/MultiHeadDotProductAttention_1/key/kernel"],
                    "bias": data[f"{pf}/MultiHeadDotProductAttention_1/key/bias"],
                },
                "value": {
                    "kernel": data[f"{pf}/MultiHeadDotProductAttention_1/value/kernel"],
                    "bias": data[f"{pf}/MultiHeadDotProductAttention_1/value/bias"],
                },
                "out": {
                    "kernel": data[f"{pf}/MultiHeadDotProductAttention_1/out/kernel"],
                    "bias": data[f"{pf}/MultiHeadDotProductAttention_1/out/bias"],
                },
            },
            "ln2": {
                "scale": data[f"{pf}/LayerNorm_2/scale"],
                "bias": data[f"{pf}/LayerNorm_2/bias"],
            },
            "mlp": {
                "fc1": {
                    "kernel": data[f"{pf}/MlpBlock_3/Dense_0/kernel"],
                    "bias": data[f"{pf}/MlpBlock_3/Dense_0/bias"],
                },
                "fc2": {
                    "kernel": data[f"{pf}/MlpBlock_3/Dense_1/kernel"],
                    "bias": data[f"{pf}/MlpBlock_3/Dense_1/bias"],
                },
            },
        }

    # Convert all arrays to jnp, in bfloat16 to halve VRAM
    params = jax.tree.map(lambda x: jnp.array(x, dtype=jnp.bfloat16), params)
    return {"params": params}


# ─────────────────── Frozen ViT vision encoder ───────────────────────────── #


class VisionEncoder:
    """Wrapper around a frozen ViT-B/16 with pretrained weights.

    Parameters live outside the trainable Flax param tree so they never
    receive gradient updates.  Weights and inference run in bfloat16 to
    halve VRAM usage (~172 MB instead of ~344 MB).
    """

    def __init__(self, checkpoint_path: str = ""):
        # Download if no local path provided
        if not checkpoint_path or not os.path.exists(checkpoint_path):
            checkpoint_path = _download_checkpoint(VIT_B16_URL)

        self.model = ViTB16(**VIT_B16_CONFIG)
        self.variables = load_vit_params_from_npz(
            checkpoint_path, VIT_B16_CONFIG["num_layers"]
        )
        self.hidden_size: int = VIT_B16_CONFIG["hidden_size"]

        # JIT-compile the forward pass
        self._apply_jit = jax.jit(self.model.apply)

        n_params = sum(x.size for x in jax.tree.leaves(self.variables))
        dtype = jax.tree.leaves(self.variables["params"])[0].dtype
        print(
            f"[VisionEncoder] Loaded ViT-B/16: {n_params:,} params, "
            f"dtype={dtype}, hidden_size={self.hidden_size}"
        )

    def __call__(self, images: jnp.ndarray) -> jnp.ndarray:
        """Encode a batch of images to CLS-token embeddings.

        Args:
            images: (B, 224, 224, 3) float32 in [0, 1].
        Returns:
            (B, 768) float32 embeddings.
        """
        # ImageNet normalisation then cast to bf16 for ViT inference
        normalized = (images - IMAGENET_MEAN) / IMAGENET_STD
        normalized = normalized.astype(jnp.bfloat16)
        # Run ViT in bf16, cast output back to float32 for ActorCritic
        return self._apply_jit(self.variables, normalized).astype(jnp.float32)


# ──────────────────── Action distribution parameters ─────────────────────── #


class ActionDistParams(NamedTuple):
    """Container for the outputs of the action head."""

    move_fb_logits: jnp.ndarray  # (B, 3)
    move_lr_logits: jnp.ndarray  # (B, 3)
    zoom_logits: jnp.ndarray  # (B, 3)
    portal_logits: jnp.ndarray  # (B, 3)
    buttons_logits: jnp.ndarray  # (B, 3) — independent Bernoulli
    mouse_mean: jnp.ndarray  # (B, 2)
    mouse_log_std: jnp.ndarray  # (2,)


# ─────────────── Action head (independent factorisation) ─────────────────── #


class IndependentActionHead(nn.Module):
    """Independent action distribution: each component sampled independently.

    Interface (sample / log_prob / entropy are static methods) so the head
    can be swapped for an autoregressive variant with the same API.
    """

    @nn.compact
    def __call__(self, features: jnp.ndarray) -> ActionDistParams:
        move_fb = nn.Dense(3, name="head_move_fb")(features)
        move_lr = nn.Dense(3, name="head_move_lr")(features)
        zoom = nn.Dense(3, name="head_zoom")(features)
        portal = nn.Dense(3, name="head_portal")(features)
        buttons = nn.Dense(3, name="head_buttons")(features)
        mouse_mean = nn.Dense(2, name="head_mouse_mean")(features)
        mouse_log_std = self.param("mouse_log_std", nn.initializers.zeros, (2,))
        return ActionDistParams(
            move_fb_logits=move_fb,
            move_lr_logits=move_lr,
            zoom_logits=zoom,
            portal_logits=portal,
            buttons_logits=buttons,
            mouse_mean=mouse_mean,
            mouse_log_std=mouse_log_std,
        )

    # ── sampling ──────────────────────────────────────────────────────── #

    @staticmethod
    def sample(dist: ActionDistParams, rng: jnp.ndarray):
        keys = jax.random.split(rng, 6)
        lp = jnp.zeros(dist.move_fb_logits.shape[:-1])

        def _cat(logits, key):
            a = jax.random.categorical(key, logits)
            lp_all = logits - jax.nn.logsumexp(logits, axis=-1, keepdims=True)
            return a, jnp.take_along_axis(lp_all, a[..., None], axis=-1).squeeze(-1)

        move_fb, lp_fb = _cat(dist.move_fb_logits, keys[0])
        move_lr, lp_lr = _cat(dist.move_lr_logits, keys[1])
        zoom, lp_z = _cat(dist.zoom_logits, keys[2])
        portal, lp_p = _cat(dist.portal_logits, keys[3])
        lp = lp + lp_fb + lp_lr + lp_z + lp_p

        probs = jax.nn.sigmoid(dist.buttons_logits)
        buttons = jax.random.bernoulli(keys[4], probs).astype(jnp.int32)
        lp_btn = buttons * jax.nn.log_sigmoid(dist.buttons_logits) + (
            1 - buttons
        ) * jax.nn.log_sigmoid(-dist.buttons_logits)
        lp = lp + lp_btn.sum(axis=-1)

        std = jnp.exp(dist.mouse_log_std)
        noise = jax.random.normal(keys[5], dist.mouse_mean.shape)
        mouse_raw = dist.mouse_mean + std * noise
        mouse = jnp.clip(mouse_raw, -1.0, 1.0)
        lp_m = (
            -0.5 * ((mouse_raw - dist.mouse_mean) / std) ** 2
            - jnp.log(std)
            - 0.5 * jnp.log(2 * jnp.pi)
        )
        lp = lp + lp_m.sum(axis=-1)

        actions = {
            "move_fb": move_fb,
            "move_lr": move_lr,
            "zoom": zoom,
            "portal": portal,
            "buttons": buttons,
            "mouse": mouse,
        }
        return actions, lp

    # ── log-prob re-evaluation ────────────────────────────────────────── #

    @staticmethod
    def log_prob(dist: ActionDistParams, actions: Dict[str, jnp.ndarray]):
        lp = jnp.zeros(dist.move_fb_logits.shape[:-1])

        def _clp(logits, a):
            lp_all = logits - jax.nn.logsumexp(logits, axis=-1, keepdims=True)
            return jnp.take_along_axis(lp_all, a[..., None], axis=-1).squeeze(-1)

        lp = (
            lp
            + _clp(dist.move_fb_logits, actions["move_fb"])
            + _clp(dist.move_lr_logits, actions["move_lr"])
            + _clp(dist.zoom_logits, actions["zoom"])
            + _clp(dist.portal_logits, actions["portal"])
        )

        b = actions["buttons"].astype(jnp.float32)
        lp_btn = b * jax.nn.log_sigmoid(dist.buttons_logits) + (
            1 - b
        ) * jax.nn.log_sigmoid(-dist.buttons_logits)
        lp = lp + lp_btn.sum(axis=-1)

        std = jnp.exp(dist.mouse_log_std)
        m = actions["mouse"]
        lp_m = (
            -0.5 * ((m - dist.mouse_mean) / std) ** 2
            - jnp.log(std)
            - 0.5 * jnp.log(2 * jnp.pi)
        )
        lp = lp + lp_m.sum(axis=-1)
        return lp

    # ── entropy ───────────────────────────────────────────────────────── #

    @staticmethod
    def entropy(dist: ActionDistParams):
        ent = jnp.zeros(dist.move_fb_logits.shape[:-1])

        def _ce(logits):
            p = jax.nn.softmax(logits, axis=-1)
            return -(p * jax.nn.log_softmax(logits, axis=-1)).sum(axis=-1)

        ent = ent + _ce(dist.move_fb_logits) + _ce(dist.move_lr_logits)
        ent = ent + _ce(dist.zoom_logits) + _ce(dist.portal_logits)

        p = jax.nn.sigmoid(dist.buttons_logits)
        be = -(
            p * jax.nn.log_sigmoid(dist.buttons_logits)
            + (1 - p) * jax.nn.log_sigmoid(-dist.buttons_logits)
        )
        ent = ent + be.sum(axis=-1)

        std = jnp.exp(dist.mouse_log_std)
        ent = ent + (0.5 * jnp.log(2 * jnp.pi * jnp.e * std**2)).sum()
        return ent


# ──────────────────────── Actor-Critic module ────────────────────────────── #


class ActorCritic(nn.Module):
    """Shared-trunk actor-critic that consumes pre-computed ViT embeddings."""

    embed_dim: int = 768
    trunk_hidden: int = 512
    trunk_out: int = 256
    max_seq_len: int = 128
    transformer_blocks: int = 1
    transformer_heads: int = 8

    @nn.compact
    def __call__(self, image_embed, position):
        """
        Args:
            image_embed: (B, T, 768) or (B, 768)
            position: (B, T, 3) or (B, 3)
        """
        is_single = image_embed.ndim == 2
        if is_single:
            image_embed = image_embed[:, None, :]
            position = position[:, None, :]

        B, T, _ = image_embed.shape

        img = nn.LayerNorm(name="ln_image")(image_embed)
        pos = nn.Dense(self.embed_dim, name="pos_proj")(position)
        pos = nn.LayerNorm(name="ln_pos")(pos)

        fused = jnp.concatenate([img, pos], axis=-1)

        # Fixed Sinusoidal Positional Encoding
        pe = create_sinusoidal_positions(self.max_seq_len, fused.shape[-1])
        x = fused + pe[:, :T, :]

        # Causal mask for sequence modeling.
        # make_causal_mask produces a boolean mask where True=keep, False=mask (-inf in softmax).
        mask = nn.make_causal_mask(jnp.ones((B, T)))

        for i in range(self.transformer_blocks):
            x = MemoryTransformerBlock(
                num_heads=self.transformer_heads,
                mlp_dim=self.trunk_hidden,
                name=f"memory_block_{i}",
            )(x, mask=mask)

        x = nn.Dense(self.trunk_hidden, name="trunk_fc1")(x)
        x = nn.relu(x)
        x = nn.LayerNorm(name="ln_trunk1")(x)
        x = nn.Dense(self.trunk_out, name="trunk_fc2")(x)
        x = nn.relu(x)
        x = nn.LayerNorm(name="ln_trunk2")(x)

        if is_single:
            x = x[:, 0, :]

        dist_params = IndependentActionHead(name="action_head")(x)
        value = nn.Dense(1, name="critic_head")(x)
        return dist_params, value
