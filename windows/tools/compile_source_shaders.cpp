// Build-only SM5 compilation/reflection of the exact translated Mac programs.
// This executable performs no rendering, desktop capture or application I/O.
#include "core/data/json.hpp"
#include "core/data/file_io.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <span>

using ehud::data::Json;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void check(HRESULT value, const char* message) { require(SUCCEEDED(value), message); }
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    require(input.good() && size >= 0 && size <= 16 * 1024 * 1024, "Invalid shader file length");
    std::string bytes(static_cast<std::size_t>(size), '\0'); input.seekg(0);
    input.read(bytes.data(), size); require(input.good(), "Cannot read shader file"); return bytes;
}
std::string sha256(std::span<const char> bytes) {
    std::array<unsigned char, 32> value{};
    require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()),
        value.data(), static_cast<ULONG>(value.size())) >= 0, "Cannot hash shader bytes");
    std::string result;
    constexpr char hex[] = "0123456789abcdef";
    for (const auto unit : value) { result += hex[unit >> 4]; result += hex[unit & 15]; }
    return result;
}
std::string utf8(const fs::path& path) {
    const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()};
}
fs::path contained(const fs::path& root, const std::string& name) {
    const fs::path relative(std::u8string_view(reinterpret_cast<const char8_t*>(name.data()), name.size()));
    require(!relative.empty() && relative.filename() == relative && !relative.is_absolute(), "Shader files must be immediate package children");
    const auto result = fs::canonical(root / relative);
    require(result.parent_path() == root && fs::is_regular_file(result), "Shader file escapes package root"); return result;
}
void write(const fs::path& path, const void* bytes, std::size_t size) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    require(output.good(), "Cannot write compiled shader artifact");
}
Json signature(ID3D11ShaderReflection* shader, bool input, unsigned count) {
    Json::Array values;
    for (unsigned i = 0; i < count; ++i) {
        D3D11_SIGNATURE_PARAMETER_DESC d{};
        check(input ? shader->GetInputParameterDesc(i, &d) : shader->GetOutputParameterDesc(i, &d), "Cannot reflect shader signature");
        values.emplace_back(Json::Object{{"semantic",d.SemanticName},{"index",int(d.SemanticIndex)},
            {"register",int(d.Register)},{"mask",int(d.Mask)},{"type",int(d.ComponentType)},
            {"systemValue",int(d.SystemValueType)}});
    }
    return values;
}
Json compile(const fs::path& root, const Json& source) {
    const auto path = contained(root, source["hlslFile"].string());
    const auto bytes = read(path);
    require(sha256(bytes) == source["hlslSHA256"].string(), "Translated shader hash differs from manifest");
    const auto profile = source["profile"].string(), entry = source["entry"].string();
    require((profile == "vs_5_0" || profile == "ps_5_0") && entry == "main", "Unsupported stage compilation contract");
    ComPtr<ID3DBlob> code, errors;
    const auto status = D3DCompile(bytes.data(), bytes.size(), source["hlslFile"].string().c_str(),
        nullptr, nullptr, entry.c_str(), profile.c_str(),
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS,
        0, &code, &errors);
    if (FAILED(status)) {
        if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        throw std::runtime_error("Source shader compile failed: " + source["id"].string());
    }
    ComPtr<ID3D11ShaderReflection> reflection;
    check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&reflection)), "Cannot reflect SM5 bytecode");
    D3D11_SHADER_DESC d{}; check(reflection->GetDesc(&d), "Cannot read SM5 descriptor");
    Json::Array resources, buffers;
    for (unsigned i = 0; i < d.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        check(reflection->GetResourceBindingDesc(i, &binding), "Cannot reflect stage binding");
        resources.emplace_back(Json::Object{{"name",binding.Name},{"type",int(binding.Type)},
            {"slot",int(binding.BindPoint)},{"count",int(binding.BindCount)},
            {"dimension",int(binding.Dimension)},{"returnType",int(binding.ReturnType)}});
    }
    for (const auto& expected : source["uniforms"].array()) {
        const auto name = expected["hlslName"].string();
        auto* buffer = reflection->GetConstantBufferByName(name.c_str());
        D3D11_SHADER_BUFFER_DESC description{};
        // Fully unused source buffers legitimately disappear during O3. Keep
        // that explicit; the app must not invent slots for eliminated buffers.
        Json mapped = expected;
        if (FAILED(buffer->GetDesc(&description))) { mapped["active"] = false; buffers.push_back(mapped); continue; }
        mapped["active"] = true;
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        check(reflection->GetResourceBindingDescByName(name.c_str(), &binding), "Uniform buffer has no stage binding");
        const auto size = expected["sourceSize"].integer();
        require(size > 0 && size <= 65536 && description.Size == (static_cast<unsigned>(size) + 15u) / 16u * 16u,
                "SM5 buffer layout size differs from original program");
        require(binding.BindCount == 1 && binding.BindPoint < 14, "Unsupported D3D11 buffer slot");
        mapped["slot"] = int(binding.BindPoint); mapped["byteWidth"] = int(description.Size);
        Json::Array members;
        for (unsigned m = 0; m < description.Variables; ++m) {
            D3D11_SHADER_VARIABLE_DESC v{};
            check(buffer->GetVariableByIndex(m)->GetDesc(&v), "Cannot reflect uniform variable");
            require(v.StartOffset + v.Size <= description.Size, "Uniform member exceeds compiled buffer");
            bool matched = false;
            for (const auto& original : expected["sourceMembers"].array()) {
                const auto originalName = original["name"].string();
                const auto variable = expected["spirvVariable"].string() + originalName;
                if (variable != v.Name) continue;
                require(original["offset"].integer() == v.StartOffset,
                        "SM5 member offset differs from original program");
                matched = true; break;
            }
            require(matched, "Unmapped compiled uniform member");
            members.emplace_back(Json::Object{{"name",v.Name},{"offset",int(v.StartOffset)},
                {"bytes",int(v.Size)},{"used",(v.uFlags & D3D_SVF_USED) != 0}});
        }
        mapped["compiledMembers"] = members; buffers.push_back(mapped);
    }
    const auto output = path.stem().wstring() + L".cso";
    write(root / output, code->GetBufferPointer(), code->GetBufferSize());
    Json result = source;
    result.erase("hlslFile");
    result["bytecodeFile"] = utf8(fs::path(output));
    result["bytecodeBytes"] = std::int64_t(code->GetBufferSize());
    result["bytecodeSHA256"] = sha256({static_cast<const char*>(code->GetBufferPointer()), code->GetBufferSize()});
    result["uniforms"] = buffers; result["bindings"] = resources;
    result["inputSignature"] = signature(reflection.Get(), true, d.InputParameters);
    result["outputSignature"] = signature(reflection.Get(), false, d.OutputParameters);
    return result;
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 2, "Pass the translated shaders.json manifest");
        const auto manifestPath = fs::canonical(argv[1]);
        const auto root = manifestPath.parent_path();
        const auto manifest = Json::parse(read(manifestPath), 16 * 1024 * 1024);
        require(manifest["schemaVersion"].integer() == 1, "Unsupported shader manifest schema");
        const auto& programs = manifest["programs"].array();
        require(!programs.empty() && programs.size() <= 512, "Invalid shader program count");
        Json::Array compiled;
        for (const auto& program : programs) compiled.push_back(compile(root, program));
        Json result = manifest; result["programs"] = compiled;
        result["compiler"] = "Windows SDK D3DCompiler_47, SM5, strict O3, warnings as errors";
        const auto bytes = result.encode(32 * 1024 * 1024);
        write(root / "compiled-shaders.json", bytes.data(), bytes.size());
        std::cout << "Compiled and reflected " << compiled.size() << " source shader stages\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
