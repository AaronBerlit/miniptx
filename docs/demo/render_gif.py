"""Renders docs/demo/demo.txt (from capture.sh) as an animated terminal GIF.

    python docs/demo/render_gif.py [path/to/monospace.ttf]

Lines starting with "$ " are typed out character by character; other lines
appear at once as command output; "---" clears the screen for a new scene.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).parent
COLS, ROWS = 92, 18
FONT_SIZE, PAD = 15, 14
BG, FG, PROMPT, DIM = (13, 17, 23), (201, 209, 217), (126, 231, 135), (139, 148, 158)

font_path = sys.argv[1] if len(sys.argv) > 1 else "DejaVuSansMono.ttf"
font = ImageFont.truetype(font_path, FONT_SIZE)
cw = font.getbbox("M")[2]
ch = int(FONT_SIZE * 1.35)
W, H = COLS * cw + 2 * PAD, ROWS * ch + 2 * PAD

frames, durations = [], []


def snapshot(lines, ms, cursor=False):
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    visible = lines[-ROWS:]
    for i, line in enumerate(visible):
        y = PAD + i * ch
        if line.startswith("$ "):
            d.text((PAD, y), "$", font=font, fill=PROMPT)
            d.text((PAD + 2 * cw, y), line[2:], font=font, fill=FG)
        else:
            color = DIM if line.lstrip().startswith("//") else FG
            d.text((PAD, y), line[:COLS], font=font, fill=color)
    if cursor:
        last = visible[-1] if visible else ""
        x = PAD + len(last) * cw
        d.rectangle([x, PAD + (len(visible) - 1) * ch + 2, x + cw - 2, PAD + len(visible) * ch - 2], fill=FG)
    frames.append(img.quantize(colors=16))
    durations.append(ms)


scenes = (HERE / "demo.txt").read_text().split("\n---\n")
for scene in scenes:
    screen = []
    for line in scene.rstrip("\n").split("\n"):
        if line.startswith("$ "):
            cmd = line[2:]
            screen.append("$ ")
            snapshot(screen, 500, cursor=True)
            for i in range(0, len(cmd), 3):  # type 3 characters per frame
                screen[-1] = "$ " + cmd[: i + 3]
                snapshot(screen, 45, cursor=True)
            snapshot(screen, 400, cursor=True)
        else:
            screen.append(line)
    snapshot(screen, 3800)

out = HERE.parent / "demo.gif"
frames[0].save(out, save_all=True, append_images=frames[1:], duration=durations, loop=0, optimize=True)
print(f"wrote {out} ({len(frames)} frames, {out.stat().st_size // 1024} KB, {W}x{H})")
