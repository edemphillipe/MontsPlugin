#pragma once

#include "CaptureModel.h"

namespace gsp
{

// Picks the loader from the file extension: .nam -> NAM, .json -> AIDA-X.
std::unique_ptr<CaptureModel> loadCapture (const std::filesystem::path& file, std::string& error);

bool isCaptureFile (const std::filesystem::path& file);

} // namespace gsp
