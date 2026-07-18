#include "simple_render_system.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// std
#include <array>
#include <cassert>
#include <stdexcept>

namespace lve {

struct SimplePushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

SimpleRenderSystem::SimpleRenderSystem(
    LveDevice& device,
    VkRenderPass renderPass,
    VkDescriptorSetLayout globalSetLayout,
    beacon::RenderTechnique renderTechnique)
    : lveDevice{device}, technique{renderTechnique} {
  createPipelineLayout(globalSetLayout);
  createPipeline(renderPass);
}

SimpleRenderSystem::~SimpleRenderSystem() {
  vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
}

void SimpleRenderSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(SimplePushConstantData);

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{globalSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  if (vkCreatePipelineLayout(lveDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void SimpleRenderSystem::createPipeline(VkRenderPass renderPass) {
  assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

  PipelineConfigInfo pipelineConfig{};
  LvePipeline::defaultPipelineConfigInfo(pipelineConfig);
  pipelineConfig.renderPass = renderPass;
  pipelineConfig.pipelineLayout = pipelineLayout;
  lvePipeline = std::make_unique<LvePipeline>(
      lveDevice,
      technique == beacon::RenderTechnique::InstancedForward
          ? "shaders/simple_shader_instanced.vert.spv"
          : "shaders/simple_shader.vert.spv",
      technique == beacon::RenderTechnique::AdaptiveClusteredExact ||
              technique == beacon::RenderTechnique::AdaptiveClusteredBounded ||
              technique == beacon::RenderTechnique::BeaconFull
          ? "shaders/simple_shader_adaptive.frag.spv"
          : technique == beacon::RenderTechnique::GpuClusteredFixed
          ? "shaders/simple_shader_clustered.frag.spv"
          : technique == beacon::RenderTechnique::SsboForward ||
              technique == beacon::RenderTechnique::SsboDiffuseReference ||
              technique == beacon::RenderTechnique::InstancedForward
          ? (technique == beacon::RenderTechnique::SsboDiffuseReference
                 ? "shaders/simple_shader_ssbo_diffuse.frag.spv"
                 : "shaders/simple_shader_ssbo.frag.spv")
          : "shaders/simple_shader.frag.spv",
      pipelineConfig);
}

void SimpleRenderSystem::renderGameObjects(FrameInfo& frameInfo) {
  begin(frameInfo);

  if (technique == beacon::RenderTechnique::InstancedForward &&
      frameInfo.instancedDrawBatches != nullptr) {
    for (const auto& batch : *frameInfo.instancedDrawBatches) {
      auto& obj = frameInfo.gameObjects.at(batch.representativeObjectId);
      if (obj.model == nullptr || batch.instanceCount == 0) continue;
      obj.model->bind(frameInfo.commandBuffer);
      obj.model->draw(frameInfo.commandBuffer, batch.instanceCount, batch.firstInstance);
    }
    return;
  }

  for (auto& kv : frameInfo.gameObjects) {
    auto& obj = kv.second;
    if (obj.model == nullptr) continue;
    renderModel(
        frameInfo,
        *obj.model,
        obj.transform.mat4(),
        glm::mat4{obj.transform.normalMatrix()});
  }
}

void SimpleRenderSystem::begin(FrameInfo& frameInfo) {
  lvePipeline->bind(frameInfo.commandBuffer);
  vkCmdBindDescriptorSets(
      frameInfo.commandBuffer,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipelineLayout,
      0,
      1,
      &frameInfo.globalDescriptorSet,
      0,
      nullptr);
}

void SimpleRenderSystem::renderModel(
    FrameInfo& frameInfo,
    LveModel& model,
    const glm::mat4& modelMatrix,
    const glm::mat4& normalMatrix) {
  SimplePushConstantData push{};
  push.modelMatrix = modelMatrix;
  push.normalMatrix = normalMatrix;
  vkCmdPushConstants(
      frameInfo.commandBuffer,
      pipelineLayout,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      0,
      sizeof(SimplePushConstantData),
      &push);
  model.bind(frameInfo.commandBuffer);
  model.draw(frameInfo.commandBuffer);
}

}  // namespace lve
