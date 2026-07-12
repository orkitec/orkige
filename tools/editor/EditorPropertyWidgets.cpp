// EditorPropertyWidgets.cpp - the generic PropertyKind -> ImGui widget renderer
// See EditorPropertyWidgets.h for the contract.
#include "EditorPropertyWidgets.h"
#include "EditorAssetDnd.h"
#include "EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace
{

//! parse exactly count whitespace-separated floats; false on junk / short input
bool parseFloats(std::string const& text, float* out, int count)
{
	std::istringstream stream(text);
	for (int i = 0; i < count; ++i)
	{
		if (!(stream >> out[i]))
		{
			return false;
		}
	}
	return true;
}

//! format count floats space-separated at round-trip precision (the wire form
//! PropertyValue::toString() emits for Vec3/Quat/Color)
std::string formatFloats(const float* values, int count)
{
	std::string result;
	char buffer[64];
	for (int i = 0; i < count; ++i)
	{
		std::snprintf(buffer, sizeof(buffer), "%.9g", values[i]);
		if (i != 0)
		{
			result += ' ';
		}
		result += buffer;
	}
	return result;
}

//! split an enum hint ("label=value,label=value,...") into parallel arrays
void parseEnumOptions(std::string const& hint,
	std::vector<std::string>& labels, std::vector<long long>& values)
{
	std::stringstream stream(hint);
	std::string entry;
	while (std::getline(stream, entry, ','))
	{
		const std::string::size_type equals = entry.find('=');
		if (equals == std::string::npos)
		{
			continue;
		}
		labels.push_back(entry.substr(0, equals));
		values.push_back(
			std::strtoll(entry.c_str() + equals + 1, nullptr, 10));
	}
}

//! @brief persistent edit state for a text/asset field, keyed by its ImGui id.
//! ImGui InputText needs the same backing buffer every frame; we resync it from
//! the streamed value only while the field is NOT being edited (tracked with the
//! public IsItemActive, one frame late - harmless), so typing is never clobbered
//! by the ~15Hz object_state stream underneath.
struct TextFieldState
{
	std::string buffer;
	bool wasActive = false;
};

//! draw an InputText bound to a persistent buffer; true (and outValue set) on
//! Enter. Shared by String and, when no reference provider is set, the reference kinds.
bool drawTextField(char const* label, std::string const& value,
	std::string& outValue)
{
	static std::unordered_map<ImGuiID, TextFieldState> states;
	const ImGuiID id = ImGui::GetID(label);
	TextFieldState& state = states[id];
	if (!state.wasActive)
	{
		state.buffer = value; // idle: follow the streamed value
	}
	char editable[1024];
	std::snprintf(editable, sizeof(editable), "%s", state.buffer.c_str());
	const bool entered = ImGui::InputText(label, editable, sizeof(editable),
		ImGuiInputTextFlags_EnterReturnsTrue);
	state.buffer = editable;
	state.wasActive = ImGui::IsItemActive();
	if (entered)
	{
		outValue = state.buffer;
		return true;
	}
	return false;
}

//! @brief render a Reference-kind property as a searchable combo over the
//! provider's candidates. The current value is always selectable (shown even
//! when the provider does not list it - a stale/missing reference stays
//! visible), plus a "(none)" entry that clears the reference. Returns true and
//! sets outValue when the user picks a different candidate. The picked value is
//! the asset file name / object id the reflected setter consumes; rename safety
//! is handled downstream by the assetId machinery at serialization time.
bool drawRefCombo(char const* label, std::string const& value,
	std::vector<PropertyRefOption> const& options, std::string& outValue)
{
	static std::unordered_map<ImGuiID, std::string> filters;
	const ImGuiID id = ImGui::GetID(label);
	const std::string preview = value.empty() ? "(none)" : value;
	bool edited = false;
	if (ImGui::BeginCombo(label, preview.c_str()))
	{
		std::string& filter = filters[id];
		char buffer[256];
		std::snprintf(buffer, sizeof(buffer), "%s", filter.c_str());
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputTextWithHint("##refsearch", "filter...", buffer,
			sizeof(buffer)))
		{
			filter = buffer;
		}
		ImGui::Separator();
		// the clear entry
		if (ImGui::Selectable("(none)", value.empty()))
		{
			outValue.clear();
			edited = !value.empty();
		}
		// keep the current value visible even if the provider dropped it
		bool currentListed = value.empty();
		for (PropertyRefOption const& option : options)
		{
			if (option.value == value)
			{
				currentListed = true;
				break;
			}
		}
		if (!currentListed)
		{
			ImGui::Selectable(value.c_str(), true);
		}
		for (PropertyRefOption const& option : options)
		{
			// case-insensitive substring filter over the label
			if (!filter.empty())
			{
				std::string haystack = option.label;
				std::string needle = filter;
				for (char& c : haystack) c = static_cast<char>(std::tolower(c));
				for (char& c : needle) c = static_cast<char>(std::tolower(c));
				if (haystack.find(needle) == std::string::npos)
				{
					continue;
				}
			}
			const bool selected = (option.value == value);
			if (ImGui::Selectable(option.label.c_str(), selected))
			{
				if (option.value != value)
				{
					outValue = option.value;
					edited = true;
				}
			}
			if (selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	return edited;
}

} // namespace

bool assetMatchesKind(std::string const& fileName, std::string const& kind)
{
	const std::string::size_type dot = fileName.find_last_of('.');
	if (dot == std::string::npos)
	{
		return kind.empty();
	}
	std::string ext = fileName.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (kind == "texture")
	{
		return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
			ext == "tga" || ext == "dds";
	}
	if (kind == "mesh")
	{
		return ext == "mesh" || ext == "gltf" || ext == "glb" || ext == "obj";
	}
	if (kind == "sound")
	{
		return ext == "wav" || ext == "ogg" || ext == "mp3" || ext == "flac";
	}
	if (kind == "script")
	{
		return ext == "lua";
	}
	if (kind == "prefab")
	{
		return ext == "oprefab";
	}
	if (kind == "material")
	{
		return ext == "omat";
	}
	return true; // an unknown hint: offer everything rather than nothing
}

namespace
{

//! @brief an asset-ref field is a drop target for a single dragged asset: a
//! drop whose file kind matches the property's hint assigns its BARE FILE NAME
//! (the value the reflected setter consumes, exactly what the picker delivers;
//! rename safety comes from the assetId serialized alongside). Call right after
//! the ref widget. Returns true and sets outValue when a matching asset drops.
//! Multi-item drags are ignored - one field takes one reference.
bool acceptAssetRefDrop(std::string const& hint, std::string& outValue)
{
	bool assigned = false;
	if (ImGui::BeginDragDropTarget())
	{
		if (ImGuiPayload const* payload =
			ImGui::AcceptDragDropPayload(ASSET_DND_PAYLOAD))
		{
			if (payload->Data && payload->DataSize ==
				static_cast<int>(sizeof(AssetDragDropPayload)))
			{
				AssetDragDropPayload data;
				std::memcpy(&data, payload->Data, sizeof(data));
				data.path[sizeof(data.path) - 1] = '\0';
				const std::string fileName =
					std::filesystem::path(data.path).filename().string();
				if (assetMatchesKind(fileName, hint))
				{
					outValue = fileName;
					assigned = true;
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	return assigned;
}

} // namespace

bool drawPropertyWidget(PropertyWidgetDesc const& desc,
	std::string const& value, std::string& outValue,
	PropertyRefProvider const& refProvider)
{
	using Orkige::PropertyKind;
	char const* label = desc.label.c_str();
	if (desc.readOnly)
	{
		ImGui::BeginDisabled();
	}
	bool edited = false;
	switch (desc.kind)
	{
	case PropertyKind::Int:
	{
		int integer = std::atoi(value.c_str());
		if (ImGui::DragInt(label, &integer))
		{
			outValue = std::to_string(integer);
			edited = true;
		}
		break;
	}
	case PropertyKind::Float:
	{
		float scalar = static_cast<float>(std::atof(value.c_str()));
		if (ImGui::DragFloat(label, &scalar, 0.05f))
		{
			outValue = formatFloats(&scalar, 1);
			edited = true;
		}
		break;
	}
	case PropertyKind::Bool:
	{
		bool flag = (value == "1");
		if (Orkige::compactCheckbox(label, &flag))
		{
			outValue = flag ? "1" : "0";
			edited = true;
		}
		break;
	}
	case PropertyKind::Vec3:
	{
		float vec[3] = { 0.0f, 0.0f, 0.0f };
		if (parseFloats(value, vec, 3))
		{
			if (ImGui::DragFloat3(label, vec, 0.05f))
			{
				outValue = formatFloats(vec, 3);
				edited = true;
			}
		}
		else
		{
			ImGui::TextDisabled("%s: %s", label, value.c_str());
		}
		break;
	}
	case PropertyKind::Quat:
	{
		float quat[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
		if (parseFloats(value, quat, 4))
		{
			// wxyz drag; the runtime re-normalizes the quaternion on set
			if (ImGui::DragFloat4(label, quat, 0.01f))
			{
				outValue = formatFloats(quat, 4);
				edited = true;
			}
		}
		else
		{
			ImGui::TextDisabled("%s: %s", label, value.c_str());
		}
		break;
	}
	case PropertyKind::Color:
	{
		float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		if (parseFloats(value, color, 4))
		{
			if (ImGui::ColorEdit4(label, color))
			{
				outValue = formatFloats(color, 4);
				edited = true;
			}
		}
		else
		{
			ImGui::TextDisabled("%s: %s", label, value.c_str());
		}
		break;
	}
	case PropertyKind::Enum:
	{
		std::vector<std::string> labels;
		std::vector<long long> values;
		parseEnumOptions(desc.hint, labels, values);
		const long long current = std::strtoll(value.c_str(), nullptr, 10);
		std::string preview = value;
		for (std::size_t i = 0; i < values.size(); ++i)
		{
			if (values[i] == current)
			{
				preview = labels[i];
				break;
			}
		}
		if (ImGui::BeginCombo(label, preview.c_str()))
		{
			for (std::size_t i = 0; i < labels.size(); ++i)
			{
				const bool selected = (values[i] == current);
				if (ImGui::Selectable(labels[i].c_str(), selected))
				{
					outValue = std::to_string(values[i]);
					edited = true;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		break;
	}
	case PropertyKind::AssetRef:
	case PropertyKind::ObjectRef:
	{
		// a picker when a provider yields candidates (the LOCAL inspector);
		// otherwise the free-text field (the remote-safe default path)
		bool drewCombo = false;
		if (refProvider)
		{
			std::vector<PropertyRefOption> options = refProvider(desc);
			if (!options.empty())
			{
				edited = drawRefCombo(label, value, options, outValue);
				drewCombo = true;
			}
		}
		if (!drewCombo)
		{
			edited = drawTextField(label, value, outValue);
		}
		// an ASSET reference field also accepts a dragged asset (the drop
		// assigns the bare file name when its kind matches the hint); object
		// refs have no asset drag source, so only AssetRef wires the target
		if (desc.kind == PropertyKind::AssetRef &&
			acceptAssetRefDrop(desc.hint, outValue))
		{
			edited = true;
		}
		break;
	}
	case PropertyKind::String:
	default:
	{
		edited = drawTextField(label, value, outValue);
		break;
	}
	}
	if (desc.readOnly)
	{
		ImGui::EndDisabled();
		return false; // a disabled widget cannot report a real edit
	}
	return edited;
}
