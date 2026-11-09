#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>
#include <knownfolders.h>
#include <string>

namespace endfield::native {
// The AppsFolder (Start) item of an AppUserModelID, as `Interface`
// (IShellItem, IShellItem2, IShellItemImageFactory). KF_FLAG_DONT_VERIFY
// returns an item for any name, an unknown one included, so every caller
// checks what it reads from the item. Shared by the Activity app names and
// icons and the Now Playing launch targets. COM must be initialised.
template<class Interface>HRESULT openAppsFolderItem(const std::wstring&appUserModelID,Interface**out)noexcept{
    if(!out)return E_POINTER;*out=nullptr;
    if(appUserModelID.empty()||appUserModelID.find(L'\0')!=std::wstring::npos)return E_INVALIDARG;
    return SHCreateItemInKnownFolder(FOLDERID_AppsFolder,KF_FLAG_DONT_VERIFY,appUserModelID.c_str(),__uuidof(Interface),reinterpret_cast<void**>(out));
}
}
#endif
