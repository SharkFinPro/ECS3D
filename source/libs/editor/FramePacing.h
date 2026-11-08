#ifndef FRAMEPACING_H
#define FRAMEPACING_H

#include <VulkanEngine/components/window/PresentMode.h>

[[nodiscard]] inline vke::PresentMode presentModeFor(const bool vsync)
{
  return vsync ? vke::PresentMode::fifo : vke::PresentMode::immediate;
}

[[nodiscard]] inline bool presentModeWaitsForVblank(const vke::PresentMode mode)
{
  return mode == vke::PresentMode::fifo || mode == vke::PresentMode::fifoRelaxed;
}

#endif  // FRAMEPACING_H
