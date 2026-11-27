#include "CaptureLoader.h"

#include <algorithm>
#include <cctype>

namespace gsp
{
namespace
{
std::string lowerExtension (const std::filesystem::path& file)
{
    auto ext = file.extension().string();
    std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return ext;
}
} // namespace

bool isCaptureFile (const std::filesystem::path& file)
{
    const auto ext = lowerExtension (file);
    return ext == ".nam" || ext == ".json" || ext == ".aidax";
}

std::unique_ptr<CaptureModel> loadCapture (const std::filesystem::path& file, std::string& error)
{
    const auto ext = lowerExtension (file);
    if (ext == ".nam")
        return loadNamCapture (file, error);
    if (ext == ".json" || ext == ".aidax")
        return loadAidaCapture (file, error);

    error = "Formato de captura nao reconhecido: " + ext + " (use .nam ou .json do AIDA-X).";
    return nullptr;
}

} // namespace gsp
