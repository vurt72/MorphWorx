"""
gen_xenostasis_wavetables.py
────────────────────────────
Exports all 8 Xenostasis wavetable banks as 32-bit IEEE float WAV files,
formatted for Phaseon1 (contiguous frames, 2048 samples each).

Each output file contains 64 frames × 2048 samples = 131072 total samples.
Phaseon1 auto-detects frameCount as totalSamples // frameSize (2048).

Usage:
    python tools/gen_xenostasis_wavetables.py [--out-dir <path>]

Output directory defaults to: userwaveforms/
"""

import math
import struct
import os
import argparse
import ctypes

# ── Constants matching the desktop Xenostasis build ──────────────────────────
XS_FRAMES     = 64
XS_FRAME_SIZE = 2048
XS_NUM_TABLES = 8
TWO_PI        = 2.0 * math.pi


# ── Helpers ───────────────────────────────────────────────────────────────────

def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def write_wav_f32(path: str, samples: list[float], sample_rate: int = 44100):
    """Write a mono 32-bit IEEE float RIFF WAV file."""
    n = len(samples)
    data_bytes = struct.pack(f"<{n}f", *samples)
    data_size  = len(data_bytes)

    with open(path, "wb") as f:
        # RIFF header
        f.write(b"RIFF")
        f.write(struct.pack("<I", 36 + data_size))  # file size - 8
        f.write(b"WAVE")

        # fmt chunk  (IEEE float = audioFormat 3)
        f.write(b"fmt ")
        f.write(struct.pack("<I", 16))          # chunk size
        f.write(struct.pack("<H", 3))           # audioFormat: IEEE float
        f.write(struct.pack("<H", 1))           # numChannels: mono
        f.write(struct.pack("<I", sample_rate)) # sampleRate
        f.write(struct.pack("<I", sample_rate * 4))  # byteRate
        f.write(struct.pack("<H", 4))           # blockAlign
        f.write(struct.pack("<H", 32))          # bitsPerSample

        # data chunk
        f.write(b"data")
        f.write(struct.pack("<I", data_size))
        f.write(data_bytes)


class XsWavetableBank:
    """Python port of Xenostasis's XsWavetableBank struct."""

    def __init__(self):
        # data[table][frame][sample] — row-major
        self.data = [[[0.0] * XS_FRAME_SIZE for _ in range(XS_FRAMES)]
                     for _ in range(XS_NUM_TABLES)]

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _add_harmonic(self, table: int, frame: int, harmonic: int,
                      amplitude: float, phase_offset: float):
        buf = self.data[table][frame]
        for s in range(XS_FRAME_SIZE):
            t = s / XS_FRAME_SIZE
            buf[s] += amplitude * math.sin(TWO_PI * harmonic * t + phase_offset)

    def _normalize_frame(self, table: int, frame: int):
        buf = self.data[table][frame]
        peak = max(abs(v) for v in buf)
        if peak > 0.001:
            scale = 1.0 / peak
            for s in range(XS_FRAME_SIZE):
                buf[s] *= scale

    @staticmethod
    def _xorshift_factory(seed: int):
        """Returns a closure that yields bipolar floats in [-1, 1]."""
        state = [seed & 0xFFFFFFFF]
        def xs() -> float:
            x = state[0]
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= (x >> 17) & 0xFFFFFFFF
            x ^= (x <<  5) & 0xFFFFFFFF
            state[0] = x & 0xFFFFFFFF
            return (x & 0xFFFF) / 32768.0 - 1.0
        return xs

    @staticmethod
    def _xorshift01_factory(seed: int):
        """Returns a closure that yields [0, 1] floats, u32 domain."""
        state = [seed & 0xFFFFFFFF]
        def xs() -> float:
            x = state[0]
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= (x >> 17) & 0xFFFFFFFF
            x ^= (x <<  5) & 0xFFFFFFFF
            state[0] = x & 0xFFFFFFFF
            return (x & 0xFFFF) / 65535.0
        return xs

    @staticmethod
    def _hash32(x: int) -> int:
        x = x & 0xFFFFFFFF
        x ^= x >> 16
        x = (x * 0x7FEB352D) & 0xFFFFFFFF
        x ^= x >> 15
        x = (x * 0x846CA68B) & 0xFFFFFFFF
        x ^= x >> 16
        return x

    # ── Table 0: Dark Consonant (Feral Machine) ───────────────────────────────

    def generate_dark_consonant(self, table: int):
        rng = [0xDEAD1337]
        def xshift():
            x = rng[0]
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= (x >> 17) & 0xFFFFFFFF
            x ^= (x <<  5) & 0xFFFFFFFF
            rng[0] = x & 0xFFFFFFFF
            return (rng[0] & 0xFFFF) / 32768.0 - 1.0

        for f in range(XS_FRAMES):
            buf = self.data[table][f]
            for s in range(XS_FRAME_SIZE):
                buf[s] = 0.0

            frame_pos = f / (XS_FRAMES - 1)
            mode = f % 8
            rng[0] = (rng[0] ^ ((f * 13337 + 54321) & 0xFFFFFFFF)) & 0xFFFFFFFF

            for s in range(XS_FRAME_SIZE):
                t = s / XS_FRAME_SIZE
                v = 0.0

                if mode == 0:
                    ratios = [1.0, 1.59, 2.83, 4.17, 5.43, 7.11]
                    for i, r in enumerate(ratios):
                        amp = 0.5 / (1.0 + i * 0.4)
                        detune = 1.0 + (frame_pos - 0.5) * 0.02 * i
                        v += amp * math.sin(TWO_PI * t * r * detune)

                elif mode == 1:
                    carrier = math.sin(TWO_PI * t)
                    mod_freq = 3.71 + frame_pos * 8.0
                    mod = math.sin(TWO_PI * t * mod_freq)
                    v = carrier * mod
                    v = math.tanh(v * (2.0 + frame_pos * 3.0))

                elif mode == 2:
                    noise = xshift()
                    reson = 2.0 + frame_pos * 10.0
                    v = noise * math.sin(TWO_PI * t * reson) * 0.7
                    v += math.sin(TWO_PI * t) * 0.3

                elif mode == 3:
                    mod_idx = 6.0 + frame_pos * 12.0
                    mod_sig = math.sin(TWO_PI * t * 3.17) * mod_idx
                    v = math.sin(TWO_PI * t + mod_sig)
                    v = math.tanh(v * 2.0)

                elif mode == 4:
                    sine = math.sin(TWO_PI * t)
                    amount = 3.0 + frame_pos * 8.0
                    v = math.sin(sine * amount)
                    v = math.sin(v * (2.0 + frame_pos * 3.0))

                elif mode == 5:
                    sub = 1.0 if math.sin(TWO_PI * t * 0.5) > 0.0 else -1.0
                    hi_noise = xshift() * (0.3 + frame_pos * 0.7)
                    v = sub * 0.5 + hi_noise * 0.5
                    v += math.sin(TWO_PI * t * 5.73) * 0.3

                elif mode == 6:
                    slave_ratio = 3.5 + frame_pos * 8.0
                    v = math.sin(TWO_PI * (t % 1.0) * slave_ratio)
                    v *= (1.0 - t * 0.3)

                elif mode == 7:
                    g1 = math.sin(TWO_PI * t * 1.41) * math.sin(TWO_PI * t * 7.07)
                    g2 = math.sin(TWO_PI * t * 2.23) * math.cos(TWO_PI * t * 11.3)
                    v = g1 * 0.5 + g2 * 0.5
                    v = math.tanh(v * (2.0 + frame_pos * 4.0))

                buf[s] = v

            self._normalize_frame(table, f)

    # ── Table 1: Hollow Resonant ──────────────────────────────────────────────

    def generate_hollow_resonant(self, table: int):
        for f in range(XS_FRAMES):
            buf = self.data[table][f]
            for s in range(XS_FRAME_SIZE):
                buf[s] = 0.0

            frame_pos = f / (XS_FRAMES - 1)
            for h in range(1, 25):
                amp = 0.1 / h
                peak3  = math.exp(-0.5 * (h - 3)  ** 2)
                peak7  = math.exp(-0.5 * (h - 7)  ** 2)
                peak11 = math.exp(-0.5 * (h - 11) ** 2)
                peaks  = (peak3 + peak7 + peak11) * 0.5

                shift  = frame_pos * 2.0
                pk3s   = math.exp(-0.5 * (h - (3  + shift)) ** 2)
                pk7s   = math.exp(-0.5 * (h - (7  + shift)) ** 2)
                pk11s  = math.exp(-0.5 * (h - (11 + shift)) ** 2)
                peaks_s = (pk3s + pk7s + pk11s) * 0.5

                amp += peaks * (1.0 - frame_pos) + peaks_s * frame_pos
                self._add_harmonic(table, f, h, amp, frame_pos * 0.5)

            self._normalize_frame(table, f)

    # ── Table 2: Dense Organic Mass (Abyssal Alloy) ───────────────────────────

    def generate_dense_organic_mass(self, table: int):
        rng = [0xA11E011A]

        def hash32(x):
            x = x & 0xFFFFFFFF
            x ^= x >> 16
            x = (x * 0x7FEB352D) & 0xFFFFFFFF
            x ^= x >> 15
            x = (x * 0x846CA68B) & 0xFFFFFFFF
            x ^= x >> 16
            return x

        def rand01():
            rng[0] = hash32(rng[0])
            return (rng[0] & 0xFFFF) / 65535.0

        def rand_bip():
            return rand01() * 2.0 - 1.0

        for f in range(XS_FRAMES):
            buf = self.data[table][f]
            for s in range(XS_FRAME_SIZE):
                buf[s] = 0.0

            frame_pos = f / (XS_FRAMES - 1)
            mode = f % 8
            rng[0] = (rng[0] ^ hash32((f * 0x9E3779B9 + 0xBEEF1234) & 0xFFFFFFFF)) & 0xFFFFFFFF

            chaos_a = rand01()
            chaos_b = rand01()
            chaos_c = rand01()

            sub_amt  = 0.55 + 0.35 * (1.0 - abs(frame_pos - 0.5) * 2.0)
            fund_amt = 0.85
            oct_amt  = 0.25 + 0.15 * chaos_a

            inharm1  = 1.41 + chaos_b * 5.7
            inharm2  = 2.09 + chaos_c * 9.3
            fm_ratio = 2.0 + (mode % 5) * 0.73 + chaos_a * 2.0
            fm_index = 2.0 + chaos_b * 12.0 + frame_pos * 6.0
            ring_ratio = 3.0 + (mode % 3) * 2.0 + chaos_c * 6.0

            noise_lp  = 0.0
            noise_hp  = 0.0
            prev_nlp  = 0.0
            noise_coeff = 0.004 + (0.08 + chaos_b * 0.25) * (0.25 + frame_pos)
            noise_coeff = clamp(noise_coeff, 0.004, 0.35)
            noise_amt = 0.04 + 0.22 * frame_pos
            if mode in (6, 7):
                noise_amt += 0.10

            for s in range(XS_FRAME_SIZE):
                t = s / XS_FRAME_SIZE

                fund = math.sin(TWO_PI * t)
                oct  = math.sin(TWO_PI * 2.0 * t)
                sub_half = math.sin(math.pi * t)
                base = fund * fund_amt + oct * oct_amt + sub_half * sub_amt

                ma = math.sin(TWO_PI * t * inharm1)
                mb = math.sin(TWO_PI * t * inharm2 + ma * (1.0 + chaos_a * 2.0))
                fm  = math.sin(TWO_PI * t + math.sin(TWO_PI * t * fm_ratio) * fm_index)
                ring = math.sin(TWO_PI * t * ring_ratio)

                if mode == 0:
                    metal = 0.55 * ma + 0.45 * mb
                elif mode == 1:
                    metal = fm
                elif mode == 2:
                    metal = fm * ring
                elif mode == 3:
                    metal = math.sin(TWO_PI * math.fmod(t * (2.0 + chaos_c * 10.0), 1.0) * (3.0 + chaos_a * 6.0))
                elif mode == 4:
                    metal = math.sin((ma + fm * 0.7) * (3.5 + chaos_b * 10.0))
                elif mode == 5:
                    metal = math.tanh((mb * 1.7 + fm * 0.9) * (2.0 + chaos_c * 4.0))
                elif mode == 6:
                    metal = (ma * 0.4 + fm * 0.6) * (0.6 + 0.4 * ring)
                else:  # mode 7
                    metal = math.sin((fm + mb) * (5.0 + chaos_a * 14.0)) * (0.7 + 0.3 * ring)

                n = rand_bip()
                noise_lp += noise_coeff * (n - noise_lp)
                noise_hp  = noise_lp - prev_nlp
                prev_nlp  = noise_lp

                x = base + metal * (0.55 + frame_pos * 0.55) + noise_hp * noise_amt
                x = x - (x * 0.02)
                drive = 1.2 + frame_pos * 2.2 + chaos_b * 1.5
                x = math.tanh(x * drive)
                if mode >= 4:
                    x = math.sin(x * (2.0 + chaos_c * 8.0))

                buf[s] = x

            self._normalize_frame(table, f)

    # ── Table 3: Substrate ────────────────────────────────────────────────────

    def generate_substrate(self, table: int):
        for f in range(XS_FRAMES):
            buf = self.data[table][f]
            for s in range(XS_FRAME_SIZE):
                buf[s] = 0.0

            fp = f / (XS_FRAMES - 1)

            self._add_harmonic(table, f, 1, 1.0, 0.0)

            h2amp = 0.70 + 0.24 * fp
            h2ph  = fp * math.pi
            self._add_harmonic(table, f, 2, h2amp, h2ph)

            h3amp = 0.07 + 0.45 * math.sin(fp * math.pi)
            h3ph  = fp * TWO_PI
            self._add_harmonic(table, f, 3, h3amp, h3ph)

            h4amp = 0.03 + 0.33 * fp
            h4ph  = (1.0 - fp) * math.pi * 0.8
            self._add_harmonic(table, f, 4, h4amp, h4ph)

            h5amp = 0.04 * (1.0 - fp)
            if h5amp > 0.003:
                self._add_harmonic(table, f, 5, h5amp, 0.0)

            h6amp = 0.02 + 0.10 * math.sin(fp * math.pi * 0.75)
            self._add_harmonic(table, f, 6, h6amp, fp * math.pi * 0.5)

            h7amp = 0.015 * (1.0 - fp)
            if h7amp > 0.003:
                self._add_harmonic(table, f, 7, h7amp, 0.0)

            self._normalize_frame(table, f)

    # ── Table 4: Percussive Strike (Submerged Monolith) ───────────────────────

    def generate_percussive_strike(self, table: int):
        rng = [0xC1AE3471]

        def xs01():
            x = rng[0]
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= (x >> 17) & 0xFFFFFFFF
            x ^= (x <<  5) & 0xFFFFFFFF
            rng[0] = x & 0xFFFFFFFF
            return (rng[0] & 0xFFFF) / 65535.0

        def smooth01(x):
            x = clamp(x, 0.0, 1.0)
            return x * x * (3.0 - 2.0 * x)

        for f in range(XS_FRAMES):
            buf = self.data[table][f]
            for s in range(XS_FRAME_SIZE):
                buf[s] = 0.0

            frame_pos = f / (XS_FRAMES - 1)

            base_amp    = 0.98
            sub_weight  = 0.10 + 0.18 * smooth01(frame_pos * 1.2)
            low_mid_w   = 0.05 + 0.35 * smooth01(frame_pos)

            r1, r2, r3 = 1.37, 1.618, 2.71

            cluster_center = 2.8 + frame_pos * 6.0 + math.sin(frame_pos * TWO_PI) * 0.35
            cluster_width  = 1.2 + frame_pos * 1.8

            warp_amt = 0.0 + 0.18 * (frame_pos ** 1.5)
            warp_k1  = 2 + (f % 5)
            warp_k2  = 5 + (f % 7)
            asym_amt = 0.0 + 0.55 * (frame_pos ** 1.2)

            late      = smooth01((frame_pos - 0.62) / 0.38)
            fold_amt  = late * 0.40
            fm_amt    = late * 0.55

            ph_a = (xs01() * 2.0 - 1.0) * math.pi
            ph_b = (xs01() * 2.0 - 1.0) * math.pi
            ph_c = (xs01() * 2.0 - 1.0) * math.pi

            for s in range(XS_FRAME_SIZE):
                t = s / XS_FRAME_SIZE

                warp = t
                warp += warp_amt * math.sin(TWO_PI * warp_k1 * t + ph_a)
                warp += warp_amt * 0.35 * math.sin(TWO_PI * warp_k2 * t + ph_b)
                warp = warp - math.floor(warp)

                carrier = math.sin(TWO_PI * warp)
                x = carrier * base_amp

                x += math.sin(TWO_PI * 2.0 * warp) * (sub_weight * 0.6)
                x += math.sin(TWO_PI * 3.0 * warp) * (sub_weight * 0.25)

                inharm_amt = (frame_pos ** 1.25) * 0.65
                inharm = 0.0
                inharm += math.sin(TWO_PI * r1 * t + ph_a) * 0.55
                inharm += math.sin(TWO_PI * r2 * t + ph_b) * 0.45
                inharm += math.sin(TWO_PI * r3 * t + ph_c) * 0.35
                x += inharm * inharm_amt

                cloud = 0.0
                for h in range(2, 21):
                    hh = float(h)
                    d = (hh - cluster_center) / max(0.6, cluster_width)
                    env = math.exp(-0.5 * d * d)
                    ph = hh * 0.37 + ph_a * 0.3 + ph_b * 0.2
                    cloud += env * math.sin(TWO_PI * hh * warp + ph)
                x += cloud * low_mid_w * 0.10

                x += asym_amt * 0.22 * x * x * x

                if late > 0.001:
                    mod = math.sin(TWO_PI * r2 * t + ph_c) * (2.0 + 9.0 * fm_amt)
                    fm  = math.sin(TWO_PI * warp + mod)
                    x = x * (1.0 - fm_amt * 0.25) + fm * (fm_amt * 0.25)
                    folded = math.sin(x * (1.2 + fold_amt * 5.0))
                    x = x * (1.0 - fold_amt) + folded * fold_amt

                x = math.tanh(x * (1.1 + frame_pos * 1.8))
                buf[s] = x

            # Endpoint crossfade
            xf = 96
            for i in range(xf):
                a  = i / (xf - 1)
                s0 = buf[i]
                s1 = buf[XS_FRAME_SIZE - xf + i]
                m  = s0 * (1.0 - a) + s1 * a
                buf[i] = m
                buf[XS_FRAME_SIZE - xf + i] = m

            # DC removal
            mean = sum(buf) / XS_FRAME_SIZE
            for s in range(XS_FRAME_SIZE):
                buf[s] -= mean

            self._normalize_frame(table, f)

    # ── Table 5: Harsh Noise ──────────────────────────────────────────────────

    def generate_harsh_noise(self, table: int):
        rng = [0xBADF00D]

        def xshift():
            x = rng[0]
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= (x >> 17) & 0xFFFFFFFF
            x ^= (x <<  5) & 0xFFFFFFFF
            rng[0] = x & 0xFFFFFFFF
            return (rng[0] & 0xFFFF) / 32768.0 - 1.0

        for f in range(XS_FRAMES):
            buf = self.data[table][f]
            for s in range(XS_FRAME_SIZE):
                buf[s] = 0.0

            mode = f % 8
            rng[0] = (rng[0] ^ ((f * 77777 + 12345) & 0xFFFFFFFF)) & 0xFFFFFFFF

            for s in range(XS_FRAME_SIZE):
                t = s / XS_FRAME_SIZE
                v = 0.0

                if mode == 0:
                    v = math.sin(TWO_PI * t * (1.0 + f * 0.3))
                    v = 1.0 if v > 0.0 else -1.0
                    v *= (1.0 - t * 0.5)

                elif mode == 1:
                    sine = math.sin(TWO_PI * t * 3.0)
                    bits = 2 + (f // 8)
                    quant = float(1 << bits)
                    v = math.floor(sine * quant) / quant

                elif mode == 2:
                    v = math.sin(TWO_PI * t) * math.sin(TWO_PI * t * 7.33)
                    v = math.tanh(v * 4.0)

                elif mode == 3:
                    v = xshift()
                    nfreq = 2.0 + f * 0.5
                    v *= math.sin(TWO_PI * t * nfreq)

                elif mode == 4:
                    mod = math.sin(TWO_PI * t * (5.0 + f * 0.2))
                    v = math.sin(TWO_PI * t + mod * (3.0 + f * 0.1))
                    v = math.tanh(v * 3.0)

                elif mode == 5:
                    sine = math.sin(TWO_PI * t * 2.0)
                    fold = sine * (2.0 + f * 0.15)
                    v = math.sin(fold)

                elif mode == 6:
                    v = abs(math.sin(TWO_PI * t * 3.0)) * 2.0 - 1.0
                    v += abs(math.sin(TWO_PI * t * 5.17)) * 0.5
                    v = math.tanh(v * 2.0)

                elif mode == 7:
                    sub   = math.sin(TWO_PI * t)
                    noise = xshift() * (1.0 - t)
                    v = sub * 0.5 + noise * 0.8

                buf[s] = v

            self._normalize_frame(table, f)

    # ── Table 6: Arcade FX ────────────────────────────────────────────────────

    def generate_arcade_fx(self, table: int):
        for f in range(XS_FRAMES):
            buf = self.data[table][f]
            for s in range(XS_FRAME_SIZE):
                buf[s] = 0.0

            mode = f % 10

            for s in range(XS_FRAME_SIZE):
                t = s / XS_FRAME_SIZE
                v = 0.0

                if mode == 0:
                    sweep = 20.0 * (1.0 - t * 0.9)
                    v = math.sin(TWO_PI * t * sweep)
                    v = 1.0 if v > 0.0 else -1.0

                elif mode == 1:
                    sweep = 2.0 + t * 30.0
                    v = math.sin(TWO_PI * t * sweep)

                elif mode == 2:
                    pattern = int(t * 256.0)
                    v = 1.0 if ((pattern ^ (pattern >> 3) ^ (pattern >> 5)) & 1) else -1.0

                elif mode == 3:
                    chirp = math.exp(t * 4.0) * 0.5
                    v = math.sin(TWO_PI * chirp)
                    v = math.tanh(v * 3.0)

                elif mode == 4:
                    v = math.sin(TWO_PI * t * 8.0) + math.sin(TWO_PI * t * 11.3)
                    v = math.tanh(v * 2.0)

                elif mode == 5:
                    duty  = 0.1 + t * 0.8
                    phase = math.fmod(t * 6.0, 1.0)
                    v = 1.0 if phase < duty else -1.0

                elif mode == 6:
                    bt  = int(t * 8000.0) + f * 200
                    raw = ((bt >> 4) | (bt << 1)) ^ (bt >> 8)
                    v = (raw & 0xFF) / 128.0 - 1.0

                elif mode == 7:
                    v  = math.sin(TWO_PI * t * 3.0) * math.cos(TWO_PI * t * 13.7)
                    v += math.sin(TWO_PI * t * 0.5) * 0.3

                elif mode == 8:
                    sr_div = 4.0 + f * 0.5
                    quant_t = math.floor(t * XS_FRAME_SIZE / sr_div) * sr_div / XS_FRAME_SIZE
                    v = math.sin(TWO_PI * quant_t * 5.0)

                elif mode == 9:
                    mod_depth = 8.0 * (1.0 - t)
                    mod = math.sin(TWO_PI * t * 12.0) * mod_depth
                    v = math.sin(TWO_PI * t * 2.0 + mod)

                buf[s] = v

            self._normalize_frame(table, f)

    # ── Table 7: Spectral Clusters (Alien Physics Collapse) ───────────────────

    def generate_spectral_clusters(self, table: int):
        rng = [0xC0A1A7E7]

        def xs01():
            x = rng[0]
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= (x >> 17) & 0xFFFFFFFF
            x ^= (x <<  5) & 0xFFFFFFFF
            rng[0] = x & 0xFFFFFFFF
            return (rng[0] & 0xFFFF) / 65535.0

        def rand_bip():
            return xs01() * 2.0 - 1.0

        def fract01(x):
            return x - math.floor(x)

        def lens(x, g):
            a = math.atan(g)
            if abs(a) < 1e-6:
                return x
            return math.atan(x * g) / a

        for f in range(XS_FRAMES):
            buf = self.data[table][f]
            for s in range(XS_FRAME_SIZE):
                buf[s] = 0.0

            frame_pos = f / (XS_FRAMES - 1)
            mode = f // 8
            z    = (f % 8) / 7.0

            seed = (0x9E3779B9 * (f + 1) + 0x7F4A7C15) & 0xFFFFFFFF
            rng[0] = (rng[0] ^ seed) & 0xFFFFFFFF

            x = 0.15 + 0.7  * xs01()
            y = 0.10 + 0.8  * xs01()
            phi = xs01()
            fb   = 0.0
            prev = 0.0

            r  = 3.57 + 0.40 * z + 0.02 * math.sin(frame_pos * TWO_PI)
            r2 = 3.62 + 0.36 * (1.0 - z)
            K  = 0.10 + 1.75 * (0.25 + 0.75 * z)
            warp_amt = 0.0 + 0.22 * (frame_pos ** 1.2)
            fold_amt = 0.0 + 0.28 * (frame_pos ** 1.6)
            asym_amt = 0.08 + 0.40 * frame_pos

            inj  = 0.06 + 0.22 * z
            damp = 0.985 - 0.02 * z

            ph_a = rand_bip() * math.pi
            ph_b = rand_bip() * math.pi
            ph_c = rand_bip() * math.pi

            for s in range(XS_FRAME_SIZE):
                x  = r  * x  * (1.0 - x)
                y  = r2 * y  * (1.0 - y)
                cx = 2.0 * x - 1.0
                cy = 2.0 * y - 1.0

                fb = fb * damp + cx * inj
                phi_step = 1.0 / XS_FRAME_SIZE
                phi = fract01(phi + phi_step + fb * 0.015)

                warp = phi
                warp += warp_amt * 0.08 * math.sin(TWO_PI * (2.0 + mode % 5) * warp + ph_a)
                warp += warp_amt * 0.03 * math.sin(TWO_PI * (7.0 + mode % 7) * warp + ph_b)
                warp = fract01(warp)

                u   = 2.0 * warp - 1.0
                tri = 1.0 - 2.0 * abs(u)
                para = u - (u ** 3) * 0.3333333

                v = 0.0

                if mode == 0:
                    a = lens(tri + cx * 0.35, 1.4 + 4.0 * z)
                    v = a + asym_amt * 0.18 * a ** 3

                elif mode == 1:
                    theta = warp
                    omega = 0.15 + 0.35 * z
                    theta = fract01(theta + omega + K * 0.07 * math.sin(TWO_PI * theta + ph_c))
                    uu = 2.0 * theta - 1.0
                    v = lens(uu, 0.9 + 5.0 * z)

                elif mode == 2:
                    tx = x
                    tx = (2.0 * tx) if (tx < 0.5) else (2.0 * (1.0 - tx))
                    w  = 2.0 * tx - 1.0
                    if cy > 0.995:
                        w = 1.0 if w > 0.0 else -1.0
                    v = lens(w + para * 0.35, 1.2 + 3.5 * z)

                elif mode == 3:
                    ww = warp
                    ww += 0.09 * warp_amt * math.sin(TWO_PI * (3.0 + 7.0 * x) * ww + ph_a)
                    ww += 0.05 * warp_amt * math.sin(TWO_PI * (5.0 + 11.0 * y) * ww + ph_b)
                    ww = fract01(ww)
                    uu = 2.0 * ww - 1.0
                    p  = uu - (uu ** 3) * 0.28
                    v  = lens(p, 1.0 + 5.5 * z)

                elif mode == 4:
                    g    = 0.8 + 6.0 * z
                    well = lens(u + fb * 0.9, g)
                    v = well
                    if x > 0.995:
                        v = -v

                elif mode == 5:
                    prod   = cx * cy
                    shaped = lens(prod + tri * 0.25, 1.6 + 4.0 * z)
                    v = shaped + 0.15 * lens(para + cx * 0.15, 2.2 + 3.0 * z)

                elif mode == 6:
                    d    = tri - prev
                    prev = tri
                    edge = lens(d * (6.0 + 10.0 * z) + u * 0.35, 1.4 + 5.0 * z)
                    v = edge
                    if frame_pos > 0.5 and abs(edge) > 0.85:
                        v = 1.0 if edge > 0.0 else -1.0

                else:  # mode 7
                    collapse  = lens(u + cx * 0.55 + cy * 0.25, 2.0 + 6.0 * z)
                    collapse += 0.22 * lens(math.sin(TWO_PI * (warp + cx * 0.05) * (3.0 + 9.0 * z) + ph_a), 1.0 + 3.0 * z)
                    v = collapse
                    if x < 0.02 or x > 0.98:
                        v = -v

                v += asym_amt * 0.10 * v ** 3
                if fold_amt > 0.0001:
                    folded = math.sin(v * (1.4 + fold_amt * 7.0))
                    v = v * (1.0 - fold_amt) + folded * fold_amt

                v = math.tanh(v * (1.2 + frame_pos * 2.3))
                buf[s] = v

            # DC removal
            mean = sum(buf) / XS_FRAME_SIZE
            for s in range(XS_FRAME_SIZE):
                buf[s] -= mean

            self._normalize_frame(table, f)

    # ── Generate all ──────────────────────────────────────────────────────────

    def generate(self):
        print("  [0/8] Generating DarkConsonant ...")
        self.generate_dark_consonant(0)
        print("  [1/8] Generating HollowResonant ...")
        self.generate_hollow_resonant(1)
        print("  [2/8] Generating DenseOrganicMass ...")
        self.generate_dense_organic_mass(2)
        print("  [3/8] Generating Substrate ...")
        self.generate_substrate(3)
        print("  [4/8] Generating PercussiveStrike ...")
        self.generate_percussive_strike(4)
        print("  [5/8] Generating HarshNoise ...")
        self.generate_harsh_noise(5)
        print("  [6/8] Generating ArcadeFX ...")
        self.generate_arcade_fx(6)
        print("  [7/8] Generating SpectralClusters ...")
        self.generate_spectral_clusters(7)
        print("  Done.")

    def flat_samples(self, table: int) -> list[float]:
        """Return all frames of a table as a flat float list (frame-major order)."""
        out = []
        for f in range(XS_FRAMES):
            out.extend(self.data[table][f])
        return out


# ── Table metadata ────────────────────────────────────────────────────────────

TABLE_NAMES = [
    "xs_DarkConsonant",
    "xs_HollowResonant",
    "xs_DenseOrganicMass",
    "xs_Substrate",
    "xs_PercussiveStrike",
    "xs_HarshNoise",
    "xs_ArcadeFX",
    "xs_SpectralClusters",
]


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out-dir", default="userwaveforms",
        help="Output directory for .wav files (default: userwaveforms/)"
    )
    parser.add_argument(
        "--sample-rate", type=int, default=44100,
        help="WAV sample rate header value (default: 44100; does not affect generation)"
    )
    args = parser.parse_args()

    out_dir = args.out_dir
    os.makedirs(out_dir, exist_ok=True)

    print(f"Generating Xenostasis wavetables → {out_dir}/")
    print(f"  {XS_FRAMES} frames × {XS_FRAME_SIZE} samples each "
          f"({XS_FRAMES * XS_FRAME_SIZE} total samples per file)")

    bank = XsWavetableBank()
    bank.generate()

    for t in range(XS_NUM_TABLES):
        name = TABLE_NAMES[t]
        path = os.path.join(out_dir, f"{name}.wav")
        samples = bank.flat_samples(t)
        write_wav_f32(path, samples, sample_rate=args.sample_rate)
        print(f"  Wrote {path}  ({len(samples)} samples)")

    print("\nAll done.  Load any of these files in Phaseon1 via the wavetable menu.")


if __name__ == "__main__":
    main()
