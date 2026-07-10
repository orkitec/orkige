// EditorAssetDnd.h - the cross-panel asset drag & drop vocabulary.
//
// The Asset browser is a drag SOURCE; the Scene viewport, the Hierarchy, a
// folder tile/tree node/breadcrumb (a MOVE target) and an Inspector asset-ref
// field are drag TARGETS. This header holds the shared payload types and tags
// so every one of those - INCLUDING the generic EditorPropertyWidgets layer,
// which is deliberately ImGui-only and does NOT include EditorApp.h - speaks
// the same protocol. Two payload types travel: a single-item struct (kind +
// absolute path) and, when a multi-selection is dragged, a newline-joined list
// of absolute paths (ImGui carries one payload type per drag, so the source
// sets exactly one of them by selection size).
#ifndef __EditorAssetDnd_h__10_7_2026__00_00_00__
#define __EditorAssetDnd_h__10_7_2026__00_00_00__

//! kind of a project asset, classified purely by file extension - drives the
//! browser's type label and how a drop instantiates into the scene
enum class AssetKind
{
	Unknown,
	Mesh,		//!< .glb/.gltf/.obj/.fbx/... (CreateObjectCommand)
	Texture,	//!< .png/.jpg/... (CreateSpriteObjectCommand)
	Script,		//!< .lua (not instantiable on its own)
	Scene,		//!< .oscene (double-click / drop opens it)
	Prefab,		//!< .oprefab (CreatePrefabInstanceCommand)
	Audio,		//!< .wav/.ogg/... (added to an object as a SoundComponent)
	VectorShape	//!< .oshape (CreateVectorShapeObjectCommand)
};

//! the single-item drag-drop payload bytes: kind + absolute path, fixed size so
//! ImGui copies it by value into its internal payload buffer
struct AssetDragDropPayload
{
	AssetKind kind = AssetKind::Unknown;
	char path[1024] = "";
};

//! ImGui drag-drop payload tag the Asset browser sets for a SINGLE dragged item
//! / the Scene + Hierarchy + inspector-ref targets accept
extern const char* const ASSET_DND_PAYLOAD;			//!< "ORKIGE_ASSET"

//! ImGui drag-drop payload tag for a MULTI-item drag: the payload bytes are the
//! '\n'-joined absolute paths of the dragged selection (variable size). The
//! MOVE targets (folder tile/tree node/breadcrumb) and the Scene/Hierarchy add
//! target accept it; a single asset-ref field does not (one field, one ref).
extern const char* const ASSET_DND_PAYLOAD_MULTI;	//!< "ORKIGE_ASSET_MULTI"

#endif // __EditorAssetDnd_h__10_7_2026__00_00_00__
