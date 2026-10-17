#include "native/media_assembly_dialog.hpp"
#include "native/media_assembly_codec.hpp"

namespace endfield::native {
std::vector<std::string>mediaAssemblyOpenExtensions(){
    return {"png","jpg","jpeg","gif","heic","heif","tif","tiff","bmp","webp","jp2","mov","mp4","m4v","avi","mpeg","mpg"};
}
}
#ifdef _WIN32
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>
namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
std::wstring wide(std::string_view s){return mediaAssemblyPath(s).wstring();}
}
std::optional<std::string>showMediaAssemblyDialog(HWND owner,const MediaAssemblyDialogRequest&r){
    const bool save=r.kind==MediaAssemblyDialogRequest::Kind::save;ComPtr<IFileDialog>dialog;
    if(FAILED(CoCreateInstance(save?CLSID_FileSaveDialog:CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return std::nullopt;
    FILEOPENDIALOGOPTIONS options{};dialog->GetOptions(&options);
    options|=FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR|(save?FOS_OVERWRITEPROMPT|FOS_STRICTFILETYPES:FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST);dialog->SetOptions(options);
    if(!r.title.empty())dialog->SetTitle(wide(r.title).c_str());
    std::wstring spec;for(const auto&e:r.extensions){if(!spec.empty())spec+=L";";spec+=L"*."+wide(e);}
    std::vector<std::wstring>labels;std::vector<COMDLG_FILTERSPEC>filters;
    if(save){labels.reserve(r.extensions.size()*2);for(const auto&e:r.extensions){labels.push_back(wide(e));labels.push_back(L"*."+wide(e));}for(std::size_t i=0;i<r.extensions.size();++i)filters.push_back({labels[i*2].c_str(),labels[i*2+1].c_str()});}
    else if(!spec.empty())filters.push_back({L"",spec.c_str()});
    if(!filters.empty())dialog->SetFileTypes(UINT(filters.size()),filters.data());
    if(save&&!r.extensions.empty())dialog->SetDefaultExtension(wide(r.extensions.front()).c_str());
    if(save&&!r.filename.empty()){
        dialog->SetFileName(wide(r.filename).c_str());
        const auto dot=r.filename.rfind('.');if(dot!=std::string::npos)for(std::size_t i=0;i<r.extensions.size();++i)if(r.filename.substr(dot+1)==r.extensions[i])dialog->SetFileTypeIndex(UINT(i+1));
    }
    if(!r.folder.empty()){ComPtr<IShellItem>folder;if(SUCCEEDED(SHCreateItemFromParsingName(wide(r.folder).c_str(),nullptr,IID_PPV_ARGS(&folder))))dialog->SetFolder(folder.Get());}
    if(FAILED(dialog->Show(owner)))return std::nullopt;
    ComPtr<IShellItem>item;if(FAILED(dialog->GetResult(&item)))return std::nullopt;
    PWSTR path{};if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))||!path)return std::nullopt;
    std::filesystem::path chosen(path);CoTaskMemFree(path);return mediaAssemblyUTF8(chosen);
}
}
#endif
