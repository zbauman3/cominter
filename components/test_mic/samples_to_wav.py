#!/usr/bin/env python3
"""
Convert raw ADC samples from the ESP32 mic test into a .wav file.

Usage:
    python3 samples_to_wav.py samples.csv output.wav

The input file can be a raw copy-paste from the serial monitor — the
script will find the data between the "--- BEGIN SAMPLES ---" and
"--- END SAMPLES ---" markers automatically, ignoring any log prefixes
or other output. Between those markers it expects:
  - First line: metadata like "sample_rate=8000,count=40000,bits=12,bias=2100"
  - Remaining lines: comma-separated integer ADC values (0-4095)

The script will:
  1. Parse the metadata to get the sample rate.
  2. Read all the raw 12-bit ADC values.
  3. Remove the DC bias using the measured value from the ESP32.
  4. Normalize to 16-bit signed PCM range.
  5. Write a standard .wav file you can play in any audio player.

No external dependencies — uses only Python standard library.
"""

import struct
import sys
import wave


def parse_metadata(line):
    """Parse the 'sample_rate=8000,count=40000,bits=12,bias=2100' metadata line."""
    metadata = {}
    for pair in line.strip().split(","):
        key, value = pair.split("=")
        metadata[key.strip()] = int(value.strip())
    return metadata


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input.csv> <output.wav>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    with open(input_path, "r") as f:
        raw_lines = [line.strip() for line in f if line.strip()]

    # Find the data between the BEGIN/END markers, so you can paste
    # the entire serial monitor output without manual cleanup.
    begin_idx = None
    end_idx = None
    for i, line in enumerate(raw_lines):
        if "--- BEGIN SAMPLES ---" in line:
            begin_idx = i + 1
        elif "--- END SAMPLES ---" in line:
            end_idx = i
            break

    if begin_idx is None or end_idx is None:
        print("Error: Could not find --- BEGIN SAMPLES --- / --- END SAMPLES --- markers")
        sys.exit(1)

    lines = raw_lines[begin_idx:end_idx]

    # First line (after the marker) is metadata
    metadata = parse_metadata(lines[0])
    sample_rate = metadata["sample_rate"]
    bias = metadata["bias"]
    print(f"Sample rate: {sample_rate} Hz")
    print(f"DC bias: {bias} (theoretical midpoint: 2048)")

    # Remaining lines are comma-separated ADC values
    samples = []
    for line in lines[1:]:
        for value in line.split(","):
            value = value.strip()
            if value:
                samples.append(int(value))

    print(f"Loaded {len(samples)} samples ({len(samples) / sample_rate:.2f} seconds)")

    # Remove DC bias using the value measured by the ESP32 at startup.
    # The ESP32 samples the ADC while the mic is idle (before you press
    # the button) to find the actual resting voltage. This is more
    # accurate than assuming 2048, because the op-amp's input offset
    # voltage gets amplified by the gain and shifts the midpoint.
    samples = [s - bias for s in samples]

    # Normalize to 16-bit signed PCM (-32768 to 32767).
    # This makes the audio as loud as possible without clipping.
    peak = max(abs(s) for s in samples) if samples else 1
    if peak > 0:
        samples = [int(s / peak * 32767) for s in samples]

    # Clamp to int16 range just in case
    samples = [max(-32768, min(32767, s)) for s in samples]

    # Write a standard 16-bit mono PCM .wav file using the built-in
    # wave module (no numpy/scipy needed).
    with wave.open(output_path, "w") as wf:
        wf.setnchannels(1)        # mono
        wf.setsampwidth(2)        # 16-bit = 2 bytes per sample
        wf.setframerate(sample_rate)
        # struct.pack with '<h' encodes each sample as a little-endian
        # signed 16-bit integer, which is the PCM format .wav expects.
        wf.writeframes(struct.pack(f"<{len(samples)}h", *samples))

    print(f"Wrote {output_path}")


if __name__ == "__main__":
    main()
