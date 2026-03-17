import math
import struct
import wave
from pathlib import Path

SR = 48000
FRAME_SIZE = 2048
FRAMES = 16
OUT_PATH = Path("userwaveforms/phaseon_dubstep_formant.wav")


def make_frame(frame_index: int) -> list[float]:
    """Generate one wavetable frame: deep sub + wild formant cluster.

    We use additive integer harmonics so the frame is strictly periodic.
    Different frames jump between vowel-ish clusters for a dubstep feel.
    """
    t_base = [i / FRAME_SIZE for i in range(FRAME_SIZE)]

    # Choose a small set of formant center patterns we can jump between.
    patterns = [
        (2.5, 5.0, 8.0),   # low, mid, high
        (3.0, 7.0, 11.0),  # classic vowel-ish
        (4.0, 9.0, 13.0),  # brighter, shouty
        (5.5, 10.0, 16.0), # harsh, metallic
        (2.0, 6.0, 12.0),  # hollow-ish
        (3.5, 6.5, 15.0),  # nasal + edge
        (6.0, 9.0, 18.0),  # screamier
        (2.0, 3.5, 7.5),   # more vocal
    ]

    # Map frames to patterns in a non-linear order for "wild" morph.
    order = [0, 3, 1, 6, 2, 5, 4, 7, 3, 0, 6, 2, 7, 1, 5, 4]
    pat = patterns[order[frame_index % len(order)]]

    f1, f2, f3 = pat

    # Amount of extra high/inharmonic grit per frame.
    fn = frame_index / max(1, FRAMES - 1)

    # Deep sub emphasis: strong fundamental, gentle even harmonics stack.
    max_h = 32
    frame = [0.0] * FRAME_SIZE
    for h in range(1, max_h + 1):
        # Base harmonic rolloff.
        base_amp = 1.0 / (1.0 + 0.6 * (h - 1))
        if h == 1:
            base_amp *= 2.2  # strong fundamental (sub weight)
        elif h == 2:
            base_amp *= 1.4
        elif h == 3:
            base_amp *= 1.0
        else:
            base_amp *= 0.8

        # Formant peaks around f1 / f2 / f3 with narrow-ish Gaussians.
        def formant_boost(center: float, width: float = 1.1) -> float:
            d = (h - center) / width
            return math.exp(-0.5 * d * d)

        boost = (
            1.0
            + 3.0 * formant_boost(f1)
            + 2.4 * formant_boost(f2)
            + 2.0 * formant_boost(f3)
        )

        # Add frame-dependent extra emphasis to one of the formants so
        # different frames feel like distinct vowel syllables.
        which = frame_index % 3
        extra = 0.0
        if which == 0:
            extra = 4.0 * formant_boost(f1, 0.8)
        elif which == 1:
            extra = 4.0 * formant_boost(f2, 0.8)
        else:
            extra = 4.0 * formant_boost(f3, 0.8)
        boost += extra

        # A bit more aggression in the upper spectrum as frame progresses.
        high_push = 1.0 + fn * 0.6 * (h / max_h)

        amp = base_amp * boost * high_push

        # Subtle random-ish detune per harmonic derived from frame index and h.
        # (Deterministic, so the wavetable is stable run-to-run.)
        seed = (frame_index * 1315423911) ^ (h * 2654435761)
        rnd = ((seed >> 9) & 0xFFFF) / 65535.0  # 0..1
        detune = 1.0 + (rnd - 0.5) * 0.015  # +/- 1.5%

        w = 2.0 * math.pi * h * detune
        for i, p in enumerate(t_base):
            frame[i] += amp * math.sin(w * p)

    # Add some inharmonic grit on higher frames for dubstep edge.
    if fn > 0.2:
        grit_amp = 0.2 + 0.5 * fn
        for i, p in enumerate(t_base):
            fm_mod = math.sin(2.0 * math.pi * p * (3.7 + 4.3 * fn)) * (2.0 + 10.0 * fn)
            grit = math.sin(2.0 * math.pi * p * (8.0 + 14.0 * fn) + fm_mod)
            frame[i] += grit_amp * math.tanh(grit * 1.6)

    # DC remove and normalize each frame to ~0.95 peak.
    mean = sum(frame) / FRAME_SIZE
    frame = [x - mean for x in frame]
    peak = max(abs(x) for x in frame) or 1.0
    g = 0.95 / peak
    frame = [x * g for x in frame]
    return frame


def main() -> None:
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)

    all_samples: list[float] = []
    for f in range(FRAMES):
        frame = make_frame(f)
        all_samples.extend(frame)

    # Write as 32-bit float mono WAV.
    with wave.open(str(OUT_PATH), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(4)  # 32-bit float
        wf.setframerate(SR)
        # Convert float list to bytes.
        frames_bytes = b"".join(struct.pack("<f", s) for s in all_samples)
        wf.writeframes(frames_bytes)

    print(f"Wrote wavetable to {OUT_PATH.resolve()}")


if __name__ == "__main__":
    main()
