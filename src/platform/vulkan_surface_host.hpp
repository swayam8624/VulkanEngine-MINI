#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace lve {

class VulkanSurfaceHost {
 public:
  virtual ~VulkanSurfaceHost() = default;

  virtual VkExtent2D framebufferExtent() const = 0;
  virtual bool wasResized() const = 0;
  virtual void resetResizedFlag() = 0;
  virtual void waitForEvents() = 0;
  virtual std::vector<const char*> requiredInstanceExtensions() const = 0;
  virtual VkSurfaceKHR createVulkanSurface(VkInstance instance) const = 0;
};

}  // namespace lve
