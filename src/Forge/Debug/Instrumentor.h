#pragma once

#include <string>
#include <chrono>
#include <fstream>
#include <thread>
#include <mutex>

namespace Forge {

	class Instrumentor
	{
	public:
		Instrumentor()
			: m_CurrentSession(nullptr), m_ProfileCount(0)
		{
		}

		void BeginSession(const std::string& name, const std::string& filepath = "results.json")
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_CurrentSession)
				Flush();

			m_CurrentSession = new std::ofstream(filepath);
			*m_CurrentSession << "{\"Header\":{\"Name\":\"Forge\", \"Description\":\"Forge Engine Profiler Results\", \"Timestamp\":\"" << GetTimestamp() << "\"},";
			m_CurrentSession << "\"Results\":[";
			m_ProfileCount = 0;
		}

		void EndSession()
		{
			Flush();
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_CurrentSession)
			{
				*m_CurrentSession << "]}";
				m_CurrentSession->close();
				delete m_CurrentSession;
				m_CurrentSession = nullptr;
			}
		}

		void WriteProfile(const std::string& name, const std::string& category, float start, float end, std::thread::id threadID)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_CurrentSession)
			{
				if (m_ProfileCount > 0)
					*m_CurrentSession << ",";

				*m_CurrentSession << "{\"Name\":\"" << name << "\",\"Category\":\"" << category << "\",\"Thread\":" << threadID << ",\"Start\":" << std::chrono::duration<double, std::nano>(start).count() << ",\"End\":" << std::chrono::duration<double, std::nano>(end).count() << "}";
				m_ProfileCount++;
			}
		}

	private:
		std::string GetTimestamp()
		{
			auto now = std::chrono::system_clock::now();
			auto time_t_now = std::chrono::system_clock::to_time_t(now);
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

			std::stringstream ss;
			ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%dT%H:%M:%S");
			ss << "." << std::setfill('0') << std::setw(3) << ms.count();
			return ss.str();
		}

		void Flush()
		{
			if (m_CurrentSession)
			{
				*m_CurrentSession << "]}";
				m_CurrentSession->close();
			}
		}

	private:
		std::ofstream* m_CurrentSession;
		uint32_t m_ProfileCount;
		std::mutex m_Mutex;
	};

	Instrumentor& GetInstrumentor()
	{
		static Instrumentor instrumentor;
		return instrumentor;
	}

}

#define FG_PROFILE_FUNCTION() \
	FG_PROFILE_FUNCTION(__FUNCTION__)

#define FG_PROFILE_FUNCTION(name) \
	do { \
		Forge::Instrumentor& instrumentor = Forge::GetInstrumentor(); \
		std::string funcName = name; \
		auto start = std::chrono::high_resolution_clock::now(); \
		std::thread::id threadID = std::this_thread::get_id(); \
		(void)threadID; \
		do { } while (0); \
		auto end = std::chrono::high_resolution_clock::now(); \
		instrumentor.WriteProfile(funcName, "Function", start, end, threadID); \
	} while (0)

#define FG_PROFILE_BEGIN_SESSION(name, filepath) \
	Forge::GetInstrumentor().BeginSession(name, filepath)

#define FG_PROFILE_END_SESSION() \
	Forge::GetInstrumentor().EndSession()
