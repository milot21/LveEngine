#pragma once

#include "lve_model.hpp"
#include "lve_texture.hpp"
#include "lve_animation.hpp"
// libs
#include <glm/gtc/matrix_transform.hpp>

// std
#include <memory>
#include <unordered_map>
#include <algorithm>

namespace lve {

/**
 * handles position, rotation, and scale of game objects
 */
struct TransformComponent {
  glm::vec3 translation{};
  glm::vec3 scale{1.f, 1.f, 1.f};
  glm::vec3 rotation{};

  // Matrix corrsponds to Translate * Ry * Rx * Rz * Scale
  // Rotations correspond to Tait-bryan angles of Y(1), X(2), Z(3)
  // https://en.wikipedia.org/wiki/Euler_angles#Rotation_matrix
  glm::mat4 mat4() const;
  //takes parents trans matrix and gives a combined world matrix for child to use and so on
  glm::mat4 parentMat4(const glm::mat4& parentMatrix) const;

  //used to correctly transform normals for lighting calculations
  glm::mat3 normalMatrix();
};



struct PointLightComponent {
  float lightIntensity = 1.0f;  //brightness multiplier
};
// 3d models that can have textures, transformations, a point of light, color
class LveGameObject {
 public:
  using id_t = unsigned int;
  using Map = std::unordered_map<id_t, LveGameObject>;

  //factory method to create a new game object with unique ID
  static LveGameObject createGameObject() {
    static id_t currentId = 0;
    return LveGameObject{currentId++};
  }

  static LveGameObject makePointLight(
      float intensity = 10.f, float radius = 0.1f, glm::vec3 color = glm::vec3(1.f));

  LveGameObject(const LveGameObject &) = delete;
  LveGameObject &operator=(const LveGameObject &) = delete;
  LveGameObject(LveGameObject &&) = default;
  LveGameObject &operator=(LveGameObject &&) = default;

  id_t getId() { return id; }
  //add child to parent
  void addchild(id_t childID) {children.push_back(childID);}
  //remove child from its parent
  void removeChild(id_t childID) {
    for (auto p = children.begin(); p != children.end(); ++p) {
      if (*p == childID) {
        children.erase(p);
        break;  // Found and removed, we're done
      }
    }
  }
  //list of child ids
  const std::vector<id_t>& getChildren() const {return children;}
  void setParent(id_t parentID) { parent = parentID;}
  int getParent() const {return parent;}
  bool hasParent() const {return parent != -1;}

  glm::mat4 getWorldMatrix(const Map& gameObjects) const;
  glm::vec3 color{};
  TransformComponent transform{};
  TransformComponent basetransform{}; //original local transform befroe anim

  // Optional pointer components
  std::shared_ptr<LveModel> model{};
  std::shared_ptr<Texture> texture{};// image data for surface, shareable
  std::unique_ptr<PointLightComponent> pointLight = nullptr; // not shareable

  // Animation controller
  std::unique_ptr<AnimationController> anim = nullptr;

 private:
  LveGameObject(id_t objId) : id{objId} {}

  id_t id; // unique identifier
  int parent = -1; //no establish parent, if so then 1
  std::vector<id_t> children; //list of children ids
};
}  // namespace lve
