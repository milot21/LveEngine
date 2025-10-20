//
// Created by milot on 9/23/2025.
//
#pragma once

#include "lve/lve_game_object.hpp"
#include "lve/lve_window.hpp"

namespace lve {
/**
     * first person style movement controller for game objects
     * Handles both keyboard movement and mouse look controls
     * WASD movement in the XZ plane
     * QE for vertical movements
     * mouse look for yaw/pitch rotation with sensitivity cotnrol
 */
class MovementController {
 public:
  // customizable key mappings for movement controls
  struct KeyMappings {
    int moveLeft = GLFW_KEY_A;
    int moveRight = GLFW_KEY_D;
    int moveForward = GLFW_KEY_W;
    int moveBackward = GLFW_KEY_S;
    int moveUp = GLFW_KEY_E;
    int moveDown = GLFW_KEY_Q;
  };

  /**
         * updates game obj pos and rot based on input
         * handles keyboard and mouse movements
         * @param window handle for input polling
         * @param dt     Delta time in secs for FR independent movement
         * @param gameObject  object to move (like camera)
   */
  void moveInPlaneXZ(GLFWwindow* window, float dt, LveGameObject& gameObject);

  KeyMappings keys{};   // config key bindings
  //adjust how quickly the controller moves and turn the game objects
  float moveSpeed{3.f}; //movement speed in units per second
  float lookSpeed{1.5f}; //mouse look sensitivity multiplier
 private:
  bool firstClick = true; //flag to prevent large jumps
  double lastX = 0.0;     // previous mouse X pos fro delta time calc
  double lastY = 0.0;     // previous mouse Y pos
}; // namespace lve

}; //namespace lve