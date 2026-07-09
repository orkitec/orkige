# MCP server — AI-agent editor control (WP #80)

`Util/orkige_mcp.py` is a [Model Context Protocol](https://modelcontextprotocol.io)
server that lets an AI agent (Claude Code, etc.) drive a running Orkige editor:
open projects, edit and save scenes, inspect and mutate the GameObject
hierarchy, read and write component properties, control play mode, take
screenshots and list assets.

## Register

```sh
claude mcp add orkige -- python3 Util/orkige_mcp.py
```

## Architecture

```
   Claude ──stdio(MCP)──▶ Util/orkige_mcp.py ──loopback TCP(line-JSON)──▶ editor control port
                                                                          (EditorControlServer)
```

The editor opens the control port only on request (OFF by default), so no normal
run or test is affected. It is a SECOND `core_debugnet` `DebugServer` — but in the
EDITOR process, not the player — reusing the tested debug transport wholesale
(one flat JSON object per line, `\n`-terminated, 64 KiB cap, loopback only). The
handler (`tools/editor/EditorControlServer.cpp`) is a thin adapter over
`EditorCore` (deliberately UI-independent) and the `EditorDocument` free
functions; ~all verbs map onto existing editor operations.

Two additive conventions ride on the transport:

- **request/response correlation** — a request may carry a `req` id; the reply
  (`ok` / `err`) echoes it, so replies match requests even when unsolicited
  notifications interleave.
- **auth token** — with a token file the editor writes a 128-bit secret there
  (`<port>\n<token>\n`); the host reads it and presents it in a `hello`. Mutation
  verbs are refused until authenticated. A control port started WITHOUT a token
  file has auth OFF (a hand-started dev port; a loopback reader is harmless).

Play control (`play`/`stop`/`pause`/`resume`/`step`) is translated into the ONE
existing player debug protocol — MCP is an editor-side bridge, never a second
player port.

## Launch the editor with a control port

```sh
# explicit port + published token (the MCP host reads both from the token file)
orkige_editor --control-port 9010 --control-token-file /tmp/orkige.token

# env equivalents
ORKIGE_CONTROL_PORT=9010 ORKIGE_CONTROL_TOKEN_FILE=/tmp/orkige.token orkige_editor
```

## Bridge configuration (environment)

| Variable                     | Meaning                                                     |
|------------------------------|-------------------------------------------------------------|
| `ORKIGE_CONTROL_TOKEN_FILE`  | the editor's `--control-token-file` (holds port + token). Preferred. |
| `ORKIGE_CONTROL_HOST`        | control host (default `127.0.0.1`)                          |
| `ORKIGE_CONTROL_PORT`        | control port (when no token file, or it has no port line)  |
| `ORKIGE_CONTROL_TOKEN`       | auth token (when no token file)                            |
| `ORKIGE_EDITOR_BIN`          | if set and no editor is reachable, spawn this editor with a fresh control port and connect |

## Tools

| Tool | Maps to |
|------|---------|
| `get_state` | project/scene/dirty/selection/play-mode snapshot |
| `open_project(path)` | `openProjectFromPath` (dirty-state policy) |
| `new_scene(force)` / `open_scene(path, force)` / `save_scene(path)` | `newScene` / `openSceneFromPath` / `saveSceneToPath` |
| `list_hierarchy()` | `GameObjectManager::getGameObjects` (+ parent/active) |
| `get_component(id, component)` | the six typed bundles `EditorCore` exposes |
| `set_component(id, component, properties)` | the typed undoable setters (`applyTransformChange`, `changeObjectMesh`, `changeObjectScript`, `applyRigidBodyChange`, `applyCameraChange`, `applySpriteChange`) |
| `create_object(id, mesh, position)` | `CreateObjectCommand` |
| `delete_object(id)` | `DeleteObjectCommand` |
| `reparent_object(id, parent)` | `EditorCore::reparentObject` |
| `add_component(id, component)` / `remove_component(id, component)` | `addComponentToObject` / `removeComponentFromObject` |
| `play()` / `stop()` | `startPlay` / `requestStopPlay` (translated to the player protocol) |
| `screenshot(path, window)` | `RenderTexture::writeContentsToFile` (chrome-free viewport) / `RenderSystem::saveWindowContents` (whole window) → returns the written path |
| `list_assets()` | `AssetDatabase::listAssets` + `Project::listScenes` |
| `console_tail(count)` | the editor `EditorConsole` line store |

The six typed component bundles (v1 — no generic reflection):
`TransformComponent` (position/orientation/scale, space-separated floats;
orientation is `w x y z`), `ModelComponent` (mesh), `ScriptComponent`
(script/enabled), `RigidBodyComponent` (body_type/shape_type/mass/friction/
restitution/planar/radius/half_height/half_extents), `CameraComponent`
(projection_mode/ortho_size), `SpriteComponent` (texture/width/height/tint/
flip_x/flip_y/z_order/visible).

## Dirty-state policy

Destructive verbs (`new_scene`, `open_scene`, `open_project`, `new_project`,
`close_project`) refuse to clobber an unsaved scene: they return an `err` unless
the scene is clean or the request carries `force=1`. Automated runs never touch
the user's recents (the editor's `gRecordRecents`/`automatedRun` suppression).

## Tests

- `editor_control` (ctest, integration): the editor starts its control server on
  an ephemeral in-process port and drives its OWN `DebugClient` through the whole
  flow — hello (auth), list_hierarchy, create_object (+ verify it exists in the
  live scene), get/set the Transform bundle (mutation + correlation), and a
  screenshot to a temp path (+ verify the file was written). Proves the C++
  bridge with no Python needed.
- `mcp_bridge_selftest` (ctest, unit): `python3 Util/orkige_mcp.py --selftest`
  stands up a stub control server on a loopback socket (stdlib only — no editor,
  no `mcp` SDK) and drives the bridge's framing, request/response correlation
  (including out-of-order and decoy replies) and auth (wrong token rejected).
