#include "fgpch.h"
#include "Forge/Core/UUID.h"

#include <random>

namespace Forge {

	static std::mt19937_64& GetRandomGenerator()
	{
		static std::mt19937_64 generator(std::random_device{}());
		return generator;
	}

	UUID::UUID()
		: m_UUID(GetRandomGenerator()())
	{
	}

	UUID::UUID(uint64_t uuid)
		: m_UUID(uuid)
	{
	}

	std::string UUID::ToString() const
	{
		return std::to_string(m_UUID);
	}

	[[maybe_unused]] static UUID GenerateUUID()
	{
		return UUID(GetRandomGenerator()());
	}

}
