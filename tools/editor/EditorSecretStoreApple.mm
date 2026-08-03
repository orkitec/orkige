/********************************************************************
	created:	Monday 2026/08/03 at 12:00
	filename: 	EditorSecretStoreApple.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorSecretStoreApple.mm - the macOS credential store: a generic-password
// keychain item per project and per slot.
//
// The platform bridge pattern (engine_input/HapticBridgeApple.mm,
// engine_sound/AudioSessionApple.mm): one file, compiled only on Apple, behind
// a portable seam that has a definition on every platform. Everything above it
// - which key, which source wins, what to say when nothing is set - is
// portable C++ in EditorSecretStore.cpp and is unit-tested against a fake.
//
// Security.framework's keychain API is a C API, so nothing here is Objective-C
// beyond the file extension the toolchain wants for a framework this old.
#include "EditorSecretStore.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <cstddef>
#include <string>
#include <vector>

namespace OrkigeEditor
{
	namespace
	{
		//! a CF reference that releases itself, so an early return cannot leak
		template <typename T>
		class ScopedCF
		{
			//--- Variables -------------------------------------
		private:
			T	mReference;

			//--- Methods ---------------------------------------
		public:
			explicit ScopedCF(T reference = 0) : mReference(reference) {}
			~ScopedCF()
			{
				if(this->mReference != 0)
				{
					CFRelease(this->mReference);
				}
			}
			ScopedCF(ScopedCF const &) = delete;
			ScopedCF & operator=(ScopedCF const &) = delete;

			T get() const { return this->mReference; }
			void reset(T reference)
			{
				if(this->mReference != 0)
				{
					CFRelease(this->mReference);
				}
				this->mReference = reference;
			}
		};

		CFStringRef createString(Orkige::String const & text)
		{
			return CFStringCreateWithBytes(kCFAllocatorDefault,
				reinterpret_cast<const UInt8 *>(text.data()),
				static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8,
				false);
		}

		Orkige::String toString(CFStringRef text)
		{
			if(text == 0)
			{
				return Orkige::String();
			}
			const CFIndex length = CFStringGetLength(text);
			const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length,
				kCFStringEncodingUTF8) + 1;
			std::vector<char> buffer(static_cast<std::size_t>(capacity), '\0');
			if(!CFStringGetCString(text, buffer.data(), capacity,
				kCFStringEncodingUTF8))
			{
				return Orkige::String();
			}
			return Orkige::String(buffer.data());
		}

		//! the platform's own words for a failure, so a person can look the
		//! message up rather than meet a number
		Orkige::String describe(OSStatus status)
		{
			ScopedCF<CFStringRef> message(SecCopyErrorMessageString(status, 0));
			const Orkige::String text = toString(message.get());
			if(!text.empty())
			{
				return text;
			}
			return "keychain error " + std::to_string(status);
		}

		//! the {generic password, our service, this account} identity every
		//! call starts from
		CFMutableDictionaryRef createQuery(Orkige::String const & account)
		{
			CFMutableDictionaryRef query = CFDictionaryCreateMutable(
				kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
				&kCFTypeDictionaryValueCallBacks);
			if(query == 0)
			{
				return 0;
			}
			ScopedCF<CFStringRef> service(createString(secretVaultService()));
			ScopedCF<CFStringRef> name(createString(account));
			CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
			CFDictionarySetValue(query, kSecAttrService, service.get());
			CFDictionarySetValue(query, kSecAttrAccount, name.get());
			return query;
		}

		SecretResult failure(OSStatus status)
		{
			SecretResult result;
			result.status = SecretStatus::Failed;
			result.message = describe(status);
			return result;
		}

		//! the macOS keychain, through the Security framework
		class KeychainVault : public SecretVault
		{
			//--- Methods ---------------------------------------
		public:
			Orkige::String name() const override { return "Keychain"; }

			SecretResult read(Orkige::String const & account) const override
			{
				SecretResult result;
				ScopedCF<CFMutableDictionaryRef> query(createQuery(account));
				if(query.get() == 0)
				{
					result.message = "could not build the keychain query";
					result.status = SecretStatus::Failed;
					return result;
				}
				CFDictionarySetValue(query.get(), kSecReturnData,
					kCFBooleanTrue);
				CFDictionarySetValue(query.get(), kSecMatchLimit,
					kSecMatchLimitOne);
				CFTypeRef found = 0;
				const OSStatus status = SecItemCopyMatching(query.get(),
					&found);
				ScopedCF<CFTypeRef> owned(found);
				if(status == errSecItemNotFound)
				{
					result.status = SecretStatus::Missing;
					return result;
				}
				if(status != errSecSuccess)
				{
					return failure(status);
				}
				if(found == 0 || CFGetTypeID(found) != CFDataGetTypeID())
				{
					result.status = SecretStatus::Missing;
					return result;
				}
				CFDataRef data = static_cast<CFDataRef>(found);
				const UInt8 * bytes = CFDataGetBytePtr(data);
				const CFIndex length = CFDataGetLength(data);
				result.value.assign(reinterpret_cast<const char *>(bytes),
					static_cast<std::size_t>(length));
				result.status = SecretStatus::Ok;
				return result;
			}

			SecretResult write(Orkige::String const & account,
				Orkige::String const & secret) override
			{
				SecretResult result;
				ScopedCF<CFDataRef> data(CFDataCreate(kCFAllocatorDefault,
					reinterpret_cast<const UInt8 *>(secret.data()),
					static_cast<CFIndex>(secret.size())));
				ScopedCF<CFMutableDictionaryRef> query(createQuery(account));
				if(query.get() == 0 || data.get() == 0)
				{
					result.message = "could not build the keychain query";
					result.status = SecretStatus::Failed;
					return result;
				}
				// replace first: a person who re-types a password expects the
				// new one to be what signs, not a second item beside the old
				ScopedCF<CFMutableDictionaryRef> attributes(
					CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
						&kCFTypeDictionaryKeyCallBacks,
						&kCFTypeDictionaryValueCallBacks));
				if(attributes.get() == 0)
				{
					result.message = "could not build the keychain update";
					result.status = SecretStatus::Failed;
					return result;
				}
				CFDictionarySetValue(attributes.get(), kSecValueData,
					data.get());
				OSStatus status = SecItemUpdate(query.get(), attributes.get());
				if(status == errSecItemNotFound)
				{
					// the label is what a person sees in the system's own
					// credential UI, which is where they go to revoke it
					const Orkige::String label = "Orkige signing credential (" +
						account + ")";
					ScopedCF<CFStringRef> title(createString(label));
					CFDictionarySetValue(query.get(), kSecAttrLabel,
						title.get());
					CFDictionarySetValue(query.get(), kSecValueData,
						data.get());
					status = SecItemAdd(query.get(), 0);
				}
				if(status != errSecSuccess)
				{
					return failure(status);
				}
				result.status = SecretStatus::Ok;
				return result;
			}

			SecretResult erase(Orkige::String const & account) override
			{
				SecretResult result;
				ScopedCF<CFMutableDictionaryRef> query(createQuery(account));
				if(query.get() == 0)
				{
					result.message = "could not build the keychain query";
					result.status = SecretStatus::Failed;
					return result;
				}
				const OSStatus status = SecItemDelete(query.get());
				if(status != errSecSuccess && status != errSecItemNotFound)
				{
					return failure(status);
				}
				// removing what is not there is what the caller asked for
				result.status = SecretStatus::Ok;
				return result;
			}
		};
	}
	//---------------------------------------------------------
	SecretVault * platformSecretVault()
	{
		// constructed on first use and never destroyed: it owns no resource,
		// and a vault outliving the last consumer costs nothing
		static KeychainVault vault;
		return &vault;
	}
}
