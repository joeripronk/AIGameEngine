#pragma once

#include <cstdint>
#include <string>

namespace Forge {

	class UUID
	{
	public:
		UUID();
		UUID(uint64_t uuid);
		UUID(const UUID&) = default;

		operator uint64_t() const { return m_UUID; }

		std::string ToString() const;
	private:
		uint64_t m_UUID;
	};

}

namespace std {
	template <typename T> struct hash;

	template<>
	struct hash<Forge::UUID>
	{
		std::size_t operator()(const Forge::UUID& uuid) const
		{
			return (uint64_t)uuid;
		}
	};

}
