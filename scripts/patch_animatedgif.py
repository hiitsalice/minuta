"""Set AnimatedGIF's source image width limit to 1920px."""

Import("env")

import os
import re

libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")

if os.path.isdir(libdeps_dir):
    pattern = re.compile(
        r"#ifdef __LINUX__\s*"
        r"#define MAX_WIDTH 2048\s*"
        r"#else\s*"
        r"#define MAX_WIDTH 480\s*"
        r"#endif // __LINUX__"
    )

    for environment in os.listdir(libdeps_dir):
        header = os.path.join(
            libdeps_dir, environment, "AnimatedGIF", "src", "AnimatedGIF.h"
        )

        if not os.path.isfile(header):
            continue

        with open(header, "r", encoding="utf-8") as source:
            contents = source.read()

        if re.search(r"^#define MAX_WIDTH 1920$", contents, re.MULTILINE):
            continue

        updated, count = pattern.subn(
            "#define MAX_WIDTH 1920",
            contents,
        )

        if count != 1:
            raise RuntimeError(
                "Could not safely patch AnimatedGIF MAX_WIDTH in " + header
            )

        with open(header, "w", encoding="utf-8") as target:
            target.write(updated)

        print("AnimatedGIF: MAX_WIDTH set to 1920 in " + header)
