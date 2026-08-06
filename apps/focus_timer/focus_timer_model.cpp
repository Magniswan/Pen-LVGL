#include "focus_timer/focus_timer_model.h"

#include <algorithm>

namespace dictpen {

void FocusTimerModel::select_minutes(int minutes)
{
    if(state_ == State::running || (minutes != 15 && minutes != 25 && minutes != 45)) return;
    duration_seconds_ = minutes * 60;
    paused_remaining_seconds_ = duration_seconds_;
    state_ = State::idle;
}

void FocusTimerModel::toggle(TimePoint now)
{
    if(state_ == State::running) {
        paused_remaining_seconds_ = remaining_seconds(now);
        state_ = State::paused;
        return;
    }
    if(state_ == State::completed) paused_remaining_seconds_ = duration_seconds_;
    deadline_ = now + std::chrono::seconds(paused_remaining_seconds_);
    state_ = State::running;
}

void FocusTimerModel::reset()
{
    paused_remaining_seconds_ = duration_seconds_;
    state_ = State::idle;
}

void FocusTimerModel::update(TimePoint now)
{
    if(state_ == State::running && remaining_seconds(now) <= 0) {
        paused_remaining_seconds_ = 0;
        state_ = State::completed;
    }
}

int FocusTimerModel::selected_minutes() const
{
    return duration_seconds_ / 60;
}

int FocusTimerModel::remaining_seconds(TimePoint now) const
{
    if(state_ != State::running) return paused_remaining_seconds_;
    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(deadline_ - now);
    return std::max(0, static_cast<int>(remaining.count()) +
                           (deadline_ > now && remaining < deadline_ - now ? 1 : 0));
}

int FocusTimerModel::progress_per_mille(TimePoint now) const
{
    if(duration_seconds_ <= 0) return 0;
    const int elapsed = duration_seconds_ - remaining_seconds(now);
    return std::clamp(elapsed * 1000 / duration_seconds_, 0, 1000);
}

FocusTimerModel::State FocusTimerModel::state() const
{
    return state_;
}

}  // namespace dictpen
