#pragma once
#include "modules/work_mode.hpp"
#include <functional>
#include <optional>

namespace endfield::modules {
// Source OverlayController wiring between WorkModeController checkpoints and
// UserProfileStore.setWorkSeconds, for a profile that loads asynchronously.
//
// Route WorkModeHooks::trackedWorkSecondsChanged to checkpoint(). Before the
// profile is readable, only the latest controller total is retained. When it
// loads, the stored lifetime is restored once into an idle controller (the
// source path). If Work Mode already counted time first, the stored lifetime
// becomes a fixed offset instead, so no session is lost or counted twice.
// This binding must be the only caller of restoreTrackedWorkSeconds().
//
// persist is normally ProfileState::setWorkSeconds: absolute, monotonic and
// idempotent. A total the profile already holds performs no write; a failed
// write stays dirty in ProfileService and the next checkpoint retries it.
// No timer, thread, IO or clock is owned here; call on the controller's owner.
class WorkModeProfileHours final {
public:
    using Persist=std::function<bool(double absoluteSeconds)>;
    void checkpoint(double controllerTotal);
    void profileLoaded(WorkModeController&,double storedSeconds,Persist);
    // For an owner that cannot restore the controller (it was constructed with
    // a zero restored total and is only readable): the stored lifetime is the
    // fixed offset, exactly the "Work Mode counted first" path above. Never
    // combine with a controller already restored from the same record.
    void profileLoaded(double storedSeconds,Persist);
    // Source PersonalProfileCanvas workSeconds closure value.
    double totalSeconds(const WorkModeController&,double now)const;
    bool loaded()const noexcept{return loaded_;}
    bool restored()const noexcept{return restored_;}
    double offset()const noexcept{return offset_;}
    // Latest absolute total that was not accepted by persist (or not yet loaded).
    std::optional<double>unsaved()const noexcept{return unsaved_;}
    // Orderly shutdown: offer the latest unsaved absolute total again. True
    // when nothing remains unsaved; false before the profile has loaded.
    bool retry();
private:
    Persist persist_;double offset_{};std::optional<double>early_,unsaved_;bool loaded_{},restored_{};
    double absolute(double controllerTotal)const noexcept;void deliver(double total);void load(double,Persist,WorkModeController*);
};
}
