#include "game/TncDataPaths.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool HasConvertedData(const fs::path &root) {
    return fs::is_directory(root / "sprites") && fs::is_directory(root / "maps");
}

}  // namespace

std::string ResolveT4CDataRoot() {
    static std::string cached;
    static bool tried = false;
    if (tried) {
        return cached;
    }
    tried = true;

    if (const char *env = std::getenv("T4C_DATA")) {
        const fs::path p(env);
        if (HasConvertedData(p)) {
            cached = p.string();
            return cached;
        }
    }

    std::vector<fs::path> candidates;
    /* SDL3 : SDL_GetBasePath() → const char* interne, ne jamais SDL_free (SDL2 était différent). */
    if (const char *base = SDL_GetBasePath()) {
        const fs::path exeDir(base);
        candidates.push_back(exeDir / "data");
        candidates.push_back(exeDir / ".." / "data");
        candidates.push_back(exeDir / ".." / ".." / "data");
    }

    candidates.push_back(fs::current_path() / "data");
    candidates.push_back(fs::current_path() / "build" / "data");

    for (const fs::path &c : candidates) {
        std::error_code ec;
        const fs::path canon = fs::weakly_canonical(c, ec);
        const fs::path &probe = ec ? c : canon;
        if (HasConvertedData(probe)) {
            cached = probe.string();
            return cached;
        }
    }
    return cached;
}

std::string T4CDataPath(const char *subpath) {
    const std::string &root = ResolveT4CDataRoot();
    if (root.empty() || !subpath) {
        return {};
    }
    return (fs::path(root) / subpath).string();
}
