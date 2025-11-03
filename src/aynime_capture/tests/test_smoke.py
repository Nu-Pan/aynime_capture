import platform

import pytest


def test_import():
    module = pytest.importorskip("aynime_capture")
    assert hasattr(module, "CaptureOptions")
    opts = module.CaptureOptions()
    assert opts.buffer_seconds == pytest.approx(2.0)
    assert opts.memory_budget_mb == 512
    assert opts.target_fps == 30


@pytest.mark.skipif(platform.system() != "Windows", reason="Windows-only smoke test")
def test_stream_factory_signatures():
    import aynime_capture as ayncap

    opts = ayncap.CaptureOptions(buffer_seconds=1.0, memory_budget_mb=128, target_fps=15)
    assert isinstance(opts, ayncap.CaptureOptions)
    assert callable(ayncap.open_window)
    assert callable(ayncap.open_monitor)
