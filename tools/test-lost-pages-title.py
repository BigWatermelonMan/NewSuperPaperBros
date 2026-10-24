"""Launch the game normally (title screen path), let SPM_LP_TESTAREA open the test area, and screenshot it.
Usage: run_title.py <src root> <out dir> <seconds> [extra input events]"""
import importlib.util
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

root, out, seconds = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), float(sys.argv[3])
extra = sys.argv[4] if len(sys.argv) > 4 else ''
spec = importlib.util.spec_from_file_location('sweep', root / 'tools' / 'sweep-spm-maps.py')
sweep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sweep)

if out.exists():
    raise SystemExit('Choose a new output folder; existing captures are preserved.')
(out / 'shots').mkdir(parents=True)
# A private copy of Play\ (fresh saves) like the sweep makes.
play = out / 'Play'
shutil.copytree(root / 'Play', play, ignore=shutil.ignore_patterns('Saves', 'NAND'))
cfg = (play / 'UserData' / 'Config.toml').read_text(encoding='utf-8')
cfg = cfg.replace('display_mode = "borderless"', 'display_mode = "windowed"\nwindow_width = 1280\nwindow_height = 720\nwindow_x = -4000\nwindow_y = 100')
cfg += '\n[audio]\nmuted = true\n'
(play / 'UserData' / 'Config.toml').write_text(cfg, encoding='utf-8')

env = dict(os.environ)
physical_f10 = os.environ.get('SPM_LP_TEST_PHYSICAL_F10') == '1'
if physical_f10:
    env.pop('SPM_LP_TESTAREA', None)
else:
    env['SPM_LP_TESTAREA'] = '1'
events = [f'{ms}:TWO:100' for ms in range(2000, 22000, 1500)] + ['6000:PLUS:100']
if extra:
    events += extra.split(',')
env['NSMBW_INPUT_SCRIPT'] = ','.join(events)
log = open(out / 'run.err.log', 'w')
p = subprocess.Popen([str(play / 'SuperPaperMario.exe')], cwd=str(play), env=env, stdout=log, stderr=subprocess.STDOUT)
start = time.monotonic()
shot = 0
sent_f10 = False
while time.monotonic() - start < seconds:
    time.sleep(1.0)
    t = time.monotonic() - start
    if physical_f10 and not sent_f10:
        log.flush()
        if 'seq -> title (next title)' in (out / 'run.err.log').read_text(errors='replace'):
            # Send only to this private test window, without moving desktop focus.
            import ctypes
            from ctypes import wintypes
            user = ctypes.windll.user32
            user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
            callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            def send_key(hwnd, _):
                owner = wintypes.DWORD()
                user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
                if owner.value == p.pid and user.IsWindowVisible(hwnd):
                    user.PostMessageW(hwnd, 0x0100, 0x79, 1 | (0x44 << 16))
                    user.PostMessageW(hwnd, 0x0101, 0x79, 1 | (0x44 << 16) | (3 << 30))
                return True
            user.EnumWindows(callback_type(send_key), 0)
            sent_f10 = True
            print('Sent F10 to private title window', flush=True)
    if t > 6:
        sweep.capture(p.pid, out / 'shots' / f't{int(t):03d}.bmp')
p.kill()
p.wait()
log.close()
print('done')
