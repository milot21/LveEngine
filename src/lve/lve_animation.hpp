//
// Created by milot on 11/01/2025.
//
#pragma once

/**
 * Animation System for lve
 * Header-only implementation
 * Supports keyframe-based animations for translation, rotation, and scale
 * with linear interpolation and automatic blend-back to original state.
 */

#include <glm/glm.hpp>
#include <map>
#include <algorithm>
#include <cmath>

namespace lve {

// Interpolation Helper
enum class Interp { LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT };

inline float interpolate(float t, Interp type) {
  t = std::clamp(t, 0.0f, 1.0f);
  switch (type) {
    case Interp::LINEAR:
      return t;
    case Interp::EASE_IN:
      return t * t;
    case Interp::EASE_OUT:
      return 1.0f - (1.0f - t) * (1.0f - t);
    case Interp::EASE_IN_OUT:
      if (t < 0.5f) {
        return 4.0f * t * t * t;
      } else {
        float f = ((2.0f * t) - 2.0f);
        return 0.5f * f * f * f + 1.0f;
      }
    default:
      return t;
  }
}

// Animation Class - Represents a single animation
class Animation {
 public:
  glm::vec3 translation, rotation, scale;
  float duration, time = 0.0f;
  Interp interp;

  glm::vec3 startT, startR, startS;
  glm::vec3 endT, endR, endS;

  Animation(glm::vec3 st, glm::vec3 sr, glm::vec3 ss,
            glm::vec3 et, glm::vec3 er, glm::vec3 es,
            float dur, Interp i = Interp::LINEAR)
      : startT(st), startR(sr), startS(ss),
        endT(et), endR(er), endS(es),
        translation(st), rotation(sr), scale(ss),
        duration(dur), interp(i) {}

  //updates interpolation and calc curr values
  void update(float dt) {
    time += dt;
    float t = std::min(time / duration, 1.0f);
    float it = interpolate(t, interp);

    translation = startT + (endT - startT) * it;
    rotation = startR + (endR - startR) * it;
    scale = startS + (endS - startS) * it;
  }

  bool done() const { return time >= duration; }
  void reset() { time = 0.0f; }
};

// AnimationController - Manages animations for a game object
class AnimationController {
 public:
  glm::vec3 origT, origR, origS;  // Original transform values
  Animation* current = nullptr;
  Animation activeAnim = Animation(glm::vec3(0), glm::vec3(0), glm::vec3(1),
                                   glm::vec3(0), glm::vec3(0), glm::vec3(1), 1.0f);  // Temp storage for active animation

  std::map<int, Animation> keyAnims;  // Animations triggered by number keys

  bool blending = false;
  float blendTime = 0.0f, blendDur = 0.5f;
  glm::vec3 blendT, blendR, blendS;

  /**
   * Constructor - stores original transform to return to after animation
   */
  AnimationController(glm::vec3 t, glm::vec3 r, glm::vec3 s)
      : origT(t), origR(r), origS(s) {}

  /**
   * Register an animation to be triggered by a number key
   * @param key The key number 1-3 or more
   * @param anim The animation to play when key is pressed
   */
  void registerKey(int key, const Animation& anim) {
    keyAnims.insert_or_assign(key, anim);
  }

  /**
   * Trigger an animation by key number
   * Adjusts animation to use object's current transform as starting point
   * calc start and end values
   * @param key The key number that was pressed
   * @return true if animation exists and was started
   */
  bool trigger(int key) {
    auto it = keyAnims.find(key);
    if (it == keyAnims.end()) return false;

    // Create a copy of the animation and adjust it to current transform
    Animation anim = it->second;

    // Preserve current translation, rotation, and scale as start values
    anim.startT = origT;
    anim.startR = origR;
    anim.startS = origS;

    // Adjust end values relative to current position
    // If the animation changes translation, add the delta to current position
    glm::vec3 translationDelta = it->second.endT - it->second.startT;
    anim.endT = origT + translationDelta;

    // If the animation changes rotation, add the delta to current rotation
    glm::vec3 rotationDelta = it->second.endR - it->second.startR;
    anim.endR = origR + rotationDelta;

    // If the animation changes scale, use ratio
    glm::vec3 scaleRatio = it->second.endS / it->second.startS;
    anim.endS = origS * scaleRatio;

    anim.reset();

    // Store in a temporary variable (we need to keep this alive)
    activeAnim = anim;
    current = &activeAnim;
    blending = false;
    return true;
  }
  /**
   * Update the current animation that is playing
   * every frame with delta time adn interpolation
   * blends back to its original position
   * @param dt Delta time in seconds
   * @param outTranslation Output translation
   * @param outRotation Output rotation
   * @param outScale Output scale
   */
  void update(float dt, glm::vec3& outTranslation, glm::vec3& outRotation, glm::vec3& outScale) {
    outTranslation = origT;
    outRotation = origR;
    outScale = origS;

    if (blending) {
      // Blending back to original state
      blendTime += dt;
      float t = std::min(blendTime / blendDur, 1.0f);
      outTranslation = blendT + (origT - blendT) * t;
      outRotation = blendR + (origR - blendR) * t;
      outScale = blendS + (origS - blendS) * t;

      if (t >= 1.0f) blending = false;
      return;
    }

    if (current) {
      current->update(dt);
      outTranslation = current->translation;
      outRotation = current->rotation;
      outScale = current->scale;

      if (current->done()) {
        // Animation finished, start blending back
        blendT = outTranslation;
        blendR = outRotation;
        blendS = outScale;
        current = nullptr;
        blending = true;
        blendTime = 0.0f;
      }
    }
  }
};

}  // namespace lve