#include "lve_window.hpp"

// std
#include <stdexcept>

namespace lve {

LveWindow::LveWindow(int w, int h, std::string name) : width{w}, height{h}, windowName{name} {
  initWindow();
}

LveWindow::~LveWindow() {
  if (window != nullptr) {
    glfwDestroyWindow(window);
  }
  glfwTerminate();
}

void LveWindow::initWindow() {
  if (glfwInit() != GLFW_TRUE) {
    throw std::runtime_error("failed to initialize GLFW");
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  window = glfwCreateWindow(width, height, windowName.c_str(), nullptr, nullptr);
  if (window == nullptr) {
    glfwTerminate();
    throw std::runtime_error("failed to create GLFW window");
  }
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void LveWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) {
  *surface = createVulkanSurface(instance);
}

void LveWindow::waitForEvents() {
  glfwWaitEvents();
}

std::vector<const char*> LveWindow::requiredInstanceExtensions() const {
  uint32_t extensionCount = 0;
  const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
  if (extensions == nullptr || extensionCount == 0) {
    throw std::runtime_error("GLFW did not report required Vulkan instance extensions");
  }
  return {extensions, extensions + extensionCount};
}

VkSurfaceKHR LveWindow::createVulkanSurface(VkInstance instance) const {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
    throw std::runtime_error("failed to create GLFW Vulkan surface");
  }
  return surface;
}

void LveWindow::framebufferResizeCallback(GLFWwindow *window, int width, int height) {
  auto lveWindow = reinterpret_cast<LveWindow *>(glfwGetWindowUserPointer(window));
  lveWindow->framebufferResized = true;
  lveWindow->width = width;
  lveWindow->height = height;
}

}  // namespace lve
