// Teardown order of the Application owner (app/application_lifetime.cpp,
// Impl::cleanup) against its declared module adapters (app/application_impl.hpp):
// every adapter is dropped before the shared design Context is freed (a
// design owner's ~DesignOwnerCore and ~Canvas read that Context), Projection's
// page goes before Notes (it borrows Notes' TSF manager), and the Context is
// freed only once no canvas is registered on it. The Application itself is
// Win32-only; the hidden module coverage runs the same cleanup and its guard.
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
std::string read(const char* path) {
    std::ifstream in(path, std::ios::binary); check(bool(in), std::string("Read ") + path);
    std::ostringstream text; text << in.rdbuf(); return text.str();
}
// The adapters of the "Module adapters" declaration (registration order).
std::vector<std::string> adapters(const std::string& header) {
    const auto at = header.find("// Module adapters");
    check(at != std::string::npos, "The header declares its module adapters");
    const auto begin = header.find("std::unique_ptr<ModuleOwner>", at), end = header.find(';', begin);
    check(begin != std::string::npos && end != std::string::npos, "The adapters are one unique_ptr<ModuleOwner> declaration");
    const auto list = header.substr(begin, end - begin);
    std::vector<std::string> names;
    const std::regex name(R"(\b([A-Za-z]+ModuleOwner)\b)");
    for (auto it = std::sregex_iterator(list.begin(), list.end(), name); it != std::sregex_iterator(); ++it) names.push_back((*it)[1]);
    return names;
}
std::string cleanup(const std::string& source) {
    const auto begin = source.find("void Application::Impl::cleanup(){");
    check(begin != std::string::npos, "cleanup() is defined");
    const auto end = source.find("\n}\n", begin);
    check(end != std::string::npos, "cleanup() ends");
    return source.substr(begin, end - begin);
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "Pass app/application_impl.hpp and app/application_lifetime.cpp");
        const auto names = adapters(read(argv[1]));
        const auto source = read(argv[2]);
        const auto body = cleanup(source);
        // An adapter cleanup drops through a helper shared with a module
        // repair: "dropXOwner();" whose definition unregisters it.
        const auto droppedAt = [&](const std::string& n) {
            if (const auto direct = body.find("drop(" + n + ")"); direct != std::string::npos) return direct;
            const std::regex helper("void Application::Impl::(drop[A-Za-z]+Owner)\\(\\)\\{[^\\n]*unregisterModule\\(" + n + "\\)");
            std::smatch found;
            if (!std::regex_search(source, found, helper)) return std::string::npos;
            return body.find(found[1].str() + "()");
        };
        check(names.size() > 10, "The adapter list was read (" + std::to_string(names.size()) + ")");
        const auto freed = body.find("designOverlay.reset()");
        check(freed != std::string::npos, "cleanup() frees the shared design Context");
        for (const auto& n : names) {
            const auto dropped = droppedAt(n);
            check(dropped != std::string::npos, n + " is dropped in cleanup()");
            check(dropped < freed, n + " is dropped before the shared design Context is freed");
        }
        const auto projection = body.find("drop(projectionModuleOwner)"), notes = body.find("drop(notesModuleOwner)");
        check(projection != std::string::npos && notes != std::string::npos && projection < notes, "Projection's page goes before Notes (it borrows Notes' TSF manager)");
        const auto areas = body.find("externalModules.clear()");
        check(areas != std::string::npos && areas < freed, "The module areas go before the shared design Context");
        const auto guard = body.find("context().canvases().empty()");
        check(guard != std::string::npos && guard < freed, "The Context is freed only once no canvas is registered on it");
        std::cout << "Application teardown order: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Application teardown order after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
