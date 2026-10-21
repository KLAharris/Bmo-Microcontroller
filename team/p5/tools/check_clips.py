"""check_clips.py - compare recordings per speaker and flag bad clips.

Run next to your clips folder:
    python3 check_clips.py               (checks clips/hey_bmo)
    python3 check_clips.py games         (checks clips/games)

For each clip it measures:
  voice  = loudness of the loudest 100 ms (dBFS, higher = louder)
  start  = when the word starts (s)
  end    = when the word ends (s)
and flags clips that are much quieter than the others, start late, or are cut off.
No extra packages needed.
"""
import sys, os, glob, wave, array, math

label = sys.argv[1] if len(sys.argv) > 1 else "hey_bmo"
folder = os.path.join("clips", label)
files = sorted(glob.glob(os.path.join(folder, "*.wav")))
if not files:
    sys.exit(f"No .wav files in {folder}")

WIN = 1600  # 100 ms at 16 kHz


def db(x):
    return 20 * math.log10(max(x, 1) / 32768)


def analyse(path):
    with wave.open(path) as w:
        n, rate = w.getnframes(), w.getframerate()
        pcm = array.array("h", w.readframes(n))
    # loudness per 10 ms block
    step = rate // 100
    blocks = [math.sqrt(sum(v * v for v in pcm[i:i + step]) / step)
              for i in range(0, len(pcm) - step + 1, step)]
    loud = max(math.sqrt(sum(b * b for b in blocks[i:i + 10]) / 10)
               for i in range(0, max(1, len(blocks) - 9)))
    floor = sorted(blocks)[len(blocks) // 10]          # quietest 10% = background
    thr = max(floor * 4, loud / 6)                     # "voice" = well above background
    on = [i for i, b in enumerate(blocks) if b > thr]
    start = on[0] / 100 if on else None
    end = (on[-1] + 1) / 100 if on else None
    return db(loud), start, end, len(pcm) / rate


rows = []
for f in files:
    name = os.path.basename(f)
    parts = name.split(".")
    speaker = parts[1] if len(parts) >= 4 else "?"
    v, s, e, length = analyse(f)
    rows.append((speaker, name, v, s, e, length))

# per-speaker summary
print(f"\n=== {label}: {len(rows)} clips ===\n")
print(f"{'speaker':10} {'clips':>5} {'voice dB':>9} {'start s':>8} {'end s':>7}")
speakers = sorted(set(r[0] for r in rows))
all_v = sorted(r[2] for r in rows)
median_v = all_v[len(all_v) // 2]
for sp in speakers:
    rs = [r for r in rows if r[0] == sp]
    av = sum(r[2] for r in rs) / len(rs)
    st = [r[3] for r in rs if r[3] is not None]
    en = [r[4] for r in rs if r[4] is not None]
    print(f"{sp:10} {len(rs):5d} {av:9.1f} {sum(st)/len(st) if st else 0:8.2f} "
          f"{sum(en)/len(en) if en else 0:7.2f}")

# flag bad clips
print(f"\nAll speakers: median voice {median_v:.1f} dB\n")
print("Clips to re-record or delete:")
bad = 0
for sp, name, v, s, e, length in rows:
    why = []
    if v < median_v - 10:
        why.append(f"too quiet ({v:.1f} dB, others ~{median_v:.1f})")
    if s is None:
        why.append("no voice found")
    else:
        if s > 0.9:
            why.append(f"word starts late ({s:.2f} s)")
        if e is not None and e > length - 0.05:
            why.append("word cut off at the end")
        if s < 0.02:
            why.append("word cut off at the start")
    if why:
        bad += 1
        print(f"  {name:28} " + "; ".join(why))
if not bad:
    print("  none - all clips look fine")
print()
