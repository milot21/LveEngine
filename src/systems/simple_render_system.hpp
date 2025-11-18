#pragma once

#include "lve/lve_camera.hpp"
#include "lve/lve_device.hpp"
#include "lve/lve_frame_info.hpp"
#include "lve/lve_game_object.hpp"
#include "lve/lve_pipeline.hpp"

// std
#include <memory>
#include <vector>

namespace lve {
class SimpleRenderSystem {
 public:
  SimpleRenderSystem(
      LveDevice &device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
  ~SimpleRenderSystem();

  SimpleRenderSystem(const SimpleRenderSystem &) = delete;
  SimpleRenderSystem &operator=(const SimpleRenderSystem &) = delete;

  void renderGameObjects(FrameInfo &frameInfo);

 private:
  void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
  void createPipeline(VkRenderPass renderPass);

  LveDevice &lveDevice;

  std::unique_ptr<LvePipeline> lvePipeline;
  VkPipelineLayout pipelineLayout;

  std::unique_ptr<LveDescriptorSetLayout> textureSetLayout; // Layout for textures
};
}  // namespace lve
