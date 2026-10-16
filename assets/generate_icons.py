"""Render the logo SVGs to PNG icons and a Windows .ico.

Requires Microsoft Edge (used headless to render the SVGs) and Pillow:

    python assets/generate_icons.py

Writes assets/icons/apdu-tracer-<size>.png and assets/apdu-tracer.ico. Sizes
up to 32 px use apdu-tracer-small.svg, which stays legible at that scale.
"""

import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

ASSETS = Path(__file__).resolve().parent
SIZES = [16, 24, 32, 48, 64, 128, 256]
SMALL_MAX = 32
EDGE_CANDIDATES = [
    Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
    Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"),
]


def find_edge() -> Path:
    for candidate in EDGE_CANDIDATES:
        if candidate.exists():
            return candidate
    found = shutil.which("msedge")
    if found:
        return Path(found)
    raise SystemExit("Microsoft Edge was not found; it is needed to render the SVGs.")


def render_sheet(edge: Path, work: Path) -> Image.Image:
    # Place each size on one transparent page at a known offset, then crop.
    cells, x = [], 0
    for size in SIZES:
        source = "small.svg" if size <= SMALL_MAX else "logo.svg"
        cells.append(f'<img src="{source}" width="{size}" height="{size}" '
                     f'style="position:absolute;left:{x}px;top:0">')
        x += size + 8
    shutil.copy(ASSETS / "apdu-tracer.svg", work / "logo.svg")
    shutil.copy(ASSETS / "apdu-tracer-small.svg", work / "small.svg")
    (work / "sheet.html").write_text(
        "<!doctype html><html><body style='margin:0;background:transparent'>"
        + "".join(cells) + "</body></html>", encoding="utf-8")
    screenshot = work / "sheet.png"
    subprocess.run([
        str(edge), "--headless=new", "--disable-gpu", "--hide-scrollbars",
        "--force-device-scale-factor=1", "--default-background-color=00000000",
        f"--window-size={x},{max(SIZES)}", f"--screenshot={screenshot}",
        (work / "sheet.html").as_uri(),
    ], check=True, capture_output=True, timeout=60)
    return Image.open(screenshot).convert("RGBA")


def main() -> None:
    edge = find_edge()
    output = ASSETS / "icons"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as directory:
        sheet = render_sheet(edge, Path(directory))
    images, x = [], 0
    for size in SIZES:
        image = sheet.crop((x, 0, x + size, size))
        if image.getchannel("A").getextrema()[1] == 0:
            raise SystemExit(f"The {size} px render is empty.")
        image.save(output / f"apdu-tracer-{size}.png")
        images.append(image)
        x += size + 8
    # Largest first; each size keeps its own render instead of a downscale.
    images[-1].save(ASSETS / "apdu-tracer.ico", sizes=[(s, s) for s in SIZES],
                    append_images=images[:-1])
    print(f"Wrote {len(images)} PNGs to {output} and {ASSETS / 'apdu-tracer.ico'}")


if __name__ == "__main__":
    main()
