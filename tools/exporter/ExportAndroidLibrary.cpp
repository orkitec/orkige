/********************************************************************
	created:	Tuesday 2026/08/04 at 10:00
	filename: 	ExportAndroidLibrary.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportAndroidLibrary.h"

#include "ExportFiles.h"
#include "ExportZip.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace OrkigeExport
{
	namespace
	{
		bool report(Orkige::String * error, Orkige::String const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
		//---------------------------------------------------------
		void emit(ExportLog const & log, Orkige::String const & message)
		{
			if(log)
			{
				log(message);
			}
		}
		//---------------------------------------------------------
		bool beginsWith(Orkige::String const & text, Orkige::String const & head)
		{
			return text.size() >= head.size() &&
				text.compare(0, head.size(), head) == 0;
		}
		//---------------------------------------------------------
		bool endsWith(Orkige::String const & text, Orkige::String const & tail)
		{
			return text.size() >= tail.size() &&
				text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
		}
		//---------------------------------------------------------
		//! @p text with every `from` replaced by `to`
		Orkige::String replaceAll(Orkige::String const & text,
			Orkige::String const & from, Orkige::String const & to)
		{
			if(from.empty())
			{
				return text;
			}
			Orkige::String out;
			std::size_t begin = 0;
			for(;;)
			{
				const std::size_t found = text.find(from, begin);
				if(found == Orkige::String::npos)
				{
					out += text.substr(begin);
					return out;
				}
				out += text.substr(begin, found - begin);
				out += to;
				begin = found + from.size();
			}
		}

		//--- the manifest subset -----------------------------------

		//! the `<manifest>` children the merge carries over, de-duplicated by
		//! `android:name`. Anything else is refused by name.
		bool isMergedTopLevel(Orkige::String const & element)
		{
			return element == "uses-permission" ||
				element == "uses-permission-sdk-23" ||
				element == "permission" ||
				element == "permission-group" ||
				element == "permission-tree" ||
				element == "uses-feature";
		}
		//---------------------------------------------------------
		//! the `<application>` children the merge carries over
		bool isMergedApplicationChild(Orkige::String const & element)
		{
			return element == "activity" || element == "activity-alias" ||
				element == "service" || element == "receiver" ||
				element == "provider" || element == "meta-data" ||
				element == "uses-library" ||
				element == "uses-native-library" ||
				element == "property";
		}
		//---------------------------------------------------------
		//! an element's merge IDENTITY: what makes two declarations "the same
		//! thing" for de-duplication. `android:name` for everything that has
		//! one; a graphics feature is named by its version instead.
		Orkige::String mergeIdentity(tinyxml2::XMLElement const & element)
		{
			char const * const name = element.Attribute("android:name");
			if(name != 0)
			{
				return Orkige::String(element.Name()) + " " + name;
			}
			char const * const gl = element.Attribute("android:glEsVersion");
			if(gl != 0)
			{
				return Orkige::String(element.Name()) + " glEsVersion " + gl;
			}
			return Orkige::String();
		}
		//---------------------------------------------------------
		//! @p element serialized, so two declarations of the same identity can
		//! be compared for being the SAME declaration
		Orkige::String printed(tinyxml2::XMLElement const & element)
		{
			tinyxml2::XMLPrinter printer(0, true);
			element.Accept(&printer);
			return Orkige::String(printer.CStr());
		}
		//---------------------------------------------------------
		//! the first `${...}` placeholder in @p text that is not
		//! `${applicationId}`, or "" - the ones this merge cannot resolve
		Orkige::String unresolvedPlaceholder(Orkige::String const & text)
		{
			const std::size_t open = text.find("${");
			if(open == Orkige::String::npos)
			{
				return Orkige::String();
			}
			const std::size_t close = text.find('}', open);
			if(close == Orkige::String::npos)
			{
				return Orkige::String();
			}
			return text.substr(open, close - open + 1);
		}
		//---------------------------------------------------------
		//! the first attribute of @p element (or anything under it) in the
		//! build-tool `tools:` namespace, or "" - the merge-directive
		//! vocabulary this merge does not implement
		Orkige::String toolsAttribute(tinyxml2::XMLElement const & element)
		{
			for(tinyxml2::XMLAttribute const * attribute =
					element.FirstAttribute();
				attribute != 0; attribute = attribute->Next())
			{
				if(beginsWith(attribute->Name(), "tools:"))
				{
					return attribute->Name();
				}
			}
			for(tinyxml2::XMLElement const * child = element.FirstChildElement();
				child != 0; child = child->NextSiblingElement())
			{
				const Orkige::String found = toolsAttribute(*child);
				if(!found.empty())
				{
					return found;
				}
			}
			return Orkige::String();
		}
		//---------------------------------------------------------
		//! the integer an `android:*SdkVersion` attribute carries, or 0
		int sdkAttribute(tinyxml2::XMLElement const & element,
			char const * name)
		{
			char const * const value = element.Attribute(name);
			if(value == 0)
			{
				return 0;
			}
			int api = 0;
			for(char const * scan = value; *scan != 0; ++scan)
			{
				if(std::isdigit(static_cast<unsigned char>(*scan)) == 0)
				{
					return 0;
				}
				api = api * 10 + static_cast<int>(*scan - '0');
			}
			return api;
		}
		//---------------------------------------------------------
		//! deep-copy @p source into @p document, so it can be inserted under a
		//! parent of that document
		tinyxml2::XMLElement * cloned(tinyxml2::XMLDocument & document,
			tinyxml2::XMLElement const & source)
		{
			tinyxml2::XMLNode * const copy = source.DeepClone(&document);
			return (copy == 0) ? 0 : copy->ToElement();
		}

		//--- the merge state ---------------------------------------

		//! what has already been merged, so a second identical declaration is
		//! dropped and a second DIFFERING one is refused
		class MergeIndex
		{
		public:
			//! record @p element under @p source. Returns false with an
			//! @p error when something with the same identity is already there
			//! and says something else.
			bool add(tinyxml2::XMLElement const & element,
				Orkige::String const & source, bool & isNew,
				Orkige::String * error)
			{
				isNew = false;
				const Orkige::String identity = mergeIdentity(element);
				if(identity.empty())
				{
					// nothing to key on: keep it, and let the platform's own
					// duplicate rules apply
					isNew = true;
					return true;
				}
				const Orkige::String text = printed(element);
				for(Record const & record : this->mRecords)
				{
					if(record.identity != identity)
					{
						continue;
					}
					if(record.text == text)
					{
						return true;	// the same declaration twice
					}
					return report(error, "the Android library '" + source +
						"' declares <" + element.Name() + " android:name=\"" +
						identity.substr(identity.find(' ') + 1) + "\"> "
						"differently from " + (record.source.empty()
							? Orkige::String("the app's own manifest")
							: "'" + record.source + "'") + " - resolve the "
						"conflict in the app manifest template, or drop one of "
						"the libraries (see Docs/android-libraries.md)");
				}
				Record record;
				record.identity = identity;
				record.text = text;
				record.source = source;
				this->mRecords.push_back(record);
				isNew = true;
				return true;
			}

		private:
			struct Record
			{
				Orkige::String identity;
				Orkige::String text;
				Orkige::String source;	//!< "" = the app's own manifest
			};
			std::vector<Record> mRecords;
		};
		//---------------------------------------------------------
		//! index everything the APP manifest already declares, so a library
		//! contribution that contradicts it is caught rather than appended
		bool indexExisting(tinyxml2::XMLElement const & manifest,
			MergeIndex & index, Orkige::String * error)
		{
			bool isNew = false;
			for(tinyxml2::XMLElement const * child =
					manifest.FirstChildElement();
				child != 0; child = child->NextSiblingElement())
			{
				const Orkige::String element = child->Name();
				if(element == "application")
				{
					for(tinyxml2::XMLElement const * inner =
							child->FirstChildElement();
						inner != 0; inner = inner->NextSiblingElement())
					{
						if(!index.add(*inner, "", isNew, error))
						{
							return false;
						}
					}
					continue;
				}
				if(!index.add(*child, "", isNew, error))
				{
					return false;
				}
			}
			return true;
		}
		//---------------------------------------------------------
		//! merge the `<queries>` children of a fragment into the app's own
		//! `<queries>` element, creating it when the app declares none
		bool mergeQueries(tinyxml2::XMLDocument & document,
			tinyxml2::XMLElement & manifest,
			tinyxml2::XMLElement const & fragmentQueries,
			Orkige::String const & source, int & merged,
			Orkige::String * error)
		{
			tinyxml2::XMLElement * target = manifest.FirstChildElement("queries");
			if(target == 0)
			{
				target = document.NewElement("queries");
				manifest.InsertEndChild(target);
			}
			for(tinyxml2::XMLElement const * child =
					fragmentQueries.FirstChildElement();
				child != 0; child = child->NextSiblingElement())
			{
				const Orkige::String text = printed(*child);
				bool already = false;
				for(tinyxml2::XMLElement const * existing =
						target->FirstChildElement();
					existing != 0; existing = existing->NextSiblingElement())
				{
					if(printed(*existing) == text)
					{
						already = true;
						break;
					}
				}
				if(already)
				{
					continue;
				}
				tinyxml2::XMLElement * const copy = cloned(document, *child);
				if(copy == 0)
				{
					return report(error, "could not merge a <queries> entry "
						"from the Android library '" + source + "'");
				}
				target->InsertEndChild(copy);
				++merged;
			}
			return true;
		}
		//---------------------------------------------------------
		//! carry a fragment's `<application>` children into the app's
		bool mergeApplication(tinyxml2::XMLDocument & document,
			tinyxml2::XMLElement & application,
			tinyxml2::XMLElement const & fragmentApplication,
			Orkige::String const & source, MergeIndex & index, int & merged,
			Orkige::String * error)
		{
			// a library that sets attributes ON the application element is
			// telling the app what its own theme, backup policy or Application
			// class is. That is a decision the project owns, so it is named
			// rather than either applied or dropped.
			if(fragmentApplication.FirstAttribute() != 0)
			{
				return report(error, "the Android library '" + source + "' sets "
					"<application " +
					fragmentApplication.FirstAttribute()->Name() + "=\"" +
					fragmentApplication.FirstAttribute()->Value() + "\">, which "
					"only the project's own manifest decides - this export "
					"merges an application's CHILDREN, not its attributes (see "
					"Docs/android-libraries.md)");
			}
			for(tinyxml2::XMLElement const * child =
					fragmentApplication.FirstChildElement();
				child != 0; child = child->NextSiblingElement())
			{
				const Orkige::String element = child->Name();
				if(!isMergedApplicationChild(element))
				{
					return report(error, "the Android library '" + source +
						"' declares <application><" + element + ">, which this "
						"export does not merge - it would be dropped, and a "
						"dropped declaration is an app that installs and then "
						"misbehaves (see Docs/android-libraries.md)");
				}
				bool isNew = false;
				if(!index.add(*child, source, isNew, error))
				{
					return false;
				}
				if(!isNew)
				{
					continue;
				}
				tinyxml2::XMLElement * const copy = cloned(document, *child);
				if(copy == 0)
				{
					return report(error, "could not merge <" + element + "> "
						"from the Android library '" + source + "'");
				}
				application.InsertEndChild(copy);
				++merged;
			}
			return true;
		}
		//---------------------------------------------------------
		//! merge ONE fragment into the app manifest document
		bool mergeFragment(tinyxml2::XMLDocument & document,
			tinyxml2::XMLElement & manifest,
			AndroidManifestFragment const & fragment,
			Orkige::String const & applicationId, int minimumApi,
			MergeIndex & index, std::vector<Orkige::String> * notes,
			Orkige::String * error)
		{
			// the ONE placeholder this merge resolves - the app's own package,
			// which library authorities and permissions are written against
			const Orkige::String substituted =
				replaceAll(fragment.text, "${applicationId}", applicationId);
			const Orkige::String placeholder =
				unresolvedPlaceholder(substituted);
			if(!placeholder.empty())
			{
				return report(error, "the Android library '" + fragment.source +
					"' has the unresolved manifest placeholder " + placeholder +
					" - this export substitutes ${applicationId} and nothing "
					"else (see Docs/android-libraries.md)");
			}
			tinyxml2::XMLDocument parsed;
			if(parsed.Parse(substituted.c_str()) != tinyxml2::XML_SUCCESS)
			{
				return report(error, "the Android library '" + fragment.source +
					"' has an unreadable AndroidManifest.xml: " +
					(parsed.ErrorStr() != 0 ? parsed.ErrorStr() : "parse error"));
			}
			tinyxml2::XMLElement const * const root =
				parsed.FirstChildElement("manifest");
			if(root == 0)
			{
				return report(error, "the Android library '" + fragment.source +
					"' has an AndroidManifest.xml with no <manifest> element");
			}
			const Orkige::String tools = toolsAttribute(*root);
			if(!tools.empty())
			{
				return report(error, "the Android library '" + fragment.source +
					"' uses the manifest merge directive '" + tools + "', which "
					"this export does not implement - applying it wrongly and "
					"ignoring it are both worse than saying so (see "
					"Docs/android-libraries.md)");
			}
			int merged = 0;
			for(tinyxml2::XMLElement const * child = root->FirstChildElement();
				child != 0; child = child->NextSiblingElement())
			{
				const Orkige::String element = child->Name();
				if(element == "uses-sdk")
				{
					const int libraryMinimum =
						sdkAttribute(*child, "android:minSdkVersion");
					if(libraryMinimum > minimumApi)
					{
						return report(error, "the Android library '" +
							fragment.source + "' needs Android API " +
							std::to_string(libraryMinimum) + " and this package "
							"declares API " + std::to_string(minimumApi) + " as "
							"its minimum - the app would install on devices the "
							"library cannot run on");
					}
					continue;
				}
				if(element == "queries")
				{
					if(!mergeQueries(document, manifest, *child,
						fragment.source, merged, error))
					{
						return false;
					}
					continue;
				}
				if(element == "application")
				{
					tinyxml2::XMLElement * const application =
						manifest.FirstChildElement("application");
					if(application == 0)
					{
						return report(error, "the app manifest has no "
							"<application> for the Android library '" +
							fragment.source + "' to merge into");
					}
					if(!mergeApplication(document, *application, *child,
						fragment.source, index, merged, error))
					{
						return false;
					}
					continue;
				}
				if(!isMergedTopLevel(element))
				{
					return report(error, "the Android library '" +
						fragment.source + "' declares <" + element + ">, which "
						"this export does not merge - it would be dropped, and a "
						"dropped declaration is an app that installs and then "
						"misbehaves (see Docs/android-libraries.md)");
				}
				bool isNew = false;
				if(element == "uses-feature")
				{
					// the platform's own rule: a feature two manifests disagree
					// about ends up REQUIRED, so a device without it never
					// installs an app half of which cannot run
					tinyxml2::XMLElement * existing = 0;
					const Orkige::String identity = mergeIdentity(*child);
					for(tinyxml2::XMLElement * scan =
							manifest.FirstChildElement("uses-feature");
						scan != 0;
						scan = scan->NextSiblingElement("uses-feature"))
					{
						if(mergeIdentity(*scan) == identity)
						{
							existing = scan;
							break;
						}
					}
					if(existing != 0)
					{
						const bool required =
							child->BoolAttribute("android:required", true);
						if(required && !existing->BoolAttribute(
							"android:required", true))
						{
							existing->SetAttribute("android:required", "true");
							if(notes != 0)
							{
								notes->push_back(fragment.source + ": " +
									identity + " becomes required");
							}
						}
						continue;
					}
				}
				if(!index.add(*child, fragment.source, isNew, error))
				{
					return false;
				}
				if(!isNew)
				{
					continue;
				}
				tinyxml2::XMLElement * const copy = cloned(document, *child);
				if(copy == 0)
				{
					return report(error, "could not merge <" + element + "> "
						"from the Android library '" + fragment.source + "'");
				}
				// in front of <application>, where a manifest conventionally
				// carries its permissions and features
				tinyxml2::XMLElement * const application =
					manifest.FirstChildElement("application");
				tinyxml2::XMLNode * const before = (application == 0)
					? 0 : application->PreviousSibling();
				if(application == 0)
				{
					manifest.InsertEndChild(copy);
				}
				else if(before == 0)
				{
					manifest.InsertFirstChild(copy);
				}
				else
				{
					manifest.InsertAfterChild(before, copy);
				}
				++merged;
			}
			if(notes != 0)
			{
				notes->push_back(fragment.source + ": " +
					std::to_string(merged) + " manifest declarations merged");
			}
			return true;
		}
	}
	//---------------------------------------------------------
	AndroidLibraryEntry androidLibraryEntry(Orkige::String const & entryName)
	{
		AndroidLibraryEntry routed;
		if(entryName.empty() ||
			entryName[entryName.size() - 1] == '/')
		{
			return routed;			// a directory entry carries nothing
		}
		if(entryName == "AndroidManifest.xml")
		{
			routed.kind = AndroidLibraryEntry::MANIFEST;
			routed.relative = entryName;
			return routed;
		}
		if(entryName == "classes.jar" ||
			(beginsWith(entryName, "libs/") && endsWith(entryName, ".jar")))
		{
			// the entry's OWN path, not its file name: two jars an archive
			// bundles under different directories may share a name, and
			// flattening them would drop one without a word
			routed.kind = AndroidLibraryEntry::JAR;
			routed.relative = entryName;
			return routed;
		}
		if(entryName == "R.txt")
		{
			routed.kind = AndroidLibraryEntry::SYMBOLS;
			routed.relative = entryName;
			return routed;
		}
		if(beginsWith(entryName, "res/") && entryName.size() > 4)
		{
			routed.kind = AndroidLibraryEntry::RESOURCE;
			routed.relative = entryName.substr(4);
			return routed;
		}
		if(beginsWith(entryName, "assets/") && entryName.size() > 7)
		{
			routed.kind = AndroidLibraryEntry::ASSET;
			routed.relative = entryName.substr(7);
			return routed;
		}
		if(beginsWith(entryName, "jni/"))
		{
			const Orkige::String tail = entryName.substr(4);
			const std::size_t slash = tail.find('/');
			if(slash != Orkige::String::npos && slash + 1 < tail.size())
			{
				routed.kind = AndroidLibraryEntry::NATIVE;
				routed.abi = tail.substr(0, slash);
				routed.relative = tail.substr(slash + 1);
				return routed;
			}
		}
		// everything else - the lint jar, the shrinker rules, the annotation
		// archive, the public-resource list, the signature block, the native
		// build headers - has no consumer in this assembly, so it is not
		// dropped from one
		return routed;
	}
	//---------------------------------------------------------
	Orkige::String androidClasspath(std::vector<Orkige::String> const & paths)
	{
#if defined(_WIN32)
		const char separator = ';';
#else
		const char separator = ':';
#endif
		Orkige::String joined;
		for(std::size_t index = 0; index < paths.size(); ++index)
		{
			if(index != 0)
			{
				joined += separator;
			}
			joined += paths[index];
		}
		return joined;
	}
	//---------------------------------------------------------
	Orkige::String androidManifestPackage(Orkige::String const & manifestText)
	{
		tinyxml2::XMLDocument document;
		if(document.Parse(manifestText.c_str()) != tinyxml2::XML_SUCCESS)
		{
			return Orkige::String();
		}
		tinyxml2::XMLElement const * const root =
			document.FirstChildElement("manifest");
		if(root == 0)
		{
			return Orkige::String();
		}
		char const * const package = root->Attribute("package");
		return (package == 0) ? Orkige::String() : Orkige::String(package);
	}
	//---------------------------------------------------------
	bool androidMergeManifest(Orkige::String const & appManifest,
		std::vector<AndroidManifestFragment> const & fragments,
		Orkige::String const & applicationId, int minimumApi,
		Orkige::String & out, std::vector<Orkige::String> * notes,
		Orkige::String * error)
	{
		if(fragments.empty())
		{
			// nothing to merge is not a rewrite: a project that depends on no
			// library packages exactly the manifest it always did, byte for
			// byte, which is what keeps this addition invisible where it is
			// unused
			out = appManifest;
			return true;
		}
		tinyxml2::XMLDocument document;
		if(document.Parse(appManifest.c_str()) != tinyxml2::XML_SUCCESS)
		{
			return report(error, "the app manifest is unreadable: " +
				Orkige::String(document.ErrorStr() != 0 ? document.ErrorStr()
					: "parse error"));
		}
		tinyxml2::XMLElement * const manifest =
			document.FirstChildElement("manifest");
		if(manifest == 0)
		{
			return report(error, "the app manifest has no <manifest> element");
		}
		MergeIndex index;
		if(!indexExisting(*manifest, index, error))
		{
			return false;
		}
		for(AndroidManifestFragment const & fragment : fragments)
		{
			if(!mergeFragment(document, *manifest, fragment, applicationId,
				minimumApi, index, notes, error))
			{
				return false;
			}
		}
		tinyxml2::XMLPrinter printer;
		document.Print(&printer);
		out = printer.CStr();
		return true;
	}
	//---------------------------------------------------------
	bool unpackAndroidLibrary(Orkige::String const & archivePath,
		Orkige::String const & workDirectory, Orkige::String const & abi,
		AndroidLibrary & out, ExportLog const & log, Orkige::String * error)
	{
		out = AndroidLibrary();
		out.path = archivePath;
		out.name = ExportFiles::fileName(archivePath);
		if(!ExportFiles::isRegularFile(archivePath))
		{
			return report(error, "no Android library archive at '" +
				archivePath + "' (export.android.libraries)");
		}
		ExportZipReader archive;
		if(!archive.open(archivePath, error))
		{
			return false;
		}
		if(!ExportFiles::removeTree(workDirectory, error) ||
			!ExportFiles::makeDirectories(workDirectory, error))
		{
			return false;
		}
		const Orkige::String resRoot = ExportFiles::join(workDirectory, "res");
		const Orkige::String assetRoot =
			ExportFiles::join(workDirectory, "assets");
		const Orkige::String jarRoot = ExportFiles::join(workDirectory, "jars");
		const Orkige::String jniRoot = ExportFiles::join(workDirectory, "jni");
		std::vector<Orkige::String> otherAbis;
		int resources = 0;
		int assets = 0;
		for(ExportZipReader::Entry const & entry : archive.entries())
		{
			const AndroidLibraryEntry routed =
				androidLibraryEntry(entry.name);
			if(routed.kind == AndroidLibraryEntry::IGNORED)
			{
				continue;
			}
			if(routed.kind == AndroidLibraryEntry::NATIVE &&
				routed.abi != abi)
			{
				if(std::find(otherAbis.begin(), otherAbis.end(), routed.abi) ==
					otherAbis.end())
				{
					otherAbis.push_back(routed.abi);
				}
				continue;
			}
			std::vector<unsigned char> bytes;
			if(!archive.read(entry.name, bytes, error))
			{
				return false;
			}
			if(routed.kind == AndroidLibraryEntry::MANIFEST)
			{
				out.manifestText.assign(
					reinterpret_cast<char const *>(bytes.data()), bytes.size());
				out.packageName = androidManifestPackage(out.manifestText);
				continue;
			}
			if(routed.kind == AndroidLibraryEntry::SYMBOLS)
			{
				// a non-empty symbol list means the library's own code resolves
				// resource ids through a generated R class - which only the app
				// can produce, because the ids are assigned when the whole
				// resource table is linked
				out.generatesResourceIds = !bytes.empty();
				continue;
			}
			Orkige::String destination;
			switch(routed.kind)
			{
			case AndroidLibraryEntry::JAR:
				destination = ExportFiles::join(jarRoot, routed.relative);
				break;
			case AndroidLibraryEntry::RESOURCE:
				destination = ExportFiles::join(resRoot, routed.relative);
				++resources;
				break;
			case AndroidLibraryEntry::ASSET:
				destination = ExportFiles::join(assetRoot, routed.relative);
				++assets;
				break;
			case AndroidLibraryEntry::NATIVE:
				destination = ExportFiles::join(jniRoot, routed.relative);
				break;
			default:
				continue;
			}
			if(!ExportFiles::writeBytes(destination, bytes, error))
			{
				return false;
			}
			if(routed.kind == AndroidLibraryEntry::JAR)
			{
				out.jars.push_back(destination);
			}
			else if(routed.kind == AndroidLibraryEntry::NATIVE)
			{
				out.nativeLibraries.push_back(
					std::make_pair(routed.relative, destination));
			}
		}
		if(out.manifestText.empty())
		{
			return report(error, "'" + out.name + "' carries no "
				"AndroidManifest.xml - an Android library archive always does, "
				"so this is not one");
		}
		if(out.nativeLibraries.empty() && !otherAbis.empty())
		{
			Orkige::String carried;
			for(std::size_t index = 0; index < otherAbis.size(); ++index)
			{
				carried += (index == 0 ? "" : ", ") + otherAbis[index];
			}
			return report(error, "'" + out.name + "' carries native code for " +
				carried + " but not for " + abi + " - the app would install and "
				"then fail when the library loads its own library");
		}
		if(resources > 0)
		{
			out.resDirectory = resRoot;
		}
		if(assets > 0)
		{
			out.assetsDirectory = assetRoot;
		}
		emit(log, "library " + out.name + ": " +
			std::to_string(out.jars.size()) + " jar(s), " +
			std::to_string(resources) + " resource(s), " +
			std::to_string(assets) + " asset(s), " +
			std::to_string(out.nativeLibraries.size()) + " native lib(s)" +
			(out.packageName.empty() ? "" : " [" + out.packageName + "]"));
		return true;
	}
}
