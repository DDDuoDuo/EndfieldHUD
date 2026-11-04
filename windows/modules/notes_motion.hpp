#pragma once
#include "core/motion.hpp"

namespace endfield::modules {
struct NotesMotionSample {
    double x{},y{},scale{1},opacity{1};
    bool active{};
};
// NotesCanvas/HUDNotesInteraction timings, evaluated on the owner's existing
// frame clock. No scheduled completion, object lifetime, renderer or I/O here.
// Card scale is around the original layer's center anchor; the caller applies
// that anchor and workspace tilt. Section change cancels other card tracks as
// NotesCanvas.setPresentation does, and only changing (unpinned) cards use it.
NotesMotionSample notesSectionMotion(bool appearing,core::MotionPoint direction,double elapsed,bool reduceMotion=false);
NotesMotionSample notesCardMotion(bool appearing,double elapsed,bool reduceMotion=false);
NotesMotionSample notesMutationMotion(double elapsed,bool reduceMotion=false);
} // namespace endfield::modules
