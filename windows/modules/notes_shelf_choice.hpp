#pragma once
// NotesShelfMediaChoice (Sources/NotesShelfMediaPicker.swift, Mac 1.3.0;
// DOCS-251): one File Shelf entry as the media lists show it (the Notes
// centre's "Choose from Shelf" menu, Archive, Media Assembly, Projection and
// the E-Reader "From shelf" lists). Nothing is resolved until one is picked;
// unsupported (folders, other kinds) and unavailable (missing files) entries
// are listed but disabled. In memory only.
#include <string>

namespace endfield::modules {
struct NotesShelfChoice {
    std::string id,title,detail;bool supported{},available{};
    bool operator==(const NotesShelfChoice&)const=default;
};
}
