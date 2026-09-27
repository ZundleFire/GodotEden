#!/usr/bin/env python3
"""Bake shaders/* into eden_foliage_shaders.gen.h as C++ raw string literals.

EdenFoliage embeds its plant shaders so a project needs nothing but the engine binary. `#include` lines are
resolved here (Godot's ShaderInclude only resolves real res:// files). The includes are the shared weather,
wind and season code; the project still declares their global uniforms (eden_weather_*, eden_wind*,
eden_calendar*) in project.godot [shader_globals] -- EdenAmbience sets them.

Runs from SCsub whenever a shader or this script changes; the output is gitignored like every *.gen.* file.
By hand: python modules/eden_foliage/gen_foliage_shaders.py
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER_DIR = os.path.join(HERE, "shaders")
OUT = os.path.join(HERE, "eden_foliage_shaders.gen.h")

INCLUDE_RE = re.compile(r'^\s*#include\s+"(?:res://[^"]*?/)?([A-Za-z0-9_.]+)"\s*$')


def read(name):
    with open(os.path.join(SHADER_DIR, name), "r", encoding="utf-8") as f:
        return f.read()


def inline_includes(text, seen, depth=0):
    """Splice `#include "res://....gdshaderinc"` lines in, each file once (they have no include guards)."""
    if depth > 4:
        raise RuntimeError("include nesting too deep -- cycle?")
    out = []
    for line in text.splitlines():
        m = INCLUDE_RE.match(line)
        if m:
            name = m.group(1)
            if name in seen:
                continue
            if not os.path.exists(os.path.join(SHADER_DIR, name)):
                raise RuntimeError("cannot resolve include %r" % name)
            seen.add(name)
            out.append("// ---- inlined from %s ----" % name)
            out.append(inline_includes(read(name), seen, depth + 1))
            out.append("// ---- end %s ----" % name)
        else:
            out.append(line)
    return "\n".join(out)


def emit(name, text):
    if ')GLSL"' in text:
        raise RuntimeError("%s contains the raw-string delimiter" % name)
    # MSVC limits one string literal to ~16 KB: split into adjacent literals
    chunks = [text[i:i + 12000] for i in range(0, len(text), 12000)] or [""]
    body = "\n".join('R"GLSL(%s)GLSL"' % c for c in chunks)
    return "inline constexpr const char *%s =\n%s;\n" % (name, body)


def generate(out_path):
    tree = inline_includes(read("eden_tree_lowpoly.gdshader"), set())
    grass = inline_includes(read("eden_grass_lowpoly.gdshader"), set())
    parts = [
        "// GENERATED FILE -- do not edit.",
        "// Produced by modules/eden_foliage/gen_foliage_shaders.py from shaders/*.",
        "",
        "#ifndef EDEN_FOLIAGE_SHADERS_GEN_H",
        "#define EDEN_FOLIAGE_SHADERS_GEN_H",
        "",
        emit("EDEN_FOLIAGE_TREE_SHADER_CODE", tree),
        "",
        emit("EDEN_FOLIAGE_GRASS_SHADER_CODE", grass),
        "",
        "#endif // EDEN_FOLIAGE_SHADERS_GEN_H",
        "",
    ]
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(parts))
    print("wrote %s (tree %d, grass %d bytes)" % (out_path, len(tree), len(grass)))


def build(target, source, env):
    generate(str(target[0]))


if __name__ == "__main__":
    generate(OUT)
    sys.exit(0)
