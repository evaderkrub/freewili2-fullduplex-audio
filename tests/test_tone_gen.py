# tests/test_tone_gen.py — pure tone math, compiled and run on host.
import pathlib, shutil, subprocess, pytest
ROOT = pathlib.Path(__file__).parent.parent
GCC = shutil.which("gcc") or r"C:/msys64/mingw64/bin/gcc.exe"
pytestmark = pytest.mark.skipif(not pathlib.Path(GCC).exists() and shutil.which("gcc") is None,
                                reason="no host gcc")


@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    exe = tmp_path_factory.mktemp("tone") / "tone.exe"
    subprocess.run([GCC, "-Wall", "-Werror", "-I", str(ROOT / "src"), "-o", str(exe),
                    str(ROOT / "tests/host/tone_gen_harness.c"),
                    str(ROOT / "src/audio/tone_gen.c"), "-lm"], check=True)
    return exe


def run(exe, *args):
    return subprocess.run([str(exe), *map(str, args)], capture_output=True,
                          text=True, check=True).stdout.split()


def test_amplitude_near_full_scale(harness):
    # 1 kHz @ 16 kHz, 64 samples: peak ~28000, never clips.
    pk = int(run(harness, "peak")[0])
    assert 24000 < pk < 32768


def test_four_periods_have_eight_zero_crossings(harness):
    # 1 kHz at 16 kHz fs => 16 samples/period; 64 samples = 4 periods => 8 sign changes.
    assert run(harness, "zc")[0] == "8"


def test_ring_tiles_seamlessly(harness):
    # 64-frame buffer = exactly 4 periods; sample[64] must equal sample[0] (ring wrap).
    assert run(harness, "wrap")[0] == "OK"
