#include "Reference.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

namespace gsp::reference
{
bool runNam (const std::string& file, const std::vector<float>& in, std::vector<float>& out,
             double sampleRate, int block, std::string& error)
{
    try
    {
        auto dsp = nam::get_dsp (std::filesystem::path (file));
        dsp->Reset (sampleRate, block);
        std::vector<NAM_SAMPLE> a ((size_t) block), b ((size_t) block);
        out.assign (in.size(), 0.0f);
        for (size_t start = 0; start < in.size(); start += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, in.size() - start);
            for (int i = 0; i < n; ++i)
                a[(size_t) i] = in[start + (size_t) i];
            NAM_SAMPLE* pin[] = { a.data() };
            NAM_SAMPLE* pout[] = { b.data() };
            dsp->process (pin, pout, n);
            for (int i = 0; i < n; ++i)
                out[start + (size_t) i] = (float) b[(size_t) i];
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
