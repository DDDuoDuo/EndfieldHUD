// Opt-in memory trace of the Application owner (ApplicationOptions::memoryTrace).
// Each stage row records the process census (app/process_diagnostics.hpp),
// the shared raster cache and the renderer's retained resources attributed to
// the module owner whose call created them (ModuleRegistry holds a
// native::LayerRasterAttribution around every owner call; "host" is the
// shell and the host's own calls), and what owners report through
// ModuleOwner::residency(). Numbers, owner names and system module file
// names only. Never on the frame path: production records at construction,
// launch idle and the first openings and closings, and rewrites its bounded
// report each time.
#include "app/application_impl.hpp"
#include "app/process_diagnostics.hpp"
#include <algorithm>
#include <cstdio>
#include <map>

namespace endfield::app {
namespace {
Json bytes(std::uint64_t value) { return Json(static_cast<std::int64_t>(value)); }
}

Json Application::Impl::memorySnapshot(std::string_view stage, bool threads) {
    const auto process = captureProcessDiagnostics(threads);
    struct Owner {
        std::size_t rasterEntries{}, rasterBytes{}, textLayouts{}, typographyBytes{}, textMetadataBytes{};
        std::size_t textures{}, textureBytes{}, meshes{}, meshBytes{}, groups{}, groupBytes{}, media{}, mediaBytes{};
        ModuleResidency residency; bool reported{}, registered{}, enabled{};
        std::uint64_t total() const noexcept { return rasterBytes + textureBytes + meshBytes + groupBytes + mediaBytes + residency.cpuBytes + residency.gpuBytes; }
    };
    std::map<std::string, Owner, std::less<>> owners;
    const auto row = [&](std::string_view owner) -> Owner& {
        const auto found = owners.find(owner.empty() ? std::string_view("host") : owner);
        if (found != owners.end()) return found->second;
        return owners[std::string(owner.empty() ? std::string_view("host") : owner)];
    };
    std::size_t retainedLayouts{}, paintedLayouts{}, rasterPixelBytes{};
    rasterizer.forEachEntry([&](const gpu::LayerRasterEntryUsage& e) {
        auto& o = row(e.owner); ++o.rasterEntries; o.rasterBytes += e.pixelBytes; o.textLayouts += e.retainedTextLayouts;
        o.typographyBytes += e.typographyBytes; o.textMetadataBytes += e.textMetadataBytes;
        retainedLayouts += e.retainedTextLayouts; paintedLayouts += e.paintedText; rasterPixelBytes += e.pixelBytes;
    });
    std::size_t textureBytes{}, meshBytes{}, groupBytes{}, mediaBytes{}, textures{}, meshes{};
    renderer.forEachResource([&](const gpu::RendererResourceUsage& r) {
        auto& o = row(r.owner);
        switch (r.kind) {
        case gpu::RendererResourceUsage::Kind::texture: ++o.textures; o.textureBytes += r.bytes; textureBytes += r.bytes; ++textures; break;
        case gpu::RendererResourceUsage::Kind::mesh: ++o.meshes; o.meshBytes += r.bytes; meshBytes += r.bytes; ++meshes; break;
        case gpu::RendererResourceUsage::Kind::nativeGroup: ++o.groups; o.groupBytes += r.bytes; groupBytes += r.bytes; break;
        case gpu::RendererResourceUsage::Kind::media: ++o.media; o.mediaBytes += r.bytes; mediaBytes += r.bytes; break;
        }
    });
    // Owner-reported residency (zero: not reported). Disabled owners are listed
    // as registered but are not asked.
    registry.forEach([&](ModuleOwner& o) {
        auto& r = row(o.name()); r.registered = r.enabled = true;
        const auto value = o.residency();
        if (value != ModuleResidency{}) { r.residency = value; r.reported = true; }
    }, "residency");
    std::vector<std::pair<std::string_view, const Owner*>> ordered;
    for (const auto& [name, o] : owners) ordered.emplace_back(name, &o);
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.second->total() > b.second->total(); });
    Json::Array ownerRows;
    for (const auto& [name, o] : ordered) {
        Json::Object value{{"owner", std::string(name)}, {"registered", o->registered},
            {"rasterEntries", bytes(o->rasterEntries)}, {"rasterPixelBytes", bytes(o->rasterBytes)},
            {"retainedTextLayouts", bytes(o->textLayouts)}, {"typographyBytes", bytes(o->typographyBytes)}, {"textMetadataBytes", bytes(o->textMetadataBytes)},
            {"textures", bytes(o->textures)}, {"textureBytes", bytes(o->textureBytes)}, {"meshes", bytes(o->meshes)}, {"meshBytes", bytes(o->meshBytes)},
            {"nativeGroups", bytes(o->groups)}, {"nativeGroupBytes", bytes(o->groupBytes)}, {"mediaTargets", bytes(o->media)}, {"mediaBytes", bytes(o->mediaBytes)}};
        if (o->reported) value["residency"] = Json::Object{{"cpuBytes", bytes(o->residency.cpuBytes)}, {"gpuBytes", bytes(o->residency.gpuBytes)}, {"threads", bytes(o->residency.threads)}};
        ownerRows.push_back(std::move(value));
    }
    const auto raster = rasterizer.stats();
    const auto rendered = renderer.stats();
    const auto source = renderer.stats().initialized ? renderer.sourceGraphics().stats() : gpu::SourceGraphicsStats{};
    Json::Object executors;
    const auto executor = [](const UtilityExecutor& e) { const auto s = e.stats(); return Json::Object{{"started", s.started}, {"workerAlive", s.workerAlive}, {"workersStarted", bytes(s.workersStarted)}, {"accepted", bytes(s.accepted)}}; };
    if (utility) executors["utility"] = executor(*utility);
    if (longWork) executors["longWork"] = executor(*longWork);
    return Json::Object{{"stage", std::string(stage)}, {"seconds", milliseconds(preparation) / 1000.},
        {"overlayVisible", session && session->phase() != core::VisibilityPhase::concealed},
        {"selectedModule", notes ? std::string(core::moduleIdentifier(notes->selected())) : std::string()},
        {"census", processDiagnosticsJSON(process)},
        {"rasterCache", Json::Object{{"entries", bytes(raster.entries)}, {"resourceBytes", bytes(raster.resourceBytes)}, {"retainedPixelBytes", bytes(rasterPixelBytes)},
            {"decodedImages", bytes(raster.decodedImages)}, {"decodedImageBytes", bytes(rasterizer.decodedImageBytes())},
            {"textMetadataBytes", bytes(raster.textMetadataBytes)}, {"typographyDescriptorBytes", bytes(raster.typographyDescriptorBytes)},
            {"retainedTextLayouts", bytes(retainedLayouts)}, {"paintedTextLayouts", bytes(paintedLayouts)},
            {"maximumEntries", bytes(gpu::LayerRasterizer::maximumEntries)}, {"maximumResourceBytes", bytes(gpu::LayerRasterizer::maximumResourceBytes)},
            {"rasterizations", bytes(raster.rasterizations)}, {"cacheHits", bytes(raster.cacheHits)}}},
        {"renderer", Json::Object{{"textures", bytes(textures)}, {"textureBytes", bytes(textureBytes)}, {"meshes", bytes(meshes)}, {"meshBytes", bytes(meshBytes)},
            {"nativeGroupBytes", bytes(groupBytes)}, {"mediaBytes", bytes(mediaBytes)}, {"objects", bytes(rendered.objects)},
            {"resourceBytes", bytes(rendered.resourceBytes)}, {"maximumResourceBytes", bytes(gpu::Renderer::maximumResourceBytes)},
            {"releasedTargetBytes", bytes(rendered.releasedTargetBytes)}, {"residentBytes", bytes(rendered.resourceBytes - std::min(rendered.resourceBytes, rendered.releasedTargetBytes))},
            {"frameTargetReleases", bytes(rendered.frameTargetReleases)}, {"driverTrimmed", rendered.driverTrimmed},
            {"sourceMaterialBytes", bytes(source.payloadBytes)}, {"sourceTextures", bytes(source.textures)}, {"sourceMeshes", bytes(source.meshes)}}},
        {"executors", std::move(executors)},
        {"owners", std::move(ownerRows)}};
}

void Application::Impl::recordMemory(std::string_view stage, bool threads) {
    if (!memoryTrace || !memoryTrace->accepts()) return;
    try {
        memoryTrace->add(memorySnapshot(stage, threads));
        if (production()) writeMemoryTrace();
    } catch (const std::exception& e) {
        if (!production()) throw;
        const auto line = std::string("EndfieldHUD memory trace could not be recorded (") + typeid(e).name() + ")\n";
        std::fputs(line.c_str(), stderr); OutputDebugStringA(line.c_str());
    }
}

void Application::Impl::writeMemoryTrace() {
    if (!memoryTrace) return;
    try {
        std::error_code ignored; fs::create_directories(memoryTrace->path().parent_path(), ignored);
        constexpr std::size_t bound = 16 * 1024 * 1024;
        Json::Object device;
        if (renderer.stats().initialized) {
            const auto info = renderer.deviceInfo();
            device = Json::Object{{"name", info.name}, {"vendorID", std::int64_t(info.vendorID)}, {"deviceID", std::int64_t(info.deviceID)},
                {"dedicatedVideoCapacityBytes", bytes(info.dedicatedVideoBytes)}, {"sharedSystemCapacityBytes", bytes(info.sharedSystemBytes)}, {"warp", args.warp}};
        }
        const Json report = Json::Object{{"schema", 1},
            {"scope", "EndfieldHUD memory and thread census: numbers, module owner names and system module file names; no text, paths or document content"},
            {"mode", production() ? "production" : "hidden"}, {"device", std::move(device)},
            {"startupStages", startup.rows()}, {"snapshots", memoryTrace->rows()}, {"visits", memoryTrace->visits()}};
        const auto text = report.encode(bound);
        ehud::data::detail::replaceFile(memoryTrace->path(), ehud::data::detail::readFile(memoryTrace->path(), bound), text, bound);
    } catch (const std::exception& e) {
        if (!production()) throw;
        const auto line = std::string("EndfieldHUD memory trace could not be written (") + typeid(e).name() + ")\n";
        std::fputs(line.c_str(), stderr); OutputDebugStringA(line.c_str());
    }
}
} // namespace endfield::app
