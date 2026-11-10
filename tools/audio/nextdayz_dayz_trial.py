#!/usr/bin/env python3
"""Install a local DayZ audio trial over NextDayz's generated WAV files.

    python tools/audio/nextdayz_dayz_trial.py --dayz-root PATH
    python tools/audio/nextdayz_dayz_trial.py --restore

Requires numpy and ffmpeg (PATH, --ffmpeg, or imageio_ffmpeg). Reads only the
selected, uncompressed OGG entries from the installed game's PBOs. Samples,
backups, provenance and audition WAVs remain in git-ignored local directories.
Restart NextDayz after switching: NextAudio caches decoded sounds per process.
Default imports also sync existing desktop build asset trees, backing each up.
PBO layout: https://community.bistudio.com/wiki/PBO_File_Format
"""

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import wave

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
RATE = 44100


@dataclass(frozen=True)
class Layer:
    archive: str
    entry: str
    gain: float = 1.0
    at: float = 0.0
    start: float = 0.0
    duration: float | None = None


@dataclass(frozen=True)
class Cue:
    layers: tuple[Layer, ...]
    peak: float = 0.8
    fade: float = 0.008
    rms: float | None = None
    channels: int = 1


def weapons(entry, **kwargs):
    return Layer("sounds_weapons", entry, **kwargs)


def characters(entry, **kwargs):
    return Layer("sounds_characters", entry, **kwargs)


def infected(entry, **kwargs):
    return Layer("sounds_ai", f"infected/VoiceFX/{entry}.ogg", **kwargs)


def cues():
    result = {}
    # DayZ separates blast, action and environmental decay. Bake those layers
    # into each variant because NextDayz currently plays one WAV per shot.
    for i in range(1, 4):
        result[f"shot_rifle_{i}"] = Cue((
            weapons(f"firearms/ak74/Wpn_AK74_Shot_1st_Close_{i:02}.ogg"),
            weapons(f"firearms/ak74/Wpn_AK74_Mech_1st_{i:02}.ogg", gain=0.3),
            weapons(f"Tails/AR_Tails/Wpn_AR_Tail_forest_Close_{i:02}.ogg", gain=0.3, at=0.035),
        ), peak=0.92)
    for i in range(1, 3):
        result[f"shot_sniper_{i}"] = Cue((
            weapons(f"firearms/SVD/SVD_close_shot_{i:02}.ogg"),
            weapons(f"firearms/SVD/SVD_closure_{i:02}.ogg", gain=0.25),
            weapons("firearms/SVD/SVD_tail_forest.ogg", gain=0.28, at=0.035),
        ), peak=0.94)
        result[f"shot_shotgun_{i}"] = Cue((
            weapons(f"shotguns/mp133/Wpn_MP133_Shot_Close_{i:02}.ogg"),
            weapons(f"shotguns/mp133/Wpn_MP133_Mech_{i:02}.ogg", gain=0.2),
            weapons(f"Tails/SG_Tails/Wpn_SG_Tail_forest_Close_{i:02}.ogg", gain=0.3, at=0.04),
        ), peak=0.94)
        result[f"shot_pistol_{i}"] = Cue((
            weapons(f"firearms/glock19/Wpn_Glock19_Shot_Close_{i:02}.ogg"),
            weapons(f"firearms/glock19/Wpn_Glock19_Mech_{i:02}.ogg", gain=0.25),
            weapons("firearms/glock19/Glock19_tail_forest.ogg", gain=0.2, at=0.025),
        ), peak=0.88)
    result["dry_fire"] = Cue((weapons("firearms/SVD/SVD_dry.ogg"),), peak=0.55)
    result["reload_rifle"] = Cue((weapons("firearms/ak74/Ak74_reload.ogg"),), peak=0.65)
    result["switch_weapon"] = Cue((characters("movement/weapons/Rifle_1.ogg"),), peak=0.55)
    for i in range(1, 4):
        result[f"hit_flesh_{i}"] = Cue((weapons(f"hits/bullet/body_{i}.ogg"),), peak=0.65)
    # DayZ has no separate bullet/head sample in this archive; use two other
    # body-hit takes, retaining NextDayz's stronger head-hit mix and HUD marker.
    for i in range(1, 3):
        result[f"hit_head_{i}"] = Cue((weapons(f"hits/bullet/body_{i + 3}.ogg"),), peak=0.75)
        result[f"impact_dirt_{i}"] = Cue((weapons(f"hits/bullet/soft_ground_{i}.ogg"),), peak=0.6)
        result[f"player_hurt_{i}"] = Cue((characters(f"voice/male_1/pain_{i}.ogg"),), peak=0.7)
        result[f"zombie_alert_{i}"] = Cue((infected(f"ZmbM_Normal_CallToArmsShort_{i}"),), peak=0.8)
        result[f"zombie_attack_{i}"] = Cue((infected(f"ZmbM_Normal_Attack_{i}"),), peak=0.78)
        # Heavy-hit vocals substitute for death (there is no death cue in
        # ZmbM_Normal). The existing delayed body-fall supplies the landing.
        result[f"zombie_death_{i}"] = Cue((infected(f"ZmbM_Normal_HeavyHit_{i}"),), peak=0.72)
        result[f"melee_swing_{i}"] = Cue((characters(f"attacks/fireaxe/fireaxe_Attack_light{i}_1.ogg"),), peak=0.55)
        result[f"ambient_crow_{i}"] = Cue((Layer("sounds_ai", f"animals/crows/CrowIdle_{i - 1}.ogg"),), peak=0.6)
        result[f"distant_shot_{i}"] = Cue((weapons(f"Tails/AR_Tails/Wpn_AR_Shot_Dist_{i:02}.ogg"),), peak=0.65)
    for i in range(1, 5):
        result[f"zombie_groan_{i}"] = Cue((infected(f"ZmbM_Normal_CalmMove_{i}"),), peak=0.65)
    result["body_fall"] = Cue((characters("movement/bodyfall/dirt_ext/bodyfall_1.ogg"),), peak=0.65)
    result["melee_impact"] = Cue((weapons("hits/melee/axe/hit_axe_body_heavy_1.ogg"),), peak=0.75)
    for i, (entry, start) in enumerate((
        ("WindForestLight_-9db", 0.0), ("WindHousesLight_-9db", 6.0),
        ("WindForestHeavy_-9db", 2.0), ("WindHousesHeavy_-9db", 4.0),
    ), 1):
        result[f"ambient_wind_{i}"] = Cue((
            Layer("sounds_environment", f"ambients/{entry}.ogg", start=start, duration=8.0),
        ), peak=0.24, fade=2.0, rms=0.075, channels=2)
    for kind, sources in (
        ("day", ("ForestDaySunny_-30db", "MeadowDay_-30db", "ForestDay_-30db")),
        ("night", ("ForestNight_-30db", "MeadowNight_-30db", "MeadowNight2_-30db")),
        ("evening", ("MeadowEveningCrickets", "MeadowEveningCrickets")),
    ):
        for i, entry in enumerate(sources, 1):
            result[f"ambient_{kind}_{i}"] = Cue((
                Layer("sounds_environment", f"ambients/{entry}.ogg", start=(i - 1) * 4.0, duration=8.0),
            ), peak=0.5, fade=2.0, rms=0.10, channels=2)
    for kind, entries in (
        ("bird", ("birds/Sparrow_1", "birds/Chickadee_1", "birds/Wabler_1",
                  "birds/Quail_1", "birds/Jay_1", "birds/Sparrow_3")),
        ("insect_day", ("insects/InsectDay_1", "insects/InsectDay_5", "insects/InsectDay_10")),
        ("insect_night", ("insects/InsectNight_1", "insects/InsectNight_3", "insects/InsectNight_8")),
        ("owl", ("birds/Owl_1", "birds/Owl_5", "birds/Owl_9")),
        ("foliage", ("weather/winds/WindTreeSmallLeaves_1", "weather/winds/WindTreeLargeLeaves_1",
                     "weather/winds/WindTreeCreak_1")),
    ):
        for i, entry in enumerate(entries, 1):
            result[f"ambient_{kind}_{i}"] = Cue((
                Layer("sounds_environment", entry + ".ogg"),
            ), peak=0.55, rms=0.12, channels=2)
    result["distant_scream"] = Cue((infected("ZmbF_Normal_CallToArmsShort_1"),), peak=0.5)
    result["pickup"] = Cue((characters("actions/items/bakedbeanscan/BakedBeansCan_pickup_1.ogg"),), peak=0.5)
    result["item_drop"] = Cue((characters("actions/items/bakedbeanscan/BakedBeansCan_drop_1.ogg"),), peak=0.5)
    result["eat"] = Cue((characters("actions/consumption/eating/EatCandy_1.ogg"),), peak=0.55)
    result["drink"] = Cue((characters("actions/consumption/drinks/WaterBottle_in_c0.ogg"),), peak=0.55)
    result["bandage"] = Cue((
        characters("actions/healing/bandage_start_0.ogg"),
        characters("actions/healing/bandage_loop_0.ogg", at=0.25),
    ), peak=0.55)
    result["radio_static"] = Cue((
        characters("actions/items/personalradio/personalradio_staticnoise.ogg", duration=2.2),
    ), peak=0.5, fade=0.06)
    result["lantern"] = Cue((characters("actions/items/portablegaslamp/portablegaslamp_turn_on_1.ogg"),), peak=0.5)
    # Keep the procedural kill-confirm UI tone and firecracker: neither has
    # a matching source cue. Record these explicitly in the local manifest.
    return result


def read_zstring(stream):
    data = bytearray()
    while True:
        byte = stream.read(1)
        if not byte or len(data) > 16384:
            raise ValueError("Invalid/truncated PBO string")
        if byte == b"\0":
            return data.decode("utf-8")
        data.extend(byte)


class Pbo:
    def __init__(self, path):
        self.path = path
        self.entries = {}
        with path.open("rb") as stream:
            ordered = []
            while True:
                name = read_zstring(stream)
                fields = stream.read(20)
                if len(fields) != 20:
                    raise ValueError(f"Truncated PBO header: {path}")
                method, _, _, _, size = struct.unpack("<5I", fields)
                if not name and method == 0x56657273:
                    while read_zstring(stream):
                        read_zstring(stream)
                    continue
                if not name:
                    if any(fields):
                        raise ValueError(f"Unsupported PBO header: {path}")
                    break
                ordered.append((name.replace("\\", "/").lower(), method, size))
            offset = stream.tell()
            for name, method, size in ordered:
                if offset + size > path.stat().st_size:
                    raise ValueError(f"PBO entry outside archive: {path}: {name}")
                self.entries[name] = (offset, size, method)
                offset += size

    def read(self, entry):
        offset, size, method = self.entries[entry.lower()]
        if method != 0:
            raise ValueError(f"Compressed PBO entry unsupported: {self.path}: {entry}")
        with self.path.open("rb") as stream:
            stream.seek(offset)
            data = stream.read(size)
        if len(data) != size or not data.startswith(b"OggS"):
            raise ValueError(f"Not an OGG sample: {self.path}: {entry}")
        return data


def find_ffmpeg(explicit):
    if explicit:
        return explicit
    if path := shutil.which("ffmpeg"):
        return path
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError as error:
        raise RuntimeError("Install ffmpeg or imageio_ffmpeg, or pass --ffmpeg") from error


def decode(ffmpeg, data, channels=1):
    result = subprocess.run([
        ffmpeg, "-hide_banner", "-loglevel", "error", "-i", "pipe:0",
        "-f", "f32le", "-ac", str(channels), "-ar", str(RATE), "pipe:1",
    ], input=data, capture_output=True, check=True)
    samples = np.frombuffer(result.stdout, dtype="<f4").copy()
    if len(samples) == 0 or not np.isfinite(samples).all() or np.max(np.abs(samples)) < 1e-6:
        raise ValueError("Empty, silent or invalid audio sample")
    return samples if channels == 1 else samples.reshape(-1, channels)


def render(cue, get_audio):
    layers = []
    for layer in cue.layers:
        samples = get_audio(layer, cue.channels)
        start = round(layer.start * RATE)
        stop = None if layer.duration is None else start + round(layer.duration * RATE)
        samples = samples[start:stop] * layer.gain
        if len(samples) == 0:
            raise ValueError(f"Empty audio slice: {layer.entry}")
        if layer.duration is not None and len(samples) < round(layer.duration * RATE):
            raise ValueError(f"Sample too short for requested slice: {layer.entry}")
        layers.append((round(layer.at * RATE), samples))
    frames = max(at + len(samples) for at, samples in layers)
    mixed = np.zeros(frames if cue.channels == 1 else (frames, cue.channels), dtype=np.float32)
    for at, samples in layers:
        mixed[at:at + len(samples)] += samples
    mixed -= mixed.mean(axis=0)
    # Preserve shot transients. Other cues get a short attack; all cues get
    # a tail fade to prevent clicks after trimming or resampling.
    fade = min(round(cue.fade * RATE), len(mixed) // 2)
    attack = min(fade, round(0.0005 * RATE))
    def ramp(start, stop, length):
        values = np.linspace(start, stop, length)
        return values if cue.channels == 1 else values[:, None]

    mixed[:attack] *= ramp(0, 1, attack)
    mixed[-fade:] *= ramp(1, 0, fade)
    if cue.fade > 0.1:
        mixed[:fade] *= ramp(0, 1, fade)
    peak = float(np.max(np.abs(mixed)))
    if peak < 1e-6:
        raise ValueError("Silent rendered cue")
    gain = cue.peak / peak
    if cue.rms is not None:
        gain = min(gain, cue.rms / float(np.sqrt(np.mean(mixed * mixed))))
    return mixed * gain


def write_wav(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        channels = 1 if samples.ndim == 1 else samples.shape[1]
        output.setparams((channels, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes((np.clip(samples, -1, 1) * 32767).astype("<i2").tobytes())


def write_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def runtime_names():
    source = ROOT / "src/Application/Game/NextDayz/Audio/NextDayzAudio.cpp"
    return set(re.findall(r'assets/sounds/nextdayz/([\w]+)\.wav', source.read_text(encoding="utf-8")))


def baseline(out, work, names):
    marker = work / "baseline.json"
    if marker.exists():
        data = json.loads(marker.read_text(encoding="utf-8"))
        if Path(data["out"]).resolve() != out:
            raise ValueError("This work directory belongs to a different --out directory")
        # A later trial can introduce more cue files. Record only new names,
        # preserving the original pre-trial backup for every existing cue.
        added = set(names) - set(data["names"])
        for name in sorted(added):
            path = out / f"{name}.wav"
            if path.exists():
                shutil.copy2(path, work / "original" / path.name)
                data["existing"].append(name)
        data["names"] = sorted(set(data["names"]) | set(names))
        write_json(marker, data)
        return data
    backup = work / "original"
    backup.mkdir(parents=True, exist_ok=True)
    existing = []
    for name in sorted(names):
        path = out / f"{name}.wav"
        if path.exists():
            shutil.copy2(path, backup / path.name)
            existing.append(name)
    data = {"out": str(out), "existing": existing, "names": sorted(names)}
    write_json(marker, data)
    return data


def restore_one(out, work):
    data = json.loads((work / "baseline.json").read_text(encoding="utf-8"))
    if Path(data["out"]).resolve() != out:
        raise ValueError("Restore --out must match the original import directory")
    names = data["names"]
    if any(not re.fullmatch(r"\w+", name) for name in names + data["existing"]):
        raise ValueError("Invalid baseline filenames")
    existing = set(data["existing"])
    # Check the full backup before changing the installed trial.
    for name in existing:
        if name not in names or not (work / "original" / f"{name}.wav").is_file():
            raise ValueError(f"Incomplete baseline backup: {name}")
    for name in names:
        path = out / f"{name}.wav"
        if name in existing:
            shutil.copy2(work / "original" / path.name, path)
        else:
            path.unlink(missing_ok=True)
    (out / "dayz_trial_manifest.json").unlink(missing_ok=True)
    print(f"Restored {len(existing)} original WAV files at {out}. Restart NextDayz to reload audio.")


def restore(out, work):
    restore_one(out, work)
    marker = work / "runtime.json"
    if marker.exists():
        for entry in json.loads(marker.read_text(encoding="utf-8")):
            backup_work = (work / "runtime" / entry["key"]).resolve()
            if not backup_work.is_relative_to((work / "runtime").resolve()):
                raise ValueError("Invalid runtime backup location")
            restore_one(Path(entry["out"]), backup_work)


def sync_runtime(out, work, names):
    if out != (ROOT / "assets/sounds/nextdayz").resolve():
        return []
    destinations = []
    for assets in sorted((ROOT / "out/build").glob("*/assets")):
        binaries = assets.parent / "bin"
        if not any((binaries / name).is_file() for name in ("NextDayz.exe", "NextDayz")):
            continue
        destination = (assets / "sounds/nextdayz").resolve()
        if destination == out:
            continue
        key = hashlib.sha256(str(destination).encode("utf-8")).hexdigest()[:12]
        backup_work = work / "runtime" / key
        backup_work.mkdir(parents=True, exist_ok=True)
        baseline(destination, backup_work, names)
        destinations.append({"out": str(destination), "key": key})
    # Save restore locations before writing any runtime copies.
    write_json(work / "runtime.json", destinations)
    for entry in destinations:
        destination = Path(entry["out"])
        destination.mkdir(parents=True, exist_ok=True)
        for name in names:
            shutil.copy2(out / f"{name}.wav", destination / f"{name}.wav")
        shutil.copy2(out / "dayz_trial_manifest.json", destination / "dayz_trial_manifest.json")
        print(f"Synced runtime audio: {destination}")
    return destinations


def audition(out, work):
    sequence = ["shot_rifle_1", "shot_sniper_1", "shot_shotgun_1", "shot_pistol_1",
                "reload_rifle", "hit_flesh_1", "zombie_groan_1", "zombie_alert_1",
                "zombie_attack_1", "player_hurt_1", "bandage", "ambient_wind_1"]
    tracks = []
    timeline = []
    cursor = 0.0
    for name in sequence:
        for label, folder in (("before", work / "original"), ("dayz", out)):
            path = folder / f"{name}.wav"
            if not path.exists():
                continue
            with wave.open(str(path), "rb") as stream:
                frames = stream.readframes(stream.getnframes())
                if stream.getsampwidth() != 2:
                    continue
                samples = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768
                samples = samples.reshape(-1, stream.getnchannels()).mean(axis=1)
                old_rate = stream.getframerate()
            if old_rate != RATE:
                samples = np.interp(np.arange(round(len(samples) * RATE / old_rate)) * old_rate / RATE,
                                    np.arange(len(samples)), samples)
            samples = samples[:3 * RATE].copy() * 0.65
            fade = min(441, len(samples) // 2)
            samples[-fade:] *= np.linspace(1, 0, fade)
            timeline.append({"cue": name, "version": label, "at_seconds": round(cursor, 3)})
            tracks.extend((samples, np.zeros(round(0.45 * RATE))))
            cursor += len(samples) / RATE + 0.45
    write_wav(work / "before_after.wav", np.concatenate(tracks))
    write_json(work / "before_after.json", timeline)


def ambience_audition(out, work):
    # Illustrative ten-second excerpts at the game's mix levels. Do not
    # normalise each scene: the contrast between clear, overcast and fog matters.
    scenes = (
        ("clear_dawn", (("day_1", 0.2), ("bird_1", 0.35), ("bird_2", 0.28), ("wind_1", 0.045))),
        ("clear_noon", (("day_2", 0.2), ("bird_3", 0.22), ("insect_day_1", 0.10), ("wind_2", 0.045))),
        ("clear_dusk", (("evening_1", 0.18), ("night_1", 0.083), ("insect_night_1", 0.13), ("wind_1", 0.045))),
        ("clear_night", (("night_2", 0.18), ("owl_1", 0.18), ("insect_night_2", 0.13), ("wind_2", 0.045))),
        ("overcast_noon", (("day_3", 0.11), ("bird_4", 0.12), ("foliage_1", 0.125), ("wind_3", 0.115))),
        ("fog_noon", (("day_1", 0.07), ("bird_5", 0.077), ("foliage_2", 0.025), ("wind_1", 0.018))),
    )
    excerpts, timeline = [], []
    for scene, layers in scenes:
        mixed = np.zeros((10 * RATE, 2), dtype=np.float32)
        for i, (name, gain) in enumerate(layers):
            with wave.open(str(out / f"ambient_{name}.wav"), "rb") as stream:
                samples = np.frombuffer(stream.readframes(stream.getnframes()), dtype="<i2").astype(np.float32) / 32768
                samples = samples.reshape(-1, stream.getnchannels())
                if samples.shape[1] == 1:
                    samples = np.repeat(samples, 2, axis=1)
            is_bed = name.startswith(("day_", "night_", "evening_", "wind_"))
            for seconds in ((0.0, 6.0) if is_bed else (1.0 + i * 1.3,)):
                at = round(seconds * RATE)
                length = min(len(samples), len(mixed) - at)
                mixed[at:at + length] += samples[:length] * gain
        fade = round(0.3 * RATE)
        mixed[:fade] *= np.linspace(0, 1, fade)[:, None]
        mixed[-fade:] *= np.linspace(1, 0, fade)[:, None]
        timeline.append({"scene": scene, "at_seconds": len(excerpts) * 10})
        excerpts.append(mixed)
    write_wav(work / "ambience_showcase.wav", np.concatenate(excerpts))
    write_json(work / "ambience_showcase.json", timeline)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--dayz-root", type=Path, help="Installed DayZ directory (read only)")
    parser.add_argument("--out", type=Path, default=ROOT / "assets/sounds/nextdayz")
    parser.add_argument("--work", type=Path, default=ROOT / "out/audio/dayz")
    parser.add_argument("--ffmpeg")
    parser.add_argument("--restore", action="store_true")
    args = parser.parse_args()
    out, work = args.out.resolve(), args.work.resolve()
    if args.restore:
        restore(out, work)
        return
    if args.dayz_root is None:
        parser.error("--dayz-root is required for import")
    # Do not allow output to modify the source installation.
    source = args.dayz_root.resolve()
    if out.is_relative_to(source) or work.is_relative_to(source):
        parser.error("Output and work directories must be outside the DayZ installation")
    ffmpeg = find_ffmpeg(args.ffmpeg)
    mapping = cues()
    names = runtime_names()
    retained = {"kill_confirm", "firecracker"}
    if set(mapping) | retained != names or set(mapping) & retained:
        raise ValueError("Trial mapping is out of sync with NextDayzAudio.cpp")
    archives = {name: Pbo(source / "Addons" / f"{name}.pbo")
                for name in sorted({layer.archive for cue in mapping.values() for layer in cue.layers})}
    # Validate every selection up front; a changed DayZ version cannot install
    # half an audio set if a selected sample is renamed or removed.
    for cue in mapping.values():
        for layer in cue.layers:
            if layer.entry.lower() not in archives[layer.archive].entries:
                raise ValueError(f"Missing DayZ sample: {layer.archive}: {layer.entry}")
    work.mkdir(parents=True, exist_ok=True)
    staging = work / "staging"
    staging.mkdir(exist_ok=True)
    cache = {}
    provenance = {}

    def get_audio(layer, channels):
        key = (layer.archive, layer.entry, channels)
        if key not in cache:
            data = archives[layer.archive].read(layer.entry)
            cache[key] = decode(ffmpeg, data, channels)
            provenance[key] = {"archive": f"{layer.archive}.pbo", "entry": layer.entry,
                               "sha256": hashlib.sha256(data).hexdigest()}
        return cache[key]

    entries = []
    for name, cue in mapping.items():
        samples = render(cue, get_audio)
        destination = staging / f"{name}.wav"
        write_wav(destination, samples)
        entries.append({"file": destination.name, "seconds": round(len(samples) / RATE, 3),
                        "peak": cue.peak, "rms_target": cue.rms, "channels": cue.channels,
                        "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
                        "layers": [{**provenance[(layer.archive, layer.entry, cue.channels)],
                                    "gain": layer.gain, "at": layer.at, "start": layer.start,
                                    "duration": layer.duration} for layer in cue.layers]})
        print(f"Prepared {name}: {len(samples) / RATE:.2f}s")
    # Only touch the active WAVs once every imported cue has rendered.
    original = baseline(out, work, names)
    missing = [name for name in retained if not (out / f"{name}.wav").exists()]
    if missing:
        from nextdayz_sfx import build
        fallback = work / "procedural"
        build(str(fallback))
        for name in missing:
            shutil.copy2(fallback / f"{name}.wav", staging / f"{name}.wav")
    out.mkdir(parents=True, exist_ok=True)
    for name in sorted(set(mapping) | set(missing)):
        (staging / f"{name}.wav").replace(out / f"{name}.wav")
    manifest = {"purpose": "Local DayZ audio trial; extracted samples are not repository assets",
                "source": str(source), "sample_rate": RATE, "format": "PCM16 WAV; stereo ambience, mono combat",
                "backup": str(work / "original"), "replaced": entries,
                "retained_procedural": sorted(retained),
                "substitutions": {"head_hit": "Alternate bullet/body takes",
                                  "zombie_death": "Heavy-hit vocals plus existing delayed body fall"}}
    write_json(out / "dayz_trial_manifest.json", manifest)
    write_json(work / "trial_manifest.json", manifest)
    sync_runtime(out, work, names)
    audition(out, work)
    ambience_audition(out, work)
    print(f"Installed {len(entries)} DayZ-based cues; kept {len(retained)} procedural cues.")
    print(f"Original backup: {work / 'original'} ({len(original['existing'])} files)")
    print(f"A/B audition (each cue: before, then DayZ): {work / 'before_after.wav'}")
    print(f"Ambience showcase (dawn, noon, dusk, night, overcast, fog): {work / 'ambience_showcase.wav'}")
    print("Restart NextDayz to reload cached audio. Use --restore to undo the trial.")


if __name__ == "__main__":
    main()
