// Verifies a Mac-staged Watch runtime tree (scripts/package-watch-resources.py
// stage <Resources/WatchSource> <destination>) natively: every EHUDZ01 payload
// decodes byte-exactly to its recorded source size and SHA-256 within the
// 128 MiB bound, and the folder holds exactly the inventory's files.
//
//   package_watch_resources verify <staged WatchSource folder>
#include "packaging/ehudz01.hpp"
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[1]) != "verify") {
        std::cerr << "usage: package_watch_resources verify <staged WatchSource folder>\n";
        return 2;
    }
    try {
        const auto report = endfield::packaging::verifyWatchRuntimeInventory(std::filesystem::path(argv[2]));
        std::cout << "Watch runtime verified: " << report.files << " files (" << report.encoded << " EHUDZ01, " << report.derived << " derived), "
                  << report.storedBytes << " stored bytes -> " << report.decodedBytes << " decoded bytes\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "package_watch_resources: " << e.what() << '\n';
        return 1;
    }
}
