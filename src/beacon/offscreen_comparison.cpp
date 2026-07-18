#include "beacon/offscreen_comparison.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace lve::beacon {
namespace {

double luminance(const unsigned char* p) {
  return (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) / 255.0;
}

}  // namespace

OffscreenComparison::OffscreenComparison(LveDevice& device, uint32_t width, uint32_t height)
    : device{device}, width{std::max(1u, width)}, height{std::max(1u, height)} {
  depthFormat = device.findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  createRenderPass();
  createTarget(targets[TARGET_REFERENCE]);
  createTarget(targets[TARGET_TEST]);
}

OffscreenComparison::~OffscreenComparison() {
  for (auto& target : targets) {
    target.staging.reset();
    vkDestroyFramebuffer(device.device(), target.framebuffer, nullptr);
    vkDestroyImageView(device.device(), target.depthView, nullptr);
    vkDestroyImage(device.device(), target.depthImage, nullptr);
    vkFreeMemory(device.device(), target.depthMemory, nullptr);
    vkDestroyImageView(device.device(), target.colorView, nullptr);
    vkDestroyImage(device.device(), target.colorImage, nullptr);
    vkFreeMemory(device.device(), target.colorMemory, nullptr);
  }
  vkDestroyRenderPass(device.device(), renderPass, nullptr);
}

void OffscreenComparison::createRenderPass() {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = colorFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = depthFormat;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthRef{};
  depthRef.attachment = 1;
  depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;
  subpass.pDepthStencilAttachment = &depthRef;

  std::array<VkSubpassDependency, 2> dependencies{};
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  dependencies[0].dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependencies[0].dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

  std::array<VkAttachmentDescription, 2> attachments{colorAttachment, depthAttachment};
  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
  renderPassInfo.pDependencies = dependencies.data();

  if (vkCreateRenderPass(device.device(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
    throw std::runtime_error("failed to create offscreen comparison render pass");
  }
}

void OffscreenComparison::createImageView(
    VkImage image, VkFormat format, VkImageAspectFlags aspect, VkImageView& view) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspect;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device.device(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
    throw std::runtime_error("failed to create offscreen image view");
  }
}

void OffscreenComparison::createTarget(Target& target) {
  VkImageCreateInfo colorInfo{};
  colorInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  colorInfo.imageType = VK_IMAGE_TYPE_2D;
  colorInfo.extent = {width, height, 1};
  colorInfo.mipLevels = 1;
  colorInfo.arrayLayers = 1;
  colorInfo.format = colorFormat;
  colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  colorInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  colorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  device.createImageWithInfo(
      colorInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, target.colorImage, target.colorMemory);
  createImageView(target.colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, target.colorView);

  VkImageCreateInfo depthInfo = colorInfo;
  depthInfo.format = depthFormat;
  depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  device.createImageWithInfo(
      depthInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, target.depthImage, target.depthMemory);
  createImageView(target.depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, target.depthView);

  std::array<VkImageView, 2> attachments{target.colorView, target.depthView};
  VkFramebufferCreateInfo framebufferInfo{};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = renderPass;
  framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  framebufferInfo.pAttachments = attachments.data();
  framebufferInfo.width = width;
  framebufferInfo.height = height;
  framebufferInfo.layers = 1;
  if (vkCreateFramebuffer(device.device(), &framebufferInfo, nullptr, &target.framebuffer) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create offscreen framebuffer");
  }

  target.staging = std::make_unique<LveBuffer>(
      device,
      1,
      width * height * 4,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  target.staging->map();
}

void OffscreenComparison::begin(VkCommandBuffer commandBuffer, uint32_t targetIndex) {
  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = renderPass;
  renderPassInfo.framebuffer = targets[targetIndex].framebuffer;
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = {width, height};

  std::array<VkClearValue, 2> clearValues{};
  clearValues[0].color = {0.01f, 0.01f, 0.01f, 1.0f};
  clearValues[1].depthStencil = {1.0f, 0};
  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport{};
  viewport.width = static_cast<float>(width);
  viewport.height = static_cast<float>(height);
  viewport.minDepth = 0.f;
  viewport.maxDepth = 1.f;
  VkRect2D scissor{{0, 0}, {width, height}};
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void OffscreenComparison::end(VkCommandBuffer commandBuffer) {
  vkCmdEndRenderPass(commandBuffer);
}

void OffscreenComparison::copyTargetsToBuffers(VkCommandBuffer commandBuffer) {
  for (auto& target : targets) {
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(
        commandBuffer,
        target.colorImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        target.staging->getBuffer(),
        1,
        &region);
  }
}

ImageComparisonMetrics OffscreenComparison::compare() {
  const auto* a = static_cast<const unsigned char*>(targets[TARGET_REFERENCE].staging->getMappedMemory());
  const auto* b = static_cast<const unsigned char*>(targets[TARGET_TEST].staging->getMappedMemory());
  size_t pixelCount = static_cast<size_t>(width) * height;
  if (pixelCount == 0 || a == nullptr || b == nullptr) {
    return {};
  }

  double mse = 0.0;
  double maximumPixelError = 0.0;
  double meanA = 0.0;
  double meanB = 0.0;
  for (size_t i = 0; i < pixelCount; ++i) {
    const unsigned char* pa = a + i * 4;
    const unsigned char* pb = b + i * 4;
    for (int c = 0; c < 3; ++c) {
      double d = (static_cast<double>(pa[c]) - pb[c]) / 255.0;
      mse += d * d;
      maximumPixelError = std::max(maximumPixelError, std::abs(d));
    }
    meanA += luminance(pa);
    meanB += luminance(pb);
  }
  mse /= static_cast<double>(pixelCount * 3);
  meanA /= static_cast<double>(pixelCount);
  meanB /= static_cast<double>(pixelCount);

  double varianceA = 0.0;
  double varianceB = 0.0;
  double covariance = 0.0;
  for (size_t i = 0; i < pixelCount; ++i) {
    double la = luminance(a + i * 4);
    double lb = luminance(b + i * 4);
    varianceA += (la - meanA) * (la - meanA);
    varianceB += (lb - meanB) * (lb - meanB);
    covariance += (la - meanA) * (lb - meanB);
  }
  double denom = std::max(1.0, static_cast<double>(pixelCount - 1));
  varianceA /= denom;
  varianceB /= denom;
  covariance /= denom;

  constexpr double c1 = 0.01 * 0.01;
  constexpr double c2 = 0.03 * 0.03;
  ImageComparisonMetrics metrics{};
  metrics.mse = mse;
  metrics.psnr = mse <= 0.0 ? 99.0 : 10.0 * std::log10(1.0 / mse);
  metrics.ssim = ((2.0 * meanA * meanB + c1) * (2.0 * covariance + c2)) /
                 ((meanA * meanA + meanB * meanB + c1) * (varianceA + varianceB + c2));
  metrics.maximumPixelError = maximumPixelError;
  return metrics;
}

void OffscreenComparison::writePpm(uint32_t targetIndex, const std::filesystem::path& path) const {
  if (targetIndex > TARGET_TEST) throw std::runtime_error("invalid offscreen target");
  const auto* pixels =
      static_cast<const unsigned char*>(targets[targetIndex].staging->getMappedMemory());
  if (pixels == nullptr) throw std::runtime_error("offscreen target is not mapped");
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output{path, std::ios::binary};
  if (!output) throw std::runtime_error("failed to write offscreen capture: " + path.string());
  output << "P6\n" << width << " " << height << "\n255\n";
  for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel) {
    output.write(reinterpret_cast<const char*>(pixels + pixel * 4), 3);
  }
}

}  // namespace lve::beacon
