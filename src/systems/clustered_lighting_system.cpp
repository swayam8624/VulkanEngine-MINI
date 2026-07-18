#include "systems/clustered_lighting_system.hpp"

#include <cassert>
#include <fstream>
#include <stdexcept>

#ifndef ENGINE_DIR
#define ENGINE_DIR "../"
#endif

namespace lve {

ClusteredLightingSystem::ClusteredLightingSystem(
    LveDevice& device, VkDescriptorSetLayout globalSetLayout)
    : lveDevice{device} {
  createPipelineLayout(globalSetLayout);
  createPipeline();
}

ClusteredLightingSystem::~ClusteredLightingSystem() {
  for (auto shaderModule : shaderModules) {
    vkDestroyShaderModule(lveDevice.device(), shaderModule, nullptr);
  }
  for (auto pipeline : pipelines) {
    vkDestroyPipeline(lveDevice.device(), pipeline, nullptr);
  }
  vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
}

std::vector<char> ClusteredLightingSystem::readFile(const std::string& filepath) {
  std::string enginePath = ENGINE_DIR + filepath;
  std::ifstream file{enginePath, std::ios::ate | std::ios::binary};
  if (!file.is_open()) {
    throw std::runtime_error("failed to open file: " + enginePath);
  }

  size_t fileSize = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  return buffer;
}

void ClusteredLightingSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(ClusterBuildPushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &globalSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  if (vkCreatePipelineLayout(lveDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create clustered compute pipeline layout!");
  }
}

void ClusteredLightingSystem::createPipeline() {
  assert(pipelineLayout != VK_NULL_HANDLE && "compute pipeline layout must be created first");
  const std::vector<std::string> shaderPaths{
      "shaders/cluster_count.comp.spv",
      "shaders/cluster_scan.comp.spv",
      "shaders/cluster_scan_sums.comp.spv",
      "shaders/cluster_add_offsets.comp.spv",
      "shaders/cluster_scatter.comp.spv"};
  pipelines.reserve(shaderPaths.size());
  shaderModules.reserve(shaderPaths.size());
  for (const auto& path : shaderPaths) {
    VkShaderModule module = VK_NULL_HANDLE;
    createShaderModule(readFile(path), &module);
    shaderModules.push_back(module);

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = module;
    shaderStage.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(
            lveDevice.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
      throw std::runtime_error("failed to create clustered compute pipeline: " + path);
    }
    pipelines.push_back(pipeline);
  }
}

void ClusteredLightingSystem::createShaderModule(
    const std::vector<char>& code, VkShaderModule* outShaderModule) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

  if (vkCreateShaderModule(lveDevice.device(), &createInfo, nullptr, outShaderModule) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create clustered compute shader module");
  }
}

void ClusteredLightingSystem::dispatch(
    VkCommandBuffer commandBuffer,
    VkDescriptorSet globalDescriptorSet,
    const ClusterBuildPushConstants& push,
    VkBuffer headerBuffer,
    VkBuffer cursorBuffer,
    VkBuffer blockSumBuffer,
    beacon::GpuProfiler* profiler) {
  if (push.clusterCount == 0) return;

  vkCmdBindDescriptorSets(
      commandBuffer,
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipelineLayout,
      0,
      1,
      &globalDescriptorSet,
      0,
      nullptr);
  vkCmdPushConstants(
      commandBuffer,
      pipelineLayout,
      VK_SHADER_STAGE_COMPUTE_BIT,
      0,
      sizeof(ClusterBuildPushConstants),
      &push);
  vkCmdFillBuffer(commandBuffer, headerBuffer, 0, VK_WHOLE_SIZE, 0);
  vkCmdFillBuffer(commandBuffer, cursorBuffer, 0, VK_WHOLE_SIZE, 0);
  vkCmdFillBuffer(commandBuffer, blockSumBuffer, 0, VK_WHOLE_SIZE, 0);

  auto barrier = [&](VkPipelineStageFlags source, VkPipelineStageFlags destination) {
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(
        commandBuffer, source, destination, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
  };
  barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[0]);
  vkCmdDispatch(commandBuffer, (push.lightCount + 63u) / 64u, 1, 1);
  barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  if (profiler != nullptr) profiler->writeTimestamp(commandBuffer, "cluster_count_end");

  uint32_t blockCount = (push.clusterCount + 255u) / 256u;
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[1]);
  vkCmdDispatch(commandBuffer, blockCount, 1, 1);
  barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  if (profiler != nullptr) profiler->writeTimestamp(commandBuffer, "cluster_local_scan_end");

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[2]);
  vkCmdDispatch(commandBuffer, 1, 1, 1);
  barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  if (profiler != nullptr) profiler->writeTimestamp(commandBuffer, "cluster_block_scan_end");

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[3]);
  vkCmdDispatch(commandBuffer, blockCount, 1, 1);
  barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  if (profiler != nullptr) profiler->writeTimestamp(commandBuffer, "cluster_offsets_end");

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[4]);
  vkCmdDispatch(commandBuffer, (push.lightCount + 63u) / 64u, 1, 1);
  barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  if (profiler != nullptr) profiler->writeTimestamp(commandBuffer, "cluster_scatter_end");
}

}  // namespace lve
