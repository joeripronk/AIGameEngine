#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace Forge {

	class UUID
	{
	public:
		UUID();
		UUID(uint64_t uuid);

		operator uint64_t() const { return m_UUID; }

		std::string ToString() const;
	private:
		uint64_t m_UUID;
	};

}

// The specialisation is declared inside namespace std with the enclosing
// namespace named explicitly (std::), not by relying on unqualified lookup.
//
// The previous form re-declared the primary template itself:
//
//     namespace std { template <typename T> struct hash; }
//
// libc++ defines std::hash inside an inline namespace (__1), so by the time this
// header is parsed that declaration is already visible and the unqualified name
// "hash" matches two different entities - std::hash and std::__1::hash - making
// the specialisation ambiguous:
//
//     UUID.h:27:9: error: reference to 'hash' is ambiguous
//     note: candidate found by name lookup is 'std::hash'
//     note: candidate found by name lookup is 'std::__1::hash'
//
// Including <functional> does not help; that is already the case here. The
// re-declaration is also unnecessary, since the primary template is declared by
// the standard header this file now includes. libstdc++ resolved the name
// differently, which is why this only ever failed on macOS.
namespace std {
	template<>
	struct hash<Forge::UUID>
	{
		std::size_t operator()(const Forge::UUID& uuid) const
		{
			return static_cast<uint64_t>(uuid);
		}
	};

}
