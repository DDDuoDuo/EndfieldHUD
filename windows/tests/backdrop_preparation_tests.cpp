#include "../app/backdrop_preparation.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using ehud::app::BackdropPreparation;
using Ticket = BackdropPreparation::Ticket;
int checks{};

void check(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <class Action> void rejects_clock(Action action, const char *message) {
    bool rejected = false;
    try {
        action();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, message);
}

// A fake asynchronous provider completes explicit tickets. Presentation and
// source clocks are host effects, so their counts can detect a stale reopen or
// a deadline/completion restarting a clock without any capture or real timers.
struct FakeHost {
    struct Start {
        double time;
        bool fallback;
    };
    BackdropPreparation preparation;
    std::vector<Ticket> requested;
    std::vector<Start> sourceStarts;
    bool visible{};

    void toggle(double time) {
        if (preparation.pending() || visible) {
            preparation.cancel();
            visible = false;
            return;
        }
        requested.push_back(preparation.begin(time));
    }

    bool complete(Ticket ticket, double time) {
        if (!preparation.accept(ticket))
            return false;
        sourceStarts.push_back({time, false});
        visible = true;
        return true;
    }

    bool advance(double time) {
        if (!preparation.timed_out(time))
            return false;
        sourceStarts.push_back({time, true});
        visible = true;
        return true;
    }
};

void delayed_ready_and_cancelled_input() {
    FakeHost host;
    check(!host.preparation.pending() && host.preparation.ticket() == 0,
          "Initial coordinator owns no pending input");
    check(!host.complete(0, 0), "The initial query ticket cannot start presentation");
    host.toggle(10);
    const auto first = host.requested.back();
    check(host.preparation.pending() && !host.visible && host.sourceStarts.empty(),
          "A requested input holds presentation and consumes no source animation time");
    check(!host.advance(10.9) && host.sourceStarts.empty(),
          "Preparation latency shorter than the deadline keeps the original pose held");
    check(host.complete(first, 11.1), "The matching delayed result starts presentation");
    check(host.sourceStarts.size() == 1 && host.sourceStarts[0].time == 11.1 &&
              !host.sourceStarts[0].fallback,
          "The source clock begins at ready time rather than at capture request time");
    check(!host.complete(first, 11.2) && !host.advance(13.1) && host.sourceStarts.size() == 1,
          "Duplicate ready and a later deadline cannot restart accepted source clocks");
    host.toggle(11.3);
    check(!host.visible && !host.complete(first, 11.4),
          "A closed presentation cannot reopen from an accepted old result");

    host.toggle(20);
    const auto cancelled = host.requested.back();
    host.toggle(20.15);
    check(!host.preparation.pending() && !host.visible && host.sourceStarts.size() == 1,
          "A second toggle cancels a held opening without starting its deployed exit pose");
    check(!host.complete(cancelled, 20.2) && !host.advance(24),
          "Completion and timeout after held cancellation have no presentation effect");
    host.toggle(25);
    const auto current = host.requested.back();
    check(current != cancelled && !host.complete(cancelled, 25.1) && host.preparation.pending(),
          "A cancelled provider callback cannot consume the new opening");
    check(host.complete(current, 25.2) && host.sourceStarts.size() == 2,
          "The new generation remains independently consumable exactly once");
}

void timeout_and_boundary() {
    FakeHost host;
    host.toggle(10);
    const auto ticket = host.requested.back();
    const double boundary = 13;
    check(!host.advance(std::nextafter(boundary, 0.0)) && host.preparation.pending(),
          "The representable instant before the exact three-second boundary stays pending");
    check(host.advance(boundary) && !host.preparation.pending() && host.visible,
          "The exact deadline consumes pending input and starts fallback");
    check(host.preparation.ticket() != ticket && host.sourceStarts.size() == 1 &&
              host.sourceStarts[0].fallback && host.sourceStarts[0].time == boundary,
          "Deadline invalidation precedes the fallback source clock");
    check(!host.complete(ticket, boundary) && !host.advance(boundary + 100) &&
              host.sourceStarts.size() == 1,
          "Late provider completion and repeated timeout cannot restart fallback");
    host.toggle(14);
    host.toggle(15);
    const auto reopened = host.requested.back();
    check(!host.complete(ticket, 15.1) && host.preparation.pending(),
          "A timed-out result cannot consume a later reopened preparation");
    check(!host.advance(17.9) && host.complete(reopened, 17.9),
          "Reopening owns a new independent deadline");
}

void invalid_clocks_and_supersession() {
    BackdropPreparation preparation;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (double value : {nan, inf, -inf}) {
        rejects_clock([&] { preparation.begin(value); }, "Nonfinite request time is rejected");
        check(!preparation.pending() && preparation.ticket() == 0,
              "A rejected initial clock cannot allocate a ticket");
        rejects_clock([&] { preparation.timed_out(value); },
                      "An idle deadline query still rejects nonfinite time");
    }
    const auto first = preparation.begin(100);
    for (double value : {nan, inf, -inf}) {
        rejects_clock([&] { preparation.begin(value); },
                      "Invalid replacement time cannot supersede a valid provider");
        rejects_clock([&] { preparation.timed_out(value); },
                      "Invalid deadline time cannot trigger fallback");
        check(preparation.pending() && preparation.ticket() == first,
              "Rejected clocks preserve the original pending generation");
    }
    check(!preparation.timed_out(99) && !preparation.timed_out(102.9),
          "A pre-start clock sample cannot expire or move the deadline");
    const auto replacement = preparation.begin(102);
    check(replacement > first && !preparation.accept(first) && preparation.pending(),
          "Beginning another input supersedes the old ticket without two pending owners");
    check(!preparation.timed_out(103) && preparation.timed_out(105),
          "Supersession resets the deadline to the replacement request time");
    const auto afterTimeout = preparation.ticket();
    preparation.cancel();
    check(!preparation.pending() && preparation.ticket() > afterTimeout,
          "Cancellation invalidates a generation even with no pending result");
    check(!preparation.accept(first) && !preparation.accept(replacement),
          "Neither replaced nor timed-out callbacks remain valid");

    const auto negative = preparation.begin(-10);
    check(!preparation.timed_out(-7.1) && preparation.accept(negative),
          "Finite relative clocks may have negative origins");
    preparation.begin(-(std::numeric_limits<double>::max)());
    check(preparation.timed_out((std::numeric_limits<double>::max)()),
          "A finite enormous elapsed interval expires without overflowing an absolute deadline");
    const auto maximumTime = preparation.begin((std::numeric_limits<double>::max)());
    check(!preparation.timed_out((std::numeric_limits<double>::max)()) &&
              preparation.accept(maximumTime),
          "A finite maximum request time is not expired by a rounded absolute deadline");
}

void reused_owner() {
    BackdropPreparation preparation;
    Ticket prior{};
    for (unsigned opening = 0; opening < 32; ++opening) {
        const double now = 1000 + opening * 10;
        const auto current = preparation.begin(now);
        check(current > prior && !preparation.accept(prior) && preparation.pending(),
              "Every reuse preserves ordered independent generation ownership");
        check(!preparation.accept(current + 1) && preparation.pending(),
              "A future ticket cannot consume the current input");
        if (opening % 3 == 0) {
            preparation.cancel();
        } else if (opening % 3 == 1) {
            check(preparation.accept(current), "The current ready result is accepted once");
        } else {
            check(preparation.timed_out(now + 3), "The current deadline is consumed once");
        }
        check(!preparation.pending() && !preparation.accept(current) &&
                  !preparation.timed_out(now + 20),
              "Reuse leaves no hidden pending work after ready, cancel or fallback");
        prior = preparation.ticket();
    }
}
} // namespace

int main() {
    try {
        delayed_ready_and_cancelled_input();
        timeout_and_boundary();
        invalid_clocks_and_supersession();
        reused_owner();
        std::cout << "PASS: " << checks
                  << " isolated backdrop preparation contracts; delayed ready, cancelled opening, "
                     "bounded timeout and rejected late completion. No capture APIs.\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
