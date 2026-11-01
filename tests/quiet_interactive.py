"""Guard against restoring the interactive per-frame FPS console reporter."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
interactive = (root / 'native/client/interactive.cpp').read_text()
loop = interactive[interactive.index('while (running && window_.opened()'):interactive.index('\nprivate:')]
assert 'view_.fps =' in loop, 'the in-game FPS value must still be updated'
assert 'drawFpsCounter(frame, view_.fps)' in interactive, 'the in-game FPS counter must remain'
assert 'std::cout' not in loop and 'std::printf' not in loop and 'std::fprintf' not in loop
for path in (root / 'native/client').rglob('*'):
    if path.suffix in {'.h', '.cpp'}:
        text = path.read_text()
        assert 'FrameRateReporter' not in text and '[fps]' not in text and 'GTC_FPS' not in text, path
print('quiet interactive loop: PASS (source-level check)')
