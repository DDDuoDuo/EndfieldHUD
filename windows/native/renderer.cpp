#include "renderer.hpp"
#include "source_graphics.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d10.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <limits>
#include <map>
#include <utility>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT result, const char *operation) {
    if (FAILED(result)) throw RendererError(operation, static_cast<std::int32_t>(result));
}
void require(bool condition, const char *reason) {
    if (!condition) throw std::invalid_argument(reason);
}
void dimensions(std::uint32_t width, std::uint32_t height) {
    require(width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
            std::uint64_t(width) * height <= Renderer::maximumRenderPixels, "Render dimensions exceed bounds");
}
void identity(const std::string &id) { require(!id.empty() && id.size() <= 512, "Invalid retained source ID"); }
const std::array<double, 256> &linearSRGBBytes() {
    static const auto values = [] {
        std::array<double, 256> result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            const double encoded = static_cast<double>(i) / 255;
            result[i] = encoded <= .04045 ? encoded / 12.92 : std::pow((encoded + .055) / 1.055, 2.4);
        }
        return result;
    }();
    return values;
}
const std::array<std::uint16_t,256>& opaqueSRGB16Bytes(){
    // One 512-byte table, with exactly the former double/lround conversion.
    static const auto values=[] {std::array<std::uint16_t,256> result{};const auto&linear=linearSRGBBytes();
        for(std::size_t i=0;i<result.size();++i)result[i]=static_cast<std::uint16_t>(std::lround(linear[i]*65535));return result;}();
    return values;
}
std::array<float, 16> matrix(const core::Matrix4 &value) {
    std::array<float, 16> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        require(std::isfinite(value.values[i]) && std::abs(value.values[i]) <= std::numeric_limits<float>::max(),
                "Invalid GPU transform");
        result[i] = static_cast<float>(value.values[i]);
    }
    return result;
}
struct alignas(16) MaskUniform {
    std::array<float, 16> worldToLocal;
    std::array<float, 4> bounds;
    std::array<float, 4> corners{};
};
struct alignas(16) ObjectUniform {
    std::array<float, 16> world;
    std::array<float, 4> tint;
    float opacity;
    std::uint32_t maskCount;
    std::uint32_t shutterEnabled{}, shutterStrips{};
    std::array<MaskUniform, 8> masks{};
    std::array<float, 16> shutterWorldToLocal{};
    std::array<std::array<float, 4>, 30> shutterEdges{};
    std::array<float,16> alphaWorldToLocal{};
    std::array<float,4> alphaBounds{},alphaControl{};
    std::array<float,16> angularWorldToLocal{};
    std::array<float,4> angularCenterControl{},angularNormals{};
};
static_assert(sizeof(Vertex) == 36 && sizeof(MaskUniform) == 96 && sizeof(ObjectUniform) == 1600);
void shutterUniforms(ObjectUniform& result, const PlaneShutter& shutter) {
    result.shutterEnabled = static_cast<std::uint32_t>(shutter.path.index())+1;
    result.shutterWorldToLocal = matrix(shutter.worldToLocal);
    std::visit([&](const auto& strips){
    for (std::size_t strip = 0; strip < strips.size(); ++strip) {
        const auto& polygon = strips[strip];
        double scale = 1;
        for (const auto vertex : polygon) {
            require(std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                std::abs(vertex.x) <= std::numeric_limits<float>::max() &&
                std::abs(vertex.y) <= std::numeric_limits<float>::max(), "Invalid shutter vertex");
            scale = std::max({scale, std::abs(vertex.x), std::abs(vertex.y)});
        }
        // Translate the area calculation to the first vertex to avoid losing
        // precision when a small source strip has a nonzero local origin.
        double area = 0;
        bool collinear = true;
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
            const auto a = polygon[i], b = polygon[i + 1], origin = polygon[0];
            const auto cross = (a.x-origin.x)*(b.y-origin.y)-(a.y-origin.y)*(b.x-origin.x);
            area += cross; collinear = collinear && cross == 0;
        }
        if (area == 0) {
            require(collinear, "Self-intersecting zero-area shutter polygon");
            continue; // Exact source collapsed endpoint; never a full plane.
        }
        const auto winding = area > 0 ? 1. : -1.;
        const auto tolerance = scale * 8 * std::numeric_limits<float>::epsilon();
        for (std::size_t edge = 0; edge < polygon.size(); ++edge) {
            const auto a = polygon[edge], b = polygon[(edge + 1) % polygon.size()];
            const auto dx = b.x-a.x, dy = b.y-a.y, length = std::hypot(dx, dy);
            if (length == 0) continue; // Repeated full-open bevel vertex.
            const auto nx = -dy / length * winding, ny = dx / length * winding;
            const auto constant = -(nx*a.x+ny*a.y);
            require(std::isfinite(constant) && std::abs(constant) <= std::numeric_limits<float>::max(),
                "Shutter edge exceeds GPU range");
            // Permit harmless roundoff at collinear source path-morph edges,
            // but reject arbitrary concave/crossing paths before any upload.
            for (const auto vertex : polygon)
                require(nx*(vertex.x-a.x)+ny*(vertex.y-a.y) >= -tolerance, "Shutter polygon is not convex");
            result.shutterEdges[strip*polygon.size()+edge] = {
                static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(constant), 0};
        }
        result.shutterStrips |= std::uint32_t{1} << strip;
    }
    },shutter.path);
}
ObjectUniform uniforms(const DrawObject &object) {
    identity(object.sourceID);
    require(object.masks.size() <= 8, "Too many plane masks");
    ObjectUniform result{};
    result.world = matrix(object.world);
    result.tint = object.linearTint;
    for (float color : result.tint)
        require(std::isfinite(color) && color >= 0 && color <= 1, "Plain material tint must be normalized linear RGBA");
    require(std::isfinite(object.opacity) && object.opacity >= 0 && object.opacity <= 1, "Invalid object opacity");
    result.opacity = object.opacity;
    result.maskCount = static_cast<std::uint32_t>(object.masks.size());
    for (std::size_t i = 0; i < object.masks.size(); ++i) {
        const auto &mask = object.masks[i];
        result.masks[i].worldToLocal = matrix(mask.worldToLocal);
        const auto &r = mask.bounds;
        require(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
                r.width > 0 && r.height > 0, "Invalid plane-mask rectangle");
        require(std::isfinite(mask.cornerRadius) && mask.cornerRadius >= 0 &&
                mask.cornerRadius <= std::min(r.width, r.height) * .5, "Invalid plane-mask corner radius");
        result.masks[i].corners[0] = static_cast<float>(mask.cornerRadius);
        const std::array<double, 4> bounds{r.x, r.y, r.x + r.width, r.y + r.height};
        for (std::size_t j = 0; j < bounds.size(); ++j) {
            require(std::isfinite(bounds[j]) && std::abs(bounds[j]) <= std::numeric_limits<float>::max(), "Plane-mask extent exceeds GPU range");
            result.masks[i].bounds[j] = static_cast<float>(bounds[j]);
        }
    }
    if (object.shutter) shutterUniforms(result, *object.shutter);
    if(object.alphaMask){const auto& mask=*object.alphaMask;identity(mask.textureID);
        result.alphaWorldToLocal=matrix(mask.worldToLocal);const auto& b=mask.bounds;
        require(std::isfinite(b.x)&&std::isfinite(b.y)&&std::isfinite(b.width)&&std::isfinite(b.height)&&b.width>0&&b.height>0,"Invalid alpha-mask rectangle");
        const std::array<double,4> bounds{b.x,b.y,b.x+b.width,b.y+b.height};
        for(unsigned k=0;k<4;++k){require(std::isfinite(bounds[k])&&std::abs(bounds[k])<=std::numeric_limits<float>::max(),"Alpha-mask bounds exceed GPU range");result.alphaBounds[k]=static_cast<float>(bounds[k]);}
        require(result.alphaBounds[2]>result.alphaBounds[0]&&result.alphaBounds[3]>result.alphaBounds[1],"Alpha-mask bounds collapse at GPU precision");result.alphaControl[0]=1;
    }
    if(object.angularMask){const auto& mask=*object.angularMask;
        constexpr double tau=6.283185307179586476925286766559;
        require(std::isfinite(mask.startAngle)&&std::isfinite(mask.sweepAngle)&&mask.sweepAngle>=0&&mask.sweepAngle<=tau,"Invalid angular-mask angles");
        require(std::isfinite(mask.center.x)&&std::isfinite(mask.center.y)&&std::abs(mask.center.x)<=std::numeric_limits<float>::max()&&std::abs(mask.center.y)<=std::numeric_limits<float>::max(),"Invalid angular-mask center");
        result.angularWorldToLocal=matrix(mask.worldToLocal);
        // Reduce before adding sweep, so a large finite caller angle cannot
        // discard all endpoint precision or overflow.
        const double start=std::remainder(mask.startAngle,tau),end=start+mask.sweepAngle;
        result.angularNormals={static_cast<float>(-std::sin(start)),static_cast<float>(std::cos(start)),static_cast<float>(std::sin(end)),static_cast<float>(-std::cos(end))};
        // mode: 1 empty, 2 intersection, 3 union, 4 full. Exact endpoints never
        // depend on rounded sin/cos or derivatives of a degenerate wedge.
        const float mode=mask.sweepAngle==0?1.f:mask.sweepAngle==tau?4.f:mask.sweepAngle<=tau*.5?2.f:3.f;
        result.angularCenterControl={static_cast<float>(mask.center.x),static_cast<float>(mask.center.y),mode,0};
        if(mask.endPlane){const auto& p=*mask.endPlane;
            for(double value:p)require(std::isfinite(value),"Invalid angular endpoint plane");
            const double length=std::hypot(p[0],p[1]);require(std::isfinite(length)&&length>0,"Degenerate angular endpoint plane");
            const double offset=p[2]/length;require(std::isfinite(offset)&&std::abs(offset)<=std::numeric_limits<float>::max(),"Angular endpoint offset exceeds GPU range");
            result.angularNormals[2]=static_cast<float>(p[0]/length);result.angularNormals[3]=static_cast<float>(p[1]/length);result.angularCenterControl[3]=static_cast<float>(offset);
        }
    }
    return result;
}
ComPtr<ID3DBlob> compile(const std::filesystem::path &path, const char *entry, const char *target) {
    ComPtr<ID3DBlob> program, errors;
    const HRESULT status = D3DCompileFromFile(path.c_str(), nullptr, nullptr, entry, target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &program, &errors);
    if (FAILED(status)) {
        std::string message = std::string("Compile ") + entry;
        if (errors) message += ": " + std::string(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
        throw RendererError(std::move(message), static_cast<std::int32_t>(status));
    }
    return program;
}
ComPtr<ID3D11Buffer> immutableBuffer(ID3D11Device *device, UINT binding, const void *data, std::size_t bytes) {
    require(bytes > 0 && bytes <= std::numeric_limits<UINT>::max(), "GPU buffer size exceeds bounds");
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(bytes);
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = binding;
    D3D11_SUBRESOURCE_DATA source{};
    source.pSysMem = data;
    ComPtr<ID3D11Buffer> buffer;
    checked(device->CreateBuffer(&description, &source, &buffer), "Create retained buffer");
    return buffer;
}
} // namespace

namespace detail {
// Shared by the real upload path and byte-exact tests; deliberately not part
// of the public renderer API. Output aliases neither the encoded input nor GPU
// storage. Partial alpha keeps the original operation order and rounding.
void prepareTextureRGBA16(std::span<const std::uint8_t> input,TextureColorSpace colorSpace,std::span<std::uint16_t> output){
    require(input.size()%4==0&&output.size()==input.size(),"Invalid native texture conversion span");
    require(colorSpace==TextureColorSpace::sRGB||colorSpace==TextureColorSpace::linear,"Invalid native texture color space");
    const bool sRGB=colorSpace==TextureColorSpace::sRGB;
    const auto*opaque=sRGB?opaqueSRGB16Bytes().data():nullptr;
    const auto*linear=sRGB?linearSRGBBytes().data():nullptr;
    for(std::size_t pixel=0;pixel<input.size();pixel+=4){const auto alphaByte=input[pixel+3];
        if(alphaByte==0){output[pixel]=output[pixel+1]=output[pixel+2]=output[pixel+3]=0;continue;}
        if(alphaByte==255){for(std::size_t channel=0;channel<3;++channel){const auto encoded=input[pixel+channel];output[pixel+channel]=sRGB?opaque[encoded]:static_cast<std::uint16_t>(unsigned(encoded)*257u);}output[pixel+3]=65535;continue;}
        const double alpha=alphaByte/255.0;
        for(std::size_t channel=0;channel<3;++channel){const auto encoded=input[pixel+channel];const double value=sRGB?linear[encoded]:encoded/255.0;
            output[pixel+channel]=static_cast<std::uint16_t>(std::lround(value*alpha*65535));}
        output[pixel+3]=static_cast<std::uint16_t>(std::lround(alpha*65535));
    }
}
} // namespace detail

void validatePlaneShutter(const PlaneShutter& shutter) {
    ObjectUniform candidate{};
    shutterUniforms(candidate, shutter);
}
void validateDrawObject(const DrawObject& object) {(void)uniforms(object);}

RendererError::RendererError(std::string operation, std::int32_t code)
    : std::runtime_error(std::move(operation) + " (HRESULT " + std::to_string(code) + ")"), code_(code) {}

namespace {
struct MediaBudget {std::atomic<std::size_t>bytes{};};
struct RetainedMediaBytes {
    std::shared_ptr<MediaBudget>budget;std::size_t bytes{};
    ~RetainedMediaBytes(){if(budget)budget->bytes.fetch_sub(bytes,std::memory_order_relaxed);}
};
}
struct RendererMediaTexture::Impl {
    std::string id;unsigned width{},height{};std::size_t bytes{};
    std::weak_ptr<const int>epoch;std::atomic<bool>registered{true};
    std::shared_ptr<MediaBudget>budget;
    ComPtr<IDXGISurface>surface;
    ComPtr<ID3D11ShaderResourceView>encoded;
    ComPtr<ID3D11RenderTargetView>linear;
    ~Impl(){if(budget)budget->bytes.fetch_sub(bytes,std::memory_order_relaxed);}
};
RendererMediaTexture::RendererMediaTexture(std::unique_ptr<Impl>impl):impl_(std::move(impl)){}
RendererMediaTexture::~RendererMediaTexture()=default;
bool RendererMediaTexture::valid()const noexcept{return impl_->registered.load(std::memory_order_relaxed)&&!impl_->epoch.expired();}
unsigned RendererMediaTexture::width()const noexcept{return impl_->width;}
unsigned RendererMediaTexture::height()const noexcept{return impl_->height;}
const std::string&RendererMediaTexture::sourceID()const noexcept{return impl_->id;}
void*RendererMediaTexture::targetSurface()const noexcept{return impl_->surface.Get();}

struct Renderer::Impl {
    struct Mesh {
        ComPtr<ID3D11Buffer> vertices, indices;
        UINT indexCount{};
        std::uint64_t revision{};
        std::size_t bytes{};
        bool groupOwned{};
    };
    struct Texture {
        ComPtr<ID3D11ShaderResourceView> view;
        TextureFilter filter{};
        std::uint64_t revision{};
        std::size_t bytes{};
        bool groupOwned{};
        bool mediaOwned{};
        std::shared_ptr<RetainedMediaBytes>mediaPoster;
    };
    struct Draw {
        std::string sourceID;
        Mesh *mesh{};
        Texture *texture{},*alphaMask{};
        ObjectUniform values{};
        ComPtr<ID3D11Buffer> constants;
    };
    struct NativeGroup {
        NativeGroupTarget requested;
        core::Rect coverage;
        unsigned width{},height{};
        std::size_t bytes{};
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11Buffer> camera;
        DrawObject output;
        std::vector<Draw> draws;
        std::vector<ObjectUniform> staged;
        bool dirty{true};
    };
    DWORD ownerThread{GetCurrentThreadId()};
    HWND window{};
    bool offscreen{},mediaVideo{};
    std::uint32_t width{}, height{};
    RendererStats counters;
    bool cameraDirty{true}, readbackReady{};
    std::array<float, 16> cameraValues = matrix(core::Matrix4{});
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    ComPtr<ID3D11Texture2D> backBuffer, linearTarget;
    ComPtr<ID3D11RenderTargetView> outputView, linearView;
    ComPtr<ID3D11ShaderResourceView> linearResource;
    ComPtr<ID3D11VertexShader> sceneVS, compositeVS;
    ComPtr<ID3D11PixelShader> scenePS, compositePS,mediaPS;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> cameraBuffer;
    ComPtr<ID3D11BlendState> overBlend, replaceBlend;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> noDepth;
    ComPtr<ID3D11SamplerState> nearestSampler, linearSampler;
    std::map<std::string, Mesh> meshes;
    std::map<std::string, Texture> textures;
    Texture white;
    std::vector<Draw> draws;
    std::vector<ObjectUniform> stagedObjectValues;
    std::map<std::string,std::unique_ptr<NativeGroup>> groups;
    std::map<std::string,std::shared_ptr<RendererMediaTexture>>media;
    std::shared_ptr<const int>epoch=std::make_shared<const int>(0);
    std::shared_ptr<MediaBudget>mediaBudget=std::make_shared<MediaBudget>();
    std::size_t groupBytes{};
    std::unique_ptr<SourceGraphics> source;

    bool assignDraws(std::vector<Draw>&,std::vector<ObjectUniform>&,
        std::span<const DrawObject>,bool localGroup);
    bool configureGroup(std::string,const NativeGroupTarget&,std::optional<std::span<const DrawObject>>);
    void groupDrawBudget(const NativeGroup* replaced,std::size_t count)const{
        require(count<=Renderer::maximumObjects,"Native group draw count exceeds limits");
        for(const auto&[id,group]:groups){(void)id;if(group.get()==replaced)continue;
            require(group->draws.size()<=Renderer::maximumObjects-count,"Aggregate native group draw count exceeds limits");count+=group->draws.size();}
    }
    void renderNativeDraws(std::span<const Draw>,ID3D11RenderTargetView*,ID3D11Buffer*,unsigned,unsigned,bool);
    bool meshReferenced(const Mesh* mesh)const{
        for(const auto&draw:draws)if(draw.mesh==mesh)return true;
        for(const auto&[id,group]:groups){(void)id;for(const auto&draw:group->draws)if(draw.mesh==mesh)return true;}return false;
    }
    bool textureReferenced(const Texture* texture)const{
        for(const auto&draw:draws)if((draw.texture==texture||draw.alphaMask==texture))return true;
        for(const auto&[id,group]:groups){(void)id;for(const auto&draw:group->draws)if((draw.texture==texture||draw.alphaMask==texture))return true;}return false;
    }
    void invalidate(const Mesh* mesh){for(auto&[id,group]:groups){(void)id;for(const auto&draw:group->draws)if(draw.mesh==mesh){group->dirty=true;break;}}}
    void invalidate(const Texture* texture){for(auto&[id,group]:groups){(void)id;for(const auto&draw:group->draws)if((draw.texture==texture||draw.alphaMask==texture)){group->dirty=true;break;}}}

    ~Impl() {
        if (target) target->SetRoot(nullptr);
        if (visual) visual->SetContent(nullptr);
        if (composition) composition->Commit();
        if (context) { context->ClearState(); context->Flush(); }
    }
    void thread() const { require(GetCurrentThreadId() == ownerThread, "Renderer calls must stay on its creating UI thread"); }
    void budget(std::size_t oldBytes, std::size_t newBytes) const {
        require(newBytes <= Renderer::maximumResourceBytes &&
                counters.resourceBytes - oldBytes + mediaBudget->bytes.load(std::memory_order_relaxed) <= Renderer::maximumResourceBytes - newBytes,
                "Retained GPU resource budget exceeded");
    }
    Texture texture(TextureData input, std::uint64_t revision) {
        // Filtering must interpolate premultiplied linear values. Sampling
        // straight RGB/alpha and multiplying afterwards creates dark borders
        // against transparent texels. Conversion happens only on replacement.
        std::vector<std::uint16_t> premultiplied(input.straightRGBA.size());
        detail::prepareTextureRGBA16(input.straightRGBA,input.colorSpace,premultiplied);
        D3D11_TEXTURE2D_DESC description{};
        description.Width = input.width; description.Height = input.height;
        description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
        description.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA pixels{};
        pixels.pSysMem = premultiplied.data(); pixels.SysMemPitch = input.width * 8;
        ComPtr<ID3D11Texture2D> resource;
        checked(device->CreateTexture2D(&description, &pixels, &resource), "Upload retained texture");
        Texture result;
        checked(device->CreateShaderResourceView(resource.Get(), nullptr, &result.view), "Create retained texture view");
        result.filter = input.filter; result.revision = revision; result.bytes = premultiplied.size() * sizeof(std::uint16_t);
        return result;
    }
    void bindTargets() {
        if (offscreen) {
            D3D11_TEXTURE2D_DESC output{};
            output.Width = width; output.Height = height;
            output.MipLevels = output.ArraySize = output.SampleDesc.Count = 1;
            output.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            output.BindFlags = D3D11_BIND_RENDER_TARGET;
            checked(device->CreateTexture2D(&output, nullptr, &backBuffer), "Create isolated output texture");
        } else checked(swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "Get composition back buffer");
        checked(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &outputView), "Bind encoded-premultiplied output");
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width; description.Height = height;
        description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
        description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        checked(device->CreateTexture2D(&description, nullptr, &linearTarget), "Create linear composition target");
        checked(device->CreateRenderTargetView(linearTarget.Get(), nullptr, &linearView), "Bind linear composition target");
        checked(device->CreateShaderResourceView(linearTarget.Get(), nullptr, &linearResource), "Bind linear composition input");
    }
    void initialize(HWND targetWindow, std::uint32_t w, std::uint32_t h, const RendererOptions &options) {
        window = targetWindow;
        dimensions(w, h);
        require(IsWindow(window) != FALSE && !options.shaderPath.empty(), "A native HUD window and shader path are required");
        require(options.driver == Driver::hardware || options.driver == Driver::warpForTests, "Unknown graphics driver selection");
        require(options.target == RenderTarget::composition || options.target == RenderTarget::offscreenForTests, "Unknown render target selection");
        offscreen = options.target == RenderTarget::offscreenForTests;
        DWORD process{};
        GetWindowThreadProcessId(window, &process);
        require(process == GetCurrentProcessId(), "Renderer target must belong to this application");
        require((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0,
                "Composition window must use WS_EX_NOREDIRECTIONBITMAP");
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
        mediaVideo=options.mediaVideo;
        // The explicit WARP fixture exercises owned texture conversion, not a
        // hardware video decoder. Some installed software runtimes reject the
        // VIDEO_SUPPORT creation flag even though their shader path works.
        // Production hardware still requires the requested video capability;
        // do not silently replace its device or downgrade its creation flags.
        const bool requireVideoDriver=mediaVideo&&options.driver==Driver::hardware;
        checked(D3D11CreateDevice(nullptr, options.driver == Driver::hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT|(requireVideoDriver?D3D11_CREATE_DEVICE_VIDEO_SUPPORT:0u), levels, 1, D3D11_SDK_VERSION, &device, nullptr, &context), "Create D3D11 device");
        if(mediaVideo){ComPtr<ID3D10Multithread>multithread;checked(device.As(&multithread),"Enable shared media device thread protection");multithread->SetMultithreadProtected(TRUE);}
        if (!offscreen) {
        ComPtr<IDXGIDevice1> dxgi;
        checked(device.As(&dxgi), "Get DXGI device");
        checked(dxgi->SetMaximumFrameLatency(1), "Bound presentation latency");
        ComPtr<IDXGIAdapter> adapter;
        checked(dxgi->GetAdapter(&adapter), "Get D3D adapter");
        ComPtr<IDXGIFactory2> factory;
        checked(adapter->GetParent(IID_PPV_ARGS(&factory)), "Get composition factory");
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = w; description.Height = h;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        checked(factory->CreateSwapChainForComposition(device.Get(), &description, nullptr, &swapchain), "Create premultiplied composition swap chain");
        checked(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition)), "Create DirectComposition device");
        checked(composition->CreateTargetForHwnd(window, TRUE, &target), "Create owned HUD composition target");
        checked(composition->CreateVisual(&visual), "Create HUD composition visual");
        checked(visual->SetContent(swapchain.Get()), "Attach HUD swap chain");
        checked(target->SetRoot(visual.Get()), "Attach HUD composition root");
        checked(composition->Commit(), "Commit HUD composition root");
        }
        width = w; height = h; bindTargets();

        auto vs = compile(options.shaderPath, "SceneVS", "vs_5_0");
        auto ps = compile(options.shaderPath, "ScenePS", "ps_5_0");
        checked(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &sceneVS), "Create scene vertex shader");
        checked(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &scenePS), "Create scene pixel shader");
        const D3D11_INPUT_ELEMENT_DESC elements[]{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        checked(device->CreateInputLayout(elements, 3, vs->GetBufferPointer(), vs->GetBufferSize(), &layout), "Create retained mesh layout");
        vs = compile(options.shaderPath, "CompositeVS", "vs_5_0");
        ps = compile(options.shaderPath, "CompositePS", "ps_5_0");
        checked(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &compositeVS), "Create presentation vertex shader");
        checked(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &compositePS), "Create presentation pixel shader");
        if(mediaVideo){ps=compile(options.shaderPath,"MediaConvertPS","ps_5_0");checked(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&mediaPS),"Create same-device media conversion shader");}
        D3D11_BUFFER_DESC camera{};
        camera.ByteWidth = 64; camera.Usage = D3D11_USAGE_DYNAMIC;
        camera.BindFlags = D3D11_BIND_CONSTANT_BUFFER; camera.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        checked(device->CreateBuffer(&camera, nullptr, &cameraBuffer), "Create retained camera uniform");
        D3D11_BLEND_DESC blend{};
        auto &over = blend.RenderTarget[0];
        over.BlendEnable = TRUE;
        over.SrcBlend = over.SrcBlendAlpha = D3D11_BLEND_ONE;
        over.DestBlend = over.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        over.BlendOp = over.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        over.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        checked(device->CreateBlendState(&blend, &overBlend), "Create linear premultiplied over blending");
        over.BlendEnable = FALSE;
        checked(device->CreateBlendState(&blend, &replaceBlend), "Create output replacement blending");
        D3D11_RASTERIZER_DESC rasterDescription{};
        rasterDescription.FillMode = D3D11_FILL_SOLID; rasterDescription.CullMode = D3D11_CULL_NONE;
        rasterDescription.DepthClipEnable = TRUE;
        checked(device->CreateRasterizerState(&rasterDescription, &raster), "Create planar raster state");
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE; depth.StencilEnable = FALSE;
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        depth.FrontFace.StencilFailOp = depth.FrontFace.StencilDepthFailOp = depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        depth.BackFace = depth.FrontFace;
        checked(device->CreateDepthStencilState(&depth, &noDepth), "Create ordered UI depth state");
        D3D11_SAMPLER_DESC sampler{};
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxAnisotropy = 1; sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        checked(device->CreateSamplerState(&sampler, &nearestSampler), "Create point texture sampler");
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        checked(device->CreateSamplerState(&sampler, &linearSampler), "Create linear texture sampler");
        const std::array<std::uint8_t, 4> opaqueWhite{255, 255, 255, 255};
        white = texture({1, 1, opaqueWhite, TextureColorSpace::linear, TextureFilter::nearest}, 0);
        counters.initialized = true;
    }
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;
void Renderer::initialize(void *window, std::uint32_t width, std::uint32_t height, const RendererOptions &options) {
    if (impl_) {
        impl_->thread();
        require(impl_->window != static_cast<HWND>(window), "Reset is required before reinitializing the same composition window");
    }
    auto next = std::make_unique<Impl>();
    next->initialize(static_cast<HWND>(window), width, height, options);
    impl_ = std::move(next);
}
void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    require(impl_ != nullptr, "Renderer is not initialized"); impl_->thread(); dimensions(width, height);
    if (impl_->width == width && impl_->height == height) return;
    try {
        auto &r = *impl_;
        r.readbackReady = false;
        r.context->ClearState();
        r.outputView.Reset(); r.backBuffer.Reset(); r.linearView.Reset(); r.linearResource.Reset(); r.linearTarget.Reset();
        if (!r.offscreen) checked(r.swapchain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0), "Resize composition buffers");
        r.width = width; r.height = height; r.bindTargets();
    } catch (...) { reset(); throw; }
}
bool Renderer::setMesh(std::string sourceID, std::uint64_t revision, MeshData input) {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread(); identity(sourceID);
    const auto existing = r.meshes.find(sourceID);
    require(existing==r.meshes.end()||!existing->second.groupOwned,"Native group output mesh is managed by its group");
    if (existing != r.meshes.end() && existing->second.revision == revision) return false;
    require(existing != r.meshes.end() || r.meshes.size() < maximumMeshes, "Retained mesh count exceeds bounds");
    require(!input.vertices.empty() && input.vertices.size() <= 1000000 && !input.indices.empty() &&
            input.indices.size() <= 3000000 && input.indices.size() % 3 == 0, "Invalid triangle mesh size");
    for (const auto &vertex : input.vertices) {
        for (float value : vertex.position) require(std::isfinite(value), "Nonfinite mesh position");
        for (float value : vertex.uv) require(std::isfinite(value), "Nonfinite mesh UV");
        for (float value : vertex.linearColor) require(std::isfinite(value) && value >= 0 && value <= 1, "Invalid linear vertex color");
    }
    for (auto index : input.indices) require(index < input.vertices.size(), "Triangle index exceeds vertex count");
    const auto bytes = input.vertices.size_bytes() + input.indices.size_bytes();
    const auto previousBytes = existing == r.meshes.end() ? 0 : existing->second.bytes;
    r.budget(previousBytes, bytes);
    Impl::Mesh mesh;
    mesh.vertices = immutableBuffer(r.device.Get(), D3D11_BIND_VERTEX_BUFFER, input.vertices.data(), input.vertices.size_bytes());
    mesh.indices = immutableBuffer(r.device.Get(), D3D11_BIND_INDEX_BUFFER, input.indices.data(), input.indices.size_bytes());
    mesh.indexCount = static_cast<UINT>(input.indices.size()); mesh.revision = revision; mesh.bytes = bytes;
    auto updated=r.meshes.insert_or_assign(std::move(sourceID), std::move(mesh));r.invalidate(&updated.first->second);
    r.counters.resourceBytes = r.counters.resourceBytes - previousBytes + bytes; ++r.counters.meshUploads;
    return true;
}
bool Renderer::setTexture(std::string sourceID, std::uint64_t revision, TextureData input) {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread(); identity(sourceID);
    const auto existing = r.textures.find(sourceID);
    require(existing==r.textures.end()||(!existing->second.groupOwned&&!existing->second.mediaOwned),"Native group/media texture is managed by its owner");
    if (existing != r.textures.end() && existing->second.revision == revision) return false;
    require(existing != r.textures.end() || r.textures.size() < maximumTextures, "Retained texture count exceeds bounds");
    require(input.width > 0 && input.height > 0 && input.width <= 8192 && input.height <= 8192 &&
            std::uint64_t(input.width) * input.height * 4 == input.straightRGBA.size(), "Invalid straight RGBA8 texture dimensions");
    require((input.colorSpace == TextureColorSpace::sRGB || input.colorSpace == TextureColorSpace::linear) &&
            (input.filter == TextureFilter::nearest || input.filter == TextureFilter::linear), "Invalid texture sampling policy");
    const auto previousBytes = existing == r.textures.end() ? 0 : existing->second.bytes;
    const auto nativeBytes = std::size_t(input.width) * input.height * 8;
    r.budget(previousBytes, nativeBytes);
    auto texture = r.texture(input, revision);
    auto updated=r.textures.insert_or_assign(std::move(sourceID), std::move(texture));r.invalidate(&updated.first->second);
    r.counters.resourceBytes = r.counters.resourceBytes - previousBytes + nativeBytes; ++r.counters.textureUploads;
    return true;
}
std::shared_ptr<void>Renderer::mediaDevice()const{
    require(impl_&&impl_->mediaVideo,"Renderer was not initialized for same-device media");impl_->thread();auto device=impl_->device;
    return std::shared_ptr<void>(device.Detach(),[](void*p){static_cast<ID3D11Device*>(p)->Release();});
}
std::shared_ptr<RendererMediaTexture>Renderer::createMediaTexture(std::string id,unsigned w,unsigned h){
    require(impl_&&impl_->mediaVideo,"Renderer was not initialized for same-device media");auto&r=*impl_;r.thread();identity(id);
    require(w&&h&&w<=8192&&h<=8192&&std::uint64_t(w)*h<=maximumMediaPixels,"Media target dimensions exceed bounds");
    require(r.media.size()<maximumMediaTargets&&r.textures.size()<maximumTextures,"Media texture count exceeds bounds");
    require(!r.textures.contains(id),"Media texture identity is already installed; explicitly retire it first");
    const auto bytes=std::size_t(w)*h*12;const auto held=r.mediaBudget->bytes.load(std::memory_order_relaxed);
    require(bytes<=maximumMediaBytes&&held<=maximumMediaBytes-bytes,"Media resources/borrowers exceed bounds");r.budget(0,bytes);
    auto media=std::make_unique<RendererMediaTexture::Impl>();media->id=id;media->width=w;media->height=h;media->epoch=r.epoch;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D>encoded;checked(r.device->CreateTexture2D(&desc,nullptr,&encoded),"Create bounded frame-server surface");checked(encoded.As(&media->surface),"Get owned frame-server DXGI surface");checked(r.device->CreateShaderResourceView(encoded.Get(),nullptr,&media->encoded),"Create retained frame-server view");
    desc.Format=DXGI_FORMAT_R16G16B16A16_UNORM;ComPtr<ID3D11Texture2D>linear;checked(r.device->CreateTexture2D(&desc,nullptr,&linear),"Create retained linear media texture");checked(r.device->CreateRenderTargetView(linear.Get(),nullptr,&media->linear),"Create media conversion target");
    Impl::Texture texture;texture.filter=TextureFilter::linear;texture.mediaOwned=true;checked(r.device->CreateShaderResourceView(linear.Get(),nullptr,&texture.view),"Create retained media scene view");
    // Initialize both targets before publishing so no frame ever samples
    // uninitialized GPU storage while the engine is still loading.
    const float transparent[4]{};ComPtr<ID3D11RenderTargetView>inputView;checked(r.device->CreateRenderTargetView(encoded.Get(),nullptr,&inputView),"Initialize frame-server surface");r.context->ClearRenderTargetView(inputView.Get(),transparent);r.context->ClearRenderTargetView(media->linear.Get(),transparent);
    std::map<std::string,Impl::Texture>stagedTexture;stagedTexture.emplace(id,std::move(texture));
    media->bytes=bytes;media->budget=r.mediaBudget;r.mediaBudget->bytes.fetch_add(bytes,std::memory_order_relaxed);
    auto handle=std::shared_ptr<RendererMediaTexture>(new RendererMediaTexture(std::move(media)));
    std::map<std::string,std::shared_ptr<RendererMediaTexture>>stagedMedia;stagedMedia.emplace(std::move(id),handle);
    r.textures.merge(stagedTexture);r.media.merge(stagedMedia);++r.counters.mediaTargetAllocations;return handle;
}
void Renderer::commitMediaTexture(const RendererMediaTexture&handle){
    require(impl_&&impl_->mediaVideo,"Renderer was not initialized for same-device media");auto&r=*impl_;r.thread();const auto&media=*handle.impl_;
    require(handle.valid()&&media.epoch.lock()==r.epoch,"Stale/foreign media target cannot commit");const auto installed=r.media.find(media.id);
    require(installed!=r.media.end()&&installed->second.get()==&handle,"Media target is no longer registered");auto texture=r.textures.find(media.id);require(texture!=r.textures.end()&&texture->second.mediaOwned,"Media texture ownership mismatch");
    require(texture->second.revision<std::numeric_limits<std::uint64_t>::max(),"Media frame revision exhausted");
    r.readbackReady=false;r.context->ClearState();D3D11_VIEWPORT viewport{0,0,static_cast<float>(media.width),static_cast<float>(media.height),0,1};r.context->RSSetViewports(1,&viewport);r.context->RSSetState(r.raster.Get());r.context->OMSetDepthStencilState(r.noDepth.Get(),0);r.context->OMSetBlendState(r.replaceBlend.Get(),nullptr,UINT_MAX);
    auto*target=media.linear.Get();r.context->OMSetRenderTargets(1,&target,nullptr);r.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);r.context->VSSetShader(r.compositeVS.Get(),nullptr,0);r.context->PSSetShader(r.mediaPS.Get(),nullptr,0);auto*source=media.encoded.Get();r.context->PSSetShaderResources(0,1,&source);r.context->Draw(3,0);
    ID3D11ShaderResourceView*empty=nullptr;r.context->PSSetShaderResources(0,1,&empty);r.context->OMSetRenderTargets(0,nullptr,nullptr);
    ++texture->second.revision;r.invalidate(&texture->second);++r.counters.mediaFrameCommits;
}
void Renderer::retainMediaPoster(const RendererMediaTexture&handle){
    require(impl_&&impl_->mediaVideo,"Renderer was not initialized for same-device media");auto&r=*impl_;r.thread();auto&media=*handle.impl_;
    require(handle.valid()&&media.epoch.lock()==r.epoch,"Stale/foreign media target cannot retain pixels");const auto installed=r.media.find(media.id);
    require(installed!=r.media.end()&&installed->second.get()==&handle,"Media target is no longer registered");auto texture=r.textures.find(media.id);
    require(texture!=r.textures.end()&&texture->second.mediaOwned&&!texture->second.mediaPoster,"Media texture ownership mismatch");
    // Allocate the tiny accounting record before changing ownership. No GPU
    // object, pixel copy or shader work occurs. The SRV remains exactly the one
    // referenced by existing draws/groups. An external encoded-surface borrower
    // keeps its separate four bytes/pixel charge until its handle is released.
    auto retained=std::make_shared<RetainedMediaBytes>();retained->budget=r.mediaBudget;
    retained->bytes=std::size_t(media.width)*media.height*8;media.bytes-=retained->bytes;
    texture->second.mediaPoster=std::move(retained);media.registered.store(false,std::memory_order_relaxed);
    media.linear.Reset();r.media.erase(installed);
}
bool Renderer::Impl::assignDraws(std::vector<Draw>&activeDraws,std::vector<ObjectUniform>&staged,
    std::span<const DrawObject>objects,bool localGroup){
    auto&r=*this;
    require(objects.size() <= maximumObjects, "Retained draw count exceeds bounds");
    if(localGroup)for(const auto&object:objects){
        const auto mesh=r.meshes.find(object.meshID);
        const auto texture=r.textures.find(object.textureID);
        require(mesh!=r.meshes.end()&&!mesh->second.groupOwned,"Native group cannot consume a group output mesh");
        require(object.textureID.empty()||(texture!=r.textures.end()&&!texture->second.groupOwned),"Native group cannot consume a group output texture");
        if(object.alphaMask){const auto mask=r.textures.find(object.alphaMask->textureID);require(mask!=r.textures.end()&&!mask->second.groupOwned,"Native group cannot consume a missing/group alpha-mask texture");}
    }
    bool retained = objects.size() == activeDraws.size();
    for (std::size_t i = 0; retained && i < objects.size(); ++i) {
        const auto &object = objects[i]; const auto &draw = activeDraws[i];
        const auto mesh = r.meshes.find(object.meshID);
        const auto texture = r.textures.find(object.textureID);
        const auto alpha = object.alphaMask?r.textures.find(object.alphaMask->textureID):r.textures.end();
        retained = object.sourceID == draw.sourceID && mesh != r.meshes.end() && &mesh->second == draw.mesh &&
            (object.textureID.empty() ? draw.texture == &r.white : texture != r.textures.end() && &texture->second == draw.texture) &&
            (object.alphaMask ? alpha!=r.textures.end() && &alpha->second==draw.alphaMask : draw.alphaMask==&r.white);
    }
    if (retained) {
        // Validate the complete pose before touching any active GPU constants.
        // Storage was reserved at the last structural commit. Pointer, fade and
        // clipping updates retain both the draw records and their identities.
        staged.resize(objects.size());
        for (std::size_t i = 0; i < objects.size(); ++i) staged[i] = uniforms(objects[i]);
        bool changed{};
        {
            for (std::size_t i = 0; i < objects.size(); ++i) {
                auto &draw = activeDraws[i]; const auto &value = staged[i];
                if (std::memcmp(&value, &draw.values, sizeof(ObjectUniform)) == 0) continue;
                D3D11_MAPPED_SUBRESOURCE mapped{};
                checked(r.context->Map(draw.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Update retained object uniform");
                std::memcpy(mapped.pData, &value, sizeof(ObjectUniform));
                r.context->Unmap(draw.constants.Get(), 0); draw.values = value; ++r.counters.objectUploads; changed=true;
            }
        }
        return changed;
    }
    staged.reserve(objects.size());
    std::vector<Impl::Draw> next;
    next.reserve(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto &object = objects[i];
        auto mesh = r.meshes.find(object.meshID);
        require(mesh != r.meshes.end(), "Draw refers to an unregistered mesh");
        auto texture = r.textures.find(object.textureID);
        require(object.textureID.empty() || texture != r.textures.end(), "Draw refers to an unregistered texture");
        Impl::Draw draw;
        draw.sourceID = object.sourceID; draw.mesh = &mesh->second;
        draw.texture = object.textureID.empty() ? &r.white : &texture->second;
        auto alpha=object.alphaMask?r.textures.find(object.alphaMask->textureID):r.textures.end();
        require(!object.alphaMask||alpha!=r.textures.end(),"Draw refers to an unregistered alpha-mask texture");
        draw.alphaMask=object.alphaMask?&alpha->second:&r.white;
        draw.values = uniforms(object);
        if (i < activeDraws.size())
            draw.constants = activeDraws[i].constants;
        else {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = sizeof(ObjectUniform); description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER; description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = &draw.values;
            checked(r.device->CreateBuffer(&description, &initial, &draw.constants), "Create retained object uniform");
            ++r.counters.objectUploads;
            ++r.counters.objectBufferAllocations;
        }
        next.push_back(std::move(draw));
    }
    // Validate/build the entire list before changing any currently bound
    // constants. Stable slots reuse their buffer during opacity/transform
    // animation; only changed numeric values are uploaded here, never in draw.
    {
        for (std::size_t i = 0; i < std::min(next.size(), activeDraws.size()); ++i)
            if (std::memcmp(&next[i].values, &activeDraws[i].values, sizeof(ObjectUniform)) != 0) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                checked(r.context->Map(next[i].constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Update retained object uniform");
                std::memcpy(mapped.pData, &next[i].values, sizeof(ObjectUniform));
                r.context->Unmap(next[i].constants.Get(), 0);
                ++r.counters.objectUploads;
            }
    }
    activeDraws = std::move(next);
    return true;
}
void Renderer::setDrawList(std::span<const DrawObject>objects){
    require(impl_!=nullptr,"Renderer is not initialized");auto&r=*impl_;r.thread();
    try{(void)r.assignDraws(r.draws,r.stagedObjectValues,objects,false);}
    catch(const RendererError&){reset();throw;}
}
bool Renderer::Impl::configureGroup(std::string id,const NativeGroupTarget&requested,
    std::optional<std::span<const DrawObject>>objects){
    identity(id);require(id.size()<=480,"Native group ID exceeds generated-identity bounds");
    const auto&b=requested.localBounds;const auto density=requested.pixelsPerPoint;
    require(std::isfinite(density)&&density>0&&density<=16,"Invalid native group density");
    require(std::isfinite(b.x)&&std::isfinite(b.y)&&std::isfinite(b.width)&&std::isfinite(b.height)&&b.width>0&&b.height>0,
        "Invalid native group local bounds");
    const double left=std::floor(b.x*density),top=std::floor(b.y*density),right=std::ceil((b.x+b.width)*density),bottom=std::ceil((b.y+b.height)*density);
    require(std::isfinite(left)&&std::isfinite(top)&&std::isfinite(right)&&std::isfinite(bottom)&&right>left&&bottom>top&&right-left<=8192&&bottom-top<=8192&&
        (right-left)*(bottom-top)<=Renderer::maximumNativeGroupPixels,"Native group pixel bounds exceed limits");
    const auto pixelWidth=static_cast<unsigned>(right-left),pixelHeight=static_cast<unsigned>(bottom-top);
    const core::Rect coverage{left/density,top/density,pixelWidth/density,pixelHeight/density};
    for(double value:{coverage.x,coverage.y,coverage.x+coverage.width,coverage.y+coverage.height})
        require(std::isfinite(value)&&std::abs(value)<=std::numeric_limits<float>::max(),"Native group coverage exceeds GPU range");
    // Complete child validation happens before target allocation or any active
    // constants change, including combined bounds/content transactions.
    if(objects){require(objects->size()<=Renderer::maximumObjects,"Native group draw count exceeds limits");
        for(const auto&object:*objects){(void)uniforms(object);const auto mesh=meshes.find(object.meshID);
            const auto texture=textures.find(object.textureID);
            require(mesh!=meshes.end()&&!mesh->second.groupOwned,"Native group cannot consume a missing/group output mesh");
            require(object.textureID.empty()||(texture!=textures.end()&&!texture->second.groupOwned),"Native group cannot consume a missing/group output texture");
            if(object.alphaMask){const auto mask=textures.find(object.alphaMask->textureID);require(mask!=textures.end()&&!mask->second.groupOwned,"Native group cannot consume a missing/group alpha-mask texture");}
        }
    }
    const auto found=groups.find(id);
    if(objects)groupDrawBudget(found==groups.end()?nullptr:found->second.get(),objects->size());
    if(found!=groups.end()&&found->second->requested==requested){
        if(!objects)return false;const auto changed=assignDraws(found->second->draws,found->second->staged,*objects,true);
        found->second->dirty|=changed;return changed;
    }
    require(found!=groups.end()||groups.size()<Renderer::maximumNativeGroups,"Native group count exceeds limits");
    auto candidate=std::make_unique<NativeGroup>();candidate->requested=requested;candidate->coverage=coverage;
    candidate->width=pixelWidth;candidate->height=pixelHeight;candidate->bytes=std::size_t(pixelWidth)*pixelHeight*8;
    candidate->output.sourceID="native-group:"+id;candidate->output.meshID=candidate->output.sourceID+"/quad";candidate->output.textureID=candidate->output.sourceID+"/color";
    const bool existing=found!=groups.end();const auto previous=existing?found->second->bytes:0;
    require(candidate->bytes<=Renderer::maximumNativeGroupBytes&&groupBytes-previous<=Renderer::maximumNativeGroupBytes-candidate->bytes,
        "Native group aggregate target budget exceeded");
    if(!existing){require(!meshes.contains(candidate->output.meshID)&&!textures.contains(candidate->output.textureID),"Native group generated identity collides with a retained resource");
        require(meshes.size()<Renderer::maximumMeshes&&textures.size()<Renderer::maximumTextures,"Native group output resource count exceeds limits");}
    constexpr std::size_t quadBytes=4*sizeof(Vertex)+6*sizeof(std::uint32_t);
    budget(existing?previous+quadBytes+64:0,candidate->bytes+quadBytes+64);
    // Stage all map/string allocations before any existing constants mutation.
    std::map<std::string,Mesh>stagedMeshes;stagedMeshes.emplace(candidate->output.meshID,Mesh{});
    std::map<std::string,Texture>stagedTextures;stagedTextures.emplace(candidate->output.textureID,Texture{});
    std::map<std::string,std::unique_ptr<NativeGroup>>stagedGroups;
    if(!existing)stagedGroups.emplace(id,nullptr);
    const auto x=float(coverage.x),y=float(coverage.y),r=float(coverage.x+coverage.width),bottomPoint=float(coverage.y+coverage.height);
    require(r>x&&bottomPoint>y,"Native group coverage collapses at GPU precision");
    const std::array<Vertex,4>vertices{{{{x,y,0},{0,0},{1,1,1,1}},{{r,y,0},{1,0},{1,1,1,1}},{{r,bottomPoint,0},{1,1},{1,1,1,1}},{{x,bottomPoint,0},{0,1},{1,1,1,1}}}};
    constexpr std::array<std::uint32_t,6>indices{0,1,2,0,2,3};auto&mesh=stagedMeshes.begin()->second;
    mesh.vertices=immutableBuffer(device.Get(),D3D11_BIND_VERTEX_BUFFER,vertices.data(),sizeof(vertices));
    mesh.indices=immutableBuffer(device.Get(),D3D11_BIND_INDEX_BUFFER,indices.data(),sizeof(indices));mesh.indexCount=6;mesh.bytes=quadBytes;mesh.groupOwned=true;
    D3D11_TEXTURE2D_DESC description{};description.Width=pixelWidth;description.Height=pixelHeight;description.MipLevels=description.ArraySize=description.SampleDesc.Count=1;
    description.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;description.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D>targetTexture;checked(device->CreateTexture2D(&description,nullptr,&targetTexture),"Create retained native group target");
    checked(device->CreateRenderTargetView(targetTexture.Get(),nullptr,&candidate->target),"Create retained native group target view");
    auto&texture=stagedTextures.begin()->second;checked(device->CreateShaderResourceView(targetTexture.Get(),nullptr,&texture.view),"Create retained native group sampled output");
    texture.filter=TextureFilter::linear;texture.bytes=candidate->bytes;texture.groupOwned=true;
    core::Matrix4 camera;camera.values={2./coverage.width,0,0,0,0,-2./coverage.height,0,0,0,0,0,0,-1-2*coverage.x/coverage.width,1+2*coverage.y/coverage.height,0,1};
    const auto groupCameraValues=matrix(camera);candidate->camera=immutableBuffer(device.Get(),D3D11_BIND_CONSTANT_BUFFER,groupCameraValues.data(),64);
    if(objects){auto&destination=existing?*found->second:*candidate;(void)assignDraws(destination.draws,destination.staged,*objects,true);}
    // Map nodes never move after installation: currently published texture and
    // mesh pointers remain valid when their owned GPU objects are replaced.
    if(existing){auto&destination=*found->second;
        meshes.find(destination.output.meshID)->second=std::move(mesh);textures.find(destination.output.textureID)->second=std::move(texture);
        destination.requested=requested;destination.coverage=coverage;destination.width=pixelWidth;destination.height=pixelHeight;destination.bytes=candidate->bytes;
        destination.target=std::move(candidate->target);destination.camera=std::move(candidate->camera);destination.dirty=true;
    }else{
        stagedGroups.begin()->second=std::move(candidate);meshes.merge(stagedMeshes);textures.merge(stagedTextures);groups.merge(stagedGroups);
    }
    groupBytes=groupBytes-previous+std::size_t(pixelWidth)*pixelHeight*8;
    counters.resourceBytes=counters.resourceBytes-(existing?previous+quadBytes+64:0)+std::size_t(pixelWidth)*pixelHeight*8+quadBytes+64;
    ++counters.meshUploads;++counters.textureUploads;++counters.nativeGroupTargetAllocations;return true;
}
bool Renderer::configureNativeGroup(std::string id,const NativeGroupTarget&target){
    require(impl_!=nullptr,"Renderer is not initialized");impl_->thread();try{return impl_->configureGroup(std::move(id),target,{});}catch(const RendererError&){reset();throw;}
}
bool Renderer::configureNativeGroup(std::string id,const NativeGroupTarget&target,std::span<const DrawObject>objects){
    require(impl_!=nullptr,"Renderer is not initialized");impl_->thread();try{return impl_->configureGroup(std::move(id),target,objects);}catch(const RendererError&){reset();throw;}
}
bool Renderer::setNativeGroupDraws(const std::string&id,std::span<const DrawObject>objects){
    require(impl_!=nullptr,"Renderer is not initialized");auto&r=*impl_;r.thread();const auto found=r.groups.find(id);require(found!=r.groups.end(),"Unknown native group");
    r.groupDrawBudget(found->second.get(),objects.size());
    try{const auto changed=r.assignDraws(found->second->draws,found->second->staged,objects,true);found->second->dirty|=changed;return changed;}
    catch(const RendererError&){reset();throw;}
}
const DrawObject&Renderer::nativeGroupOutput(const std::string&id)const{
    require(impl_!=nullptr,"Renderer is not initialized");impl_->thread();const auto found=impl_->groups.find(id);require(found!=impl_->groups.end(),"Unknown native group");return found->second->output;
}
bool Renderer::removeNativeGroup(const std::string&id){
    if(!impl_)return false;auto&r=*impl_;r.thread();const auto found=r.groups.find(id);if(found==r.groups.end())return false;
    const auto&group=*found->second;const auto mesh=r.meshes.find(group.output.meshID);
    const auto color=r.textures.find(group.output.textureID);require(mesh!=r.meshes.end()&&color!=r.textures.end(),"Native group owned resources are missing");
    if(r.meshReferenced(&mesh->second)||r.textureReferenced(&color->second))return false;
    r.context->ClearState();r.counters.resourceBytes-=mesh->second.bytes+color->second.bytes+64;r.groupBytes-=group.bytes;
    r.meshes.erase(mesh);r.textures.erase(color);r.groups.erase(found);return true;
}
void Renderer::setCamera(const core::Matrix4 &viewProjection) {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread();
    auto next = matrix(viewProjection);
    if (next != r.cameraValues) { r.cameraValues = next; r.cameraDirty = true; }
}
void Renderer::Impl::renderNativeDraws(std::span<const Draw>objects,ID3D11RenderTargetView*passTarget,
    ID3D11Buffer*passCamera,unsigned pixelWidth,unsigned pixelHeight,bool group){
    auto&r=*this;
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(pixelWidth), static_cast<float>(pixelHeight), 0, 1};
    r.context->RSSetViewports(1, &viewport); r.context->RSSetState(r.raster.Get());
    r.context->OMSetDepthStencilState(r.noDepth.Get(), 0);
    ID3D11ShaderResourceView *emptyResources[2]{};
    r.context->PSSetShaderResources(0, 2, emptyResources);
    auto *linear = passTarget;
    r.context->OMSetRenderTargets(1, &linear, nullptr);
    constexpr float clear[4]{};
    r.context->ClearRenderTargetView(linear, clear);
    r.context->OMSetBlendState(r.overBlend.Get(), nullptr, UINT_MAX);
    r.context->IASetInputLayout(r.layout.Get());
    r.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.context->VSSetShader(r.sceneVS.Get(), nullptr, 0); r.context->PSSetShader(r.scenePS.Get(), nullptr, 0);
    auto *camera = passCamera;
    r.context->VSSetConstantBuffers(0, 1, &camera);
    UINT stride = sizeof(Vertex), offset = 0;
    for (const auto &draw : objects) {
        if (draw.values.opacity == 0 || draw.values.tint[3] == 0) continue;
        auto *vertices = draw.mesh->vertices.Get();
        r.context->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
        r.context->IASetIndexBuffer(draw.mesh->indices.Get(), DXGI_FORMAT_R32_UINT, 0);
        auto *constants = draw.constants.Get();
        r.context->VSSetConstantBuffers(1, 1, &constants); r.context->PSSetConstantBuffers(1, 1, &constants);
        ID3D11ShaderResourceView* textures[2]{draw.texture->view.Get(),draw.alphaMask->view.Get()};
        ID3D11SamplerState* samplers[2]{draw.texture->filter==TextureFilter::nearest?r.nearestSampler.Get():r.linearSampler.Get(),r.linearSampler.Get()};
        r.context->PSSetShaderResources(0,2,textures);r.context->PSSetSamplers(0,2,samplers);
        r.context->DrawIndexed(draw.mesh->indexCount, 0, 0); ++r.counters.drawCalls;if(group)++r.counters.nativeGroupDrawCalls;
    }
    r.context->PSSetShaderResources(0,2,emptyResources);
    r.context->OMSetRenderTargets(0,nullptr,nullptr);
}
void Renderer::draw(bool present) {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread();
    require(!present || !r.offscreen, "An isolated render target cannot present to the desktop");
    r.readbackReady = false;
    const bool originalBackground = r.source && r.source->active();
    if (originalBackground) {
        r.source->renderTo(r.backBuffer.Get(), r.width, r.height);
        if (r.draws.empty()) {
            if (present) { checked(r.swapchain->Present(1, 0), "Present original HUD materials"); ++r.counters.presents; }
            else r.readbackReady = true;
            return;
        }
    }
    if (r.cameraDirty) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(r.context->Map(r.cameraBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Update camera uniform");
        std::memcpy(mapped.pData, r.cameraValues.data(), sizeof(r.cameraValues));
        r.context->Unmap(r.cameraBuffer.Get(), 0);
        r.cameraDirty = false; ++r.counters.cameraUploads;
    }
    for(auto&[id,group]:r.groups){(void)id;if(!group->dirty)continue;
        const auto*texture=&r.textures.find(group->output.textureID)->second;
        const bool visible=std::any_of(r.draws.begin(),r.draws.end(),[&](const auto&draw){return (draw.texture==texture||draw.alphaMask==texture)&&draw.values.opacity>0&&draw.values.tint[3]>0;});
        if(!visible)continue;
        r.renderNativeDraws(group->draws,group->target.Get(),group->camera.Get(),group->width,group->height,true);
        group->dirty=false;++r.counters.nativeGroupRenders;
    }
    r.renderNativeDraws(r.draws,r.linearView.Get(),r.cameraBuffer.Get(),r.width,r.height,false);
    ID3D11ShaderResourceView*emptyResource=nullptr;
    auto *output = r.outputView.Get();
    r.context->OMSetRenderTargets(1, &output, nullptr);
    // Native captions/modules form a separate premultiplied encoded surface,
    // matching the source's Core Animation layer above its Metal attachment.
    // Preserve original shader output; only the native surface is composited.
    r.context->OMSetBlendState(originalBackground ? r.overBlend.Get() : r.replaceBlend.Get(), nullptr, UINT_MAX);
    r.context->IASetInputLayout(nullptr);
    r.context->VSSetShader(r.compositeVS.Get(), nullptr, 0); r.context->PSSetShader(r.compositePS.Get(), nullptr, 0);
    auto *source = r.linearResource.Get();
    r.context->PSSetShaderResources(0, 1, &source);
    r.context->Draw(3, 0);
    r.context->PSSetShaderResources(0, 1, &emptyResource);
    r.context->OMSetRenderTargets(0, nullptr, nullptr);
    if (present) {
        checked(r.swapchain->Present(1, 0), "Present HUD composition");
        ++r.counters.presents;
    } else r.readbackReady = true;
}
Readback Renderer::readback() {
    require(impl_ != nullptr, "Renderer is not initialized"); auto &r = *impl_; r.thread();
    require(r.readbackReady, "Readback requires a completed render-only draw");
    D3D11_TEXTURE2D_DESC description{};
    r.backBuffer->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ; description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    checked(r.device->CreateTexture2D(&description, nullptr, &staging), "Create application-target readback");
    r.context->CopyResource(staging.Get(), r.backBuffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    checked(r.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read application render target");
    Readback result;
    result.width = r.width; result.height = r.height; result.rowBytes = r.width * 4;
    try {
        result.pixels.resize(std::size_t(result.rowBytes) * result.height);
        for (std::size_t row = 0; row < result.height; ++row)
            std::memcpy(result.pixels.data() + row * result.rowBytes,
                        static_cast<const std::uint8_t *>(mapped.pData) + row * mapped.RowPitch, result.rowBytes);
    } catch (...) { r.context->Unmap(staging.Get(), 0); throw; }
    r.context->Unmap(staging.Get(), 0);
    return result;
}
bool Renderer::removeMesh(const std::string &sourceID) {
    if (!impl_) return false;
    auto &r = *impl_; r.thread(); auto item = r.meshes.find(sourceID);
    if (item == r.meshes.end() || item->second.groupOwned || r.meshReferenced(&item->second)) return false;
    r.context->ClearState(); r.counters.resourceBytes -= item->second.bytes; r.meshes.erase(item); return true;
}
bool Renderer::removeTexture(const std::string &sourceID) {
    if (!impl_) return false;
    auto &r = *impl_; r.thread(); auto item = r.textures.find(sourceID);
    if (item == r.textures.end() || item->second.groupOwned || r.textureReferenced(&item->second)) return false;
    r.context->ClearState();r.counters.resourceBytes -= item->second.bytes;
    if(item->second.mediaOwned){auto media=r.media.find(sourceID);if(media!=r.media.end()){media->second->impl_->registered.store(false,std::memory_order_relaxed);r.media.erase(media);}}
    r.textures.erase(item); return true;
}
void Renderer::clearDrawList() { if (impl_) { impl_->thread(); impl_->draws.clear(); impl_->context->ClearState(); } }
void Renderer::clearResources() {
    if (!impl_) return;
    impl_->thread(); impl_->draws.clear();impl_->groups.clear();impl_->groupBytes=0; impl_->meshes.clear(); impl_->textures.clear();
    for(auto&[id,media]:impl_->media){(void)id;media->impl_->registered.store(false,std::memory_order_relaxed);}impl_->media.clear();
    impl_->context->ClearState(); impl_->counters.resourceBytes = 0;
    if (impl_->source) impl_->source->clear();
}
void Renderer::reset() noexcept { impl_.reset(); }
RendererStats Renderer::stats() const noexcept {
    if (!impl_) return {};
    auto result = impl_->counters;
    result.meshes = impl_->meshes.size(); result.textures = impl_->textures.size(); result.objects = impl_->draws.size();
    result.nativeGroups=impl_->groups.size();result.nativeGroupBytes=impl_->groupBytes;
    result.mediaTargets=impl_->media.size();result.mediaLiveBytes=impl_->mediaBudget->bytes.load(std::memory_order_relaxed);result.resourceBytes+=result.mediaLiveBytes;
    return result;
}
RendererDeviceInfo Renderer::deviceInfo() const {
    require(impl_ != nullptr, "Renderer is not initialized"); impl_->thread();
    ComPtr<IDXGIDevice> device; checked(impl_->device.As(&device), "Read renderer DXGI device");
    ComPtr<IDXGIAdapter> adapter; checked(device->GetAdapter(&adapter), "Read selected renderer adapter");
    DXGI_ADAPTER_DESC description{}; checked(adapter->GetDesc(&description), "Read selected renderer identity");
    description.Description[127] = 0;
    const auto length = static_cast<int>(std::wcslen(description.Description));
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, description.Description, length, nullptr, 0, nullptr, nullptr);
    require(bytes > 0, "Selected renderer adapter has no valid name");
    std::string name(static_cast<std::size_t>(bytes), '\0');
    require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, description.Description, length, name.data(), bytes, nullptr, nullptr) == bytes,
        "Cannot encode selected renderer adapter name");
    return {std::move(name), description.VendorId, description.DeviceId,
        static_cast<std::uint64_t>(description.DedicatedVideoMemory), static_cast<std::uint64_t>(description.SharedSystemMemory)};
}
SourceGraphics& Renderer::sourceGraphics() {
    require(impl_ != nullptr, "Renderer is not initialized"); impl_->thread();
    if (!impl_->source) impl_->source = std::make_unique<SourceGraphics>(impl_->device.Get(), impl_->context.Get());
    return *impl_->source;
}

} // namespace endfield::native
