#!/usr/bin/env python3
"""record_clips.py - record "hey BMO" training clips from the BMO microphone.

The STM32 (running mic_stream.c) streams the INMP441 audio over the USB cable.
This script prompts each person, records short clips, and saves labelled WAV
files that Edge Impulse can import directly (label = text before the first dot).

  python3 record_clips.py --meter               check the stream works (live level bar)
  python3 record_clips.py --speaker kla         run a recording session for one person

Files are saved as  clips/<label>/<label>.<speaker>.<number>.wav
Needs:  python3 -m pip install pyserial
"""
import argparse
import collections
import glob
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import wave
from array import array

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing. Install it with:  python3 -m pip install pyserial")

MAGIC = b"BMOA"
RATE = 16000
SAMPLES_PER_PACKET = 512
PACKET_SIZE = 4 + 2 + 2 + SAMPLES_PER_PACKET * 2 + 2
BAUD = 921600

# What each person records. Mix of styles helps the model work for everyone.
WAKE_STYLES = ["normal voice", "quietly", "loudly", "fast", "slowly",
               "from arm's length", "turned slightly away", "normal voice"]
UNKNOWN_WORDS = ["hey Bob", "hey Mo", "BMO", "hey man", "hey buddy", "be more",
                 "hello", "okay", "play", "stop", "yes", "no", "good morning",
                 "thank you", "what's up", "nobody", "alright", "maybe",
                 "hey Siri", "a short sentence of your choice"]
NOISE_PROMPTS = ["stay completely silent", "tap the desk lightly", "rustle some paper",
                 "type on the keyboard", "breathe / move around normally",
                 "let the room be (fan, air-con)", "someone else talks in the background"]


class MicStream:
    """Reads the board non-stop in a background thread, so the audio is always live.

    Every valid packet is numbered (0, 1, 2, ...) as it arrives. A recording takes
    the packets that arrive from the moment you press Enter, never old buffered audio.
    """
    KEEP = 31 * 30                                   # keep about 30 s of packets

    def __init__(self, port, baud=BAUD):
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.buf = bytearray()
        self.last_seq = None
        self.total = 0                               # packets received so far
        self.history = collections.deque(maxlen=self.KEEP)   # (index, payload, gap_before)
        self.last_rx = time.time()
        self.error = None
        self.lock = threading.Lock()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while True:
            try:
                chunk = self.ser.read(4096)
            except Exception as e:                   # e.g. USB cable unplugged
                self.error = e
                return
            if not chunk:
                continue
            self.buf += chunk
            while True:
                pkt = self._parse_one()
                if pkt is None:
                    break
                payload, gap = pkt
                with self.lock:
                    self.history.append((self.total, payload, gap))
                    self.total += 1
                    self.last_rx = time.time()

    def _parse_one(self):
        while True:
            i = self.buf.find(MAGIC)
            if i < 0:
                del self.buf[:max(0, len(self.buf) - 3)]   # keep a possible partial magic
                return None
            if i > 0:
                del self.buf[:i]
            if len(self.buf) < PACKET_SIZE:
                return None
            seq, count = struct.unpack_from("<HH", self.buf, 4)
            payload = bytes(self.buf[8:8 + SAMPLES_PER_PACKET * 2])
            (chk,) = struct.unpack_from("<H", self.buf, 8 + SAMPLES_PER_PACKET * 2)
            if count != SAMPLES_PER_PACKET or (sum(payload) & 0xFFFF) != chk:
                del self.buf[:1]                          # false match or corrupted: resync
                continue
            del self.buf[:PACKET_SIZE]
            gap = 0
            if self.last_seq is not None:
                gap = (seq - self.last_seq - 1) & 0xFFFF
                if gap >= 1000:                      # a huge jump means the board was reset
                    gap = 0
            self.last_seq = seq
            return payload, gap

    def now(self):
        with self.lock:
            return self.total

    def wait_for(self, count, timeout=2.0):
        while True:
            with self.lock:
                if self.total >= count:
                    return
                quiet = time.time() - self.last_rx
            if self.error:
                raise RuntimeError(f"Lost the board: {self.error}")
            if quiet > timeout:
                raise TimeoutError(
                    "No audio from the board. Is mic_stream running? Press RESET on the Nucleo, "
                    "and make sure no other program (screen, another record_clips.py) has the port open.")
            time.sleep(0.01)

    def take(self, start, n):
        """Return packets start .. start+n-1 joined as bytes, and how many were lost inside."""
        self.wait_for(start + n)
        with self.lock:
            items = [it for it in self.history if start <= it[0] < start + n]
        data = b"".join(p for _, p, _ in items)
        dropped = sum(g for i, _, g in items if i > start)
        return data, dropped

    def record(self, seconds, preroll=0.1):
        """Record `seconds` of audio starting `preroll` s before now (catches an early start)."""
        n = math.ceil(seconds * RATE / SAMPLES_PER_PACKET)
        start = max(self.now() - round(preroll * RATE / SAMPLES_PER_PACKET), 0)
        data, dropped = self.take(start, n)
        return data[:int(seconds * RATE) * 2], dropped


def levels(pcm_bytes):
    a = array("h")
    a.frombytes(pcm_bytes)
    if sys.byteorder == "big":
        a.byteswap()
    if not a:
        return -120.0, -120.0
    mean = sum(a) / len(a)
    peak = max(abs(x - mean) for x in a)
    rms = math.sqrt(sum((x - mean) ** 2 for x in a) / len(a))
    to_db = lambda v: 20 * math.log10(v / 32768) if v > 0 else -120.0
    return to_db(peak), to_db(rms)


def loudest_window_db(pcm_bytes, window=1600):
    """RMS level of the loudest 100 ms in the clip: high when someone spoke, low for silence."""
    a = array("h")
    a.frombytes(pcm_bytes)
    if sys.byteorder == "big":
        a.byteswap()
    best = 0.0
    for i in range(0, max(len(a) - window + 1, 1), window // 2):
        s = a[i:i + window]
        if not s:
            break
        m = sum(s) / len(s)
        best = max(best, math.sqrt(sum((x - m) ** 2 for x in s) / len(s)))
    return 20 * math.log10(best / 32768) if best > 0 else -120.0


def bar(db, width=40, lo=-90, hi=0):
    n = int((min(max(db, lo), hi) - lo) / (hi - lo) * width)
    return "#" * n + "." * (width - n)


def save_wav(path, pcm_bytes):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm_bytes)


def play(pcm_bytes):
    player = shutil.which("afplay") or shutil.which("aplay")
    if not player:
        print("   (no audio player found on this computer)")
        return
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        tmp = f.name
    save_wav(tmp, pcm_bytes)
    subprocess.run([player, tmp])
    os.remove(tmp)


def next_index(folder, label, speaker):
    if not os.path.isdir(folder):
        return 1
    prefix = f"{label}.{speaker}."
    nums = [int(f[len(prefix):-4]) for f in os.listdir(folder)
            if f.startswith(prefix) and f.endswith(".wav") and f[len(prefix):-4].isdigit()]
    return max(nums, default=0) + 1


def find_port():
    ports = sorted(glob.glob("/dev/tty.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("Board not found. Plug the USB into the ST-LINK port, or give --port "
                 "(on Windows something like --port COM5).")
    return ports[0]


def run_meter(mic):
    print("Live level (Ctrl+C to stop). Talk and clap: the bar should move.\n")
    idx = mic.now()
    try:
        while True:
            chunk, dropped = mic.take(idx, 8)                 # ~0.25 s
            idx += 8
            peak, rms = levels(chunk)
            drop = f"  DROPPED {dropped}" if dropped else "            "
            print(f"\r peak {peak:6.1f} dBFS  {bar(peak)}{drop}   ", end="", flush=True)
    except KeyboardInterrupt:
        print()


def build_plan(n_wake, n_unknown, n_noise):
    plan = []
    for i in range(n_wake):
        plan.append(("hey_bmo", f'Say "hey BMO"  ({WAKE_STYLES[i % len(WAKE_STYLES)]})'))
    for i in range(n_unknown):
        plan.append(("unknown", f'Say "{UNKNOWN_WORDS[i % len(UNKNOWN_WORDS)]}"'))
    for i in range(n_noise):
        plan.append(("noise", f"Don't speak: {NOISE_PROMPTS[i % len(NOISE_PROMPTS)]}"))
    return plan


def run_session(mic, args):
    plan = build_plan(args.wake, args.unknown, args.noise)
    print(f"\nRecording session for '{args.speaker}': {len(plan)} clips of {args.seconds} s.")
    print("Speak 10-15 cm from the BMO mic on the breadboard (not the laptop!).")
    print("For each clip: press Enter, then speak straight away.\n")

    i = 0
    while i < len(plan):
        label, prompt = plan[i]
        folder = os.path.join(args.out, label)
        input(f"[{i + 1}/{len(plan)}] {prompt}\n   press Enter to record... ")
        print("   ● recording")
        pcm, dropped = mic.record(args.seconds)
        peak, rms = levels(pcm)
        voice = loudest_window_db(pcm)
        print(f"   voice {voice:6.1f} dBFS  {bar(voice)}   (peak {peak:.1f})")

        problems = []
        if dropped:
            problems.append(f"{dropped} packets dropped")
        if peak > -1.0:
            problems.append("too loud (clipped), move back a little")
        if label != "noise" and voice < -42:
            problems.append("no voice found: speak right after Enter, 10-15 cm from the mic")
        if problems:
            print("   ! " + "; ".join(problems))

        choice = input("   Enter = keep   r = redo   p = play back   q = quit: ").strip().lower()
        while choice == "p":
            play(pcm)
            choice = input("   Enter = keep   r = redo   p = play back   q = quit: ").strip().lower()
        if choice == "q":
            break
        if choice == "r":
            continue
        n = next_index(folder, label, args.speaker)
        path = os.path.join(folder, f"{label}.{args.speaker}.{n:03d}.wav")
        save_wav(path, pcm)
        print(f"   saved {path}\n")
        i += 1

    print(f"\nDone. Clips are in the '{args.out}' folder.")


def main():
    ap = argparse.ArgumentParser(description="Record BMO wake-word training clips.")
    ap.add_argument("--port", help="serial port (auto-detected on Mac/Linux)")
    ap.add_argument("--meter", action="store_true", help="just show a live level bar")
    ap.add_argument("--speaker", help="name of the person recording, e.g. kla")
    ap.add_argument("--wake", type=int, default=30, help='number of "hey BMO" clips')
    ap.add_argument("--unknown", type=int, default=20, help="number of other-word clips")
    ap.add_argument("--noise", type=int, default=10, help="number of background-noise clips")
    ap.add_argument("--seconds", type=float, default=1.5, help="length of each clip")
    ap.add_argument("--out", default="clips", help="output folder")
    args = ap.parse_args()

    port = args.port or find_port()
    print(f"Using {port} at {BAUD} baud")
    mic = MicStream(port)
    mic.wait_for(5, timeout=3.0)                  # make sure the board is streaming

    if args.meter:
        run_meter(mic)
        return
    if not args.speaker:
        sys.exit('Give a name, e.g.  python3 record_clips.py --speaker kla   (or use --meter)')
    args.speaker = "".join(c for c in args.speaker.lower() if c.isalnum()) or "anon"
    run_session(mic, args)


if __name__ == "__main__":
    main()
