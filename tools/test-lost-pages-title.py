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
# SPM_LP_TEST_KEYS=8:F5,10:DOWN,13:RETURN posts those keys to the private window that many seconds after the
# test area opens (the F5 story battle menu and the like).
KEYS = {'F5': (0x74, 0x3F), 'F6': (0x75, 0x40), 'F7': (0x76, 0x41), 'RIGHT': (0x27, 0x14D), 'UP': (0x26, 0x148), 'DOWN': (0x28, 0x150), 'RETURN': (0x0D, 0x1C), 'ESCAPE': (0x1B, 0x01)}
pending_keys = sorted((float(t), name) for t, name in
                      (item.split(':') for item in os.environ.get('SPM_LP_TEST_KEYS', '').split(',') if item))
test_area_at = None


def post_key(vk, scan):
    import ctypes
    from ctypes import wintypes
    user = ctypes.windll.user32
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    extended = 1 << 24 if scan & 0x100 else 0

    def send(hwnd, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == p.pid and user.IsWindowVisible(hwnd):
            user.PostMessageW(hwnd, 0x0100, vk, 1 | ((scan & 0xFF) << 16) | extended)
            user.PostMessageW(hwnd, 0x0101, vk, 1 | ((scan & 0xFF) << 16) | extended | (3 << 30))
        return True
    user.EnumWindows(callback_type(send), 0)
interval = float(os.environ.get('SPM_TEST_SHOT_INTERVAL', '1'))  # seconds between screenshots
while time.monotonic() - start < seconds:
    time.sleep(interval)
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
    if pending_keys:
        if test_area_at is None:
            log.flush()
            if 'opening the test area' in (out / 'run.err.log').read_text(errors='replace'):
                test_area_at = t
        while test_area_at is not None and pending_keys and t - test_area_at >= pending_keys[0][0]:
            _, name = pending_keys.pop(0)
            post_key(*KEYS[name])
            print(f'Sent {name} at {t:.0f} s', flush=True)
    if t > 6:
        sweep.capture(p.pid, out / 'shots' / (f't{int(t):03d}.bmp' if interval >= 1 else f't{t:07.2f}.bmp'))
p.kill()
p.wait()
log.close()
print('done')
