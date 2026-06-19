# tests/test_vu_meter.py — pure VU logic, compiled and run on host.
import pathlib, shutil, subprocess, pytest
ROOT = pathlib.Path(__file__).parent.parent
GCC = shutil.which("gcc")
pytestmark = pytest.mark.skipif(GCC is None, reason="no host gcc on PATH")

@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    exe = tmp_path_factory.mktemp("vu_meter") / "vu_meter.exe"
    subprocess.run([GCC, "-Wall", "-Werror", "-I", str(ROOT / "src"),
                    "-o", str(exe),
                    str(ROOT / "tests" / "host" / "vu_meter_harness.c"),
                    str(ROOT / "src" / "audio" / "vu_meter.c"),
                    "-lm"], check=True)
    return exe

def run(exe, *args):
    return subprocess.run([str(exe), *map(str, args)], capture_output=True,
                          text=True, check=True).stdout.split()

def test_sample_left_is_high_word(harness):
    # frame 0x1234FFFF -> left = 0x1234 (4660), right = 0xFFFF (-1)
    assert run(harness, "sample", "0x1234FFFF", "0") == ["4660"]
    assert run(harness, "sample", "0x1234FFFF", "1") == ["-1"]

def test_sample_sign_extends(harness):
    # left = 0x8000 = -32768
    assert run(harness, "sample", "0x80000000", "0") == ["-32768"]

def test_peak_finds_max_abs_in_slot(harness):
    # two frames, left words 0x0064 (100) and 0xFF9C (-100); peak left = 100
    assert run(harness, "peak", "0", "0x00640000", "0xFF9C0000") == ["100"]

def test_peak_clamps_32768_to_32767(harness):
    # left = 0x8000 = -32768 -> abs clamps to 32767
    assert run(harness, "peak", "0", "0x80000000") == ["32767"]

def test_bar_floor_is_zero(harness):
    assert run(harness, "bar", "0", "100") == ["0"]

def test_bar_full_scale_is_max(harness):
    assert run(harness, "bar", "32767", "100") == ["100"]

def test_bar_monotonic_midscale(harness):
    lo = int(run(harness, "bar", "1000", "100")[0])
    hi = int(run(harness, "bar", "10000", "100")[0])
    assert 0 < lo < hi < 100

def test_color_thresholds(harness):
    # green / yellow / red as big-endian RGB565 hex
    assert run(harness, "color", "1000") == ["e007"]   # green 0x07E0 -> BE 0xE007
    assert run(harness, "color", "12000") == ["e0ff"]  # yellow 0xFFE0 -> BE 0xE0FF
    assert run(harness, "color", "30000") == ["00f8"]  # red 0xF800 -> BE 0x00F8
