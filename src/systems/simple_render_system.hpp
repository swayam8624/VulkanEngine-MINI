#pragma once

#include "beacon/benchmark_config.hpp"
#include "lve_camera.hpp"
#include "lve_device.hpp"
#include "lve_frame_info.hpp"
#include "lve_game_object.hpp"
#include "lve_pipeline.hpp"

// std
#include <memory>
#include <vector>

namespace lve {
class SimpleRenderSystem {
 public:
  SimpleRenderSystem(
      LveDevice &device,
      VkRenderPass renderPass,
      VkDescriptorSetLayout globalSetLayout,
      beacon::RenderTechnique technique = beacon::RenderTechnique::BaselineForward);
  ~SimpleRenderSystem();

  SimpleRenderSystem(const SimpleRenderSystem &) = delete;
  SimpleRenderSystem &operator=(const SimpleRenderSystem &) = delete;

  void renderGameObjects(FrameInfo &frameInfo);
  void begin(FrameInfo &frameInfo);
  void renderModel(
      FrameInfo &frameInfo,
      LveModel &model,
      const glm::mat4 &modelMatrix = glm::mat4{1.f},
      const glm::mat4 &normalMatrix = glm::mat4{1.f});

 private:
  void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
  void createPipeline(VkRenderPass renderPass);

  LveDevice &lveDevice;
  beacon::RenderTechnique technique;

  std::unique_ptr<LvePipeline> lvePipeline;
  VkPipelineLayout pipelineLayout;
};
}  // namespace lve
