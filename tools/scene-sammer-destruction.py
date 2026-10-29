"""Jump straight to the chapter 6 scene where The Void destroys Sammer's Kingdom (map wa1_02, GSW0 279) and record it:
dialogue is advanced automatically, the whole window is captured every half second, and a contact sheet plus Mario's
pose changes (SPM_LP_LOG_POSE) are written to the output folder. The private Play copy is deleted afterwards.
Usage: python -I tools/scene-sammer-destruction.py autotest/scene-x [seconds]"""
import glob
import os
import shutil
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 70
if out.exists():
    raise SystemExit('Choose a new output folder; existing captures are preserved.')

# Two seconds after the map loads Mario is put at x 0, where the map itself starts the scene. (SCENE_FORCE=1 also
# starts the scene's script, 0x80D7B34C, directly; then it can run twice and assert in npcNameToPtr.)
force = {'SPM_LP_RUN_EVT': '0x80D7B34C'} if os.environ.get('SCENE_FORCE') == '1' else {}
env = dict(os.environ, **force, SPM_LP_LOG_POSE='1', SPM_LP_RUN_EVT_X=os.environ.get('SCENE_MARIO_X', '0'), SPM_SWEEP_VSYNC='false', SPM_SWEEP_WINDOW='1280x720')
# Walk right, then left (the scene starts when Mario reaches its trigger), then press A through the dialogue.
walk = os.environ.get('SCENE_WALK', '')  # DOWN = screen right, UP = screen left (sideways Remote)
events, at = [], int(os.environ.get('SCENE_WALK_AT', '20000'))  # the map is up ~25 s after launch
for step in filter(None, walk.split(',')):
    button, ms = step.split(':')
    events.append(f'{at}:{button}:{ms}')
    at += int(ms) + 300
events += [f'{ms}:TWO:80' for ms in range(at, int(seconds * 1000), 1100)]
presses = ','.join(events)
step = float(os.environ.get('SCENE_SHOT_STEP', '0.5'))  # seconds between captures (0.1 for a GIF)
shots = ','.join(f'{i * step:g}' for i in range(int(5 / step), int(seconds / step)))
subprocess.run([sys.executable, '-I', str(root / 'tools/play-script.py'), '--map', 'wa1_02', '--gsw0', '279',
                '--input', presses, '--shots', shots, '--seconds', str(seconds), '--out', str(out)], env=env, check=False)

# Contact sheet: every 4th frame, 320x180 tiles.
from PIL import Image
frames = sorted(glob.glob(str(out / 'shots' / '*.bmp')), key=lambda f: float(Path(f).stem))[::4]
if frames:
    cols = 6
    sheet = Image.new('RGB', (cols * 320, ((len(frames) + cols - 1) // cols) * 200), 'black')
    for i, f in enumerate(frames):
        tile = Image.open(f).convert('RGB').resize((320, 180))
        sheet.paste(tile, ((i % cols) * 320, (i // cols) * 200))
    sheet.save(out / 'sheet.png')

# Mario's pose changes, from the runtime console log, before the Play copy goes.
lines = []
for log in glob.glob(str(out / 'Play' / 'UserData' / 'Logs' / '*' / 'console.log')):
    lines += [l.strip() for l in open(log, errors='replace') if '[lppose]' in l and ('wa1_02' in l or 'anim pose' in l or 'Sammer' in l)]
(out / 'poses.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('\n'.join(lines[-40:]))
shutil.rmtree(out / 'Play', ignore_errors=True)
print(f'sheet: {out / "sheet.png"}  frames: {len(frames) * 4}')
