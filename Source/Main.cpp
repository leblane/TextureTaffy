#include <iostream>
#include <string>
#include <map>
#include <tuple>
#include <algorithm>
#include <vulkan/vulkan.hpp>
#include <math.h>
#include <thread>
#include <mutex>
#include <numeric>
#include <fstream>
#include <iomanip>
#include <vector>
#include "stb_image_resize.h"
#include "stb_image.h"
#include "dfd.h"
#include "ispc_texcomp/ispc_texcomp.h"
#include "HalfFloat.h"

const std::vector<std::string> formatOrder = {
  "BC1",
  "BC1_SRGB",
  "BC4",
  "BC5",
  "BC3",
  "BC3_SRGB",
  "BC6H",
  "BC7",
  "BC7_SRGB"
};

const std::map<std::string, std::tuple<std::string, int, vk::Format>> formats = {
  {"BC1", {"(DXT1) 5:6:5 Color, 1 bit alpha. 8 bytes per block.", 8, vk::Format::eBc1RgbUnormBlock}},
  {"BC1_SRGB", {"(DXT1) 5:6:5 Color, 1 bit alpha. 8 bytes per block.", 8, vk::Format::eBc1RgbSrgbBlock}},
  {"BC4", {"Greyscale, 8 bytes per block.", 8, vk::Format::eBc4UnormBlock}},
  {"BC5", {"2x BC4 images. 16 bytes per block.", 16, vk::Format::eBc5UnormBlock}},
  {"BC3", {"(DXT5) BC1 Color, BC4 Alpha, 16 bytes per block.", 16, vk::Format::eBc3UnormBlock}},
  {"BC3_SRGB", {"(DXT5) BC1 Color, BC4 Alpha, 16 bytes per block.", 16, vk::Format::eBc3SrgbBlock}},
  {"BC6H", {"16 bit RGB, no alpha. Signed. 16 bytes per block.", 16, vk::Format::eBc6HUfloatBlock}},
  {"BC7", {"8 bit RGBA - Good general purpose. 16 bytes per block.", 16, vk::Format::eBc7UnormBlock}},
  {"BC7_SRGB", {"8 bit RGBA - Good general purpose. 16 bytes per block.", 16, vk::Format::eBc7SrgbBlock}}
};

const std::string usage = "Usage: TextureConverter [cube|array] <input> [input2, input3...] <output> <format> [fast|slow|veryslow]";

int main(int argc, char ** argv)
{
  ISPCInit();

  if (argc < 4) {
    std::cout << usage << std::endl;
    std::cout << "Formats:" << std::endl;
    for (auto & formatName : formatOrder) {
      auto format = formats.at(formatName);
      std::cout << "  " << formatName << " - " << std::get<0>(format) << std::endl;
    }
    return 1;
  }

  int numInputs = argc - 3;
  unsigned int inputsStart = 1;
  std::vector<std::string> inputs;
  std::string output;
  std::string option(argv[1]);
  if (option == "cube" || option == "array") {
    numInputs -= 1;
    inputsStart += 1;
  } else {
    option = "none";
  }

  std::string speed(argv[argc - 1]);
  std::string formatString;
  bool fastMode = true;
  bool verySlow = false;
  if (speed == "fast" || speed == "slow" || speed == "veryslow") {
    formatString = std::string(argv[argc - 2]);
    numInputs -= 1;

    if (speed == "slow") {
      fastMode = false;
    } else if (speed == "veryslow") {
      fastMode = false;
      verySlow = true;
    }
  } else {
    formatString = std::string(argv[argc - 1]);
  }

  if (numInputs < 1) {
    std::cout << usage << std::endl;
    return 1;
  }

  if (option == "cube" && numInputs != 6) {
    std::cout << "Cube maps must have 6 inputs." << std::endl;
    return 1;
  }

  if (option == "array" && numInputs < 2) {
    std::cout << "Array maps must have at least 2 inputs." << std::endl;
    return 1;
  }

  if (formats.find(formatString) == formats.end()) {
    std::cout << "Invalid format: " << formatString << std::endl;
    std::cout << usage << std::endl;
    std::cout << "Formats:" << std::endl;
    for (auto & formatName : formatOrder) {
      auto format = formats.at(formatName);
      std::cout << "  " << formatName << " - " << std::get<0>(format) << std::endl;
    }
    return 1;
  }

  /* Check if it ends in SRGB */
  bool srgb = false;
  if (formatString.length() > 5 && formatString.substr(formatString.length() - 5, 5) == "_SRGB") {
    srgb = true;
  }

  bool hdr = false;
  if (formatString.substr(0, 3) == "BC6") {
    hdr = true;
  }

  for (int i = inputsStart; i < (int)(inputsStart + numInputs); i++) {
    inputs.push_back(std::string(argv[i]));
  }

  output = std::string(argv[inputsStart + numInputs]);

  /* Print inputs and output */
  std::cout << "Inputs: " << std::endl;
  for (auto & input : inputs) {
    std::cout << "  " << input << std::endl;
  }
  std::cout << "Output: " << output << std::endl;
  std::cout << "Format: " << formatString << std::endl;
  std::cout << "Speed: " << (fastMode ? "Fast" : verySlow ? "Very slow" : "Slow") << std::endl;

  int isa;
  isa = ISPCIsa();

  std::string isaName;
  switch(isa) {
    case 0:
      isaName = "SSE2";
      break;
    case 1:
      isaName = "SSE4";
      break;
    case 2:
      isaName = "AVX2";
      break;
    default:
      isaName = "Unknown";
  };

  std::cout << "ISPC ISA: " << isaName << std::endl;

  unsigned char * ldrBufferA, * ldrBufferB, * ldrBufferMain, * ldrBufferOther;
  float * hdrBufferA, * hdrBufferB, * hdrBufferMain, * hdrBufferOther;
  int width, height, channels;

  int copyChannels = 4;
  int forcedChannels = 4;
  if (formatString == "BC4") {
    copyChannels = 1;
  } else if (formatString == "BC5") {
    copyChannels = 2;
  }

  std::vector<std::vector<std::vector<unsigned char>>> ldrLevels;
  std::vector<std::vector<std::vector<float>>> hdrLevels;

  std::vector<std::vector<std::vector<std::vector<unsigned char>>>> ldrLevelBlocks;
  std::vector<std::vector<std::vector<std::vector<uint16_t>>>> hdrLevelBlocks;

  if (hdr) {
    hdrLevels.resize(numInputs);
    hdrLevelBlocks.resize(numInputs);
  } else {
    ldrLevels.resize(numInputs);
    ldrLevelBlocks.resize(numInputs);
  }

  uint32_t levelCount;

  for (int input = 0; input < numInputs; input++) {
    int level = 0;

    std::cout << "Loading/scaling " << input << ": " << inputs[input] << std::endl;

    if (hdr) {
      hdrBufferA = stbi_loadf(inputs[input].c_str(), &width, &height, &channels, forcedChannels);
      if (hdrBufferA == nullptr) {
        std::cout << "Failed to load image: " << inputs[input] << std::endl;
        return 1;
      }

      hdrBufferB = new float[width * height * forcedChannels];
      hdrBufferMain = hdrBufferA;
      hdrBufferOther = hdrBufferB;
    } else {
      ldrBufferA = stbi_load(inputs[input].c_str(), &width, &height, &channels, forcedChannels);
      if (ldrBufferA == nullptr) {
        std::cout << "Failed to load image: " << inputs[input] << std::endl;
        return 1;
      }

      ldrBufferB = new unsigned char[width * height * forcedChannels];
      ldrBufferMain = ldrBufferA;
      ldrBufferOther = ldrBufferB;
    }

    int oldWidth = width;
    int oldHeight = height;

    levelCount = 1;
    {
      int levelWidth = width;
      int levelHeight = height;
      while (levelWidth > 1 || levelHeight > 1) {
        levelWidth = std::max(1, (int)floorf((float)levelWidth / 2));
        levelHeight = std::max(1, (int)floorf((float)levelHeight / 2));
        levelCount++;
      }
    }
    
    while(1) {
      if (hdr) {
        hdrLevels[input].push_back(std::vector<float>(hdrBufferMain, hdrBufferMain + oldWidth * oldHeight * forcedChannels));
      } else {
        ldrLevels[input].push_back(std::vector<uint8_t>(ldrBufferMain, ldrBufferMain + oldWidth * oldHeight * forcedChannels));
      }

      if (oldWidth == 1 && oldHeight == 1) {
        break;
      }

      int newWidth = std::max(1, (int)floorf((float)oldWidth / 2));
      int newHeight = std::max(1, (int)floorf((float)oldHeight / 2));

      stbir_colorspace colorspace = srgb ? STBIR_COLORSPACE_SRGB : STBIR_COLORSPACE_LINEAR;
      int alphaChannel = channels == 4 ? 3 : STBIR_ALPHA_CHANNEL_NONE;

      if (hdr) {
        int rv = stbir_resize_float_generic(hdrBufferMain, oldWidth, oldHeight, 0, hdrBufferOther, newWidth, newHeight, 0, forcedChannels, alphaChannel, 0, STBIR_EDGE_CLAMP, STBIR_FILTER_MITCHELL, colorspace, nullptr);
        if (rv != 1) {
          std::cerr << "Error resizing" << std::endl;
        }
        std::swap(hdrBufferMain, hdrBufferOther);
      } else {
        int rv = stbir_resize_uint8_generic(ldrBufferMain, oldWidth, oldHeight, 0, ldrBufferOther, newWidth, newHeight, 0, forcedChannels, alphaChannel, 0, STBIR_EDGE_CLAMP, STBIR_FILTER_MITCHELL, colorspace, nullptr);
        if (rv != 1) {
          std::cerr << "Error resizing" << std::endl;
        }
        std::swap(ldrBufferMain, ldrBufferOther);
      }

      oldWidth = newWidth;
      oldHeight = newHeight;
      level++;
    }

    oldWidth = width;
    oldHeight = height;
    level = 0;
    if (hdr) {
      free(hdrBufferA);
       delete[] hdrBufferB;
    } else {
      free(ldrBufferA);
      delete[] ldrBufferB;
    }
  }

  bc6h_enc_settings bc6henc;
  bc7_enc_settings bc7enc;

  if (formatString == "BC6H") {
    if (fastMode) {
      GetProfile_bc6h_basic(&bc6henc);
    } else {
      if (verySlow) {
        GetProfile_bc6h_veryslow(&bc6henc);
      } else {
        GetProfile_bc6h_slow(&bc6henc);
      }
    }
  } else {
    if (channels == 3) {
      if (fastMode) {
        GetProfile_basic(&bc7enc);
      } else {
        GetProfile_slow(&bc7enc);
      }
    } else {
      if (fastMode) {
        GetProfile_alpha_basic(&bc7enc);
      } else {
        GetProfile_alpha_slow(&bc7enc);
      }
    }
  }

  std::vector<std::vector<std::vector<unsigned char>>> levelBlocksCompressed(numInputs);
  std::tuple<std::string, int, vk::Format> format = formats.find(formatString)->second;
  size_t blockSize = std::get<1>(format);

  for (int input = 0; input < numInputs; input++) {
    if (numInputs > 1) {
      if (option == "cube") {
        std::cout << "Face " << input << std::endl;
      } else {
        std::cout << "Layer " << input << std::endl;
      }
    }

    if (hdr) {
      hdrLevelBlocks[input].resize(levelCount);
    } else {
      ldrLevelBlocks[input].resize(levelCount);
    }
    int level = 0;

    int oldWidth = width;
    int oldHeight = height;

    while(1) {
      unsigned int blocksWidth = (oldWidth + 3) / 4;
      unsigned int blocksHeight = (oldHeight + 3) / 4;

      if (hdr) {
        hdrLevelBlocks[input][level].resize(blocksWidth * blocksHeight);

        for (unsigned int y = 0; y < blocksHeight; y++) {
          for (unsigned int x = 0; x < blocksWidth; x++) {
            std::vector<uint16_t> block(16 * copyChannels);

            for (unsigned int pixelY = y * 4; pixelY < y * 4 + 4; pixelY++) {
              for (unsigned int pixelX = x * 4; pixelX < x * 4 + 4; pixelX++) {
                unsigned int clampedY = std::min(pixelY, (unsigned int)oldHeight - 1);
                unsigned int clampedX = std::min(pixelX, (unsigned int)oldWidth - 1);

                for (int channel = 0; channel < copyChannels; channel++) {
                  float value = hdrLevels[input][level][(clampedY * oldWidth + clampedX) * forcedChannels + channel];
                  if (value < 0.0f) {
                    value = 0.0f;
                  }

                  if (value > 65504.0f) {
                    value = 65504.0f;
                  }

                  block[((pixelY % 4) * 4 + (pixelX % 4)) * copyChannels + channel] = HalfFloat::FromFloat(value);
                }
              }
            }

            hdrLevelBlocks[input][level][blocksWidth * y + x] = block;
          }
        }
      } else {
        ldrLevelBlocks[input][level].resize(blocksWidth * blocksHeight);

        for (unsigned int y = 0; y < blocksHeight; y++) {
          for (unsigned int x = 0; x < blocksWidth; x++) {
            std::vector<uint8_t> block(16 * copyChannels);

            for (unsigned int pixelY = y * 4; pixelY < y * 4 + 4; pixelY++) {
              for (unsigned int pixelX = x * 4; pixelX < x * 4 + 4; pixelX++) {
                unsigned int clampedY = std::min(pixelY, (unsigned int)oldHeight - 1);
                unsigned int clampedX = std::min(pixelX, (unsigned int)oldWidth - 1);

                for (int channel = 0; channel < copyChannels; channel++) {
                  block[((pixelY % 4) * 4 + (pixelX % 4)) * copyChannels + channel] = ldrLevels[input][level][(clampedY * oldWidth + clampedX) * forcedChannels + channel];
                }
              }
            }

            ldrLevelBlocks[input][level][blocksWidth * y + x] = block;
          }
        }
      }

      if (oldWidth == 1 && oldHeight == 1) {
        break;
      }

      oldWidth = std::max(1, (int)floorf((float)oldWidth / 2));
      oldHeight = std::max(1, (int)floorf((float)oldHeight / 2));      
      level++;
    }


    /* Compress */
    levelBlocksCompressed[input].resize(levelCount);
    for (unsigned int l = 0; l < levelCount; l++) {
      if (hdr) {
        levelBlocksCompressed[input][l].resize(hdrLevelBlocks[input][l].size() * blockSize);
      } else {
        levelBlocksCompressed[input][l].resize(ldrLevelBlocks[input][l].size() * blockSize);
      }
    }

    std::mutex mutex;
    std::vector<unsigned int> completedBlocks(levelCount, 0);

    unsigned int numThreads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    unsigned int maxLevel = 0;
    for (unsigned t = 0; t < numThreads; t++) {
      threads.push_back(std::thread([&, t](){
        for (unsigned int l = 0; l < levelCount; l++) {
          unsigned int blocksPerThread;
          if (hdr) {
            blocksPerThread = hdrLevelBlocks[input][l].size() / numThreads;
          } else {
            blocksPerThread = ldrLevelBlocks[input][l].size() / numThreads;
          }
          unsigned int startBlock = t * blocksPerThread;
          unsigned int endBlock = startBlock + blocksPerThread;

          if (hdr) {
            if (t == numThreads - 1) {
              endBlock = hdrLevelBlocks[input][l].size();
            }
          } else {
            if (t == numThreads - 1) {
              endBlock = ldrLevelBlocks[input][l].size();
            }
          }

          for (unsigned int b = startBlock; b < endBlock; b++) {
            if (formatString == "BC6H") {
              rgba_surface surface;
              surface.ptr = (uint8_t *)hdrLevelBlocks[input][l][b].data();
              surface.width = 4;
              surface.height = 4;
              surface.stride = copyChannels * 4 * 2;

              CompressBlocksBC6H(&surface, &levelBlocksCompressed[input][l][b * blockSize], &bc6henc);
            } else {
              rgba_surface surface;
              surface.ptr = ldrLevelBlocks[input][l][b].data();
              surface.width = 4;
              surface.height = 4;
              surface.stride = copyChannels * 4;

              if (formatString == "BC1" || formatString == "BC1_SRGB") {
                CompressBlocksBC1(&surface, &levelBlocksCompressed[input][l][b * blockSize]);
              } else if (formatString == "BC3" || formatString == "BC3_SRGB") {
                CompressBlocksBC3(&surface, &levelBlocksCompressed[input][l][b * blockSize]);
              } else if (formatString == "BC4") {
                CompressBlocksBC4(&surface, &levelBlocksCompressed[input][l][b * blockSize]);
              } else if (formatString == "BC5") {
                CompressBlocksBC5(&surface, &levelBlocksCompressed[input][l][b * blockSize]);
              } else if (formatString == "BC7" || formatString == "BC7_SRGB") {
                CompressBlocksBC7(&surface, &levelBlocksCompressed[input][l][b * blockSize], &bc7enc);
              }
            }

            std::lock_guard<std::mutex> lock(mutex);
            completedBlocks[l]++;
            if (completedBlocks[l] % 100 == 0) {
              maxLevel = std::max(l, maxLevel);
              if (l == maxLevel) {
                float progress;
                if (hdr) {
                  progress = (float)completedBlocks[l] / hdrLevelBlocks[input][l].size();
                } else {
                  progress = (float)completedBlocks[l] / ldrLevelBlocks[input][l].size();
                }
                int barWidth = 70;

                std::cout << std::setw(2) << l << " [";
                int pos = barWidth * progress;
                for (int i = 0; i < barWidth; ++i) {
                    if (i < pos) std::cout << "=";
                    else if (i == pos) std::cout << ">";
                    else std::cout << " ";
                }
                std::cout << "] " << std::setw(2) << int(progress * 100.0) << " %\r";
                std::cout.flush();
              }
            }
          }
        }
      }));
    }

    for (unsigned t = 0; t < numThreads; t++) {
      threads[t].join();
    }

    if (hdr) {
      hdrLevelBlocks[input].clear();
    } else {
      ldrLevelBlocks[input].clear();
    }

    int barWidth = 70;
    std::cout << std::setw(2) << (levelCount - 1) << " [";
    int pos = barWidth;
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << std::setw(2) << 100 << " %\r";
    std::cout.flush();

    std::cout << std::endl;
  }

  /* Write KTX2 */
  std::ofstream fh (output, std::ios::out | std::ios::binary);
  if (!fh.is_open()) {
    std::cout << "Failed to open output file: " << output << std::endl;
    return 1;
  }

  const uint8_t identifier[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};  
  fh.write((char *)identifier, sizeof(identifier));
  vk::Format vkformat = std::get<2>(formats.at(formatString));
  fh.write((char *)&vkformat, sizeof(vkformat));
  uint32_t typeSize = 1; // Fix for uncompressed vkformats, size of an individual component
  fh.write((char *)&typeSize, sizeof(typeSize));
  uint32_t pixelWidth = width;
  uint32_t pixelHeight = height;
  uint32_t pixelDepth = 0;

  uint32_t layerCount;
  uint32_t faceCount;
  if (numInputs > 1) {
    if (option == "cube") {
      layerCount = 0;
      faceCount = 6;
    } else {
      layerCount = numInputs;
      faceCount = 1;
    }
  } else {
    layerCount = 0;
    faceCount = 1;
  }
  uint32_t supercompressionScheme = 0;
  fh.write((char *)&pixelWidth, sizeof(pixelWidth));
  fh.write((char *)&pixelHeight, sizeof(pixelHeight));
  fh.write((char *)&pixelDepth, sizeof(pixelDepth));
  fh.write((char *)&layerCount, sizeof(layerCount));
  fh.write((char *)&faceCount, sizeof(faceCount));
  fh.write((char *)&levelCount, sizeof(levelCount));
  fh.write((char *)&supercompressionScheme, sizeof(supercompressionScheme));

  uint32_t * dfd = vk2dfd(*(VkFormat *)&vkformat);

  uint32_t dfdByteOffset = 0;
  uint32_t dfdByteLength = dfd[0];
  uint32_t kvdByteOffset = 0;
  uint32_t kvdByteLength = 0;
  uint64_t sgdByteOffset = 0;
  uint64_t sgdByteLength = 0;

  auto dfdByteOffsetPosition = fh.tellp();

  fh.write((char *)&dfdByteOffset, sizeof(dfdByteOffset));
  fh.write((char *)&dfdByteLength, sizeof(dfdByteLength));
  fh.write((char *)&kvdByteOffset, sizeof(kvdByteOffset));
  fh.write((char *)&kvdByteLength, sizeof(kvdByteLength));
  fh.write((char *)&sgdByteOffset, sizeof(sgdByteOffset));
  fh.write((char *)&sgdByteLength, sizeof(sgdByteLength));

  auto levelOffsetBytePosition = fh.tellp();
  for (unsigned int i = 0; i < levelCount; i++) {
    uint64_t byteOffset = 0;
    uint64_t byteLength = 0;
    uint64_t uncompressedByteLength = 0;
    fh.write((char *)&byteOffset, sizeof(byteOffset));
    fh.write((char *)&byteLength, sizeof(byteLength));
    fh.write((char *)&uncompressedByteLength, sizeof(uncompressedByteLength));
  }

  dfdByteOffset = fh.tellp();
  fh.write((char *)dfd, dfdByteLength);
  free(dfd);

  kvdByteOffset = fh.tellp();
  fh.seekp(dfdByteOffsetPosition);
  fh.write((char *)&dfdByteOffset, sizeof(dfdByteOffset));
  fh.seekp(kvdByteOffset);

  size_t alignment = std::lcm((size_t)4, (size_t)std::get<1>(formats.at(formatString)));

  for (int level = levelCount - 1; level >= 0; level--) {
    // Alignment
    while (fh.tellp() % alignment != 0) {
      uint8_t padding = 0;
      fh.write((char *)&padding, sizeof(padding));
    }

    auto levelBytePosition = fh.tellp();
    fh.seekp(levelOffsetBytePosition + (std::ofstream::pos_type)(level * 24));
    uint64_t byteOffset = levelBytePosition;
    fh.write((char *)&byteOffset, sizeof(byteOffset));
    uint64_t byteLength = levelBlocksCompressed[0][level].size() * numInputs;
    fh.write((char *)&byteLength, sizeof(byteLength));
    fh.write((char *)&byteLength, sizeof(byteLength));
    fh.seekp(levelBytePosition);

    for (int input = 0; input < numInputs; input++) {
      fh.write((char *)levelBlocksCompressed[input][level].data(), levelBlocksCompressed[input][level].size());
    }
  }

  return 0;
}