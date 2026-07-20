#!/usr/bin/env python3
# Generates the binary seed corpus files that are awkward to keep as static
# committed samples: valid zip archives for fuzz_minizip and a couple of
# JSON-RPC replies for fuzz_json. The text-parser corpora (.omat/.oanim/
# .oshape) are copied real repo assets and live in git directly.
#
# Re-run after changing what a valid archive looks like; the outputs ARE
# committed so CI/local fuzzing needs no python. Stdlib only (project policy).
import json
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
CORPUS = os.path.join(HERE, "corpus")


def write_minizip_seeds():
    out = os.path.join(CORPUS, "fuzz_minizip")
    os.makedirs(out, exist_ok=True)

    # a small mixed archive: STORED + DEFLATE + a nested path (mirrors the
    # MiniZipReadTest fixture so the seed starts from real structure)
    with zipfile.ZipFile(os.path.join(out, "mixed.zip"), "w") as z:
        z.writestr("stored.txt", b"stored payload contents 12345",
                   zipfile.ZIP_STORED)
        z.writestr("deflate.txt", b"x" * 4096, zipfile.ZIP_DEFLATED)
        z.writestr("sub/nested.bin", bytes(range(256)), zipfile.ZIP_DEFLATED)

    # the smallest well-formed archive: one empty stored entry
    with zipfile.ZipFile(os.path.join(out, "tiny.zip"), "w") as z:
        z.writestr("a", b"", zipfile.ZIP_STORED)

    # a directory entry (trailing '/') plus a file, so the directory-skip path
    # is seeded
    with zipfile.ZipFile(os.path.join(out, "withdir.zip"), "w") as z:
        z.writestr("dir/", b"")
        z.writestr("dir/file.txt", b"hello", zipfile.ZIP_DEFLATED)


def write_json_seeds():
    out = os.path.join(CORPUS, "fuzz_json")
    os.makedirs(out, exist_ok=True)

    # a JSON-RPC 2.0 tools/call reply shaped like the MCP endpoint's own
    # traffic (nested content array + structuredContent object)
    reply = {
        "jsonrpc": "2.0",
        "id": 7,
        "result": {
            "content": [{"type": "text", "text": "ok"}],
            "structuredContent": {
                "count": 3,
                "names": ["a", "b", "c"],
                "nested": {"flag": True, "ratio": 0.25, "empty": None},
            },
            "isError": False,
        },
    }
    with open(os.path.join(out, "rpc_reply.json"), "w") as f:
        f.write(json.dumps(reply))

    # scalars + escapes + a surrogate pair, exercising the string reader
    with open(os.path.join(out, "escapes.json"), "w") as f:
        f.write(json.dumps({"s": "line\n\ttab \"q\" \\ é \U0001f600"}))

    with open(os.path.join(out, "array.json"), "w") as f:
        f.write("[1,2.5,-3e10,true,false,null,\"x\",{},[]]")


def main():
    write_minizip_seeds()
    write_json_seeds()
    print("seed corpus written under", CORPUS)


if __name__ == "__main__":
    main()
