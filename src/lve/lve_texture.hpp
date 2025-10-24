//
// Created by milot on 10/20/2025.
//

#pragma once

#include "lve_device.hpp"
#include <string.h>
#include <vulkan/vulkan_core.h>

namespace lve {
class Texture {
 public:
  /**
   * encapsulates a complete texture resource
   * like image data, sampler, and view
   * @param device  refrence to the vulkan device wrapper
   * @param filepath filepath Path to the image file (jpg, png, etc)
   */
  Texture(LveDevice &device, const std::string &filepath);
  ~Texture();

  //delete copy constructors since they shouldn't be copied
  Texture(const Texture &) = delete;
  Texture &operator=(const Texture &) = delete;
  Texture(Texture &&) = delete;
  Texture &operator=(Texture &&) = delete;


  VkSampler getSampler() { return sampler; }  //used for texture filtering and wrapping modes
  VkImageView getImageView() { return imageView; } // used to access the image in shaders
  VkImageLayout getImageLayout() { return imageLayout; }  // important for synchronization and pipeline barriers
 private:
  //transition from current to desired layout of  the image
  void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout);
  //improve texture quality at different distances
  // uses linear filtering to downsample each level
  void generateMipmaps();

  int width, height, mipLevels; //w&h in pixels

  LveDevice& lveDevice;       //refrence
  VkImage image;              //
  VkDeviceMemory imageMemory; // memory for image
  VkImageView imageView;      //view int the image for shader access
  VkSampler sampler;          //defining how to read the texture
  VkFormat imageFormat;       //in pixels
  VkImageLayout imageLayout;  //current layout
};
}