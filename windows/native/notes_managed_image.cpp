#include "native/notes_managed_image.hpp"
#include "core/data/data_store.hpp"
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace endfield::native {
namespace {
bool validName(std::string_view name){return name.size()==40&&name.ends_with(".png")&&ehud::data::validUUID(name.substr(0,36));}
std::filesystem::path imagesDirectory(const std::filesystem::path& root){return root/"Notes"/"Images";}
}
std::string writeNotesManagedImage(const std::filesystem::path& root,std::span<const std::uint8_t> png){
    if(!root.is_absolute()||png.empty()||png.size()>notesManagedImageMaximumBytes)throw std::invalid_argument("Invalid managed Notes image");
    const auto directory=imagesDirectory(root);std::error_code error;std::filesystem::create_directories(directory,error);
    if(error||std::filesystem::is_symlink(directory)||!std::filesystem::is_directory(directory))throw std::runtime_error("The Notes image folder is unavailable");
    const auto name=ehud::data::makeUUID()+".png";const auto target=directory/name;const auto temporary=directory/("."+name+".tmp");
#ifdef _WIN32
    const auto handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("The Notes image could not be written");
    bool written=true;for(std::size_t offset=0;offset<png.size()&&written;){DWORD count{};const auto requested=static_cast<DWORD>(std::min<std::size_t>(png.size()-offset,1<<20));written=WriteFile(handle,png.data()+offset,requested,&count,nullptr)&&count;offset+=count;}
    written=written&&FlushFileBuffers(handle);CloseHandle(handle);
    if(!written||!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)){DeleteFileW(temporary.c_str());throw std::runtime_error("The Notes image could not be written");}
#else
    throw std::runtime_error("Managed Notes images require Windows");
#endif
    return name;
}
bool removeNotesManagedImage(const std::filesystem::path& root,std::string_view name)noexcept{
    try{if(!validName(name))return false;const auto path=imagesDirectory(root)/std::filesystem::u8path(std::string(name));std::error_code error;
        if(std::filesystem::is_symlink(imagesDirectory(root),error))return false;std::filesystem::remove(path,error);return !error;}catch(...){return false;}
}
} // namespace endfield::native
