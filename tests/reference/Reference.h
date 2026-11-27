#pragma once

// Direct calls into the upstream libraries, used as ground truth by the tests.

#include <string>
#include <vector>

namespace gsp::reference
{
// Runs a .nam file through NeuralAmpModelerCore at `sampleRate` in blocks of `block`.
bool runNam (const std::string& file, const std::vector<float>& in, std::vector<float>& out,
             double sampleRate, int block, std::string& error);

// Runs an AIDA-X json through RTNeural's dynamic (runtime-parsed) model.
bool runAidaDynamic (const std::string& file, const std::vector<float>& in, std::vector<float>& out,
                     std::string& error);
} // namespace gsp::reference
