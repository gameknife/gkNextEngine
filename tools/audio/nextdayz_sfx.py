#!/usr/bin/env python3
"""Procedural sound effects for NextDayz.

Every NextDayz sound is synthesised here from noise, oscillators and simple
spectral shaping -- no recorded or third-party samples. Each sound is seeded;
running the script twice writes identical files. The .wav files are not
committed (assets/sounds/nextdayz/ is git-ignored): run this once per checkout.
Without them the game runs silent and NextAudio warns once per missing file.

    python tools/audio/nextdayz_sfx.py            # writes assets/sounds/nextdayz/*.wav
    python tools/audio/nextdayz_sfx.py --out DIR  # somewhere else

Needs numpy only. Game code reaches these files through
src/Application/Game/NextDayz/Audio/NextDayzAudio.hpp; keep the names in sync.
"""

import argparse
import os
import wave

import numpy as np

RATE = 32000


# --- building blocks -----------------------------------------------------------

def t_axis(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def env_exp(seconds, decay, attack=0.002):
    """Linear attack, exponential decay (`decay` = time constant, s)."""
    t = t_axis(seconds)
    env = np.exp(-t / max(decay, 1e-4))
    if attack > 0:
        env *= np.clip(t / attack, 0.0, 1.0)
    return env


def shape(signal, gains):
    """Zero-phase spectral filter. `gains(freq_hz_array) -> gain array`."""
    spectrum = np.fft.rfft(signal)
    freqs = np.fft.rfftfreq(len(signal), 1.0 / RATE)
    return np.fft.irfft(spectrum * gains(freqs), len(signal))


def lowpass(signal, cutoff, order=2):
    return shape(signal, lambda f: 1.0 / np.sqrt(1.0 + (f / cutoff) ** (2 * order)))


def highpass(signal, cutoff, order=2):
    return shape(signal, lambda f: 1.0 / np.sqrt(1.0 + (cutoff / np.maximum(f, 1e-3)) ** (2 * order)))


def bandpass(signal, center, width):
    return shape(signal, lambda f: np.exp(-0.5 * ((f - center) / width) ** 2))


def formants(signal, bands):
    """Sum of gaussian resonances: bands = [(center, width, gain), ...]."""
    return shape(signal, lambda f: sum(g * np.exp(-0.5 * ((f - c) / w) ** 2) for c, w, g in bands))


def place(total, sound, at):
    """Mixes `sound` into `total` starting at `at` seconds (extends as needed)."""
    start = int(at * RATE)
    end = start + len(sound)
    if end > len(total):
        total = np.concatenate([total, np.zeros(end - len(total))])
    total[start:end] += sound
    return total


def reverb(signal, seconds, decay, mix, rng, damp=2500.0):
    """Convolution with a decaying, darkened noise tail (an outdoor slap)."""
    tail = rng.standard_normal(int(seconds * RATE)) * env_exp(seconds, decay, attack=0.004)
    tail = lowpass(tail, damp)
    tail /= np.sqrt(np.sum(tail ** 2)) + 1e-9
    n = len(signal) + len(tail)
    size = 1 << (n - 1).bit_length()
    wet = np.fft.irfft(np.fft.rfft(signal, size) * np.fft.rfft(tail, size), size)[:n]
    out = np.concatenate([signal, np.zeros(len(tail))])
    return out + wet * mix


def glottal(f0, seconds, rng, jitter=0.02, shimmer=0.15, vibrato=(5.0, 0.03)):
    """Voiced source: band-limited pulse train following the f0 curve."""
    t = t_axis(seconds)
    f = np.interp(t, np.linspace(0, seconds, len(f0)), f0) if np.ndim(f0) else np.full(len(t), f0)
    f = f * (1.0 + vibrato[1] * np.sin(2 * np.pi * vibrato[0] * t))
    f = f * (1.0 + jitter * lowpass(rng.standard_normal(len(t)), 30.0) * 3.0)
    phase = np.cumsum(f) / RATE
    # Sum of harmonics with a -12 dB/oct roll-off: a breathy, buzzy throat.
    source = np.zeros(len(t))
    for k in range(1, 40):
        source += np.sin(2 * np.pi * k * phase) / (k ** 1.15)
    amp = 1.0 + shimmer * lowpass(rng.standard_normal(len(t)), 25.0) * 3.0
    return source * amp


def finish(signal, peak=0.89, fade=0.01):
    signal = signal - np.mean(signal)
    n = int(fade * RATE)
    if n > 0 and len(signal) > 2 * n:
        signal[-n:] *= np.linspace(1.0, 0.0, n)
    top = np.max(np.abs(signal)) + 1e-9
    return signal * (peak / top)


def write(path, signal):
    data = (np.clip(signal, -1.0, 1.0) * 32767.0).astype(np.int16)
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(data.tobytes())


# --- weapons ---------------------------------------------------------------------

def gunshot(rng, body_hz, boom, crack, tail_seconds, tail_mix, pitch=1.0, mech=0.15):
    """Supersonic crack + muzzle blast + low thump + the land giving it back."""
    seconds = 0.35
    noise = rng.standard_normal(int(seconds * RATE))
    crack_part = highpass(noise, 1800.0 * pitch) * env_exp(seconds, 0.012, attack=0.0005) * crack
    blast = lowpass(noise, 2400.0 * pitch) * env_exp(seconds, 0.045, attack=0.001)
    t = t_axis(seconds)
    sweep = body_hz * pitch * (1.0 + 1.6 * np.exp(-t / 0.02))
    thump = np.sin(2 * np.pi * np.cumsum(sweep) / RATE) * env_exp(seconds, 0.07, attack=0.001) * boom
    dry = crack_part + blast + thump
    if mech > 0:
        click = highpass(rng.standard_normal(int(0.02 * RATE)), 3000.0) * env_exp(0.02, 0.003) * mech
        dry = place(dry, click, 0.055)
    wet = reverb(dry, tail_seconds, tail_seconds * 0.28, tail_mix, rng, damp=1800.0)
    # Two slaps off far terrain.
    for delay, gain in ((0.09, 0.22), (0.21, 0.12)):
        wet = place(wet, lowpass(dry, 1200.0) * gain, delay)
    return finish(wet)


def reload_rifle(rng):
    out = np.zeros(int(2.25 * RATE))

    def clack(at, bright, gain, length=0.05):
        nonlocal out
        burst = rng.standard_normal(int(length * RATE))
        part = bandpass(burst, bright, bright * 0.45) * env_exp(length, length * 0.18, attack=0.0005)
        ring = np.sin(2 * np.pi * bright * 0.8 * t_axis(length)) * env_exp(length, length * 0.25) * 0.3
        out = place(out, (part + ring) * gain, at)

    def slide(at, length, gain, center=1600.0):
        nonlocal out
        noise = bandpass(rng.standard_normal(int(length * RATE)), center, 900.0)
        env = np.sin(np.pi * np.clip(t_axis(length) / length, 0, 1)) ** 0.6
        out = place(out, noise * env * gain, at)

    clack(0.22, 2600.0, 0.6)          # magazine catch
    slide(0.27, 0.16, 0.35)           # magazine out
    slide(0.62, 0.30, 0.18, 900.0)    # hand to the pouch (cloth)
    slide(1.15, 0.14, 0.3)            # magazine in
    clack(1.30, 1800.0, 1.0, 0.07)    # seated
    clack(1.52, 1400.0, 0.7, 0.06)    # palm slap
    slide(1.80, 0.09, 0.45, 2400.0)   # bolt back
    clack(1.90, 3200.0, 0.9)          # bolt home
    return finish(reverb(out, 0.25, 0.05, 0.15, rng), peak=0.7)


def dry_fire(rng):
    burst = highpass(rng.standard_normal(int(0.06 * RATE)), 2500.0) * env_exp(0.06, 0.006, attack=0.0003)
    return finish(burst, peak=0.5)


def switch_weapon(rng):
    out = np.zeros(int(0.9 * RATE))
    cloth = bandpass(rng.standard_normal(int(0.45 * RATE)), 700.0, 500.0)
    cloth *= np.sin(np.pi * t_axis(0.45) / 0.45) ** 0.8
    out = place(out, cloth * 0.5, 0.05)
    click = bandpass(rng.standard_normal(int(0.05 * RATE)), 2200.0, 900.0) * env_exp(0.05, 0.008)
    out = place(out, click, 0.62)
    return finish(out, peak=0.55)


# --- hits ------------------------------------------------------------------------

def flesh_hit(rng, wet=1.0, thump_hz=85.0):
    seconds = 0.25
    noise = rng.standard_normal(int(seconds * RATE))
    smack = bandpass(noise, 650.0, 380.0) * env_exp(seconds, 0.03, attack=0.0008)
    t = t_axis(seconds)
    thump = np.sin(2 * np.pi * thump_hz * t * (1.0 + 0.5 * np.exp(-t / 0.015))) * env_exp(seconds, 0.04) * 0.8
    squelch = bandpass(noise, 1500.0, 500.0) * env_exp(seconds, 0.05, attack=0.01) * 0.35 * wet
    return finish(smack + thump + squelch)


def head_hit(rng):
    base = flesh_hit(rng, wet=1.4, thump_hz=110.0)
    t = t_axis(len(base) / RATE)
    # The bright crack that tells you it was the head.
    ping = (np.sin(2 * np.pi * 2900.0 * t) + 0.6 * np.sin(2 * np.pi * 4300.0 * t)) * np.exp(-t / 0.035)
    crack = highpass(rng.standard_normal(len(t)), 3500.0) * np.exp(-t / 0.006)
    return finish(base + ping * 0.55 + crack * 0.6)


def kill_confirm(rng):
    t = t_axis(0.22)
    thud = np.sin(2 * np.pi * 70.0 * t) * np.exp(-t / 0.06)
    tick = np.sin(2 * np.pi * 1750.0 * t) * np.exp(-t / 0.02) * 0.25
    return finish(thud + tick, peak=0.6)


def impact_dirt(rng):
    seconds = 0.3
    noise = rng.standard_normal(int(seconds * RATE))
    puff = lowpass(noise, 1800.0) * env_exp(seconds, 0.035, attack=0.0005)
    grit = highpass(noise, 3000.0) * env_exp(seconds, 0.08, attack=0.01) * 0.15
    return finish(puff + grit, peak=0.6)


def body_fall(rng):
    seconds = 0.6
    out = np.zeros(int(seconds * RATE))
    for at, gain in ((0.0, 1.0), (0.12, 0.55), (0.25, 0.3)):
        t = t_axis(0.25)
        thud = np.sin(2 * np.pi * 55.0 * t * (1 + 0.6 * np.exp(-t / 0.02))) * np.exp(-t / 0.06)
        dirt = lowpass(rng.standard_normal(len(t)), 900.0) * np.exp(-t / 0.05) * 0.5
        out = place(out, (thud + dirt) * gain, at)
    return finish(out, peak=0.75)


# --- voices ----------------------------------------------------------------------

def vowel(kind):
    return {
        "ah": [(720, 110, 1.0), (1150, 140, 0.55), (2500, 220, 0.25)],
        "uh": [(560, 100, 1.0), (1000, 130, 0.45), (2400, 220, 0.18)],
        "aa": [(850, 140, 1.0), (1350, 170, 0.7), (2750, 260, 0.35)],
        "oh": [(500, 90, 1.0), (850, 120, 0.4), (2400, 200, 0.15)],
    }[kind]


def zombie_voice(rng, seconds, f0_curve, kind, rasp=0.6, growl_hz=24.0, breath=0.35, attack=0.05, release=0.25):
    source = glottal(np.asarray(f0_curve, dtype=float), seconds, rng, jitter=0.05, shimmer=0.35,
                     vibrato=(4.0 + rng.random() * 2.0, 0.04))
    t = t_axis(seconds)
    # Rasp: the throat flutters, chopping the voice into a growl.
    flutter = 1.0 - rasp * 0.5 * (1.0 + np.sin(2 * np.pi * growl_hz * t + 3 * lowpass(rng.standard_normal(len(t)), 8.0)))
    voiced = formants(source * flutter, vowel(kind))
    air = formants(rng.standard_normal(len(t)), vowel(kind)) * breath
    env = np.clip(t / attack, 0, 1) * np.clip((seconds - t) / release, 0, 1)
    env *= 0.75 + 0.25 * lowpass(rng.standard_normal(len(t)), 4.0) * 3.0
    return (voiced + air) * env


def zombie_groan(rng, seconds, low, high, kind):
    f0 = [low, high, (low + high) / 2, low * 0.9]
    return finish(reverb(zombie_voice(rng, seconds, f0, kind), 0.4, 0.08, 0.25, rng))


def zombie_alert(rng):
    voice = zombie_voice(rng, 1.0, [160, 260, 240, 190, 140], "aa", rasp=0.75, growl_hz=31.0, breath=0.6,
                         attack=0.03, release=0.3)
    return finish(reverb(voice, 0.6, 0.12, 0.35, rng))


def zombie_attack(rng):
    voice = zombie_voice(rng, 0.55, [120, 170, 110], "ah", rasp=0.85, growl_hz=36.0, breath=0.7, attack=0.02,
                         release=0.12)
    snap = highpass(rng.standard_normal(int(0.03 * RATE)), 2500.0) * env_exp(0.03, 0.004)
    return finish(place(voice, snap * 0.8, 0.42))


def zombie_death(rng):
    voice = zombie_voice(rng, 1.1, [140, 110, 80, 60], "oh", rasp=0.9, growl_hz=14.0, breath=0.8, attack=0.02,
                         release=0.4)
    # Gurgle: bubbling amplitude.
    t = t_axis(len(voice) / RATE)
    voice *= 0.6 + 0.4 * np.abs(lowpass(rng.standard_normal(len(t)), 18.0)) * 4.0
    return finish(reverb(voice, 0.4, 0.08, 0.2, rng))


def player_hurt(rng, f0, kind):
    voice = zombie_voice(rng, 0.32, [f0 * 1.15, f0, f0 * 0.85], kind, rasp=0.15, growl_hz=10.0, breath=0.5,
                         attack=0.01, release=0.15)
    t = t_axis(0.12)
    punch = np.sin(2 * np.pi * 80.0 * t) * np.exp(-t / 0.03)
    return finish(place(voice * 0.8, punch, 0.0), peak=0.8)


# --- survival and atmosphere ------------------------------------------------------

def melee_swing(rng, pitch=1.0):
    """A blade or bar cutting air: band-passed noise sweeping up then down."""
    seconds = 0.32
    t = t_axis(seconds)
    noise = rng.standard_normal(len(t))
    out = np.zeros(len(t))
    centers = 700.0 * pitch + 1400.0 * pitch * np.sin(np.pi * t / seconds)
    for k in range(6):
        a, b = k * len(t) // 6, (k + 1) * len(t) // 6
        out[a:b] = bandpass(noise, float(np.mean(centers[a:b])), 450.0)[a:b]
    env = np.sin(np.pi * np.clip(t / seconds, 0, 1)) ** 2
    return finish(out * env, peak=0.55)


def melee_impact(rng):
    """Blunt metal into a body: a heavy thud with a short crunch."""
    base = flesh_hit(rng, wet=0.6, thump_hz=70.0)
    t = t_axis(len(base) / RATE)
    crunch = bandpass(rng.standard_normal(len(t)), 2200.0, 900.0) * np.exp(-t / 0.018) * 0.7
    return finish(base + crunch, peak=0.8)


def firecracker(rng):
    """Three sharp pops with an outdoor slap."""
    out = np.zeros(int(0.9 * RATE))
    for at, gain in ((0.0, 1.0), (0.13 + rng.random() * 0.05, 0.8), (0.31 + rng.random() * 0.06, 0.9)):
        n = int(0.05 * RATE)
        pop = highpass(rng.standard_normal(n), 900.0) * env_exp(0.05, 0.006, attack=0.0003)
        out = place(out, pop * gain, at)
    return finish(reverb(out, 0.9, 0.2, 0.5, rng), peak=0.8)


def wind(rng, seconds=7.0, gust=1.0):
    """Wind over open ground: brown-ish noise breathing through a moving low-pass."""
    n = int(seconds * RATE)
    noise = np.cumsum(rng.standard_normal(n))
    noise = highpass(noise - np.mean(noise), 40.0)
    noise /= np.max(np.abs(noise)) + 1e-9
    t = t_axis(seconds)
    swell = 0.55 + 0.45 * np.sin(2 * np.pi * t / seconds * (1.0 + gust) + rng.random() * 6.0)
    whistle = bandpass(rng.standard_normal(n), 520.0 + 180.0 * gust, 60.0) * 0.25
    body = lowpass(noise, 380.0) + whistle
    env = np.clip(t / 1.5, 0, 1) * np.clip((seconds - t) / 1.5, 0, 1)
    return finish(body * swell * env, peak=0.6, fade=0.2)


def crow(rng):
    """Two or three harsh caws drifting over the fields."""
    out = np.zeros(int(1.6 * RATE))
    at = 0.0
    for _ in range(2 + int(rng.random() * 2)):
        caw = zombie_voice(rng, 0.28, [620, 700, 560], "aa", rasp=0.9, growl_hz=70.0, breath=0.5, attack=0.01,
                           release=0.1)
        out = place(out, highpass(caw, 400.0), at)
        at += 0.36 + rng.random() * 0.12
    return finish(reverb(lowpass(out, 3500.0), 0.8, 0.18, 0.45, rng), peak=0.5)


def distant_shot(rng, body_hz):
    """Someone else's fight, far away: a dull crack and a long rolling echo."""
    shot = gunshot(rng, body_hz, 1.0, 0.4, 2.0, 1.2, pitch=0.85, mech=0.0)
    return finish(reverb(lowpass(shot, 700.0), 2.5, 0.6, 1.4, rng, damp=600.0), peak=0.45, fade=0.3)


def distant_scream(rng):
    voice = zombie_voice(rng, 1.4, [180, 300, 280, 200, 150], "aa", rasp=0.8, growl_hz=28.0, breath=0.7,
                         attack=0.05, release=0.5)
    return finish(reverb(lowpass(voice, 1400.0), 2.0, 0.5, 1.2, rng, damp=900.0), peak=0.4, fade=0.3)


def rustle(rng, seconds=0.45, center=2600.0):
    """Cloth and gear being handled (picking up, packing)."""
    t = t_axis(seconds)
    noise = rng.standard_normal(len(t))
    grains = np.abs(lowpass(rng.standard_normal(len(t)), 25.0)) * 3.0
    out = bandpass(noise, center, 1400.0) * grains * np.sin(np.pi * t / seconds)
    return finish(out, peak=0.45)


def eat(rng):
    out = np.zeros(int(1.0 * RATE))
    for k in range(4):
        n = int(0.12 * RATE)
        crunch = bandpass(rng.standard_normal(n), 1800.0 + rng.random() * 800.0, 900.0) * env_exp(0.12, 0.03)
        out = place(out, crunch * (0.7 + 0.3 * rng.random()), 0.05 + k * 0.22)
    return finish(out, peak=0.45)


def drink(rng):
    out = np.zeros(int(1.0 * RATE))
    for k in range(3):
        t = t_axis(0.18)
        gulp = np.sin(2 * np.pi * (180.0 + 260.0 * np.exp(-t / 0.04)) * t) * np.exp(-t / 0.05)
        out = place(out, gulp, 0.1 + k * 0.28)
    return finish(lowpass(out, 1200.0), peak=0.5)


def bandage(rng):
    """Fabric tearing, then wrapping."""
    seconds = 0.9
    t = t_axis(seconds)
    tear = highpass(rng.standard_normal(len(t)), 1500.0) * (np.abs(lowpass(rng.standard_normal(len(t)), 60.0)) * 4.0)
    env = np.clip(t / 0.02, 0, 1) * np.exp(-t / 0.25)
    return finish(tear * env + rustle(rng, seconds, 2000.0) * 0.5, peak=0.5)


def radio_static(rng):
    """Tuning across the band: hiss, whistles, a broken voice-like burst and three beeps."""
    seconds = 2.2
    t = t_axis(seconds)
    hiss = bandpass(rng.standard_normal(len(t)), 2400.0, 1600.0) * (0.6 + 0.4 * np.sin(2 * np.pi * 3.0 * t))
    whistle = np.sin(2 * np.pi * (900.0 + 700.0 * np.sin(2 * np.pi * 0.6 * t)) * t) * 0.15
    voice = zombie_voice(rng, 0.7, [150, 170, 140], "ah", rasp=0.3, growl_hz=12.0, breath=0.2)
    out = place(hiss * 0.5 + whistle, bandpass(voice, 1200.0, 600.0) * 0.8, 0.6)
    for k in range(3):
        bt = t_axis(0.12)
        out = place(out, np.sin(2 * np.pi * 1000.0 * bt) * 0.5, 1.6 + k * 0.18)
    return finish(out, peak=0.5)


def lantern(rng):
    t = t_axis(0.5)
    click = highpass(rng.standard_normal(len(t)), 3000.0) * np.exp(-t / 0.004)
    whoosh = lowpass(rng.standard_normal(len(t)), 900.0) * np.clip(t / 0.05, 0, 1) * np.exp(-t / 0.15) * 0.6
    return finish(click + whoosh, peak=0.45)


# --- the set ---------------------------------------------------------------------

def build(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    sounds = {}

    def add(name, seed, make):
        sounds[name] = make(np.random.default_rng(seed))

    for i in range(3):
        add(f"shot_rifle_{i + 1}", 100 + i,
            lambda r, i=i: gunshot(r, 95.0, 0.9, 1.0, 0.9, 0.55, pitch=1.0 + (i - 1) * 0.04, mech=0.18))
    for i in range(2):
        add(f"shot_sniper_{i + 1}", 110 + i,
            lambda r, i=i: gunshot(r, 70.0, 1.2, 1.2, 1.6, 0.8, pitch=0.92 + i * 0.05, mech=0.1))
        add(f"shot_shotgun_{i + 1}", 120 + i,
            lambda r, i=i: gunshot(r, 60.0, 1.5, 0.6, 1.2, 0.7, pitch=0.8 + i * 0.05, mech=0.0))
        add(f"shot_pistol_{i + 1}", 130 + i,
            lambda r, i=i: gunshot(r, 130.0, 0.5, 0.9, 0.6, 0.4, pitch=1.15 + i * 0.05, mech=0.25))
    add("reload_rifle", 140, reload_rifle)
    add("dry_fire", 141, dry_fire)
    add("switch_weapon", 142, switch_weapon)
    for i in range(3):
        add(f"hit_flesh_{i + 1}", 150 + i, lambda r: flesh_hit(r))
    for i in range(2):
        add(f"hit_head_{i + 1}", 160 + i, head_hit)
        add(f"impact_dirt_{i + 1}", 170 + i, impact_dirt)
    add("kill_confirm", 175, kill_confirm)
    add("body_fall", 176, body_fall)
    groans = [(1.6, 70, 95, "uh"), (1.3, 85, 120, "ah"), (1.9, 62, 80, "oh"), (1.2, 95, 135, "ah")]
    for i, (seconds, low, high, kind) in enumerate(groans):
        add(f"zombie_groan_{i + 1}", 180 + i, lambda r, a=(seconds, low, high, kind): zombie_groan(r, *a))
    for i in range(2):
        add(f"zombie_alert_{i + 1}", 190 + i, zombie_alert)
        add(f"zombie_attack_{i + 1}", 195 + i, zombie_attack)
        add(f"zombie_death_{i + 1}", 200 + i, zombie_death)
    add("player_hurt_1", 210, lambda r: player_hurt(r, 125.0, "uh"))
    add("player_hurt_2", 211, lambda r: player_hurt(r, 110.0, "ah"))
    for i in range(2):
        add(f"melee_swing_{i + 1}", 220 + i, lambda r, i=i: melee_swing(r, 1.0 + i * 0.15))
    add("melee_impact", 222, melee_impact)
    add("firecracker", 223, firecracker)
    for i in range(3):
        add(f"ambient_wind_{i + 1}", 230 + i, lambda r, i=i: wind(r, 7.0 + i, 0.6 + i * 0.4))
    for i in range(2):
        add(f"ambient_crow_{i + 1}", 235 + i, crow)
        add(f"distant_shot_{i + 1}", 237 + i, lambda r, i=i: distant_shot(r, 90.0 - i * 25.0))
    add("distant_scream", 239, distant_scream)
    add("pickup", 240, lambda r: rustle(r))
    add("item_drop", 241, lambda r: rustle(r, 0.3, 900.0))
    add("eat", 242, eat)
    add("drink", 243, drink)
    add("bandage", 244, bandage)
    add("radio_static", 245, radio_static)
    add("lantern", 246, lantern)

    for name, signal in sounds.items():
        write(os.path.join(out_dir, name + ".wav"), signal)
    return sorted(sounds)


def main():
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", default=os.path.join(root, "assets", "sounds", "nextdayz"))
    args = parser.parse_args()
    names = build(args.out)
    print(f"wrote {len(names)} sounds to {args.out}")


if __name__ == "__main__":
    main()
