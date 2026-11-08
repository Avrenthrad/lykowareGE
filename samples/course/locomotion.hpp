#pragma once
#include <algorithm>
#include <cmath>

// Sample game decisions. Engine pose crossfading does not know about these states.
namespace locomotion {
enum class State { Idle, Walk, Run, Jump, Fall, Land };
inline const char* name(State state)
{
    switch (state) {
    case State::Idle: return "idle";
    case State::Walk: return "walk";
    case State::Run: return "run";
    case State::Jump: return "jump";
    case State::Fall: return "fall";
    case State::Land: return "land";
    }
    return "unknown";
}
class Selector {
public:
    bool advance(float seconds, float speed, bool grounded, float vertical)
    {
        seconds = std::isfinite(seconds) ? std::clamp(seconds, 0.F, .1F) : 0.F;
        speed = std::isfinite(speed) ? std::max(speed, 0.F) : 0.F;
        vertical = std::isfinite(vertical) ? vertical : 0.F;
        const float elapsed = std::min(seconds_ + seconds, 3600.F);
        State next;
        if (!grounded) {
            next = (state_ == State::Jump ? vertical >= -.15F : vertical > .15F) ? State::Jump : State::Fall;
        } else if (observed_ && (state_ == State::Jump || state_ == State::Fall)) {
            next = State::Land;
        } else if (state_ == State::Land && elapsed < .18F) {
            next = State::Land;
        } else if (speed >= (state_ == State::Run ? 4.5F : 5.F)) {
            next = State::Run;
        } else if (speed >= ((state_ == State::Walk || state_ == State::Run) ? .12F : .22F)) {
            next = State::Walk;
        } else { next = State::Idle; }
        const bool changed = !observed_ || next != state_;
        seconds_ = changed ? 0.F : elapsed;
        state_ = next; observed_ = true;
        return changed;
    }
    State state() const { return state_; }
    float seconds() const { return seconds_; }
private:
    State state_ = State::Idle;
    float seconds_ = 0.F;
    bool observed_ = false;
};
}
