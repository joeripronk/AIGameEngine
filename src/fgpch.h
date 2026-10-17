#pragma once

#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <cstring>
#include <cstdint>
#include <cfloat>
#include <cmath>

// This engine renders through Vulkan, so GLFW's OpenGL includes are never used.
// GLFW_INCLUDE_NONE stops glfw3.h from pulling in <GL/gl.h>, which would
// otherwise make every translation unit depend on OpenGL headers that nothing
// here reads - and fail to compile on a machine that has a Vulkan SDK but no
// GL development package, with an error naming a file no source includes.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <entt/entt.hpp>
