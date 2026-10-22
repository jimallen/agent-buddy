#!/usr/bin/env python3
"""Show what the seven-inch RGB panel will display, without the board.

Runs the firmware's own RgbScanout (the only code that decides where content lands on the
panel) on a raw RGB565 frame or a built-in test pattern, then converts the result to a PNG.

    python3 tools/rgb_panel_preview.py --pattern
    python3 tools/rgb_panel_preview.py --in frame.raw --width 412 --height 466 --x 27
    python3 tools/rgb_panel_preview.py --pattern --angle 90
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def build_tool() -> Path:
    BUILD.mkdir(exist_ok=True)
    tool = BUILD / "rgb-panel-preview"
    subprocess.run([
        "clang++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined",
        str(ROOT / "tools/rgb_panel_preview.cpp"), "-o", str(tool),
    ], check=True)
    return tool


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--in", dest="raw", help="raw RGB565 frame, as a device capture writes it")
    parser.add_argument("--pattern", action="store_true", help="draw a test pattern instead of a capture")
    parser.add_argument("--width", type=int, default=412)
    parser.add_argument("--height", type=int, default=466)
    parser.add_argument("--x", type=int, default=27, help="where the frame sits inside the content")
    parser.add_argument("--y", type=int, default=0)
    parser.add_argument("--content", type=int, default=466)
    parser.add_argument("--panel-width", type=int, default=800)
    parser.add_argument("--panel-height", type=int, default=480)
    parser.add_argument("--order", choices=("display", "native"), default="display")
    parser.add_argument("--angle", type=float, default=0.0)
    parser.add_argument("--fleet", type=int, default=1,
                        help="tile this many characters across the panel's full width")
    parser.add_argument("--out", default="build/rgb-panel.png")
    args = parser.parse_args()
    if not args.raw and not args.pattern:
        parser.error("pass --in <raw RGB565> or --pattern")

    tool = build_tool()
    ppm = BUILD / "rgb-panel.ppm"
    command = [str(tool), "--out", str(ppm),
               "--width", str(args.width), "--height", str(args.height),
               "--x", str(args.x), "--y", str(args.y), "--content", str(args.content),
               "--panel-width", str(args.panel_width), "--panel-height", str(args.panel_height),
               "--order", args.order, "--angle", str(args.angle),
               "--fleet", str(args.fleet)]
    command += ["--in", args.raw] if args.raw else ["--pattern"]
    summary = subprocess.run(command, check=True, capture_output=True, text=True).stdout.strip()

    from PIL import Image
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    Image.open(ppm).save(out)
    print(summary)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
