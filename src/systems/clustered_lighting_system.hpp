#pragma once

#include "lve_device.hpp"
#include "lve_frame_info.hpp"
#include "beacon/gpu_profiler.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace lve {

struct ClusterBuildPushConstants {
  uint32_t clusterCount = 0;
  uint32_t lightCount = 0;
  uint32_t maxLightsPerCluster = 0;
  uint32_t lightIndexCapacity = 0;
  glm::vec4 worldMin{};
  glm::vec4 worldMax{};
  glm::uvec4 gridSize{};
  glm::vec4 viewportNearFar{};
};

class ClusteredLightingSystem {
 public:
  ClusteredLightingSystem(LveDevice& device, VkDescriptorSetLayout globalSetLayout);
  ~ClusteredLightingSystem();

  ClusteredLightingSystem(const ClusteredLightingSystem&) = delete;
  ClusteredLightingSystem& operator=(const ClusteredLightingSystem&) = delete;

  void dispatch(
      VkCommandBuffer commandBuffer,
      VkDescriptorSet globalDescriptorSet,
      const ClusterBuildPushConstants& push,
      VkBuffer headerBuffer,
      VkBuffer cursorBuffer,
      VkBuffer blockSumBuffer,
      beacon::GpuProfiler* profiler = nullptr);

 private:
  static std::vector<char> readFile(const std::string& filepath);
  void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
  void createPipeline();
  void createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);

  LveDevice& lveDevice;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  std::vector<VkPipeline> pipelines;
  std::vector<VkShaderModule> shaderModules;
};

}  // namespace lve
