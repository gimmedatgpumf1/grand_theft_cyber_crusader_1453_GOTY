#!/usr/bin/env python3
"""Prepare reusable CC0 MakeHuman skin detail for the offline WebUI.

The source images are generic age/sex body skins, never portrait textures.
Albedo is normalized to a near-white modulation map so every game skin palette
entry remains usable. Normal and roughness maps contain only generic surface
detail and are shared by every citizen with the same sex/age texture set.

Run with: PYTHONPATH=.cache/python python3 tools/prepare-skin.py
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tools' / 'human-source' / 'skins'
OUTPUT = ROOT / 'web' / 'assets' / 'human'
SIZE = 1024


def normalized_albedo(image):
    pixels = np.asarray(image, dtype=np.float32) / 255.0
    r, g, b = pixels[..., 0], pixels[..., 1], pixels[..., 2]
    skin = (r > g * 1.025) & (g > b * 1.01) & (r > 0.35) & (r < 0.98) & ((r - b) < 0.55)
    sample = pixels[skin]
    if len(sample) < 1000:
        raise RuntimeError('Could not estimate the generic skin albedo')
    base = np.percentile(sample, 56, axis=0)
    detail = 1.0 + (pixels / np.maximum(base, 0.05) - 1.0) * 0.68
    detail = np.clip(detail, 0.28, 1.42)
    return Image.fromarray(np.uint8(np.clip(detail * 255.0, 0, 255)), 'RGB'), pixels


def surface_maps(image, pixels):
    luminance = pixels @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    lum_image = Image.fromarray(np.uint8(np.clip(luminance * 255.0, 0, 255)), 'L')
    fine = np.asarray(lum_image.filter(ImageFilter.GaussianBlur(2.0)), dtype=np.float32) / 255.0
    broad = np.asarray(lum_image.filter(ImageFilter.GaussianBlur(7.0)), dtype=np.float32) / 255.0
    height = (luminance - fine) + (luminance - broad) * 0.32
    dy, dx = np.gradient(height)
    normal = np.stack((-dx * 7.0, dy * 7.0, np.ones_like(dx)), axis=-1)
    normal /= np.maximum(np.linalg.norm(normal, axis=-1, keepdims=True), 1e-6)
    encoded = np.uint8(np.clip((normal * 0.5 + 0.5) * 255.0, 0, 255))
    normal_image = Image.fromarray(encoded, 'RGB')

    red_excess = pixels[..., 0] - (pixels[..., 1] + pixels[..., 2]) * 0.5
    roughness = 0.76 - np.maximum(red_excess - 0.12, 0.0) * 0.72
    roughness += np.clip(height, -0.035, 0.035) * 0.55
    roughness = np.uint8(np.clip(roughness * 255.0, 80, 220))
    return normal_image, Image.fromarray(roughness, 'L')


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for sex in ('female', 'male'):
        for age in ('young', 'old'):
            source = SOURCE / f'{age}_caucasian_{sex}.jpg'
            image = Image.open(source).convert('RGB').resize((SIZE, SIZE), Image.Resampling.LANCZOS)
            albedo, pixels = normalized_albedo(image)
            normal, roughness = surface_maps(image, pixels)
            stem = f'skin-{sex}-{age}'
            albedo.save(OUTPUT / f'{stem}-detail.jpg', quality=93, subsampling=0, optimize=True)
            normal.save(OUTPUT / f'{stem}-normal.png', optimize=True)
            roughness.save(OUTPUT / f'{stem}-roughness.jpg', quality=91, optimize=True)
            print(f'Prepared {stem}: universal albedo, normal and roughness')


if __name__ == '__main__':
    main()
