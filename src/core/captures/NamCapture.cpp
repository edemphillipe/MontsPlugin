// NAM (.nam) captures, read and run by the official NeuralAmpModelerCore library.

#include "CaptureModel.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include <algorithm>
#include <vector>

namespace gsp
{
namespace
{

class NamCapture final : public CaptureModel
{
public:
    NamCapture (std::unique_ptr<nam::DSP> model, CaptureInfo infoIn)
        : dsp (std::move (model))
    {
        captureInfo = std::move (infoIn);
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        inBuffer.assign ((size_t) maxBlockSize, 0.0);
        outBuffer.assign ((size_t) maxBlockSize, 0.0);
        // Reset() also prewarms the model so it starts without a transient.
        dsp->Reset (sampleRate, maxBlockSize);
        preparedRate = sampleRate;
        preparedBlock = maxBlockSize;
    }

    void process (float* data, int numSamples) noexcept override
    {
        if (numSamples > (int) inBuffer.size())
            return; // contract violation; leave audio untouched rather than allocate

        for (int i = 0; i < numSamples; ++i)
            inBuffer[(size_t) i] = (NAM_SAMPLE) data[i];

        NAM_SAMPLE* in[] = { inBuffer.data() };
        NAM_SAMPLE* out[] = { outBuffer.data() };
        dsp->process (in, out, numSamples);

        for (int i = 0; i < numSamples; ++i)
            data[i] = (float) outBuffer[(size_t) i];
    }

    void reset() override
    {
        if (preparedBlock > 0)
            dsp->Reset (preparedRate, preparedBlock);
    }

private:
    std::unique_ptr<nam::DSP> dsp;
    std::vector<NAM_SAMPLE> inBuffer, outBuffer;
    double preparedRate = 48000.0;
    int preparedBlock = 0;
};

std::string jsonToString (const nlohmann::json& value)
{
    return value.is_string() ? value.get<std::string>() : value.dump();
}

} // namespace

std::unique_ptr<CaptureModel> loadNamCapture (const std::filesystem::path& file, std::string& error)
{
    try
    {
        nam::dspData config;
        auto model = nam::get_dsp (file, config);

        if (model == nullptr)
        {
            error = "NAM: o arquivo foi lido, mas a arquitetura nao e suportada.";
            return nullptr;
        }

        if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1)
        {
            error = "NAM: apenas capturas mono (1 entrada, 1 saida) sao suportadas nesta versao.";
            return nullptr;
        }

        CaptureInfo info;
        info.path = file;
        info.name = file.stem().string();
        info.format = CaptureFormat::NAM;
        info.architecture = config.architecture;
        info.expectedSampleRate = model->GetExpectedSampleRate();
        info.supportsAnySampleRate = model->SupportsArbitrarySampleRate();
        info.hasLoudness = model->HasLoudness();
        info.loudnessDb = info.hasLoudness ? model->GetLoudness() : 0.0;

        if (config.metadata.is_object())
        {
            for (auto it = config.metadata.begin(); it != config.metadata.end(); ++it)
                if (! it.value().is_null())
                    info.metadata[it.key()] = jsonToString (it.value());

            if (auto n = info.metadata.find ("name"); n != info.metadata.end() && ! n->second.empty())
                info.name = n->second;
        }

        return std::make_unique<NamCapture> (std::move (model), std::move (info));
    }
    catch (const std::exception& e)
    {
        error = std::string ("NAM: nao foi possivel ler o arquivo: ") + e.what();
        return nullptr;
    }
}

} // namespace gsp
