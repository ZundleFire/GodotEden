#!/usr/bin/env python3
"""Retarget the import settings of the space panoramas so they fit in VRAM.

The source .HDR panoramas are 16384x8192. Godot imports them fine, but at runtime that is
roughly half a gigabyte of VRAM EACH (16384 * 8192 * 4 bytes as RGBE, more once expanded), which
a 2GB card cannot hold even one of alongside a scene. The import succeeds and then the texture
fails to become resident -- which looks like "Godot isn't loading them".

This rewrites three import options per file:

  process/size_limit   caps the largest axis, so 16384x8192 -> 4096x2048 (a 16x pixel reduction)
  compress/mode = 2    VRAM Compressed; for an HDR source that is BPTC/BC6H at 1 byte/pixel,
                       which Maxwell (GTX 750 Ti) supports natively
  mipmaps/generate     on -- a full-screen background panorama aliases badly without them, and
                       the sky shader's seam-safe textureGrad path only matters once mips exist

Net effect per panorama: ~536 MB -> ~11 MB of VRAM.

On quality: at 4096 wide, a 75-degree FOV samples about 850 texels across the screen, so at 1080p
it is slightly soft. Pass a larger limit if you would rather spend the VRAM -- 8192 is ~33 MB
each and effectively 1:1 at 1080p.

Usage:
    python demo_eden/set_panorama_import.py [size_limit] [folder]

Defaults: size_limit 4096, folder <this script's dir>/Panoramics

After running, re-import (the script clears the cached artifacts so the editor re-imports on next
open, or run:  godot --path demo_eden --headless --import ).
"""

import os
import re
import shutil
import sys

DEFAULT_SIZE_LIMIT = 4096
HERE = os.path.dirname(os.path.abspath(__file__))

# param name -> value to force
FORCED = {
    "process/size_limit": None,  # filled in from argv
    "compress/mode": "2",
    "mipmaps/generate": "true",
}


def patch_import_file(path, size_limit):
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    forced = dict(FORCED)
    forced["process/size_limit"] = str(size_limit)

    changed = False
    for key, value in forced.items():
        # Keys contain '/', so escape for the regex.
        pattern = re.compile(r"^%s=.*$" % re.escape(key), re.MULTILINE)
        replacement = "%s=%s" % (key, value)
        if pattern.search(text):
            new_text = pattern.sub(replacement, text)
        else:
            # Option absent (older import file): append it under [params].
            new_text = text.rstrip("\n") + "\n" + replacement + "\n"
        if new_text != text:
            changed = True
            text = new_text

    if changed:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    return changed


def main():
    size_limit = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SIZE_LIMIT
    folder = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "Panoramics")

    if not os.path.isdir(folder):
        print("no such folder: %s" % folder)
        return 1

    imports = sorted(f for f in os.listdir(folder) if f.endswith(".import"))
    if not imports:
        print("no .import files in %s -- open the project once so Godot generates them" % folder)
        return 1

    patched = 0
    for name in imports:
        if patch_import_file(os.path.join(folder, name), size_limit):
            patched += 1

    # Drop the cached artifacts so the next import actually re-runs. Godot keys the cache on a
    # hash of the source plus its params, so stale .ctex/.md5 pairs would otherwise be reused.
    imported_dir = os.path.join(HERE, ".godot", "imported")
    removed = 0
    if os.path.isdir(imported_dir):
        stems = {os.path.splitext(n)[0] for n in imports}  # e.g. "SkySphere_01.HDR"
        for entry in os.listdir(imported_dir):
            for stem in stems:
                if entry.startswith(stem + "-"):
                    os.remove(os.path.join(imported_dir, entry))
                    removed += 1
                    break

    print("patched %d/%d import files (size_limit=%d, BC6H, mipmaps on)" % (patched, len(imports), size_limit))
    print("cleared %d cached artifacts -- re-import now" % removed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
