#pragma once

#include <chrono>

namespace dictpen {

class FocusTimerModel {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    enum class State {
        idle,
        running,
        paused,
        completed,
    };

    void select_minutes(int minutes);
    void toggle(TimePoint now = Clock::now());
    void reset();
    void update(TimePoint now = Clock::now());

    int selected_minutes() const;
    int remaining_seconds(TimePoint now = Clock::now()) const;
    int progress_per_mille(TimePoint now = Clock::now()) const;
    State state() const;

private:
    int duration_seconds_ {25 * 60};
    int paused_remaining_seconds_ {25 * 60};
    TimePoint deadline_ {};
    State state_ {State::idle};
};

}  // namespace dictpen
