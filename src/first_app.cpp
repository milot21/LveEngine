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
  bool keyPressed[7] = {false, false, false, false, false,false,false};
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

        // handle - Keys 1,2,3,4,5,6
        for (int i = 0; i < 6; i++) {
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
  auto guyT = std::make_shared<Texture>(lveDevice, "../models/fallguys/shaded.png");
  auto guyMetallicT = std::make_shared<Texture>(lveDevice, "../models/fallguys/texture_normal.png");
  auto guyFireT = std::make_shared<Texture>(lveDevice, "../models/fallguys/texture_pbr.png");
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

  // Quick scale pulse animation
  Animation scaleAnim(
      glm::vec3(0.f),
      glm::vec3(0.f),
      glm::vec3(1.f),
      glm::vec3(0.f),
      glm::vec3(0.f),
      glm::vec3(2.5f),          // scale end (2.5x bigger)
      6.f,                     // 3 seconds to scale up
      Interp::EASE_IN_OUT       // smooth scaling
  );
  Animation zombieAttackAnim(
      glm::vec3(0.f),
      glm::vec3(0.f),
      glm::vec3(1.f),
      glm::vec3(0.f, 0.f, 10.f), // End translation - move +10 on Z axis
      glm::vec3(0.f),
      glm::vec3(1.f),
      3.0f,                     // 2 seconds duration
      Interp::LINEAR            // Linear movement -can change to EASE_IN_OUT for smoother
  );


  // PARENT: torso and head
  auto torso = LveGameObject::createGameObject();
  torso.model = torsoHead;
  torso.texture = guyT;  // Use one of your existing textures
  glm::vec3 torsoWorld = {0.f, -1.f, -1.f};
  torso.transform.translation = torsoWorld;  // World position
  torso.transform.scale = {0.1f, 0.1f, 0.1f};      // Adjust if too big/small
  torso.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
  torso.basetransform = torso.transform;
  torso.anim = std::make_unique<AnimationController>(glm::vec3(0.f),glm::vec3(0.f),glm::vec3(1.f));
  torso.anim->registerKey(1, jumpAnim);
  auto torsoId = torso.getId();
  gameObjects.emplace(torsoId, std::move(torso));

   //CHILD: left leg
  auto ll = LveGameObject::createGameObject();
  ll.model = lLeg;
  ll.texture = guyMetallicT;  // Use one of your existing textures
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
  rl.texture = guyMetallicT;  // Use one of your existing textures
  glm::vec3 rlLocal ={0.f, -1.f, -1.f};
  rl.transform.translation = rlLocal - torsoWorld;  // World position
  rl.transform.scale = {0.1f, 0.1f, 0.1f};      // Adjust if too big/small
  rl.transform.rotation = {0.f, 0.f, 0.f};
  rl.basetransform = rl.transform;

  rl.setParent(torsoId);
  rl.anim = std::make_unique<AnimationController>(glm::vec3(0.f), glm::vec3(0.f),glm::vec3(1.f));
  rl.anim->registerKey(4, armSwingAnim);
  auto rlId = rl.getId();
  gameObjects.emplace(rlId, std::move(rl));
  gameObjects.at(torsoId).addchild(rlId);

  //CHILD: left arm
  auto la = LveGameObject::createGameObject();
  la.model = lArm;
  la.texture = guyMetallicT;  // Use one of your existing textures
  glm::vec3 laLocal = {0.f, -1.f, -1.f};
  la.transform.translation = laLocal - torsoWorld;  // World position
  la.transform.scale = {0.1f, 0.1f, 0.1f};      // Adjust if too big/small
  la.transform.rotation = {0.f, 0.f, 0.f};
  la.basetransform = la.transform;

  la.setParent(torsoId);
  la.anim = std::make_unique<AnimationController>(glm::vec3(0.f), glm::vec3(0.f),glm::vec3(1.f));
  la.anim->registerKey(4, swingAnim);
  auto laId = la.getId();
  gameObjects.emplace(laId, std::move(la));
  gameObjects.at(torsoId).addchild(laId);

  //CHILD: right arm
  auto ra = LveGameObject::createGameObject();
  ra.model = rArm;
  ra.texture = guyMetallicT;  // Use one of your existing textures
  glm::vec3 raLocal = {0.f, -1.f, -1.f};
  ra.transform.translation = raLocal - torsoWorld;  // World position
  ra.transform.scale = {0.1f, 0.1f, 0.1f};      // Adjust if too big/small
  ra.transform.rotation = {0.f, 0.f, 0.f};
  ra.basetransform = ra.transform; //store original

  ra.setParent(torsoId);
  ra.anim = std::make_unique<AnimationController>(glm::vec3(0.f), glm::vec3(0.f),glm::vec3(1.f));
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
    bench.basetransform = bench.transform;
    // Initialize animation controller
//    bench.anim = std::make_unique<AnimationController>(glm::vec3(0.f),glm::vec3(0.f),glm::vec3(1.f));
//    bench.anim->registerKey(1, jumpAnim);
//    bench.anim->registerKey(3, swingAnim);
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
//  trashCan.anim = std::make_unique<AnimationController>(
//      trashCan.transform.translation,
//      trashCan.transform.rotation,
//      trashCan.transform.scale);
//  trashCan.anim->registerKey(2, spin360Anim);
//  trashCan.anim->registerKey(3, swingAnim);
  gameObjects.emplace(trashCan.getId(), std::move(trashCan));

  //VASE WITH TEXTURE
  std::shared_ptr<LveModel> flat_vase =
      LveModel::createModelFromFile(lveDevice, "models/flat_vase.obj");
  auto flatVase = LveGameObject::createGameObject();
  flatVase.model = flat_vase;
  flatVase.texture = vaseTexture;
  flatVase.transform.translation = {-1.7f, .5f, 0.f};
  flatVase.transform.scale = {3.f, 1.5f, 3.f};
//  flatVase.anim = std::make_unique<AnimationController>(
//      flatVase.transform.translation,
//      flatVase.transform.rotation,
//      flatVase.transform.scale);
//  flatVase.anim->registerKey(1, jumpAnim);
//  flatVase.anim->registerKey(2, spin360Anim);
  gameObjects.emplace(flatVase.getId(), std::move(flatVase));


  //FALL GUY
  std::shared_ptr<LveModel> fallGuy = LveModel::createModelFromFile(lveDevice, "models/fallguys/base.obj");
  auto guy = LveGameObject::createGameObject();
  guy.model = fallGuy;
  guy.texture = guyFireT;
  guy.transform.translation = {0, 0.5f, 2.9f};
  guy.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
  guy.transform.scale = {1.0f, 1.0f, 1.0f};
  guy.anim = std::make_unique<AnimationController>(
      guy.transform.translation,
      guy.transform.rotation,
      guy.transform.scale);
  guy.anim->registerKey(1, jumpAnim);
  guy.anim->registerKey(2, scaleAnim);
  guy.anim->registerKey(3,swingAnim);
  gameObjects.emplace(guy.getId(), std::move(guy));

  //FALL GUY
  auto ccT = std::make_shared<Texture>(lveDevice, "../models/crust_crab/shaded.png");
  std::shared_ptr<LveModel> crustyCrab = LveModel::createModelFromFile(lveDevice, "models/crust_crab/base.obj");
  auto cc = LveGameObject::createGameObject();
  cc.model = crustyCrab;
  cc.texture = ccT;
  cc.transform.translation = {0, 0.5f, 7.f};
  cc.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
  cc.transform.scale = {5.0f, 5.0f, 5.0f};
  gameObjects.emplace(cc.getId(), std::move(cc));

  //PATH FLOOR FOR ZOMBIES
  std::shared_ptr<LveModel> Quad2= LveModel::createModelFromFile(lveDevice, "models/quad.obj");
  auto floor2 = LveGameObject::createGameObject();
  floor2.model = Quad2;
  floor2.texture = floorTexture;
  floor2.transform.translation = {0.f, .5f, -6.f};
  floor2.transform.scale = {3.f, 1.f, 3.f};
  gameObjects.emplace(floor2.getId(), std::move(floor2));

  // PARENT ZOMBIE
  auto zT = std::make_shared<Texture>(lveDevice, "../models/zombie/shaded.png");
  std::shared_ptr<LveModel> zombie = LveModel::createModelFromFile(lveDevice, "models/zombie/base.obj");

  auto zParent = LveGameObject::createGameObject();
  glm::vec3 parentWorld = {0.f, 0.5f, -7.f};  // Parent's world position
  zParent.model = zombie;
  zParent.texture = zT;
  zParent.transform.translation = parentWorld;
  zParent.transform.rotation = {0.f, 0.f, glm::pi<float>()};
  zParent.transform.scale = {1.0f, 1.0f, 1.0f};
  zParent.basetransform = zParent.transform;  // Don't forget to store base transform!
  zParent.anim = std::make_unique<AnimationController>(glm::vec3(0.f),glm::vec3(0.f),glm::vec3(1.f));
  zParent.anim->registerKey(6, jumpAnim);
  auto zParentId = zParent.getId();
  gameObjects.emplace(zParentId, std::move(zParent));

  // LEFT ZOMBIE CHILD
  auto zchildL = LveGameObject::createGameObject();
  glm::vec3 childLWorld = {-1.f, 0.5f, -7.5f};  // Current world position
  zchildL.model = zombie;
  zchildL.texture = zT;
  zchildL.transform.translation = childLWorld - parentWorld;  // Convert to local space
  zchildL.transform.rotation = {0.f, 0.f, 0.f};
  zchildL.transform.scale = {0.5f, 0.5f, 0.5f};
  zchildL.basetransform = zchildL.transform;
  zchildL.anim = std::make_unique<AnimationController>(glm::vec3(0.f),glm::vec3(0.f),glm::vec3(1.f));
  zchildL.anim->registerKey(5, zombieAttackAnim);
  zchildL.setParent(zParentId);
  auto zchildLId = zchildL.getId();
  gameObjects.emplace(zchildLId, std::move(zchildL));
  gameObjects.at(zParentId).addchild(zchildLId);

  // RIGHT ZOMBIE CHILD
  auto zchildR = LveGameObject::createGameObject();
  glm::vec3 childRWorld = {1.f, 0.5f, -7.5f};  // Current world position
  zchildR.model = zombie;
  zchildR.texture = zT;
  zchildR.transform.translation = childRWorld - parentWorld;  // Convert to local space
  zchildR.transform.rotation = {0.f, 0.f, 0.f};
  zchildR.transform.scale = {0.5f, 0.5f, 0.5f};
  zchildR.basetransform = zchildR.transform;
  zchildR.anim = std::make_unique<AnimationController>(glm::vec3(0.f),glm::vec3(0.f),glm::vec3(1.f));
  zchildR.anim->registerKey(5, zombieAttackAnim);
  zchildR.setParent(zParentId);
  auto zchildRId = zchildR.getId();
  gameObjects.emplace(zchildRId, std::move(zchildR));
  gameObjects.at(zParentId).addchild(zchildRId);

  // BACK ZOMBIE CHILD
  auto zchildB = LveGameObject::createGameObject();
  glm::vec3 childBWorld = {0.f, 0.5f, -8.5f};  // Current world position
  zchildB.model = zombie;
  zchildB.texture = zT;
  zchildB.transform.translation = childBWorld - parentWorld;  // Convert to local space
  zchildB.transform.rotation = {0.f, 0.f, 0.f};
  zchildB.transform.scale = {0.5f, 0.5f, 0.5f};
  zchildB.basetransform = zchildB.transform;
  zchildB.setParent(zParentId);
  auto zchildBId = zchildB.getId();
  gameObjects.emplace(zchildBId, std::move(zchildB));
  gameObjects.at(zParentId).addchild(zchildBId);
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

    //bottom right corner zombies
    auto lampZ = LveGameObject::createGameObject();
    lampZ.model = Lamp;
    lampZ.texture = lampTexture;
    lampZ.transform.translation = {-2.9f, 0.5f, -8.9f};
    lampZ.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
    lampZ.transform.scale = {0.01f, 0.01f, 0.01f};
    gameObjects.emplace(lampZ.getId(), std::move(lampZ));

    //bottom left
    auto lamp2 = LveGameObject::createGameObject();
    lamp2.model = Lamp;
    lamp2.texture = lampTexture;
    lamp2.transform.translation = {2.9f, 0.5f, -2.9f};  // Bottom-left (flip X)
    lamp2.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
    lamp2.transform.scale = {0.01f, 0.01f, 0.01f};
    gameObjects.emplace(lamp2.getId(), std::move(lamp2));

    //bottom left zombies
//    auto lamp2Z = LveGameObject::createGameObject();
//    lamp2Z.model = Lamp;
//    lamp2Z.texture = lampTexture;
//    lamp2Z.transform.translation = {2.9f, 0.5f, -8.9f};  // Bottom-left (flip X)
//    lamp2Z.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
//    lamp2Z.transform.scale = {0.01f, 0.01f, 0.01f};
//    gameObjects.emplace(lamp2Z.getId(), std::move(lamp2Z));

    //top right
    auto lamp3 = LveGameObject::createGameObject();
    lamp3.model = Lamp;
    lamp3.texture = lampTexture;
    lamp3.transform.translation = {-2.9f, 0.5f, 2.9f};  // Top-right (flip Z)
    lamp3.transform.rotation = {glm::pi<float>(), 0.f, 0.f};
    lamp3.transform.scale = {0.01f, 0.01f, 0.01f};
    gameObjects.emplace(lamp3.getId(), std::move(lamp3));

    //top left
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

  // bottom right for zombies
  auto lampLightRight2 = LveGameObject::makePointLight(3.0f);  // Bright
  lampLightRight2.color = {1.f, 0.9f, 0.8f};  // Warm white
  lampLightRight2.transform.translation = {-2.55f, -2.3f, -8.9f};  // Left of lamp (adjust Y for bulb height)
  gameObjects.emplace(lampLightRight2.getId(), std::move(lampLightRight2));
  auto lampLightLeft2 = LveGameObject::makePointLight(3.0f);  // Bright
  lampLightLeft2.color = {1.f, 0.9f, 0.8f};  // Warm white
  lampLightLeft2.transform.translation = {-3.275f, -2.3f, -8.9f};  // Right of lamp (adjust Y for bulb height)
  gameObjects.emplace(lampLightLeft2.getId(), std::move(lampLightLeft2));

  //bottom left
  auto lamp2LightRight = LveGameObject::makePointLight(3.0f);
  lamp2LightRight.color = {1.f, 0.9f, 0.8f};
  lamp2LightRight.transform.translation = {2.5f, -2.3f, -2.9f};  // Positive X
  gameObjects.emplace(lamp2LightRight.getId(), std::move(lamp2LightRight));
  auto lamp2LightLeft = LveGameObject::makePointLight(3.0f);
  lamp2LightLeft.color = {1.f, 0.9f, 0.8f};
  lamp2LightLeft.transform.translation = {3.275f, -2.3f, -2.9f};  // Positive X
  gameObjects.emplace(lamp2LightLeft.getId(), std::move(lamp2LightLeft));

//  //bottom left for zombies
//  auto lamp2LightRightZ = LveGameObject::makePointLight(3.0f);
//  lamp2LightRightZ.color = {1.f, 0.9f, 0.8f};
//  lamp2LightRightZ.transform.translation = {2.5f, -2.3f, -8.9f};  // Positive X
//  gameObjects.emplace(lamp2LightRightZ.getId(), std::move(lamp2LightRightZ));
//  auto lamp2LightLeftZ = LveGameObject::makePointLight(3.0f);
//  lamp2LightLeftZ.color = {1.f, 0.9f, 0.8f};
//  lamp2LightLeftZ.transform.translation = {3.275f, -2.3f, -8.9f};  // Positive X
//  gameObjects.emplace(lamp2LightLeftZ.getId(), std::move(lamp2LightLeftZ));

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
