#include "native/audio_application_name.hpp"
#include <algorithm>
#include <cwctype>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <wrl/client.h>
#if defined(_MSC_VER)
#pragma comment(lib,"xmllite.lib")
#pragma comment(lib,"shlwapi.lib")
#endif
#endif

namespace endfield::native {
namespace {
constexpr std::size_t maximumText=4096;
bool identityText(std::wstring_view v)noexcept{
    if(v.empty()||v.size()>maximumText)return false;
    return std::none_of(v.begin(),v.end(),[](wchar_t c){return c==L'\0'||c==L'{'||c==L'}'||c==L'?'||c==L'@'||std::iswspace(static_cast<wint_t>(c));});
}
bool containsResources(std::wstring_view key)noexcept{
    constexpr std::wstring_view word=L"resources";if(key.size()<word.size())return false;
    for(std::size_t n=0;n+word.size()<=key.size();++n){bool same=true;for(std::size_t k=0;k<word.size()&&same;++k)same=std::towlower(static_cast<wint_t>(key[n+k]))==static_cast<wint_t>(word[k]);if(same)return true;}
    return false;
}
std::wstring_view trimmed(std::wstring_view v)noexcept{
    while(!v.empty()&&std::iswspace(static_cast<wint_t>(v.front())))v.remove_prefix(1);
    while(!v.empty()&&std::iswspace(static_cast<wint_t>(v.back())))v.remove_suffix(1);
    return v;
}
}
std::vector<std::wstring>audioPackageResourceReferences(std::wstring_view full,std::wstring_view name,std::wstring_view display){
    constexpr std::wstring_view prefix=L"ms-resource:";
    display=trimmed(display);
    if(!display.starts_with(prefix)||!identityText(full)||!identityText(name)||display.size()>maximumText)return {};
    std::wstring_view key=display.substr(prefix.size());
    if(key.empty()||key.find(L'\0')!=key.npos||key.find(L'}')!=key.npos)return {};
    const auto wrap=[&](std::wstring_view uri){std::wstring out;out.reserve(full.size()+uri.size()+3);out+=L"@{";out+=full;out+=L'?';out+=uri;out+=L'}';return out;};
    if(key.starts_with(L"//"))return {wrap(std::wstring(prefix)+std::wstring(key))};
    std::wstring path;if(!key.starts_with(L'/'))path=L"/";path+=key;
    if(path.find_first_not_of(L'/')==path.npos)return {};
    std::vector<std::wstring>out;
    if(containsResources(path))out.push_back(wrap(std::wstring(prefix)+L"//"+std::wstring(name)+path));
    else{out.push_back(wrap(std::wstring(prefix)+L"//"+std::wstring(name)+L"/resources"+path));out.push_back(wrap(std::wstring(prefix)+L"///resources"+path));}
    return out;
}
std::wstring_view audioPackageApplicationID(std::wstring_view aumid)noexcept{
    const auto bang=aumid.find(L'!');if(bang==aumid.npos||bang==0||bang+1>=aumid.size())return {};
    const auto id=aumid.substr(bang+1);return id.size()<=maximumText&&id.find(L'!')==id.npos?id:std::wstring_view{};
}
#ifdef _WIN32
std::optional<AudioPackageManifestNames>readAudioPackageManifest(const std::filesystem::path&file,std::wstring_view applicationID){
    using Microsoft::WRL::ComPtr;
    try{
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if(!GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&data)||(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return std::nullopt;
        const auto size=(static_cast<unsigned long long>(data.nFileSizeHigh)<<32)|data.nFileSizeLow;if(!size||size>4ull*1024*1024)return std::nullopt;
        ComPtr<IStream>stream;if(FAILED(SHCreateStreamOnFileEx(file.c_str(),STGM_READ|STGM_SHARE_DENY_WRITE,FILE_ATTRIBUTE_NORMAL,FALSE,nullptr,&stream)))return std::nullopt;
        ComPtr<IXmlReader>reader;if(FAILED(CreateXmlReader(__uuidof(IXmlReader),reinterpret_cast<void**>(reader.GetAddressOf()),nullptr)))return std::nullopt;
        if(FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit))||FAILED(reader->SetProperty(XmlReaderProperty_MaxElementDepth,64))||FAILED(reader->SetInput(stream.Get())))return std::nullopt;
        AudioPackageManifestNames names;std::vector<std::wstring>stack;stack.reserve(16);bool matched{},capturing{};std::wstring text;
        const auto attribute=[&](const wchar_t*key)->std::wstring{
            std::wstring value;if(reader->MoveToAttributeByName(key,nullptr)==S_OK){const wchar_t*raw{};UINT length{};if(SUCCEEDED(reader->GetValue(&raw,&length))&&raw&&length<=maximumText)value=std::wstring(trimmed(std::wstring_view(raw,length)));}
            (void)reader->MoveToElement();return value;};
        const auto parent=[&](std::size_t up)->std::wstring_view{return stack.size()>up?std::wstring_view(stack[stack.size()-1-up]):std::wstring_view{};};
        XmlNodeType type{};HRESULT hr{};
        while((hr=reader->Read(&type))==S_OK){
            if(type==XmlNodeType_Element){
                const wchar_t*raw{};UINT length{};if(FAILED(reader->GetLocalName(&raw,&length))||!raw)return std::nullopt;
                const std::wstring local(raw,length);const bool empty=reader->IsEmptyElement()!=FALSE;
                if(local==L"Identity"&&parent(0)==L"Package"&&stack.size()==1)names.packageName=attribute(L"Name");
                else if(local==L"Application"&&parent(0)==L"Applications")matched=!applicationID.empty()&&attribute(L"Id")==applicationID;
                else if(local==L"VisualElements"&&parent(0)==L"Application"&&matched&&names.applicationDisplayName.empty())names.applicationDisplayName=attribute(L"DisplayName");
                else if(local==L"DisplayName"&&parent(0)==L"Properties"&&parent(1)==L"Package"&&stack.size()==2&&!empty){capturing=true;text.clear();}
                if(!empty)stack.push_back(local);
            }else if(type==XmlNodeType_EndElement){
                if(capturing&&!stack.empty()&&stack.back()==L"DisplayName"){capturing=false;names.packageDisplayName=std::wstring(trimmed(text));}
                if(!stack.empty()){if(stack.back()==L"Application")matched=false;stack.pop_back();}
            }else if(capturing&&(type==XmlNodeType_Text||type==XmlNodeType_CDATA)){
                const wchar_t*raw{};UINT length{};if(SUCCEEDED(reader->GetValue(&raw,&length))&&raw){if(text.size()+length>maximumText)return std::nullopt;text.append(raw,length);}
            }
        }
        // A malformed document (or a DTD) fails the whole read.
        if(FAILED(hr)||names.packageName.empty())return std::nullopt;
        for(const auto*value:{&names.packageName,&names.applicationDisplayName,&names.packageDisplayName})
            if(value->find(L'\0')!=value->npos)return std::nullopt;
        return names;
    }catch(...){return std::nullopt;}
}
#endif
} // namespace endfield::native
