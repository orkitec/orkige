#!/usr/bin/env python3
"""Build the script-in-pak test fixture: a STORED zip holding ONE Lua script
under "scripts/" and ONE authored data file under "data/", and nothing else. The
player mounts this pak and loads a path-bound ScriptComponent whose file lives
ONLY inside it (no loose file, no --project), proving Lua scripts read through
the resource system from an archive in place instead of via fopen - the read
that removes the Android fopen-tree extraction (Docs/filesystem.md). The script
then reads the data file through the `data` table, so the SAME in-place read is
proven for content a script asks for at runtime.

Usage: make_script_pak_fixture.py <out.pak>

Stdlib only (python_stdlib_lint): zipfile with ZIP_STORED so the entry is read
in place, exactly the choice the Android `stored` mode makes.
"""
import os
import sys
import zipfile


# a path-bound ScriptComponent script. Its top-level chunk + init + update each
# stamp a field on the global `pak_marker` table the selfcheck created up front
# (a sandbox write to a member of an existing global table mutates that shared
# table - the ScriptRuntime `shared`-table idiom), so the C++ side observes that
# the script LOADED and RAN purely from what it published.
PAK_SCRIPT = """-- loaded from inside a mounted pak, no loose file on disk
pak_marker.loaded = true
function init(self)
    pak_marker.inited = true
    -- the authored data file rides in the SAME pak: this read resolves by
    -- project-relative name through the content mounts, so it never needs a
    -- real file on disk (an APK entry answers the identical call)
    local tuning, err = data.readJson("data/pak_data.json")
    if tuning == nil then
        pak_marker.dataError = err
        return
    end
    pak_marker.dataLevels = tuning.levels
    pak_marker.dataName = tuning.name
    pak_marker.dataFirstRoom = tuning.rooms[1].id
    -- the raw text road is the same read without the decode
    local text, textErr = data.read("data/pak_data.json")
    if text == nil then
        pak_marker.dataError = textErr
        return
    end
    pak_marker.dataBytes = #text
    -- a name outside the project is refused, mounted or not
    local escape = data.read("../pak_data.json")
    pak_marker.escapeRefused = (escape == nil)
end
function update(self, dt)
    pak_marker.updates = (pak_marker.updates or 0) + 1
end
"""

# the authored data file the script reads. Ordinary JSON - the engine reads it
# as CONTENT, never as code.
PAK_DATA = """{
    "name": "pak world",
    "levels": 7,
    "rooms": [{"id": 11}, {"id": 12}]
}
"""


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: make_script_pak_fixture.py <out.pak>\n")
        return 2
    out_pak = argv[1]
    out_dir = os.path.dirname(out_pak)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    # ZIP_STORED: the entry stays uncompressed so it is read in place (the same
    # choice the APK `stored` mode makes for the mount-in-place path)
    with zipfile.ZipFile(out_pak, "w", compression=zipfile.ZIP_STORED) as pak:
        pak.writestr("scripts/pak_script.lua", PAK_SCRIPT)
        pak.writestr("data/pak_data.json", PAK_DATA)

    with zipfile.ZipFile(out_pak) as pak:
        for info in pak.infolist():
            if info.compress_type != zipfile.ZIP_STORED:
                sys.stderr.write("make_script_pak_fixture: %s is not STORED\n"
                                 % info.filename)
                return 1
    sys.stdout.write("make_script_pak_fixture: wrote %s (2 STORED entries)\n"
                     % out_pak)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
