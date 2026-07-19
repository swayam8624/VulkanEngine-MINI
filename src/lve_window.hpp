#pragma once

#include "platform/vulkan_surface_host.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>
#include <vector>
namespace lve {

class LveWindow final : public VulkanSurfaceHost {
 public:
  LveWindow(int w, int h, std::string name);
  ~LveWindow();

  LveWindow(const LveWindow &) = delete;
  LveWindow &operator=(const LveWindow &) = delete;

  bool shouldClose() { return glfwWindowShouldClose(window); }
  VkExtent2D getExtent() const { return framebufferExtent(); }
  VkExtent2D framebufferExtent() const override {
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  }
  bool wasWindowResized() const { return wasResized(); }
  bool wasResized() const override { return framebufferResized; }
  void resetResizedFlag() override { framebufferResized = false; }
  void resetWindowResizedFlag() { resetResizedFlag(); }
  void waitForEvents() override;
  std::vector<const char*> requiredInstanceExtensions() const override;
  VkSurfaceKHR createVulkanSurface(VkInstance instance) const override;
  GLFWwindow *getGLFWwindow() const { return window; }

  // Retained for source compatibility with existing engine callers.
  void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface);

 private:
  static void framebufferResizeCallback(GLFWwindow *window, int width, int height);
  void initWindow();

  int width;
  int height;
  bool framebufferResized = false;

  std::string windowName;
  GLFWwindow *window = nullptr;
};
}  // namespace lve
