#pragma once
#include <optional>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace endfield::native {
// Source NSOpenPanel (media extensions) / NSSavePanel (source folder,
// suggestedFilename, allowedFileTypes). Overwrite confirmation is the save
// dialog's own prompt, exactly as NSSavePanel confirms before returning.
struct MediaAssemblyDialogRequest {
    enum class Kind {open,save};
    Kind kind{Kind::open};
    std::string folder,filename,title; // UTF-8; folder/filename for save
    std::vector<std::string>extensions; // without dots
    bool operator==(const MediaAssemblyDialogRequest&)const=default;
};
// Source NotesMediaFactory.supportedFileExtensions.
std::vector<std::string>mediaAssemblyOpenExtensions();
#ifdef _WIN32
// One modal IFileOpenDialog / IFileSaveDialog owned by `owner` on the calling
// STA thread. Returns the chosen UTF-8 path, nullopt when cancelled. Never
// call inside an input/state callback: the owner posts a message first.
std::optional<std::string>showMediaAssemblyDialog(HWND owner,const MediaAssemblyDialogRequest&);
#endif
}
