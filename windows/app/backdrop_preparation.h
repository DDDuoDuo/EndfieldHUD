#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ehud::app {

// HUDSourceWatchView owns one pending input, invalidates late results by
// generation, and starts fallback after its independent three-second deadline.
// The host owns capture and starts source clocks only after accept/timeout.
// Call this coordinator on its owning thread; workers carry ticket values only.
class BackdropPreparation {
  public:
    using Ticket = std::uint64_t;
    static constexpr double deadlineSeconds = 3.0;

    Ticket begin(double time) {
        requireFinite(time);
        if (ticket_ == (std::numeric_limits<Ticket>::max)())
            throw std::overflow_error("Backdrop preparation ticket exhausted");
        ++ticket_;
        started_ = time;
        pending_ = true;
        return ticket_;
    }

    void cancel() noexcept {
        // Never wrap and reuse an old ticket, even if the counter is exhausted.
        if (ticket_ != (std::numeric_limits<Ticket>::max)())
            ++ticket_;
        pending_ = false;
    }

    bool accept(Ticket ticket) noexcept {
        if (!pending_ || ticket != ticket_)
            return false;
        pending_ = false;
        return true;
    }

    bool timed_out(double time) {
        requireFinite(time);
        if (!pending_ || time < started_ || time - started_ < deadlineSeconds)
            return false;
        cancel();
        return true;
    }

    bool pending() const noexcept { return pending_; }
    Ticket ticket() const noexcept { return ticket_; }

  private:
    static void requireFinite(double time) {
        if (!std::isfinite(time))
            throw std::invalid_argument("Backdrop preparation requires a finite clock");
    }

    Ticket ticket_{};
    double started_{};
    bool pending_{};
};

} // namespace ehud::app
