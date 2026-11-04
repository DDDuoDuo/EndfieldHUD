#include "tools/archive_options.hpp"
#include "native/notes_image_decoder.hpp"
#include "modules/notes_media_presentation.hpp"

namespace endfield::tools {
std::string notesMediaErrorDescription(HRESULT result,core::Language language){
    const auto text=modules::notesMediaErrorText(native::notesMediaError(result));return core::localized(text.english,text.simplifiedChinese,language);
}
}
