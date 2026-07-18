#pragma once

#include "lve_buffer.hpp"
#include "lve_device.hpp"

#include <memory>
#include <filesystem>

namespace lve::beacon {

struct ImageComparisonMetrics {
  double mse = 0.0;
  double psnr = 99.0;
  double ssim = 1.0;
  double maximumPixelError = 0.0;
};

class OffscreenComparison {
 public:
  static constexpr uint32_t TARGET_REFERENCE = 0;
  static constexpr uint32_t TARGET_TEST = 1;

  OffscreenComparison(LveDevice& device, uint32_t width, uint32_t height);
  ~OffscreenComparison();

  OffscreenComparison(const OffscreenComparison&) = delete;
  OffscreenComparison& operator=(const OffscreenComparison&) = delete;

  VkRenderPass getRenderPass() const { return renderPass; }

  void begin(VkCommandBuffer commandBuffer, uint32_t target);
  void end(VkCommandBuffer commandBuffer);
  void copyTargetsToBuffers(VkCommandBuffer commandBuffer);
  ImageComparisonMetrics compare();
  void writePpm(uint32_t target, const std::filesystem::path& path) const;

 private:
  struct Target {
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    std::unique_ptr<LveBuffer> staging;
  };

  void createRenderPass();
  void createTarget(Target& target);
  void createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect, VkImageView& view);

  LveDevice& device;
  uint32_t width = 1;
  uint32_t height = 1;
  VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  Target targets[2];
};

}  // namespace lve::beacon
