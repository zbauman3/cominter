#!/usr/bin/env python3
"""
Convert the ESP32 mic test's filtered ADC dump into a .wav file.

Usage:
    python3 samples_to_wav.py samples.txt output.wav

The ESP32 dumps the filtered capture as one labeled block. This script finds it
(ignoring any surrounding log lines) and writes it out as a .wav you can listen
to. It expects:

    --- BEGIN SAMPLES filtered ---
    sample_rate=8000,count=20000,bits=12,bias=2087
    <comma-separated 12-bit ADC values, wrapped across lines>
    --- END SAMPLES filtered ---

It also prints diagnostics — peak level and how many samples hit the 0/4095
rails — so clipping is easy to spot.

No external dependencies — uses only the Python standard library.
"""

import re
import struct
import sys
import wave

ADC_MAX = 4095  # 12-bit rail


def parse_metadata(line):
    """Parse 'sample_rate=8000,count=20000,bits=12,bias=2087' into a dict."""
    metadata = {}
    for pair in line.strip().split(","):
        if "=" not in pair:
            continue
        key, value = pair.split("=")
        metadata[key.strip()] = int(value.strip())
    return metadata


def extract_block(text):
    """Find the first '--- BEGIN SAMPLES ... ---' ... '--- END SAMPLES ---'
    block in the serial dump. Returns {'meta':..., 'values':[...]} or None."""
    begin_re = re.compile(r"---\s*BEGIN SAMPLES")
    end_re = re.compile(r"---\s*END SAMPLES")

    lines = text.splitlines()
    for i, line in enumerate(lines):
        if not begin_re.search(line):
            continue

        # Collect lines until the matching END marker.
        body = []
        j = i + 1
        while j < len(lines) and not end_re.search(lines[j]):
            body.append(lines[j])
            j += 1

        # First non-empty body line is metadata; the rest are values.
        body = [ln for ln in body if ln.strip()]
        if not body:
            return None
        meta = parse_metadata(body[0])
        values = []
        for ln in body[1:]:
            for tok in ln.split(","):
                tok = tok.strip()
                if tok:
                    values.append(int(tok))
        return {"meta": meta, "values": values}
    return None


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input.txt> <output.wav>")
        sys.exit(1)

    with open(sys.argv[1], "r") as f:
        block = extract_block(f.read())

    if block is None:
        print("Error: no '--- BEGIN SAMPLES ... ---' block found in input")
        sys.exit(1)

    sample_rate = block["meta"]["sample_rate"]
    bias = block["meta"].get("bias", 2048)
    vals = block["values"]

    # --- Diagnostics ---
    rails = sum(1 for v in vals if v <= 0 or v >= ADC_MAX)
    peak_counts = max((abs(v - bias) for v in vals), default=0)
    print(f"{len(vals)} samples, peak={peak_counts} counts, "
          f"rail-hits={rails} ({'possible clipping!' if rails else 'clean'})")

    # Center on the measured bias, then normalize to full 16-bit scale with a
    # little headroom (0.95) so the loudest sample doesn't sit on the rail.
    centered = [v - bias for v in vals]
    peak = max((abs(s) for s in centered), default=1) or 1
    scale = 0.95 * 32767 / peak
    out_samples = [max(-32768, min(32767, int(s * scale))) for s in centered]

    with wave.open(sys.argv[2], "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(struct.pack(f"<{len(out_samples)}h", *out_samples))

    print(f"Sample rate: {sample_rate} Hz")
    print(f"Wrote {sys.argv[2]}  ({len(out_samples) / sample_rate:.2f}s)")


if __name__ == "__main__":
    main()
