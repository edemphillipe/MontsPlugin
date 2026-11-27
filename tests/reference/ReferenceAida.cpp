#include "Reference.h"

#include <RTNeural/RTNeural.h>

#include <cmath>
#include <fstream>

namespace gsp::reference
{
bool runAidaDynamic (const std::string& file, const std::vector<float>& in, std::vector<float>& out,
                     std::string& error)
{
    try
    {
        std::ifstream s (file);
        nlohmann::json j;
        s >> j;
        auto model = RTNeural::json_parser::parseJson<float> (j, false);
        model->reset();
        const bool skip = j.contains ("in_skip") && j["in_skip"].is_number() && j["in_skip"].get<int>() == 1;
        const float ig = (j.contains ("in_gain") && j["in_gain"].is_number()) ? std::pow (10.0f, j["in_gain"].get<float>() / 20.0f) : 1.0f;
        const float og = (j.contains ("out_gain") && j["out_gain"].is_number()) ? std::pow (10.0f, j["out_gain"].get<float>() / 20.0f) : 1.0f;
        out.resize (in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            float x[4] = { in[i] * ig, 0.5f, 0.5f, 0.0f };
            const float y = model->forward (x);
            out[i] = (skip ? x[0] + y : y) * og;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}
} // namespace gsp::reference
