#include "FixedTimestep.h"

FixedTimestep::Plan FixedTimestep::advance(const float accumulator, const float dt, const float fixedDt, const int maxSteps)
{
  float remaining = accumulator + dt;

  int steps = 0;
  while (remaining >= fixedDt && steps < maxSteps)
  {
    remaining -= fixedDt;
    ++steps;
  }

  if (steps == maxSteps)
  {
    remaining = 0.0f;
  }

  return { steps, remaining };
}
