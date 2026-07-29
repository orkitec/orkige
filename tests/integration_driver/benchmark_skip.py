#!/usr/bin/env python3
"""Shared scene-skip resolution for the benchmark ctest drivers.

benchmark.skipScenes is a comma-separated set of scene BASENAMES the autonomous
tour walks straight past (never loads) - a per-host quarantine for a scene whose
material/shader mix faults a specific software driver's compiler. The director
reads it from the ORKIGE_CVAR_benchmark_skipScenes boot seed (a String cvar) and
the drivers derive their pass criteria from the SAME set, so the quarantined
tour passes by design (the skipped scene is simply absent) rather than by
accident.

The one host that needs it today is Windows CI: its software-Vulkan driver
faults inside the cold shader-variant compile of the mirror lake scene (the
crash-breadcrumb trail dead-ends within ~0.5s of the mirrorlake scene load, with
planar reflection already off - so the fault is the scene's shader mix in that
driver's compiler, not the planar mirror render nor the engine). Linux/lavapipe
and macOS/Metal never reproduce it and real GPUs are unaffected; the mirror
FEATURE itself stays fully tested everywhere through the dedicated pixel gates
(water_mirror_wobble, benchmark_crossflavor_parity_mirror), which keep planar
reflection ON and never load this scene.

Pure stdlib. Imported by the three tour drivers (run_benchmark_test.py,
run_benchmark_restart_test.py, run_benchmark_budget_test.py).
"""

import sys

SKIP_ENV = "ORKIGE_CVAR_benchmark_skipScenes"

# the tour in sequence order: (scene basename, recorder label). The basename is
# what benchmark.skipScenes names (and the scene file stem); the label is the
# director's sceneLabel export the artifact records under.
TOUR_ORDER = [
    ("vista", "Terrace Vista"),
    ("lake", "Still Water"),
    ("mirrorlake", "Mirror Lake"),
    ("lumens", "Night Lumens"),
    ("swarm", "Ember Swarm"),
    ("field", "Instance Field"),
    ("cast", "Character Cast"),
    ("flatland", "Flatland"),
    ("console", "Console"),
    ("cascade", "Cascade"),
    ("tally", "Tally"),
]


def resolve_skip_scenes(env):
    """The scene basenames the tour skips on this host. A manual
    ORKIGE_CVAR_benchmark_skipScenes env (local proving) is honored and, on
    win32, mirrorlake is added by default (the software-Vulkan driver-compiler
    quarantine, see the module docstring). Writes the resolved value back into
    env so the player's director sees exactly the set the driver's expectations
    assume. Returns the set."""
    skip = set()
    for name in env.get(SKIP_ENV, "").split(","):
        name = name.strip()
        if name:
            skip.add(name)
    if sys.platform == "win32":
        skip.add("mirrorlake")
    if skip:
        env[SKIP_ENV] = ",".join(sorted(skip))
    return skip
