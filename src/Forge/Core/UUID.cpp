#include "fgpch.h"
#include "Forge/Core/UUID.h"

#include <random>

namespace Forge {

	UUID::UUID()
		: m_UUID(0)
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

	static std::mt19937_64& GetRandomGenerator()
	{
		static std::mt19937_64 generator(std::random_device{}());
		return generator;
	}

	static UUID GenerateUUID()
	{
		return UUID(GetRandomGenerator()());
	}

	UUID::UUID(const UUID&) = default;

}
