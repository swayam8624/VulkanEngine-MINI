#pragma once

#include "lve_camera.hpp"
#include "lve_game_object.hpp"

// lib
#include <vulkan/vulkan.h>

// std
#include <cstddef>
#include <type_traits>
#include <vector>

namespace lve {

#define MAX_LIGHTS 10

struct alignas(16) PointLight {
  glm::vec4 position{};  // ignore w
  glm::vec4 color{};     // w is intensity
};

struct alignas(16) SsboPointLight {
  glm::vec4 positionRadius{};  // xyz position, w finite influence radius
  glm::vec4 colorIntensity{};  // rgb color, w intensity
};

struct alignas(16) SsboObjectData {
  glm::mat4 model{1.f};
  glm::mat4 normal{1.f};
};

struct alignas(16) ClusterConfigData {
  glm::vec4 worldMin{};
  glm::vec4 worldMax{};
  glm::uvec4 gridSize{};
  glm::vec4 viewportNearFar{};
  uint32_t clusterCount = 0;
  uint32_t maxLightsPerCluster = 0;
  uint32_t lightIndexCapacity = 0;
  uint32_t flags = 0;
};

struct alignas(16) ClusterHeaderData {
  uint32_t dataOffset = 0;
  uint32_t storedCount = 0;
  uint32_t totalCandidateCount = 0;
  uint32_t flags = 0;
};

struct alignas(16) AdaptiveClusterNodeData {
  glm::vec4 boundsMin{};
  glm::vec4 boundsMax{};
};

struct alignas(16) GlobalUbo {
  glm::mat4 projection{1.f};
  glm::mat4 view{1.f};
  glm::mat4 inverseView{1.f};
  glm::vec4 ambientLightColor{1.f, 1.f, 1.f, .02f};  // w is intensity
  PointLight pointLights[MAX_LIGHTS];
  int numLights;
};

static_assert(std::is_standard_layout<PointLight>::value, "PointLight must remain GLSL ABI-safe");
static_assert(
    std::is_standard_layout<SsboPointLight>::value, "SsboPointLight must remain GLSL ABI-safe");
static_assert(
    std::is_standard_layout<SsboObjectData>::value, "SsboObjectData must remain GLSL ABI-safe");
static_assert(std::is_standard_layout<GlobalUbo>::value, "GlobalUbo must remain GLSL ABI-safe");
static_assert(sizeof(PointLight) == 32, "PointLight must match two std140 vec4 values");
static_assert(sizeof(SsboPointLight) == 32, "SsboPointLight must match two std430 vec4 values");
static_assert(sizeof(SsboObjectData) == 128, "SsboObjectData must match two std430 mat4 values");
static_assert(sizeof(ClusterConfigData) == 80, "ClusterConfigData must match GLSL UBO layout");
static_assert(sizeof(ClusterHeaderData) == 16, "ClusterHeaderData must match GLSL std430 layout");
static_assert(sizeof(AdaptiveClusterNodeData) == 32, "AdaptiveClusterNodeData must match GLSL layout");
static_assert(offsetof(GlobalUbo, projection) == 0, "GlobalUbo projection offset changed");
static_assert(offsetof(GlobalUbo, view) == 64, "GlobalUbo view offset changed");
static_assert(offsetof(GlobalUbo, inverseView) == 128, "GlobalUbo inverse view offset changed");
static_assert(offsetof(GlobalUbo, ambientLightColor) == 192, "GlobalUbo ambient offset changed");
static_assert(offsetof(GlobalUbo, pointLights) == 208, "GlobalUbo light array offset changed");

struct FrameInfo {
  struct InstancedDrawBatch {
    LveGameObject::id_t representativeObjectId{};
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 0;
  };

  int frameIndex;
  float frameTime;
  VkCommandBuffer commandBuffer;
  LveCamera &camera;
  VkDescriptorSet globalDescriptorSet;
  LveGameObject::Map &gameObjects;
  std::vector<InstancedDrawBatch> *instancedDrawBatches = nullptr;
};
}  // namespace lve
