#include "runtime_paths.h"

#include <SDL.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

std::filesystem::path vkutil::runtimePath(const std::filesystem::path& relativePath)
{
    static const std::filesystem::path executableDirectory = [] {
        std::unique_ptr<char, decltype(&SDL_free)> basePath(SDL_GetBasePath(), SDL_free);
        if (!basePath) {
            throw std::runtime_error(std::string("Cannot locate executable directory: ") + SDL_GetError());
        }
        return std::filesystem::path(std::u8string_view(
            reinterpret_cast<const char8_t*>(basePath.get())));
    }();
    return executableDirectory / relativePath;
}
