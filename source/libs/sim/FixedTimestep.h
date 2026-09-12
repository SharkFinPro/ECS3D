#ifndef FIXEDTIMESTEP_H
#define FIXEDTIMESTEP_H

// The fixed-timestep catch-up math behind ServerApp's run loop, pulled out so it can be tested without a
// live server/network stack.
class FixedTimestep {
public:
  struct Plan {
    int steps = 0;
    float remainingAccumulator = 0.0f;
  };

  // accumulator/dt/fixedDt are seconds already banked / just elapsed / one fixed step. A call never
  // reports more than maxSteps: a stall (GC pause, breakpoint, OS scheduling hiccup) that banks far more
  // time than that has the excess dropped rather than carried into the next call - otherwise the backlog
  // would keep hitting the same cap and replay the whole stall as a burst of full-speed ticks once
  // whatever caused it clears.
  [[nodiscard]] static Plan advance(float accumulator, float dt, float fixedDt, int maxSteps);
};

#endif //FIXEDTIMESTEP_H
