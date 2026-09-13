#!/usr/bin/env python3
"""Bake shaders/* into eden_atmosphere_shaders.gen.h as C++ raw string literals.

The module embeds its shaders rather than loading them from res://, so that a game using
EdenPlanetAtmosphere / EdenCloudShell needs nothing but the engine binary -- no shader files
to copy into the project. `#include` lines are resolved here, at generation time: Godot's
ShaderInclude resolution only works against real res:// paths, which the embedded copies do
not have, and RenderingDevice's glslang has no include path at all.

Runs automatically from SCsub whenever a shader or this script changes, so the output is a
normal build artefact and is gitignored like every other *.gen.* file in the engine. It can
still be run by hand for a quick check:

    python modules/eden_atmosphere/gen_shader_header.py
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER_DIR = os.path.join(HERE, "shaders")
OUT = os.path.join(HERE, "eden_atmosphere_shaders.gen.h")

INCLUDE_RE = re.compile(r'^\s*#include\s+"(?:res://[^"]*?/)?([A-Za-z0-9_.]+)"\s*$')


def read(name):
    with open(os.path.join(SHADER_DIR, name), "r", encoding="utf-8") as f:
        return f.read()


def inline_includes(text, depth=0):
    """Splice any `#include "res://....gdshaderinc"` line into the text."""
    if depth > 4:
        raise RuntimeError("include nesting too deep -- cycle?")
    out = []
    for line in text.splitlines():
        m = INCLUDE_RE.match(line)
        if m:
            inc_name = m.group(1)
            inc_path = os.path.join(SHADER_DIR, inc_name)
            if not os.path.exists(inc_path):
                raise RuntimeError("cannot resolve include %r from %r" % (inc_name, inc_path))
            out.append("// ---- inlined from %s ----" % inc_name)
            out.append(inline_includes(read(inc_name), depth + 1))
            out.append("// ---- end %s ----" % inc_name)
        else:
            out.append(line)
    return "\n".join(out)


def emit(name, text):
    # A raw string literal cannot contain its own delimiter. )" never appears in GLSL, but
    # check rather than silently emit a file that fails to compile.
    if ')GLSL"' in text:
        raise RuntimeError("%s contains the raw-string delimiter" % name)
    return 'inline constexpr const char *%s = R"GLSL(\n%s\n)GLSL";\n' % (name, text)


def generate(out_path):
    sky = inline_includes(read("planet_sky.gdshader"))
    clouds = inline_includes(read("planet_clouds.gdshader"))
    # RenderingDevice compute shaders for the post-process (fog + light rays). glslang requires
    # `#version` to be the first line, so it must survive inlining untouched -- it does, because
    # the include only ever appears after it.
    rays = inline_includes(read("atmosphere_rays.glsl"))
    fog = inline_includes(read("atmosphere_fog_composite.glsl"))

    parts = [
        "// GENERATED FILE -- do not edit.",
        "// Produced by modules/eden_atmosphere/gen_shader_header.py from shaders/*.gdshader.",
        "// Regenerated automatically by SCsub when a shader changes.",
        "",
        "#ifndef EDEN_ATMOSPHERE_SHADERS_GEN_H",
        "#define EDEN_ATMOSPHERE_SHADERS_GEN_H",
        "",
        emit("EDEN_SKY_SHADER_CODE", sky),
        "",
        emit("EDEN_CLOUD_SHADER_CODE", clouds),
        "",
        emit("EDEN_RAYS_COMPUTE_CODE", rays),
        "",
        emit("EDEN_FOG_COMPOSITE_COMPUTE_CODE", fog),
        "",
        "#endif // EDEN_ATMOSPHERE_SHADERS_GEN_H",
        "",
    ]

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(parts))

    print("wrote %s (sky %d, clouds %d, rays %d, fog %d bytes)" % (out_path, len(sky), len(clouds), len(rays), len(fog)))


# SCons action entry point (see SCsub).
def build(target, source, env):
    generate(str(target[0]))


def main():
    generate(OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
