#ifndef FRAMEPACING_H
#define FRAMEPACING_H

#include <VulkanEngine/components/window/PresentMode.h>

[[nodiscard]] inline vke::PresentMode presentModeFor(const bool vsync)
{
  return vsync ? vke::PresentMode::fifo : vke::PresentMode::immediate;
}

#endif  // FRAMEPACING_H
