"""Hidden scripted play-through from one map: warps there once (start_map.txt), then the game runs freely while the
input script plays, so door/elevator/cutscene transitions to other maps keep going (unlike sweep-spm-maps.py, which
stops at a map change). Screenshots at the given seconds after launch.
Usage: python tools\\play-script.py --map ta4_11 --gsw0 123 --input "1000:TWO:100,..." --shots 20,21,22 --seconds 40
       --out autotest\\x [--disc Disc_PAL\\DATA]   (env SPM_SWEEP_* window/aspect options work as in the sweep)"""
import argparse, importlib.util, os, subprocess, sys, time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sweep', root / 'tools/sweep-spm-maps.py')
sweep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sweep)  # the sweep's guarded main does not run on import

ap = argparse.ArgumentParser()
ap.add_argument('--map', required=True)
ap.add_argument('--gsw0', type=int, default=-1)
ap.add_argument('--input', default='')
ap.add_argument('--shots', default='')
ap.add_argument('--seconds', type=float, default=40)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--disc', type=Path)
args = ap.parse_args()

out = args.out.resolve()
out.mkdir(parents=True, exist_ok=False)
disc = (args.disc or root / 'Disc/DATA').resolve()
play = sweep.prepare(root, out, root / 'Play/SuperPaperMario.exe', disc)
(play / 'start_map.txt').write_text(f'{args.map} {args.gsw0}\n' if args.gsw0 >= 0 else f'{args.map}\n')
if os.environ.get('SPM_PLAY_DRAWDUMP'):  # draw list of that presented frame (vi.cpp reads drawdump.txt)
    (play / 'drawdump.txt').write_text(os.environ['SPM_PLAY_DRAWDUMP'] + '\n')
env = os.environ.copy()
env.pop('SPM_MAP_SWEEP_FILE', None)
env['NSMBW_INPUT_SCRIPT'] = args.input
env.setdefault('AURORA_INTERP_NOCAP', '1')  # SPM_SWEEP_INTERP still interpolates on a 60 Hz test monitor
shots = sorted(float(s) for s in args.shots.split(',') if s)
(out / 'shots').mkdir(exist_ok=True)
with (out / 'run.log').open('wb') as log:
    proc = subprocess.Popen([str(play / 'SuperPaperMario.exe')], cwd=play, env=env, stdout=log, stderr=subprocess.STDOUT,
                            creationflags=0x08000000 if os.name == 'nt' else 0)
    start = time.monotonic()
    try:
        while time.monotonic() - start < args.seconds and proc.poll() is None:
            while shots and time.monotonic() - start >= shots[0]:
                s = shots.pop(0)
                sweep.capture(proc.pid, out / 'shots' / f'{s:g}.bmp')
            time.sleep(0.02)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=10)
print(f'done: {out}')
