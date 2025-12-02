#include "first_app.hpp"

#include "lve/lve_buffer.hpp"
#include "lve/lve_camera.hpp"
#include "movement_controller.hpp"
#include "systems/point_light_system.hpp"
#include "systems/simple_render_system.hpp"
#include "lve/lve_texture.hpp"
#include "lve/lve_animation.hpp"


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

  // Frame descriptor pool for per-object textures
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
  bool keyPressed[3] = {false, false, false};
  while (!lveWindow.shouldClose()) {
    glfwPollEvents();

    auto newTime = std::chrono::high_resolution_clock::now();
    float frameTime =
        std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
    currentTime = newTime;

        // update all animations
        for (auto& kv : gameObjects) {
      auto& obj = kv.second;
      if (obj.anim) {
        glm::vec3 t, r, s;
        obj.anim->update(frameTime, t, r, s);
        obj.transform.translation = obj.basetransform.translation + t;
        obj.transform.rotation = obj.basetransform.rotation + r;
        obj.transform.scale = obj.basetransform.scale * s;
      }
    }

        // handle - Keys 1,2,3
        for (int i = 0; i < 3; i++) {
      int glfwKey = GLFW_KEY_1 + i;
      int animKey = i + 1;

      if (glfwGetKey(lveWindow.getGLFWwindow(), glfwKey) == GLFW_PRESS) {
        if (!keyPressed[i]) {
          keyPressed[i] = true;
          // Trigger animation on all objects that have this key registered
          for (auto& kv : gameObjects) {
            if (kv.second.anim) {
              kv.second.anim->trigger(animKey);
            }
          }
        }
      } else {
        keyPressed[i] = false;
      }
    }

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
  // Load all body part models
  std::shared_ptr<LveModel> torsoHead =
      LveModel::createModelFromFile(lveDevice, "models/Hierarchical_char/head_torso.obj");
  std::shared_ptr<LveModel> lArm =
      LveModel::createModelFromFile(lveDevice, "models/Hierarchical_char/right_arm.obj");
  std::shared_ptr<LveModel> rArm =
      LveModel::createModelFromFile(lveDevice, "models/Hierarchical_char/left_arm.obj");
  std::shared_ptr<LveModel> lLeg =
      LveModel::createModelFromFile(lveDevice, "models/Hierarchical_char/left_leg.obj");
  std::shared_ptr<LveModel> rLeg =
      LveModel::createModelFromFile(lveDevice, "models/Hierarchical_char/right_leg.obj");

  //defualt texture
  auto defTexture = std::make_shared<Texture>(lveDevice, "../textures/grey.png");
  //load textures
  auto lampTexture = std::make_shared<Texture>(lveDevice, "../textures/lamp/lamp_normal.png");
  auto vaseTexture = std::make_shared<Texture>(lveDevice, "../textures/meme.png");
  auto floorTexture = std::make_shared<Texture>(lveDevice, "../textures/road.jpg");
  auto benchT = std::make_shared<Texture>(lveDevice, "../textures/bench/germany010.jpg");

  // Anim1:Jump
  Animation jumpAnim(
      glm::vec3(0.f, 0.f, 0.f),// Start at ground level
      glm::vec3(0.f),
      glm::vec3(1.f),
      glm::vec3(0.f, -2.f, 0.f),// Jump up 2 units
      glm::vec3(0.f),
      glm::vec3(1.f),
      0.8f,     // 0.8 seconds
      Interp::EASE_OUT
  );

  // Anim2: 360 Spin Y axis
  Animation spin360Anim(
      glm::vec3(0.f),
      glm::vec3(0.f),// Start rotation
      glm::vec3(1.f),
      glm::vec3(0.f),
      glm::vec3(0.f, glm::two_pi<float>(), 0.f),  // 360 rotation
      glm::vec3(1.f),
      2.0f,
      Interp::LINEAR
  );

  // Anim3: Swing tilt
  Animation swingAnim(
      glm::vec3(0.f),
      glm::vec3(0.f),
      glm::vec3(1.f),
      glm::vec3(0.f),
      glm::vec3(0.f, 0.f, 0.3f),// Tilt on Z axis
      glm::vec3(1.f),
      1.0f,
      Interp::EASE_IN_OUT
  );
  // Animation for left arm swing
  Animation armSwingAnim(
      glm::vec3(0.f),
      glm::vec3(0.f),
      glm::vec3(1.f),
      glm::vec3(0.f),
      glm::vec3(-0.8f, 0.f, 0.f), //swing arm forward 45°
      glm::vec3(1.f),
      1.0f,
      Interp::EASE_IN_OUT
  );

  // PARENT: torso and head
  auto torso = LveGameObject::createGameObject();
  torso.model = torsoHead;
  torso.texture = defTexture;  // Use one of your existing textures
  glm::vec3 torsoWorld = {0.f, -1.f, -1.f};
  torso.transform.translation = torsoWorld;  // World position
  torso.transform.scale = {0.1f, 0.1f, 0.1f};      // Adjust if too big/small
  torso.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
  torso.basetransform = torso.transform;
  torso.anim = std::make_unique<AnimationController>(
      glm::vec3(0.f),  // Animation offset from base
      glm::vec3(0.f),
      glm::vec3(1.f)   // Scale multiplier
  );
  torso.anim->registerKey(1, jumpAnim);
  auto torsoId = torso.getId();
  gameObjects.emplace(torsoId, std::move(torso));

   //CHILD: left leg
  auto ll = LveGameObject::createGameObject();
  ll.model = lLeg;
  ll.texture = defTexture;  // Use one of your existing textures
  glm::vec3 llLocal ={0.f, -1.f, -1.f};
  ll.transform.translation = llLocal - torsoWorld;  // World position
  ll.transform.scale = {0.1f, 0.1f, 0.1f};      // Adjust if too big/small
  ll.transform.rotation = {0.f, 0.f, 0.f};
  ll.basetransform = ll.transform;

  ll.setParent(torsoId);
  auto llId = ll.getId();
  gameObjects.emplace(llId, std::move(ll));
  gameObjects.at(torsoId).addchild(llId);

  //CHILD: right leg
  auto rl = LveGameObject::createGameObject();
  rl.model = rLeg;
  rl.texture = defTexture;  // Use one of your existing textures
  glm::vec3 rlLocal ={0.f, -1.f, -1.f};
  rl.transform.translation = rlLocal - torsoWorld;  // World position
  rl.transform.scale = {0.1f, 0.1f, 0.1f};      // Adjust if too big/small
  rl.transform.rotation = {0.f, 0.f, 0.f};
  rl.basetransform = rl.transform;

  rl.setParent(torsoId);
  auto rlId = rl.getId();
  gameObjects.emplace(rlId, std::move(rl));
  gameObjects.at(torsoId).addchild(rlId);

  //CHILD: left arm
  auto la = LveGameObject::createGameObject();
  la.model = lArm;
  la.texture = defTexture;  // Use one of your existing textures
  glm::vec3 laLocal = {0.f, -1.f, -1.f};
  la.transform.translation = laLocal - torsoWorld;  // World position
  la.transform.scale = {0.1f, 0.1f, 0.1f};      // Adjust if too big/small
  la.transform.rotation = {0.f, 0.f, 0.f};
  la.basetransform = la.transform;

  la.setParent(torsoId);
  auto laId = la.getId();
  gameObjects.emplace(laId, std::move(la));
  gameObjects.at(torsoId).addchild(laId);

  //CHILD: right arm
  auto ra = LveGameObject::createGameObject();
  ra.model = rArm;
  ra.texture = defTexture;  // Use one of your existing textures
  glm::vec3 raLocal = {0.f, -1.f, -1.f};
  ra.transform.translation = raLocal - torsoWorld;  // World position
  ra.transform.scale = {0.1f, 0.1f, 0.1f};      // Adjust if too big/small
  ra.transform.rotation = {0.f, 0.f, 0.f};
  ra.basetransform = ra.transform; //store original

  ra.setParent(torsoId);
  ra.anim = std::make_unique<AnimationController>(
      glm::vec3(0.f),
      glm::vec3(0.f),
      glm::vec3(1.f)
  );
  ra.anim->registerKey(4, armSwingAnim);

  auto raId = ra.getId();
  gameObjects.emplace(raId, std::move(ra));
  gameObjects.at(torsoId).addchild(raId);


  //BENCH
    std::shared_ptr<LveModel> benchModel =
        LveModel::createModelFromFile(lveDevice, "models/objBench.obj"); // Adjust filename as needed
    auto bench = LveGameObject::createGameObject();
    bench.model = benchModel;
    bench.texture = benchT;
    bench.transform.translation = {0.f, 0.5f, 0.f}; // Centered on the floor
    bench.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
    bench.transform.scale = {0.25f, 0.25f, 0.25f}; // Adjust scale as needed
    // Initialize animation controller
    bench.anim = std::make_unique<AnimationController>(
        bench.transform.translation,
        bench.transform.rotation,
        bench.transform.scale
        );

    bench.anim->registerKey(1, jumpAnim);
    bench.anim->registerKey(3, swingAnim);
    gameObjects.emplace(bench.getId(), std::move(bench));

  //TRASH CAN
  std::shared_ptr<LveModel> binOBJ =
      LveModel::createModelFromFile(lveDevice, "models/outdoorBin.obj");
  auto trashCan = LveGameObject::createGameObject();
  trashCan.model = binOBJ;
  //trashCan.texture = defTexture;
  trashCan.transform.translation = {1.7f, .03f, 0.f};
  trashCan.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
  trashCan.transform.scale = {0.005f, 0.005f, 0.005f};
  trashCan.anim = std::make_unique<AnimationController>(
      trashCan.transform.translation,
      trashCan.transform.rotation,
      trashCan.transform.scale
  );
  trashCan.anim->registerKey(2, spin360Anim);
  trashCan.anim->registerKey(3, swingAnim);
  gameObjects.emplace(trashCan.getId(), std::move(trashCan));

  //VASE WITH TEXTURE
  std::shared_ptr<LveModel> flat_vase =
      LveModel::createModelFromFile(lveDevice, "models/flat_vase.obj");
  auto flatVase = LveGameObject::createGameObject();
  flatVase.model = flat_vase;
  flatVase.texture = vaseTexture;
  flatVase.transform.translation = {-1.7f, .5f, 0.f};
  flatVase.transform.scale = {3.f, 1.5f, 3.f};
  flatVase.anim = std::make_unique<AnimationController>(
      flatVase.transform.translation,
      flatVase.transform.rotation,
      flatVase.transform.scale
  );

  flatVase.anim->registerKey(1, jumpAnim);
  flatVase.anim->registerKey(2, spin360Anim);

  gameObjects.emplace(flatVase.getId(), std::move(flatVase));

  //LAMPS FOR EACH CORNER
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

    //PATH FLOOR
    std::shared_ptr<LveModel> Quad= LveModel::createModelFromFile(lveDevice, "models/quad.obj");
  auto floor = LveGameObject::createGameObject();
  floor.model = Quad;
  floor.texture = floorTexture;
  floor.transform.translation = {0.f, .5f, 0.f};
  floor.transform.scale = {3.f, 1.f, 3.f};
  gameObjects.emplace(floor.getId(), std::move(floor));

  //LAMP LIGHTS BELOW/////////////////////////////////////

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
