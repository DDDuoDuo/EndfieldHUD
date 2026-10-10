#include "native/notes_clipboard.hpp"
#include "native/file_shelf_transfer.hpp"
#include <atomic>
#include <cstring>
#ifdef _WIN32
#include <shlobj.h>
#endif
#include <limits>
#include <stdexcept>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;
constexpr std::size_t richPayloadMaximumBytes=64*1024*1024;
bool high(char16_t c){return c>=0xd800&&c<=0xdbff;}
bool low(char16_t c){return c>=0xdc00&&c<=0xdfff;}
std::string utf8(std::u16string_view s){
    std::string out;out.reserve(s.size());
    for(std::size_t i=0;i<s.size();++i){std::uint32_t c=s[i];
        if(high(s[i])&&i+1<s.size()&&low(s[i+1])){c=0x10000+((c-0xd800)<<10)+(s[i+1]-0xdc00);++i;}else if(high(s[i])||low(s[i]))return {};
        if(c<0x80)out+=char(c);else if(c<0x800){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}
        else if(c<0x10000){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
        else{out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}}
    return out;
}
// Validated UTF-8 (Json::validUtf8) to UTF-16.
std::u16string utf16(std::string_view s){
    std::u16string out;out.reserve(s.size());
    for(std::size_t i=0;i<s.size();){const auto b=static_cast<unsigned char>(s[i]);std::uint32_t c;std::size_t n;
        if(b<0x80){c=b;n=1;}else if((b>>5)==6){c=b&31;n=2;}else if((b>>4)==14){c=b&15;n=3;}else{c=b&7;n=4;}
        for(std::size_t k=1;k<n;++k)c=(c<<6)|(static_cast<unsigned char>(s[i+k])&63);i+=n;
        if(c>=0x10000){c-=0x10000;out+=char16_t(0xd800+(c>>10));out+=char16_t(0xdc00+(c&1023));}else out+=char16_t(c);}
    return out;
}
} // namespace

std::u16string notesClipboardPlatformText(std::u16string_view value){
    std::u16string out;out.reserve(value.size()+value.size()/16);
    for(std::size_t i=0;i<value.size();++i){const auto c=value[i];
        if(c==u'\r'){out+=u"\r\n";if(i+1<value.size()&&value[i+1]==u'\n')++i;}else if(c==u'\n')out+=u"\r\n";else out+=c;}
    return out;
}
std::u16string notesClipboardDocumentText(std::u16string_view value){
    std::u16string out;out.reserve(value.size());
    for(std::size_t i=0;i<value.size();++i){const auto c=value[i];
        if(c==u'\r'){out+=u'\n';if(i+1<value.size()&&value[i+1]==u'\n')++i;}else out+=c;}
    return out;
}
std::string encodeNotesClipboardRich(const NotesClipboardText& value){
    if(!value.rich)throw std::invalid_argument("A Notes clipboard rich payload needs runs");
    const auto text=utf8(value.text);if(text.empty()&&!value.text.empty())throw std::invalid_argument("Notes clipboard text is not valid UTF-16");
    Json root=Json::Object{{"version",1},{"text",text},{"rich",core::notes::encodeRichText(*value.rich,value.text)}};return root.encode(richPayloadMaximumBytes);
}
std::optional<NotesClipboardText> decodeNotesClipboardRich(std::string_view bytes){
    try{
        if(bytes.size()>richPayloadMaximumBytes||!Json::validUtf8(bytes))return std::nullopt;const auto root=Json::parse(bytes,richPayloadMaximumBytes);
        if(!root.isObject()||root.object().size()!=3||!root["version"].isNumber()||root["version"].integer()!=1||!root["text"].isString())return std::nullopt;
        NotesClipboardText value;value.text=utf16(root["text"].string());if(!core::text::Buffer::validUTF16(value.text))return std::nullopt;
        value.rich=core::notes::decodeRichText(root["rich"],value.text);return value;
    }catch(const std::bad_alloc&){throw;}catch(const std::exception&){return std::nullopt;}
}
std::optional<NotesClipboardText> notesClipboardSelection(const core::text::Document& document,bool formatted){
    if(document.composition())return std::nullopt;const auto range=document.selection().range;if(range.start==range.end)return std::nullopt;
    NotesClipboardText value;value.text=std::u16string(document.text().substr(range.start,range.end-range.start));
    if(const auto* rich=formatted?dynamic_cast<const core::notes::RichDocument*>(&document):nullptr)value.rich=core::notes::richTextSlice(rich->runs(),range);
    return value;
}

#ifdef _WIN32
ProjectedEditorResult notesClipboardPaste(NativeNotesEditor& editor,core::text::Document& document,const NotesClipboardText& value,bool richField){
    if(document.readOnly()||document.composition()||!core::text::Buffer::validUTF16(value.text))return {true,false,false};
    const auto range=document.selection().range;
    auto* rich=richField?dynamic_cast<core::notes::RichDocument*>(&document):nullptr;
    // Unformatted runs into an unformatted note are plain text in the typing style.
    const bool layout=editor.richLayoutEnabled();
    const bool styled=rich&&value.rich&&(layout||!value.rich->runs.empty())&&value.text.size()<=std::numeric_limits<std::uint32_t>::max()&&value.rich->validFor(value.text);
    if(styled)rich->setInsertionRuns(value.rich->runs,static_cast<std::uint32_t>(value.text.size()));
    ProjectedEditorResult result;
    try{result=editor.replaceTextFromHost(range,value.text);if(rich)rich->clearInsertionRuns();}
    catch(...){if(rich)rich->clearInsertionRuns();throw;}
    // The first formatting turns an unformatted note's editor to rich layout;
    // a same-size command at the collapsed caret does exactly that and edits nothing.
    if(styled&&result.changed&&!layout){editor.syncContent();core::notes::FormatChange change;change.kind=core::notes::FormatKind::size;change.fontSize=rich->typingStyle().fontSize;editor.applyFormat(change);}
    return result;
}
ProjectedEditorResult notesClipboardDeleteSelection(NativeNotesEditor& editor,core::text::Document& document){
    if(document.readOnly()||document.composition())return {true,false,false};const auto range=document.selection().range;if(range.start==range.end)return {true,false,false};
    return editor.replaceTextFromHost(range,u"");
}

namespace {
using Microsoft::WRL::ComPtr;
CLIPFORMAT richFormat(){static const auto format=static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"EndfieldHUD.NotesRichText.v1"));return format;}
HGLOBAL globalCopy(const void* bytes,std::size_t size){
    auto memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,size?size:1);if(!memory)return nullptr;auto* target=GlobalLock(memory);if(!target){GlobalFree(memory);return nullptr;}
    if(size)std::memcpy(target,bytes,size);GlobalUnlock(memory);return memory;
}
// Minimal owned-format data object: each GetData hands out a fresh copy.
class ClipboardData final:public IDataObject {
public:
    struct Format {CLIPFORMAT format;std::string bytes;};
    explicit ClipboardData(std::vector<Format> formats):formats_(std::move(formats)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_IDataObject){*out=static_cast<IDataObject*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--references_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* request,STGMEDIUM* medium)override{
        if(!request||!medium)return E_INVALIDARG;std::memset(medium,0,sizeof(*medium));const auto* found=find(*request);if(!found)return DV_E_FORMATETC;
        medium->hGlobal=globalCopy(found->bytes.data(),found->bytes.size());if(!medium->hGlobal)return E_OUTOFMEMORY;medium->tymed=TYMED_HGLOBAL;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* request)override{return request&&find(*request)?S_OK:DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC* out)override{if(out)out->ptd=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(direction!=DATADIR_GET)return E_NOTIMPL;std::vector<FORMATETC> list;list.reserve(formats_.size());
        for(const auto& f:formats_)list.push_back({f.format,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL});return SHCreateStdEnumFmtEtc(static_cast<UINT>(list.size()),list.data(),out);}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*)override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD)override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**)override{return OLE_E_ADVISENOTSUPPORTED;}
private:
    const Format* find(const FORMATETC& r)const{if(r.dwAspect!=DVASPECT_CONTENT||!(r.tymed&TYMED_HGLOBAL)||r.lindex!=-1)return nullptr;for(const auto& f:formats_)if(f.format==r.cfFormat)return &f;return nullptr;}
    std::atomic<ULONG> references_{1};std::vector<Format> formats_;
};
// Copies one HGLOBAL format out of a data object (bounded), releasing its medium.
std::optional<std::string> globalBytes(IDataObject& source,CLIPFORMAT format,std::size_t maximum){
    FORMATETC request{format,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium{};if(!format||FAILED(source.GetData(&request,&medium)))return std::nullopt;
    struct Release{STGMEDIUM& m;~Release(){ReleaseStgMedium(&m);}}release{medium};if(medium.tymed!=TYMED_HGLOBAL||!medium.hGlobal)return std::nullopt;
    const auto size=GlobalSize(medium.hGlobal);if(size>maximum)return std::nullopt;const auto* data=static_cast<const char*>(GlobalLock(medium.hGlobal));if(!data)return std::nullopt;
    std::string out(data,size);GlobalUnlock(medium.hGlobal);return out;
}
class SystemClipboard final:public NotesClipboard {
public:
    std::optional<std::vector<std::uint8_t>> readImage(std::size_t maximum)override{ComPtr<IDataObject> source;if(FAILED(OleGetClipboard(&source))||!source)return std::nullopt;try{return readNotesClipboardImage(*source.Get(),maximum);}catch(const std::bad_alloc&){return std::nullopt;}}
    std::optional<NotesClipboardText> readText(std::uint32_t maximum)override{ComPtr<IDataObject> source;if(FAILED(OleGetClipboard(&source))||!source)return std::nullopt;try{return readNotesClipboardData(*source.Get(),maximum);}catch(const std::bad_alloc&){return std::nullopt;}}
    std::vector<std::string> readFiles()override{ComPtr<IDataObject> source;if(FAILED(OleGetClipboard(&source))||!source)return {};try{return readShelfTransferPaths(*source.Get());}catch(const std::exception&){return {};}}
    bool writeText(const NotesClipboardText& value)override{
        ComPtr<IDataObject> data;try{data=makeNotesClipboardData(value);}catch(const std::exception&){return false;}
        // Flushing renders every format now and releases this object, so the
        // text stays available after the field (or the application) closes.
        return SUCCEEDED(OleSetClipboard(data.Get()))&&SUCCEEDED(OleFlushClipboard());
    }
};
} // namespace

Microsoft::WRL::ComPtr<IDataObject> makeNotesClipboardData(const NotesClipboardText& value){
    if(!core::text::Buffer::validUTF16(value.text))throw std::invalid_argument("Notes clipboard text is not valid UTF-16");
    const auto platform=notesClipboardPlatformText(value.text);std::vector<ClipboardData::Format> formats;
    formats.push_back({CF_UNICODETEXT,std::string(reinterpret_cast<const char*>(platform.c_str()),(platform.size()+1)*sizeof(char16_t))});
    // NUL-terminated: GlobalSize may report more than the payload's bytes.
    if(value.rich){const auto format=richFormat();if(format)formats.push_back({format,encodeNotesClipboardRich(value)+'\0'});}
    ComPtr<IDataObject> object;object.Attach(new ClipboardData(std::move(formats)));return object;
}
std::optional<NotesClipboardText> readNotesClipboardData(IDataObject& source,std::uint32_t maximum){
    // CRLF expansion at most doubles a document's units on the clipboard.
    const auto bytes=globalBytes(source,CF_UNICODETEXT,(std::size_t(maximum)*2+1)*sizeof(char16_t));if(!bytes||bytes->size()<sizeof(char16_t))return std::nullopt;
    std::u16string platform(bytes->size()/sizeof(char16_t),u'\0');std::memcpy(platform.data(),bytes->data(),platform.size()*sizeof(char16_t));
    if(const auto end=platform.find(u'\0');end!=std::u16string::npos)platform.resize(end);
    for(std::size_t i=0;i<platform.size();++i){if(high(platform[i])&&i+1<platform.size()&&low(platform[i+1])){++i;continue;}if(high(platform[i])||low(platform[i]))platform[i]=u'\xfffd';}
    NotesClipboardText value;
    if(const auto payload=globalBytes(source,richFormat(),richPayloadMaximumBytes))
        if(auto decoded=decodeNotesClipboardRich(std::string_view(payload->data(),payload->find('\0')==std::string::npos?payload->size():payload->find('\0'))))
            if(notesClipboardPlatformText(decoded->text)==platform)value=std::move(*decoded);
    if(!value.rich)value.text=notesClipboardDocumentText(platform);
    if(value.text.size()>maximum)return std::nullopt;return value;
}
std::optional<std::vector<std::uint8_t>> readNotesClipboardImage(IDataObject& source,std::size_t maximum){
    auto bytes=[](const std::string& v){return std::vector<std::uint8_t>(v.begin(),v.end());};
    static const auto png=static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"PNG"));
    if(const auto data=globalBytes(source,png,maximum);data&&!data->empty())return bytes(*data);
    for(const CLIPFORMAT format:{CLIPFORMAT(CF_DIBV5),CLIPFORMAT(CF_DIB)}){
        const auto dib=globalBytes(source,format,maximum>14?maximum-14:0);if(!dib||dib->size()<sizeof(BITMAPINFOHEADER))continue;
        BITMAPINFOHEADER header{};std::memcpy(&header,dib->data(),sizeof(header));if(header.biSize<sizeof(BITMAPINFOHEADER)||header.biSize>dib->size())continue;
        // The pixel offset follows the header, BI_BITFIELDS masks of a plain
        // info header and the color table, exactly as in a bitmap file.
        const std::uint64_t masks=header.biSize==sizeof(BITMAPINFOHEADER)?(header.biCompression==BI_BITFIELDS?12:header.biCompression==6?16:0):0;
        const std::uint64_t colors=header.biClrUsed?header.biClrUsed:(header.biBitCount<=8?(1ull<<header.biBitCount):0);
        const std::uint64_t offset=sizeof(BITMAPFILEHEADER)+header.biSize+masks+colors*4;if(offset>sizeof(BITMAPFILEHEADER)+dib->size())continue;
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfSize=static_cast<DWORD>(sizeof(BITMAPFILEHEADER)+dib->size());file.bfOffBits=static_cast<DWORD>(offset);
        std::vector<std::uint8_t> out(sizeof(file)+dib->size());std::memcpy(out.data(),&file,sizeof(file));std::memcpy(out.data()+sizeof(file),dib->data(),dib->size());return out;
    }
    if(const auto data=globalBytes(source,CF_TIFF,maximum);data&&!data->empty())return bytes(*data);
    return std::nullopt;
}
std::shared_ptr<NotesClipboard> systemNotesClipboard(){return std::make_shared<SystemClipboard>();}
#endif
} // namespace endfield::native
