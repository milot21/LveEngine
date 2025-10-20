//
// Created by milot on 9/23/2025.
//

#include "movement_controller.hpp"

// std
#include <limits>

namespace lve {

/**
     * implements first-person movements and mouse look controls
     * combines keyboard movement with mouse
     * @param window
     * @param dt
     * @param gameObject
 */
void MovementController::moveInPlaneXZ(
    GLFWwindow* window, float dt, LveGameObject& gameObject) {
  //only process mouse look when left mouse button is held down
  if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos); //get cursor position

    //handle first click to prevent large rotation jumps
    if (firstClick) {
      lastX = xpos;
      lastY = ypos;
      firstClick = false;
    }

    //calculate mouse movement delta since last frame
    float xOffset = static_cast<float>(xpos - lastX); //horizontal mouse movement
    float yOffset = static_cast<float>(ypos - lastY); //vertical mouse movement
    //store current pos for next frame
    lastX = xpos;
    lastY = ypos;
    //sensitivity
    float mouseSensitivity = 0.2f; //tune for faster/slower speed
    gameObject.transform.rotation.y += lookSpeed * dt * xOffset * mouseSensitivity; //yaw
    gameObject.transform.rotation.x -= lookSpeed * dt * yOffset * mouseSensitivity; //Pitch

    // constrain to prevent over rotating
    gameObject.transform.rotation.x = glm::clamp(gameObject.transform.rotation.x, -1.5f, 1.5f);
    //wrap yaww to keep it in 2 pie range
    gameObject.transform.rotation.y = glm::mod(gameObject.transform.rotation.y, glm::two_pi<float>());

  } else {
    firstClick = true; //reset so we don't get a big jump on next click
  }

  //calc movement dir based on curr yaw rotation
  float yaw = gameObject.transform.rotation.y;
  //forward dir varies based on yaw, local Z-axis
  const glm::vec3 forwardDir{sin(yaw), 0.f, cos(yaw)};
  //right dir perpindicular to forwards in XZ plane, local x-axis
  const glm::vec3 rightDir{forwardDir.z, 0.f, -forwardDir.x};
  //up directions
  const glm::vec3 upDir{0.f, -1.f, 0.f};

  //Accumulate movement input from all pressed keys
  glm::vec3 moveDir{0.f};// start with none
  //check each movement key and add corresponding dir vector
  if (glfwGetKey(window, keys.moveForward) == GLFW_PRESS) moveDir += forwardDir;
  if (glfwGetKey(window, keys.moveBackward) == GLFW_PRESS) moveDir -= forwardDir;
  if (glfwGetKey(window, keys.moveRight) == GLFW_PRESS) moveDir += rightDir;
  if (glfwGetKey(window, keys.moveLeft) == GLFW_PRESS) moveDir -= rightDir;
  if (glfwGetKey(window, keys.moveUp) == GLFW_PRESS) moveDir += upDir;
  if (glfwGetKey(window, keys.moveDown) == GLFW_PRESS) moveDir -= upDir;

  //apply movement if any keys are pressed
  if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
    gameObject.transform.translation += moveSpeed * dt * glm::normalize(moveDir);
  }
}
}  // namespace lve