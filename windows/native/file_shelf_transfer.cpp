#include "native/file_shelf_transfer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#ifdef _WIN32
#include <atomic>
#include <ole2.h>
#include <shlobj.h>
#include <shldisp.h>
#include <shobjidl.h>
#include <wrl/client.h>
#endif

namespace endfield::native {
using namespace ehud::data;
namespace {
void require(bool b,const char* message){if(!b)throw StoreError(StoreErrorCode::invalid,message);}
void bound(bool b){if(!b)throw StoreError(StoreErrorCode::tooLarge,"Shelf transfer exceeds its explicit memory/path bound");}
std::uint32_t u32(std::span<const std::uint8_t> b,std::size_t at){return std::uint32_t(b[at])|(std::uint32_t(b[at+1])<<8)|(std::uint32_t(b[at+2])<<16)|(std::uint32_t(b[at+3])<<24);}
void utf8(std::string& s,std::uint32_t c){
    if(c<0x80)s.push_back(char(c));else if(c<0x800){s.push_back(char(0xc0|(c>>6)));s.push_back(char(0x80|(c&63)));}
    else if(c<0x10000){s.push_back(char(0xe0|(c>>12)));s.push_back(char(0x80|((c>>6)&63)));s.push_back(char(0x80|(c&63)));}
    else{s.push_back(char(0xf0|(c>>18)));s.push_back(char(0x80|((c>>12)&63)));s.push_back(char(0x80|((c>>6)&63)));s.push_back(char(0x80|(c&63)));}
}
void word(std::vector<std::uint8_t>& out,std::uint16_t c){bound(out.size()<=shelfTransferMaximumBytes-2);out.push_back(std::uint8_t(c));out.push_back(std::uint8_t(c>>8));}
}
std::vector<std::uint8_t> encodeShelfDropPaths(std::span<const std::string> paths){
    require(!paths.empty(),"Empty shelf transfer");bound(paths.size()<=shelfTransferMaximumPaths);
    std::vector<std::uint8_t> out(20,0);out[0]=20;out[16]=1; // DROPFILES, fWide=TRUE
    for(const auto& path:paths){require(validWindowsFilePath(path),"Invalid shelf transfer path");
        for(std::size_t i=0;i<path.size();){const auto first=std::uint8_t(path[i++]);std::uint32_t c{};unsigned n{};
            if(first<0x80)c=first;else if(first<0xe0){c=first&31;n=1;}else if(first<0xf0){c=first&15;n=2;}else{c=first&7;n=3;}
            while(n--){c=(c<<6)|(std::uint8_t(path[i++])&63);}
            if(c<0x10000)word(out,std::uint16_t(c));else{c-=0x10000;word(out,std::uint16_t(0xd800+(c>>10)));word(out,std::uint16_t(0xdc00+(c&1023)));}
        }word(out,0);
    }word(out,0);return out;
}
std::vector<std::string> decodeShelfDropPaths(std::span<const std::uint8_t> bytes){
    bound(bytes.size()<=shelfTransferMaximumBytes);require(bytes.size()>=24,"Truncated DROPFILES header/list");
    const auto offset=std::size_t(u32(bytes,0));require(offset>=20&&offset<=bytes.size()-4&&offset%2==0,"Invalid DROPFILES offset/alignment");
    require(u32(bytes,16)!=0,"This shelf transfer supports Unicode CF_HDROP only");
    std::vector<std::string> paths;std::string path;bool ended{};
    const auto readWord=[&](std::size_t at){return std::uint16_t(std::uint16_t(bytes[at])|(std::uint16_t(bytes[at+1])<<8));};
    for(std::size_t i=offset;i+1<bytes.size();i+=2){std::uint32_t c=readWord(i);
        if(!c){if(path.empty()){require(!paths.empty(),"Empty DROPFILES list");ended=true;break;}
            require(validWindowsFilePath(path),"Invalid incoming shelf filesystem path");bound(paths.size()<shelfTransferMaximumPaths);paths.push_back(std::move(path));path.clear();continue;}
        if(c>=0xd800&&c<=0xdbff){require(i+3<bytes.size(),"Truncated UTF-16 pair");const auto low=readWord(i+2);require(low>=0xdc00&&low<=0xdfff,"Invalid UTF-16 pair");c=0x10000+((c-0xd800)<<10)+(low-0xdc00);i+=2;}
        else require(c<0xdc00||c>0xdfff,"Unpaired UTF-16 low surrogate");
        utf8(path,c);bound(path.size()<=32768);
    }require(ended&&path.empty(),"Unterminated DROPFILES list");return paths;
}
ShelfDragImage composeShelfDragImage(std::span<const ShelfDragIcon> icons,unsigned side,unsigned offset){
    require(side>0&&side<=shelfDragImageMaximumSide&&offset<=side,"Invalid shelf drag image geometry");
    const auto frames=std::min(icons.size(),shelfDragImageMaximumFrames);if(!frames)return {};
    for(std::size_t n=0;n<frames;++n){const auto& icon=icons[n];
        require(icon.width&&icon.height&&icon.width<=1024&&icon.height<=1024&&icon.premultipliedBGRA.size()==std::size_t(icon.width)*icon.height*4,"Invalid shelf drag icon pixels");}
    ShelfDragImage image;image.width=image.height=side+offset*unsigned(frames-1);
    image.premultipliedBGRA.assign(std::size_t(image.width)*image.height*4,0);
    for(std::size_t n=0;n<frames;++n){
        const auto& icon=icons[n];const double scale=std::min(double(side)/icon.width,double(side)/icon.height);
        const auto width=std::max(1u,unsigned(std::lround(icon.width*scale))),height=std::max(1u,unsigned(std::lround(icon.height*scale)));
        // Frame n sits n offsets right of and above frame 0 (y grows downward).
        const unsigned left=unsigned(n)*offset+(side-std::min(side,width))/2,top=unsigned(frames-1-n)*offset+(side-std::min(side,height))/2;
        const auto sample=[&](double x,double y,unsigned channel){
            x=std::clamp(x,0.0,double(icon.width-1));y=std::clamp(y,0.0,double(icon.height-1));
            const auto x0=unsigned(x),y0=unsigned(y),x1=std::min(x0+1,icon.width-1),y1=std::min(y0+1,icon.height-1);const double fx=x-x0,fy=y-y0;
            const auto at=[&](unsigned px,unsigned py){return double(icon.premultipliedBGRA[(std::size_t(py)*icon.width+px)*4+channel]);};
            return (at(x0,y0)*(1-fx)+at(x1,y0)*fx)*(1-fy)+(at(x0,y1)*(1-fx)+at(x1,y1)*fx)*fy;
        };
        for(unsigned y=0;y<height&&top+y<image.height;++y)for(unsigned x=0;x<width&&left+x<image.width;++x){
            const double sx=(x+.5)*icon.width/width-.5,sy=(y+.5)*icon.height/height-.5;
            std::array<double,4> source{};for(unsigned c=0;c<4;++c)source[c]=sample(sx,sy,c);
            auto* target=image.premultipliedBGRA.data()+(std::size_t(top+y)*image.width+left+x)*4;const double keep=1-source[3]/255;
            for(unsigned c=0;c<4;++c)target[c]=std::uint8_t(std::clamp(std::lround(source[c]+target[c]*keep),0L,255L));
        }
    }
    image.cursorX=int(side/2);image.cursorY=int(offset*unsigned(frames-1)+side/2);
    return image;
}

#ifdef _WIN32
namespace {
struct Notice {
    ShelfTransferRoute route;DWORD thread{GetCurrentThreadId()};unsigned depth{};bool dirty{},queued{};
    explicit Notice(ShelfTransferRoute r,bool required=false):route(r){
        require(!required||r.owner,"Shelf target needs an existing owner HWND");
        if(r.owner)require(IsWindow(r.owner)&&GetWindowThreadProcessId(r.owner,nullptr)==thread&&r.message>=WM_APP&&r.message<=0xbfff,"Invalid shelf owner notification route");
        else require(!r.message&&!r.generation,"Incomplete shelf notification route");
    }
    bool ownerThread()const noexcept{return GetCurrentThreadId()==thread;}
    void flush()noexcept{if(dirty&&!depth&&!queued&&route.owner)queued=PostMessageW(route.owner,route.message,route.generation,0)!=FALSE;}
    void signal()noexcept{dirty=true;flush();}
    bool drain(UINT_PTR generation)noexcept{if(!ownerThread()||generation!=route.generation)return false;queued=false;if(depth)return false;dirty=false;return true;}
    void detach()noexcept{route.owner=nullptr;queued=false;}
};
struct Call {
    IUnknown* object;Notice& notice;
    Call(IUnknown* o,Notice& n):object(o),notice(n){object->AddRef();++notice.depth;}
    ~Call(){--notice.depth;notice.flush();object->Release();}
};
HRESULT failure()noexcept{try{throw;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(const StoreError& e){return e.code()==StoreErrorCode::tooLarge?HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE):E_INVALIDARG;}catch(...){return E_FAIL;}}
struct Medium {STGMEDIUM value{};~Medium(){if(value.tymed)ReleaseStgMedium(&value);}};
struct GlobalRead {HGLOBAL handle;const std::uint8_t* data{};explicit GlobalRead(HGLOBAL h):handle(h),data(static_cast<const std::uint8_t*>(GlobalLock(h))){require(data,"Cannot lock incoming shelf data");}~GlobalRead(){GlobalUnlock(handle);}};
std::vector<std::string> incoming(IDataObject& object){
    FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};Medium medium;
    const auto hr=object.GetData(&format,&medium.value);if(FAILED(hr))throw StoreError(StoreErrorCode::unavailable,"Drop source did not provide filesystem paths");
    require(medium.value.tymed==TYMED_HGLOBAL&&medium.value.hGlobal,"Invalid incoming shelf storage medium");
    const auto size=GlobalSize(medium.value.hGlobal);bound(size<=shelfTransferMaximumBytes);require(size>=24,"Invalid incoming shelf allocation");
    GlobalRead data(medium.value.hGlobal);return decodeShelfDropPaths({data.data,size});
}
struct Formats final:IEnumFORMATETC {
    std::atomic<ULONG> refs{1};std::vector<FORMATETC> values;ULONG index{};
    explicit Formats(std::vector<FORMATETC> list):values(std::move(list)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IEnumFORMATETC)return E_NOINTERFACE;*out=static_cast<IEnumFORMATETC*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Next(ULONG count,FORMATETC* out,ULONG* fetched)override{if(!out||(!fetched&&count!=1))return E_POINTER;ULONG n{};while(n<count&&index<values.size())out[n++]=values[index++];if(fetched)*fetched=n;return n==count?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Skip(ULONG n)override{const auto left=ULONG(values.size())-index;index+=std::min(n,left);return n<=left?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Reset()override{index=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out)override{if(!out)return E_POINTER;*out=nullptr;try{auto* copy=new Formats(values);copy->index=index;*out=copy;return S_OK;}catch(...){return failure();}}
};
struct TransferData {
    ShelfCopyBundle bundle;ShelfDragTransfer::Resolver resolver;std::vector<ShelfRecord> records;std::vector<std::uint8_t> bytes;
    TransferData(ShelfCopyBundle b,ShelfDragTransfer::Resolver r):bundle(std::move(b)),resolver(std::move(r)){
        require(bool(resolver)&&!bundle.ids.empty()&&bundle.ids.size()==bundle.accesses.size(),"Incomplete leased shelf transfer");
        bound(bundle.ids.size()<=shelfTransferMaximumPaths);std::vector<std::string> paths;paths.reserve(bundle.ids.size());records.reserve(bundle.ids.size());
        for(std::size_t i=0;i<bundle.ids.size();++i){require(validUUID(bundle.ids[i])&&bundle.accesses[i].open(),"Invalid outgoing shelf lease");
            const auto& metadata=bundle.accesses[i].metadata();ShelfRecord record;record.id=bundle.ids[i];record.windowsPath=metadata.windowsPath;record.identity=metadata.identity;
            paths.push_back(metadata.windowsPath);records.push_back(std::move(record));}
        bytes=encodeShelfDropPaths(paths);
    }
    void validate(){std::vector<ShelfFileAccess> checks;checks.reserve(records.size());
        for(const auto& record:records){auto access=resolver(record);require(access.open()&&record.identity.matches(access.metadata().identity)&&record.windowsPath==access.metadata().windowsPath,"Outgoing shelf reference changed before extraction");checks.push_back(std::move(access));}
    }
};
struct MediumOwner final:IUnknown {
    std::atomic<ULONG> refs{1};HGLOBAL memory{};std::shared_ptr<TransferData> data;
    MediumOwner(HGLOBAL m,std::shared_ptr<TransferData> d):memory(m),data(std::move(d)){}
    ~MediumOwner(){GlobalFree(memory);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown)return E_NOINTERFACE;*out=static_cast<IUnknown*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
};
HRESULT mediumFor(std::span<const std::uint8_t> bytes,const std::shared_ptr<TransferData>& data,STGMEDIUM* out){
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,bytes.size());if(!memory)return E_OUTOFMEMORY;
    auto* buffer=GlobalLock(memory);if(!buffer){GlobalFree(memory);return E_OUTOFMEMORY;}std::memcpy(buffer,bytes.data(),bytes.size());GlobalUnlock(memory);
    try{auto* release=new MediumOwner(memory,data);out->tymed=TYMED_HGLOBAL;out->hGlobal=memory;out->pUnkForRelease=release;return S_OK;}catch(...){GlobalFree(memory);return failure();}
}
}

std::vector<std::string> readShelfTransferPaths(IDataObject&object){return incoming(object);}

// Shell-private formats the drag-image helper and drop targets keep on the
// source object (DragImageBits, DragContext, DragWindow, DropDescription...).
// Bounded byte copies; formats that could report a performed move/paste back
// to this copy-only source, and the two formats it owns, are refused.
// The helper keeps DragContext as a stream; other formats are HGLOBALs.
struct StoredFormat {CLIPFORMAT format{};DWORD tymed{TYMED_HGLOBAL};std::vector<std::uint8_t> bytes;};
// Bounded copy of an HGLOBAL or the whole stream (read from its start, cursor restored).
HRESULT copyMedium(const STGMEDIUM& medium,std::size_t limit,std::vector<std::uint8_t>& out){
    if(medium.tymed==TYMED_HGLOBAL){if(!medium.hGlobal)return DV_E_STGMEDIUM;const auto size=GlobalSize(medium.hGlobal);if(size>limit)return E_OUTOFMEMORY;out.resize(size);
        if(size){const auto* source=static_cast<const std::uint8_t*>(GlobalLock(medium.hGlobal));if(!source)return E_OUTOFMEMORY;std::memcpy(out.data(),source,size);GlobalUnlock(medium.hGlobal);}return S_OK;}
    if(medium.tymed!=TYMED_ISTREAM||!medium.pstm)return DV_E_TYMED;
    STATSTG stat{};if(const auto hr=medium.pstm->Stat(&stat,STATFLAG_NONAME);FAILED(hr))return hr;if(stat.cbSize.QuadPart>limit)return E_OUTOFMEMORY;
    LARGE_INTEGER zero{};ULARGE_INTEGER position{};if(const auto hr=medium.pstm->Seek(zero,STREAM_SEEK_CUR,&position);FAILED(hr))return hr;
    if(const auto hr=medium.pstm->Seek(zero,STREAM_SEEK_SET,nullptr);FAILED(hr))return hr;
    out.resize(std::size_t(stat.cbSize.QuadPart));ULONG read{};HRESULT hr=out.empty()?S_OK:medium.pstm->Read(out.data(),ULONG(out.size()),&read);
    LARGE_INTEGER restore{};restore.QuadPart=LONGLONG(position.QuadPart);medium.pstm->Seek(restore,STREAM_SEEK_SET,nullptr);
    if(FAILED(hr))return hr;if(read!=out.size())return STG_E_READFAULT;return S_OK;
}
inline constexpr std::size_t storedFormatLimit=16,storedFormatBytes=16*1024*1024,storedTotalBytes=32*1024*1024;
struct ShelfDragTransfer::Object final:IDataObject,IDropSource,IDataObjectAsyncCapability {
    std::atomic<ULONG> refs{1};Notice notice;std::shared_ptr<TransferData> data;ShelfDragChanges changes;
    CLIPFORMAT preferred{};std::array<CLIPFORMAT,3> refused{};std::vector<StoredFormat> stored;
    bool extracting{},cancelled{},started{},completed{},running{},asyncMode{true},asyncActive{};
    Object(ShelfCopyBundle bundle,Resolver resolver,ShelfTransferRoute route):notice(route),data(std::make_shared<TransferData>(std::move(bundle),std::move(resolver))){
        preferred=static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));require(preferred!=0,"Cannot register copy-only transfer format");
        refused={static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT)),static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PASTESUCCEEDED)),
            static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_LOGICALPERFORMEDDROPEFFECT))};
    }
    const StoredFormat* find(CLIPFORMAT format)const noexcept{for(const auto& value:stored)if(value.format==format)return &value;return nullptr;}
    HRESULT storedFormat(const FORMATETC* f)const noexcept{if(!f)return E_POINTER;if(!find(f->cfFormat))return DV_E_FORMATETC;if(f->ptd)return DV_E_DVTARGETDEVICE;if(f->dwAspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;if(f->lindex!=-1)return DV_E_LINDEX;if(!(f->tymed&(TYMED_HGLOBAL|TYMED_ISTREAM)))return DV_E_TYMED;return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;
        if(id==IID_IUnknown||id==IID_IDataObject)*out=static_cast<IDataObject*>(this);else if(id==IID_IDropSource)*out=static_cast<IDropSource*>(this);
        else if(id==__uuidof(IDataObjectAsyncCapability))*out=static_cast<IDataObjectAsyncCapability*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT format(const FORMATETC* f)const noexcept{if(!f)return E_POINTER;if(f->cfFormat!=CF_HDROP&&f->cfFormat!=preferred)return DV_E_FORMATETC;if(f->ptd)return DV_E_DVTARGETDEVICE;if(f->dwAspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;if(f->lindex!=-1)return DV_E_LINDEX;if(!(f->tymed&TYMED_HGLOBAL))return DV_E_TYMED;return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f)override{if(f&&find(f->cfFormat))return storedFormat(f);return format(f);}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f,STGMEDIUM* out)override{
        if(!out)return E_POINTER;*out={};if(!notice.ownerThread())return RPC_E_WRONG_THREAD;
        if(f&&find(f->cfFormat)){const auto valid=storedFormat(f);if(FAILED(valid))return valid;
            // An independent copy the receiver frees: an HGLOBAL, or a new
            // stream at its start when the format was stored as a stream.
            const auto* value=find(f->cfFormat);const auto& bytes=value->bytes;HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,std::max<std::size_t>(bytes.size(),1));if(!memory)return E_OUTOFMEMORY;
            if(!bytes.empty()){auto* target=GlobalLock(memory);if(!target){GlobalFree(memory);return E_OUTOFMEMORY;}std::memcpy(target,bytes.data(),bytes.size());GlobalUnlock(memory);}
            if((f->tymed&TYMED_ISTREAM)&&(value->tymed==TYMED_ISTREAM||!(f->tymed&TYMED_HGLOBAL))){IStream* stream{};const auto hr=CreateStreamOnHGlobal(memory,TRUE,&stream);if(FAILED(hr)){GlobalFree(memory);return hr;}
                ULARGE_INTEGER size{};size.QuadPart=bytes.size();stream->SetSize(size);out->tymed=TYMED_ISTREAM;out->pstm=stream;out->pUnkForRelease=nullptr;return S_OK;}
            out->tymed=TYMED_HGLOBAL;out->hGlobal=memory;out->pUnkForRelease=nullptr;return S_OK;}
        const auto valid=format(f);if(FAILED(valid))return valid;if(extracting)return RPC_E_CALL_REJECTED;
        Call call(static_cast<IDataObject*>(this),notice);extracting=true;
        struct Reset {bool& value;~Reset(){value=false;}}reset{extracting};
        try{if(f->cfFormat==CF_HDROP){data->validate();return mediumFor(data->bytes,data,out);}
            const std::array<std::uint8_t,4> copy{DROPEFFECT_COPY,0,0,0};return mediumFor(copy,data,out);
        }catch(...){return failure();}
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*)override{return DATA_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* in,FORMATETC* out)override{if(!in||!out)return E_POINTER;*out=*in;out->ptd=nullptr;return DATA_S_SAMEFORMATETC;}
    // No performed MOVE/delete-on-paste format can mutate shelf/original files:
    // those are refused, as are the path list and its preferred effect. Other
    // Shell-private formats are kept as bounded copies for the drag session.
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* f,STGMEDIUM* medium,BOOL release)override{
        if(!f||!medium)return E_POINTER;if(!notice.ownerThread())return RPC_E_WRONG_THREAD;
        if(f->cfFormat==CF_HDROP||f->cfFormat==preferred||std::find(refused.begin(),refused.end(),f->cfFormat)!=refused.end())return E_NOTIMPL;
        if(f->ptd||f->dwAspect!=DVASPECT_CONTENT||f->lindex!=-1)return DV_E_FORMATETC;
        if(!(medium->tymed&(TYMED_HGLOBAL|TYMED_ISTREAM))||!(f->tymed&medium->tymed))return DV_E_TYMED;
        try{
            std::vector<std::uint8_t> bytes;if(const auto copied=copyMedium(*medium,storedFormatBytes,bytes);FAILED(copied))return copied;
            std::size_t total=bytes.size();for(const auto& value:stored)if(value.format!=f->cfFormat)total+=value.bytes.size();
            auto existing=std::find_if(stored.begin(),stored.end(),[&](const auto& value){return value.format==f->cfFormat;});
            if(total>storedTotalBytes||(existing==stored.end()&&stored.size()>=storedFormatLimit))return E_OUTOFMEMORY;
            if(existing!=stored.end()){existing->bytes=std::move(bytes);existing->tymed=medium->tymed;}else stored.push_back({f->cfFormat,medium->tymed,std::move(bytes)});
        }catch(...){return failure();}
        // On success the object owns a released medium (IDataObject::SetData).
        if(release)ReleaseStgMedium(medium);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC** out)override{if(!out)return E_POINTER;*out=nullptr;if(direction!=DATADIR_GET)return E_NOTIMPL;
        try{std::vector<FORMATETC> list{{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL},{preferred,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL}};
            for(const auto& value:stored)list.push_back({value.format,nullptr,DVASPECT_CONTENT,-1,value.tymed});*out=new Formats(std::move(list));return S_OK;}catch(...){return failure();}}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD* connection)override{if(connection)*connection=0;return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD)override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** out)override{if(out)*out=nullptr;return OLE_E_ADVISENOTSUPPORTED;}
    void began()noexcept{if(!started){started=true;changes.started=true;notice.signal();}}
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys)override{if(!notice.ownerThread())return RPC_E_WRONG_THREAD;Call call(static_cast<IDropSource*>(this),notice);began();if(escape||cancelled)return DRAGDROP_S_CANCEL;return keys&MK_LBUTTON?S_OK:DRAGDROP_S_DROP;}
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD)override{if(!notice.ownerThread())return RPC_E_WRONG_THREAD;Call call(static_cast<IDropSource*>(this),notice);began();return DRAGDROP_S_USEDEFAULTCURSORS;}
    HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL enabled)override{if(!notice.ownerThread())return RPC_E_WRONG_THREAD;if(asyncActive||completed)return E_UNEXPECTED;asyncMode=enabled!=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL* out)override{if(!out)return E_POINTER;if(!notice.ownerThread())return RPC_E_WRONG_THREAD;*out=asyncMode?TRUE:FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx* reserved)override{if(!notice.ownerThread())return RPC_E_WRONG_THREAD;if(reserved)return E_INVALIDARG;if(!asyncMode||asyncActive||completed)return E_UNEXPECTED;asyncActive=true;AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE InOperation(BOOL* out)override{if(!out)return E_POINTER;if(!notice.ownerThread())return RPC_E_WRONG_THREAD;*out=asyncActive?TRUE:FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE EndOperation(HRESULT result,IBindCtx* reserved,DWORD effect)override{
        if(!notice.ownerThread())return RPC_E_WRONG_THREAD;if(reserved)return E_INVALIDARG;if(!asyncActive)return E_UNEXPECTED;
        Call call(static_cast<IDataObject*>(this),notice);asyncActive=false;changes.asyncFinished=true;changes.asyncResult=result;changes.asyncEffect=effect&DROPEFFECT_COPY;notice.signal();Release();return S_OK;
    }
    void finish(HRESULT result,DWORD effect)noexcept{if(completed)return;completed=true;changes.finished=true;changes.result=result;changes.effect=result==DRAGDROP_S_DROP?(effect&DROPEFFECT_COPY):DROPEFFECT_NONE;notice.signal();}
};
ShelfDragTransfer::ShelfDragTransfer(ShelfCopyBundle bundle,Resolver resolver,ShelfTransferRoute route):object_(new Object(std::move(bundle),std::move(resolver),route)){}
ShelfDragTransfer::~ShelfDragTransfer(){if(object_){if(!object_->notice.ownerThread())std::terminate();object_->notice.detach();object_->Release();}}
IDataObject* ShelfDragTransfer::dataObject()const noexcept{return object_;}IDropSource* ShelfDragTransfer::dropSource()const noexcept{return object_;}
HRESULT ShelfDragTransfer::run(DWORD* out)noexcept{if(!out)return E_POINTER;*out=DROPEFFECT_NONE;auto* object=object_;if(!object->notice.ownerThread())return RPC_E_WRONG_THREAD;if(object->running||object->completed)return E_UNEXPECTED;
    object->AddRef();object->running=true;const auto hr=DoDragDrop(object,object,DROPEFFECT_COPY,out);*out&=DROPEFFECT_COPY;object->running=false;object->finish(hr,*out);object->Release();return hr;
}
void ShelfDragTransfer::cancel()noexcept{if(object_->notice.ownerThread())object_->cancelled=true;}
HRESULT ShelfDragTransfer::setDragImage(const ShelfDragImage& image)noexcept{
    auto* object=object_;if(!object->notice.ownerThread())return RPC_E_WRONG_THREAD;if(object->running||object->completed)return E_UNEXPECTED;
    if(!image.width||!image.height||image.width>shelfDragImageMaximumSide*2||image.height>shelfDragImageMaximumSide*2||image.premultipliedBGRA.size()!=std::size_t(image.width)*image.height*4)return E_INVALIDARG;
    Microsoft::WRL::ComPtr<IDragSourceHelper> helper;
    if(const auto created=CoCreateInstance(CLSID_DragDropHelper,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&helper));FAILED(created))return created;
    // Let targets that set a drop description show it with the image.
    Microsoft::WRL::ComPtr<IDragSourceHelper2> flags;if(SUCCEEDED(helper.As(&flags)))flags->SetFlags(DSH_ALLOWDROPDESCRIPTIONTEXT);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=LONG(image.width);info.bmiHeader.biHeight=-LONG(image.height);
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits{};HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!bitmap||!bits){if(bitmap)DeleteObject(bitmap);return E_OUTOFMEMORY;}
    std::memcpy(bits,image.premultipliedBGRA.data(),image.premultipliedBGRA.size());
    SHDRAGIMAGE value{};value.sizeDragImage={LONG(image.width),LONG(image.height)};value.ptOffset={image.cursorX,image.cursorY};value.hbmpDragImage=bitmap;value.crColorKey=CLR_NONE;
    // The drag-image manager owns the bitmap only after a successful call.
    if(const auto initialized=helper->InitializeFromBitmap(&value,static_cast<IDataObject*>(object));FAILED(initialized)){DeleteObject(bitmap);return initialized;}
    return S_OK;
}
std::size_t ShelfDragTransfer::storedFormats()const noexcept{return object_->stored.size();}
void ShelfDragTransfer::complete(HRESULT result,DWORD effect)noexcept{if(object_->notice.ownerThread())object_->finish(result,effect);}
ShelfDragChanges ShelfDragTransfer::takeChanges(UINT_PTR generation)noexcept{if(!object_->notice.drain(generation))return {};return std::exchange(object_->changes,{});}

struct ShelfDropTarget::Object final:IDropTarget {
    std::atomic<ULONG> refs{1};Notice notice;std::shared_ptr<const Callbacks> callbacks;ShelfDropChanges changes;
    bool alive{true},enabled{true},registered{},offered{},busy{};std::uint64_t revision{};
    // Virtual-file-only offer of the current drag and its once-per-drag notice.
    bool virtualOffered{},virtualNoticed{};std::array<CLIPFORMAT,2> descriptors{};
    // Optional Shell drag image over the HUD; entered once per drag session.
    bool images{},imageEntered{};Microsoft::WRL::ComPtr<IDropTargetHelper> helper;
    // The current drag's paths for the optional hover callback (read once).
    std::vector<std::string> dragPaths;bool hoverActive{},hovered{};
    Object(ShelfTransferRoute route,Callbacks value):notice(route,true),callbacks(std::make_shared<const Callbacks>(std::move(value))){
        require(bool(callbacks->acceptsPoint)&&bool(callbacks->commit),"Incomplete shelf drop callbacks");
        descriptors={static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"FileGroupDescriptorW")),static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"FileGroupDescriptor"))};
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IDropTarget)return E_NOINTERFACE;*out=static_cast<IDropTarget*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    void hover(bool value)noexcept{if(changes.hovering==value)return;changes.hovering=value;changes.feedbackChanged=true;notice.signal();}
    void error(const char* text,std::optional<StoreErrorCode> code={})noexcept{try{changes.error=text;changes.errorCode=code;notice.signal();}catch(...){}}
    void virtualNotice()noexcept{if(virtualNoticed)return;virtualNoticed=true;changes.virtualFiles=true;notice.signal();}
    bool accepts(POINTL point){const auto snapshot=callbacks;const auto version=revision;return alive&&enabled&&snapshot&&snapshot->acceptsPoint(point)&&alive&&enabled&&version==revision;}
    // Drag feedback: true without a hover callback (unchanged behaviour).
    bool hoverAccepts(std::optional<POINTL> point){
        const auto snapshot=callbacks;if(!hoverActive||!snapshot||!snapshot->hover)return true;
        if(!point&&!hovered)return true;hovered=point.has_value();
        try{return snapshot->hover(dragPaths,point)||!point;}catch(...){return false;}
    }
    void endHover()noexcept{try{(void)hoverAccepts(std::nullopt);}catch(...){}hovered=hoverActive=false;dragPaths.clear();}
    // Virtual files only: some descriptor format without any CF_HDROP offer.
    bool virtualOnly(IDataObject& source,std::uint64_t version){
        for(const auto descriptor:descriptors){if(!descriptor)continue;FORMATETC format{descriptor,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
            const auto hr=source.QueryGetData(&format);if(!alive||version!=revision)return false;if(SUCCEEDED(hr))return true;}
        return false;
    }
    // IDropTargetHelper calls run after this target's own decision, with the
    // effect actually returned, and never decide acceptance themselves.
    void imageEnter(IDataObject* source,POINTL point,DWORD effect)noexcept{if(!helper||!source)return;POINT p{point.x,point.y};if(SUCCEEDED(helper->DragEnter(owner,source,&p,effect)))imageEntered=true;}
    void imageOver(POINTL point,DWORD effect)noexcept{if(!helper||!imageEntered)return;POINT p{point.x,point.y};helper->DragOver(&p,effect);}
    void imageLeave()noexcept{if(!helper||!imageEntered)return;imageEntered=false;helper->DragLeave();}
    void imageDrop(IDataObject* source,POINTL point,DWORD effect)noexcept{if(!helper||!imageEntered||!source){imageEntered=false;return;}imageEntered=false;POINT p{point.x,point.y};helper->Drop(source,&p,effect);}
    struct ImageScope {Object& o;IDataObject* source;POINTL point;DWORD* effect;enum Kind{enter,over,drop}kind;
        ~ImageScope(){const auto value=effect?*effect:DROPEFFECT_NONE;if(kind==enter)o.imageEnter(source,point,value);else if(kind==over)o.imageOver(point,value);else o.imageDrop(source,point,value);}};
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* source,DWORD,POINTL point,DWORD* effect)override{
        if(!effect)return E_POINTER;const auto allowed=*effect;*effect=DROPEFFECT_NONE;if(!notice.ownerThread())return RPC_E_WRONG_THREAD;if(busy)return RPC_E_CALL_REJECTED;
        Call call(this,notice);ImageScope image{*this,source,point,effect,ImageScope::enter};
        virtualOffered=virtualNoticed=false;
        if(!alive||!enabled||!source)return S_OK;busy=true;struct Reset{bool& b;~Reset(){b=false;}}reset{busy};const auto version=++revision;offered=false;
        FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
        const auto hr=source->QueryGetData(&format);if(!alive||version!=revision)return S_OK;offered=SUCCEEDED(hr);
        if(!offered)virtualOffered=virtualOnly(*source,version);
        // Decode the offered paths once per drag for the hover callback.
        dragPaths.clear();hovered=false;
        try{hoverActive=offered&&callbacks&&callbacks->hover&&(!callbacks->hoverWanted||callbacks->hoverWanted());}catch(...){hoverActive=false;}
        if(hoverActive){try{dragPaths=incoming(*source);}catch(...){dragPaths.clear();}if(!alive||version!=revision)return S_OK;}
        try{const bool accepted=offered&&(allowed&DROPEFFECT_COPY)&&accepts(point)&&hoverAccepts(point);hover(accepted);if(accepted)*effect=DROPEFFECT_COPY;
            else if(virtualOffered&&(allowed&DROPEFFECT_COPY)&&accepts(point))virtualNotice();return S_OK;}catch(...){hover(false);return failure();}
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL point,DWORD* effect)override{if(!effect)return E_POINTER;const auto allowed=*effect;*effect=DROPEFFECT_NONE;if(!notice.ownerThread())return RPC_E_WRONG_THREAD;if(busy)return RPC_E_CALL_REJECTED;Call call(this,notice);
        ImageScope image{*this,nullptr,point,effect,ImageScope::over};busy=true;struct Reset{bool& b;~Reset(){b=false;}}reset{busy};
        try{const bool inside=offered&&(allowed&DROPEFFECT_COPY)&&accepts(point);bool accepted=false;
            // Outside the accepting area the owner's highlight ends; the paths stay for a re-entry.
            if(inside)accepted=hoverAccepts(point);else if(hovered)(void)hoverAccepts(std::nullopt);
            hover(accepted);if(accepted)*effect=DROPEFFECT_COPY;
            else if(virtualOffered&&!virtualNoticed&&(allowed&DROPEFFECT_COPY)&&accepts(point))virtualNotice();return S_OK;}catch(...){hover(false);return failure();}}
    HRESULT STDMETHODCALLTYPE DragLeave()override{if(!notice.ownerThread())return RPC_E_WRONG_THREAD;Call call(this,notice);++revision;offered=virtualOffered=false;endHover();hover(false);imageLeave();return S_OK;}
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* source,DWORD,POINTL point,DWORD* effect)override{
        if(!effect)return E_POINTER;const auto allowed=*effect;*effect=DROPEFFECT_NONE;if(!notice.ownerThread())return RPC_E_WRONG_THREAD;if(busy)return RPC_E_CALL_REJECTED;Call call(this,notice);
        ImageScope image{*this,source,point,effect,ImageScope::drop};
        offered=virtualOffered=false;hover(false);endHover();if(!source||!(allowed&DROPEFFECT_COPY)||changes.committed)return S_OK;
        busy=true;struct Reset{bool& b;~Reset(){b=false;}}reset{busy};
        try{if(!accepts(point))return S_OK;const auto version=revision;
            auto paths=incoming(*source); // external COM + medium release finish here
            if(!alive||!enabled||version!=revision)return S_OK;
            const auto snapshot=callbacks;
            // No user-visible callback runs here. The owner-provided transaction
            // is synchronous and owns its dependencies until it returns.
            if(!snapshot->commit(paths)){error("Shelf references could not be committed");return S_OK;}
            *effect=DROPEFFECT_COPY;
            if(alive){changes.committed=true;changes.paths=std::move(paths);changes.error.reset();changes.errorCode.reset();notice.signal();}
            return S_OK;
        }catch(const StoreError& e){error(e.what(),e.code());return S_OK;}catch(const std::exception& e){error(e.what());return S_OK;}catch(...){error("Shelf drop failed");return S_OK;}
    }
    void stop()noexcept{if(!alive)return;alive=false;enabled=false;offered=virtualOffered=false;++revision;callbacks.reset();imageLeave();helper.Reset();notice.detach();if(registered){registered=false;RevokeDragDrop(owner);}}
    HWND owner{notice.route.owner};
};
ShelfDropTarget::ShelfDropTarget(ShelfTransferRoute route,Callbacks callbacks):object_(new Object(route,std::move(callbacks))){}
ShelfDropTarget::~ShelfDropTarget(){if(!object_->notice.ownerThread())std::terminate();stop();object_->Release();}
IDropTarget* ShelfDropTarget::dropTarget()const noexcept{return object_;}
HRESULT ShelfDropTarget::registerTarget()noexcept{auto* object=object_;if(!object->notice.ownerThread())return RPC_E_WRONG_THREAD;if(!object->alive)return E_UNEXPECTED;if(object->registered)return S_FALSE;
    const auto hr=RegisterDragDrop(object->owner,object);if(SUCCEEDED(hr))object->registered=true;return hr;
}
void ShelfDropTarget::setEnabled(bool value)noexcept{auto* object=object_;if(!object->notice.ownerThread()||!object->alive||object->enabled==value)return;object->enabled=value;++object->revision;if(!value){object->offered=object->virtualOffered=false;object->hover(false);}}
void ShelfDropTarget::setDragImages(bool value)noexcept{auto* object=object_;if(!object->notice.ownerThread()||!object->alive||object->images==value)return;object->images=value;
    if(!value){object->imageLeave();object->helper.Reset();return;}
    // In-process Shell helper only; unavailable helpers leave drags image-less.
    if(FAILED(CoCreateInstance(CLSID_DragDropHelper,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&object->helper))))object->helper.Reset();}
void ShelfDropTarget::stop()noexcept{if(object_&&object_->notice.ownerThread())object_->stop();}
ShelfDropChanges ShelfDropTarget::takeChanges(UINT_PTR generation){auto* object=object_;if(!object->notice.drain(generation))return {};auto result=std::move(object->changes);object->changes={};object->changes.hovering=result.hovering;return result;}
#endif
} // namespace endfield::native
