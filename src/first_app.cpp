#include "first_app.hpp"

#include "lve/lve_buffer.hpp"
#include "lve/lve_camera.hpp"
#include "movement_controller.hpp"
#include "systems/point_light_system.hpp"
#include "systems/simple_render_system.hpp"
#include "lve/lve_texture.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// std
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace lve {

FirstApp::FirstApp() {
  globalPool =
      LveDescriptorPool::Builder(lveDevice)
          .setMaxSets(LveSwapChain::MAX_FRAMES_IN_FLIGHT)
          .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, LveSwapChain::MAX_FRAMES_IN_FLIGHT)
          .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, LveSwapChain::MAX_FRAMES_IN_FLIGHT)
          .build();
  loadGameObjects();
}

FirstApp::~FirstApp() {}

void FirstApp::run() {
  std::vector<std::unique_ptr<LveBuffer>> uboBuffers(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  for (int i = 0; i < uboBuffers.size(); i++) {
    uboBuffers[i] = std::make_unique<LveBuffer>(
        lveDevice,
        sizeof(GlobalUbo),
        1,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    uboBuffers[i]->map();
  }

  // Global descriptor pool (for UBOs)
  globalPool =
      LveDescriptorPool::Builder(lveDevice)
          .setMaxSets(LveSwapChain::MAX_FRAMES_IN_FLIGHT)
          .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, LveSwapChain::MAX_FRAMES_IN_FLIGHT)
          .build();

  //Texture texture = Texture(lveDevice, "../textures/meme.png");

  // Frame descriptor pool (for per-object textures)
  std::vector<std::unique_ptr<LveDescriptorPool>> framePools(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  for (int i = 0; i < framePools.size(); i++) {
    framePools[i] = LveDescriptorPool::Builder(lveDevice)
                        .setMaxSets(1000)  // Enough for many objects
                        .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000)
                        .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
                        .build();
  }

  auto globalSetLayout =
      LveDescriptorSetLayout::Builder(lveDevice)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS)
          .build();

  std::vector<VkDescriptorSet> globalDescriptorSets(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
  for (int i = 0; i < globalDescriptorSets.size(); i++) {
    auto bufferInfo = uboBuffers[i]->descriptorInfo();
    LveDescriptorWriter(*globalSetLayout, *globalPool)
        .writeBuffer(0, &bufferInfo)
        .build(globalDescriptorSets[i]);
  }

  SimpleRenderSystem simpleRenderSystem{
      lveDevice,
      lveRenderer.getSwapChainRenderPass(),
      globalSetLayout->getDescriptorSetLayout()};
  PointLightSystem pointLightSystem{
      lveDevice,
      lveRenderer.getSwapChainRenderPass(),
      globalSetLayout->getDescriptorSetLayout()};
  LveCamera camera{};

  auto viewerObject = LveGameObject::createGameObject();
  viewerObject.transform.translation = {0.f, -2.0f, -7.0f};
  viewerObject.transform.rotation = {glm::radians(-20.f), 0.f, 0.f}; // Tilt down 20 degrees
  MovementController cameraController{};

  auto currentTime = std::chrono::high_resolution_clock::now();
  while (!lveWindow.shouldClose()) {
    glfwPollEvents();

    auto newTime = std::chrono::high_resolution_clock::now();
    float frameTime =
        std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
    currentTime = newTime;

    cameraController.moveInPlaneXZ(lveWindow.getGLFWwindow(), frameTime, viewerObject);
    camera.setViewYXZ(viewerObject.transform.translation, viewerObject.transform.rotation);

    float aspect = lveRenderer.getAspectRatio();
    camera.setPerspectiveProjection(glm::radians(70.f), aspect, 0.1f, 100.f);

    if (auto commandBuffer = lveRenderer.beginFrame()) {
      int frameIndex = lveRenderer.getFrameIndex();
      framePools[frameIndex]->resetPool();

      FrameInfo frameInfo{
          frameIndex,
          frameTime,
          commandBuffer,
          camera,
          globalDescriptorSets[frameIndex],
          *framePools[frameIndex],
          gameObjects};

      // update
      GlobalUbo ubo{};
      ubo.projection = camera.getProjection();
      ubo.view = camera.getView();
      ubo.inverseView = camera.getInverseView();
      pointLightSystem.update(frameInfo, ubo);
      uboBuffers[frameIndex]->writeToBuffer(&ubo);
      uboBuffers[frameIndex]->flush();

      // render
      lveRenderer.beginSwapChainRenderPass(commandBuffer);

      // order here matters
      simpleRenderSystem.renderGameObjects(frameInfo);
      pointLightSystem.render(frameInfo);

      lveRenderer.endSwapChainRenderPass(commandBuffer);
      lveRenderer.endFrame();
    }
  }

  vkDeviceWaitIdle(lveDevice.device());
}

void FirstApp::loadGameObjects() {
  //defualt texture
  auto defTexture = std::make_shared<Texture>(lveDevice, "../textures/grey.png");
  //load textures
  auto lampTexture = std::make_shared<Texture>(lveDevice, "../textures/lamp/lamp_normal.png");
  auto vaseTexture = std::make_shared<Texture>(lveDevice, "../textures/meme.png");
  auto floorTexture = std::make_shared<Texture>(lveDevice, "../textures/road.jpg");
  auto benchT = std::make_shared<Texture>(lveDevice, "../textures/bench/germany010.jpg");

  try {
    std::shared_ptr<LveModel> benchModel =
        LveModel::createModelFromFile(lveDevice, "models/objBench.obj"); // Adjust filename as needed
    auto bench = LveGameObject::createGameObject();
    bench.model = benchModel;
    bench.texture = benchT;
    bench.transform.translation = {0.f, 0.5f, 0.f}; // Centered on the floor
    bench.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
    bench.transform.scale = {0.25f, 0.25f, 0.25f}; // Adjust scale as needed
    gameObjects.emplace(bench.getId(), std::move(bench));
  } catch (const std::exception& e) {
    std::cerr << "Failed to load bench: " << e.what() << std::endl;
  }

  std::shared_ptr<LveModel> binOBJ =
      LveModel::createModelFromFile(lveDevice, "models/outdoorBin.obj");
  auto trashCan = LveGameObject::createGameObject();
  trashCan.model = binOBJ;
  //trashCan.texture = vaseTexture;
  trashCan.transform.translation = {1.7f, .0f, 0.f};
  trashCan.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
  trashCan.transform.scale = {0.005f, 0.005f, 0.005f};

  gameObjects.emplace(trashCan.getId(), std::move(trashCan));

  std::shared_ptr<LveModel> flat_vase =
      LveModel::createModelFromFile(lveDevice, "models/flat_vase.obj");
  auto flatVase = LveGameObject::createGameObject();
  flatVase.model = flat_vase;
  flatVase.texture = vaseTexture;
  flatVase.transform.translation = {-1.7f, .5f, 0.f};
  flatVase.transform.scale = {3.f, 1.5f, 3.f};
  gameObjects.emplace(flatVase.getId(), std::move(flatVase));

  /*lveModel = LveModel::createModelFromFile(lveDevice, "models/smooth_vase.obj");
  auto smoothVase = LveGameObject::createGameObject();
  smoothVase.model = lveModel;
  smoothVase.texture = defTexture;
  smoothVase.transform.translation = {.5f, .5f, 0.f};
  smoothVase.transform.scale = {3.f, 1.5f, 3.f};
  gameObjects.emplace(smoothVase.getId(), std::move(smoothVase));*/

    //bottom right corner
    std::shared_ptr<LveModel> Lamp = LveModel::createModelFromFile(lveDevice, "models/Street_Lamp.obj");
    auto lamp = LveGameObject::createGameObject();
    lamp.model = Lamp;
    lamp.texture = lampTexture;
    lamp.transform.translation = {-2.9f, 0.5f, -2.9f};
    lamp.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
    lamp.transform.scale = {0.01f, 0.01f, 0.01f};
    gameObjects.emplace(lamp.getId(), std::move(lamp));

    //bottom left
    Lamp = LveModel::createModelFromFile(lveDevice, "models/Street_Lamp.obj");
    auto lamp2 = LveGameObject::createGameObject();
    lamp2.model = Lamp;
    lamp2.texture = lampTexture;
    lamp2.transform.translation = {2.9f, 0.5f, -2.9f};  // Bottom-left (flip X)
    lamp2.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
    lamp2.transform.scale = {0.01f, 0.01f, 0.01f};
    gameObjects.emplace(lamp2.getId(), std::move(lamp2));

    //top right
    Lamp = LveModel::createModelFromFile(lveDevice, "models/Street_Lamp.obj");
    auto lamp3 = LveGameObject::createGameObject();
    lamp3.model = Lamp;
    lamp3.texture = lampTexture;
    lamp3.transform.translation = {-2.9f, 0.5f, 2.9f};  // Top-right (flip Z)
    lamp3.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
    lamp3.transform.scale = {0.01f, 0.01f, 0.01f};
    gameObjects.emplace(lamp3.getId(), std::move(lamp3));

    //top left
    Lamp = LveModel::createModelFromFile(lveDevice, "models/Street_Lamp.obj");
    auto lamp4 = LveGameObject::createGameObject();
    lamp4.model = Lamp;
    lamp4.texture = lampTexture;
    lamp4.transform.translation = {2.9f, 0.5f, 2.9f};  // Top-left (flip both X and Z)
    lamp4.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
    lamp4.transform.scale = {0.01f, 0.01f, 0.01f};
    gameObjects.emplace(lamp4.getId(), std::move(lamp4));

    std::shared_ptr<LveModel> Quad= LveModel::createModelFromFile(lveDevice, "models/quad.obj");
  auto floor = LveGameObject::createGameObject();
  floor.model = Quad;
  floor.texture = floorTexture;
  floor.transform.translation = {0.f, .5f, 0.f};
  floor.transform.scale = {3.f, 1.f, 3.f};
  gameObjects.emplace(floor.getId(), std::move(floor));

 /* std::vector<glm::vec3> lightColors{
      {1.f, .1f, .1f},
      {.1f, .1f, 1.f},
      {.1f, 1.f, .1f},
      {1.f, 1.f, .1f},
      {.1f, 1.f, 1.f},
      {1.f, 1.f, 1.f}  //
  };

  for (int i = 0; i < lightColors.size(); i++) {
    auto pointLight = LveGameObject::makePointLight(0.2f);
    pointLight.color = lightColors[i];
    auto rotateLight = glm::rotate(
        glm::mat4(1.f),
        (i * glm::two_pi<float>()) / lightColors.size(),
        {0.f, -1.f, 0.f});
    pointLight.transform.translation = glm::vec3(rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f));
    gameObjects.emplace(pointLight.getId(), std::move(pointLight));
  }*/

  // bottom right
  auto lampLightRight = LveGameObject::makePointLight(3.0f);  // Bright
  lampLightRight.color = {1.f, 0.9f, 0.8f};  // Warm white
  lampLightRight.transform.translation = {-2.55f, -2.3f, -2.9f};  // Left of lamp (adjust Y for bulb height)
  gameObjects.emplace(lampLightRight.getId(), std::move(lampLightRight));
  auto lampLightLeft = LveGameObject::makePointLight(3.0f);  // Bright
  lampLightLeft.color = {1.f, 0.9f, 0.8f};  // Warm white
  lampLightLeft.transform.translation = {-3.275f, -2.3f, -2.9f};  // Right of lamp (adjust Y for bulb height)
  gameObjects.emplace(lampLightLeft.getId(), std::move(lampLightLeft));

  //bottom left
  auto lamp2LightRight = LveGameObject::makePointLight(3.0f);
  lamp2LightRight.color = {1.f, 0.9f, 0.8f};
  lamp2LightRight.transform.translation = {2.5f, -2.3f, -2.9f};  // Positive X
  gameObjects.emplace(lamp2LightRight.getId(), std::move(lamp2LightRight));

  auto lamp2LightLeft = LveGameObject::makePointLight(3.0f);
  lamp2LightLeft.color = {1.f, 0.9f, 0.8f};
  lamp2LightLeft.transform.translation = {3.275f, -2.3f, -2.9f};  // Positive X
  gameObjects.emplace(lamp2LightLeft.getId(), std::move(lamp2LightLeft));

  //top right
  auto lamp3LightRight = LveGameObject::makePointLight(3.0f);
  lamp3LightRight.color = {1.f, 0.9f, 0.8f};
  lamp3LightRight.transform.translation = {-2.55f, -2.3f, 2.9f};  // Positive Z
  gameObjects.emplace(lamp3LightRight.getId(), std::move(lamp3LightRight));

  auto lamp3LightLeft = LveGameObject::makePointLight(3.0f);
  lamp3LightLeft.color = {1.f, 0.9f, 0.8f};
  lamp3LightLeft.transform.translation = {-3.275f, -2.3f, 2.9f};  // Positive Z
  gameObjects.emplace(lamp3LightLeft.getId(), std::move(lamp3LightLeft));

  //top left
  auto lamp4LightRight = LveGameObject::makePointLight(3.0f);
  lamp4LightRight.color = {1.f, 0.9f, 0.8f};
  lamp4LightRight.transform.translation = {2.5f, -2.3f, 2.9f};  // Positive X and Z
  gameObjects.emplace(lamp4LightRight.getId(), std::move(lamp4LightRight));

  auto lamp4LightLeft = LveGameObject::makePointLight(3.0f);
  lamp4LightLeft.color = {1.f, 0.9f, 0.8f};
  lamp4LightLeft.transform.translation = {3.275f, -2.3f, 2.9f};  // Positive X and Z
  gameObjects.emplace(lamp4LightLeft.getId(), std::move(lamp4LightLeft));

}

}  // namespace lve
