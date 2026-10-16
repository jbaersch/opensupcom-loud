#pragma once

#include <algorithm>

namespace osc {

/// Fixed-timestep scheduling for the sim: add this frame's (speed-scaled)
/// time to `accumulator` and return how many whole `step`s to run now, at
/// most `max_steps`. If more were due, the backlog is dropped (keeping only
/// the fractional remainder) so a sim that cannot keep up slows the game
/// down instead of making every frame run ever more ticks -- the "spiral of
/// death" that freezes rendering and input.
inline int consume_fixed_steps(double& accumulator, double frame_dt, double step, int max_steps) {
    accumulator += std::max(frame_dt, 0.0);
    int steps = 0;
    while (accumulator >= step && steps < max_steps) {
        accumulator -= step;
        ++steps;
    }
    if (accumulator >= step) accumulator = std::min(accumulator, step * 0.999);
    return steps;
}

/// CWldSession::SessionFrame (faf-re): one tick after a beat that didn't advance the sim.
inline int session_ticks(double& accumulator, bool& one_tick, double frame_dt, double step,
                         int max_steps) {
    if (one_tick) {
        one_tick = false;
        accumulator = 0.0;
        return 1;
    }
    return consume_fixed_steps(accumulator, frame_dt, step, max_steps);
}

/// Execute the already scheduled ticks within a soft wall-time budget.
/// The first due tick always runs: a tick is indivisible and may itself
/// exceed the budget. `run_tick` returns false at a replay's end; `elapsed`
/// reports seconds since this batch began. The caller has already consumed
/// the scheduled time (session_ticks), so unstarted ticks are not refunded:
/// like the fixed-step cap, overload drops excess wall time, never native
/// tick numbers or commands. The fractional accumulator is untouched.
template <typename Tick, typename Elapsed>
int run_budgeted_ticks(int scheduled, double budget_seconds, Tick&& run_tick, Elapsed&& elapsed) {
    int completed = 0;
    while (completed < scheduled) {
        if (completed > 0 && elapsed() >= budget_seconds) break;
        if (!run_tick()) break;
        ++completed;
    }
    return completed;
}

} // namespace osc
