#include "native/app_shortcut_field_text.hpp"
#include "modules/app_shortcut_model.hpp"
#include <stdexcept>
#ifdef _WIN32
#include <icu.h>
#else
#include <unicode/ubrk.h>
#endif
namespace endfield::native {
std::u16string normalizeShortcutField(std::u16string_view text) {
    if(text.size()>modules::shortcutMaximumBytes/2)throw std::invalid_argument("Shortcut field exceeds source payload bound");
    std::u16string value;value.reserve(text.size());
    for(std::size_t n=0;n<text.size();++n){const auto c=text[n];
        if(c>=0xd800&&c<=0xdbff){if(n+1==text.size()||text[n+1]<0xdc00||text[n+1]>0xdfff)throw std::invalid_argument("Invalid Shortcut UTF16");value.push_back(c);value.push_back(text[++n]);}
        else if(c>=0xdc00&&c<=0xdfff)throw std::invalid_argument("Invalid Shortcut UTF16");
        else value.push_back((c>=0x0a&&c<=0x0d)||c==0x85||c==0x2028||c==0x2029?u' ':c);
    }
    UErrorCode error=U_ZERO_ERROR;auto*iterator=ubrk_open(UBRK_CHARACTER,"",reinterpret_cast<const UChar*>(value.data()),static_cast<int32_t>(value.size()),&error);
    if(U_FAILURE(error)||!iterator){if(iterator)ubrk_close(iterator);throw std::runtime_error("Cannot segment Shortcut name");}
    struct Close{UBreakIterator*p;~Close(){ubrk_close(p);}}close{iterator};
    ubrk_first(iterator);for(unsigned count=0;count<128;++count)if(ubrk_next(iterator)==UBRK_DONE)return value;
    value.resize(static_cast<std::size_t>(ubrk_current(iterator)));return value;
}
}
