#include "modules/reader_repository.hpp"
#include "core/data/file_io.hpp"

namespace endfield::modules {
ReaderJSONRepository::ReaderJSONRepository(std::filesystem::path directory,ReaderPreferences defaults):directory_(std::move(directory)),path_(directory_/"library.json"),defaults_(std::move(defaults)){
    if(!directory_.is_absolute()||!defaults_.valid())throw ReaderError(ReaderErrorCode::invalidBook);
}
void ReaderJSONRepository::onOwner(){if(!owner_)owner_=std::this_thread::get_id();else if(*owner_!=std::this_thread::get_id())throw std::logic_error("Reader repository belongs to one FIFO file executor");}
ReaderLibrary ReaderJSONRepository::load(){onOwner();if(loaded_)return value_;ehud::data::detail::validateRoot(directory_);auto raw=ehud::data::detail::readFile(path_,readerMaximumLibraryBytes);ReaderLibrary value;value.preferences=defaults_;if(raw)value=decodeReaderLibrary(ReaderJson::parse(*raw,readerMaximumLibraryBytes));value_=std::move(value);persisted_=std::move(raw);loaded_=true;return value_;}
void ReaderJSONRepository::save(const ReaderLibrary&value){onOwner();if(!loaded_)(void)load();const auto encoded=encodeReaderLibrary(value).encode(readerMaximumLibraryBytes);if(value==value_)return;
    // Allocate candidate state before replacing the durable file. A throwing
    // copy cannot leave the repository's expected-byte baseline out of sync.
    auto next=value;std::optional<std::string>baseline(encoded);
    ehud::data::detail::replaceFile(path_,persisted_,encoded,readerMaximumLibraryBytes);
    value_=std::move(next);persisted_=std::move(baseline);
}
} // namespace endfield::modules
