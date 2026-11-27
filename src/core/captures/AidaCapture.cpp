// AIDA-X (.json) captures, run with RTNeural - the same inference library AIDA-X uses.
//
// File format (aidadsp / RTNeural Keras export):
//   in_shape : [null, null, N]   N = 1 (audio) or 2..3 (audio + 1..2 conditioning knobs)
//   layers   : [ {type: "lstm"|"gru", shape: [.., H], weights: ...}, {type: "dense", ...} ]
//   in_skip  : optional, 1 = the model predicts the residual (output += input)
//   in_gain  : optional, dB applied before the network
//   out_gain : optional, dB applied after the network
//   metadata : optional, { name, samplerate, based, author, ... }
//
// Common shapes (one recurrent layer + dense, hidden 8..80) are compiled as static
// RTNeural models for speed. Anything else falls back to RTNeural's dynamic model.

#include "CaptureModel.h"

#include <RTNeural/RTNeural.h>

#include <array>
#include <cmath>
#include <fstream>
#include <functional>

namespace gsp
{
namespace
{

using json = nlohmann::json;

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

// ---------------------------------------------------------------------------------------------
struct AidaNetwork
{
    virtual ~AidaNetwork() = default;
    virtual void load (const json& j) = 0;
    virtual void reset() = 0;
    // in[0] = audio sample, in[1..] = conditioning
    virtual float forward (const float* in) noexcept = 0;
    virtual int inputSize() const noexcept = 0;
};

template <int In, int Hidden, bool IsGru>
struct StaticNetwork final : AidaNetwork
{
    using Recurrent = std::conditional_t<IsGru,
                                         RTNeural::GRULayerT<float, In, Hidden>,
                                         RTNeural::LSTMLayerT<float, In, Hidden>>;
    RTNeural::ModelT<float, In, 1, Recurrent, RTNeural::DenseT<float, Hidden, 1>> model;

    void load (const json& j) override { model.parseJson (j, false); model.reset(); }
    void reset() override { model.reset(); }
    int inputSize() const noexcept override { return In; }

    float forward (const float* in) noexcept override
    {
        alignas (RTNEURAL_DEFAULT_ALIGNMENT) float buffer[In];
        for (int i = 0; i < In; ++i)
            buffer[i] = in[i];
        return model.forward (buffer);
    }
};

struct DynamicNetwork final : AidaNetwork
{
    std::unique_ptr<RTNeural::Model<float>> model;
    int numInputs = 1;

    void load (const json& j) override
    {
        model = RTNeural::json_parser::parseJson<float> (j, false);
        if (model == nullptr)
            throw std::runtime_error ("RTNeural could not build the network");
        numInputs = model->getInSize();
        model->reset();
    }
    void reset() override { model->reset(); }
    int inputSize() const noexcept override { return numInputs; }
    float forward (const float* in) noexcept override { return model->forward (in); }
};

// --- factory over the shapes AIDA-X ships: hidden in {8,12,16,20,24,32,40,64,80}, inputs 1..3
using Factory = std::function<std::unique_ptr<AidaNetwork>()>;

template <int In, int Hidden>
Factory pick (bool gru)
{
    if (gru)
        return [] { return std::make_unique<StaticNetwork<In, Hidden, true>>(); };
    return [] { return std::make_unique<StaticNetwork<In, Hidden, false>>(); };
}

template <int In>
Factory pickHidden (int hidden, bool gru)
{
    switch (hidden)
    {
        case 8:  return pick<In, 8> (gru);
        case 12: return pick<In, 12> (gru);
        case 16: return pick<In, 16> (gru);
        case 20: return pick<In, 20> (gru);
        case 24: return pick<In, 24> (gru);
        case 32: return pick<In, 32> (gru);
        case 40: return pick<In, 40> (gru);
        case 64: return pick<In, 64> (gru);
        case 80: return pick<In, 80> (gru);
        default: return {};
    }
}

Factory staticFactoryFor (const json& j)
{
    const auto& layers = j.at ("layers");
    if (layers.size() != 2 || layers.at (1).at ("type").get<std::string>() != "dense")
        return {};

    const auto type = layers.at (0).at ("type").get<std::string>();
    if (type != "lstm" && type != "gru")
        return {};

    const int hidden = layers.at (0).at ("shape").back().get<int>();
    const int inputs = j.at ("in_shape").back().get<int>();
    const bool gru = type == "gru";

    switch (inputs)
    {
        case 1: return pickHidden<1> (hidden, gru);
        case 2: return pickHidden<2> (hidden, gru);
        case 3: return pickHidden<3> (hidden, gru);
        default: return {};
    }
}

// ---------------------------------------------------------------------------------------------
class AidaCapture final : public CaptureModel
{
public:
    AidaCapture (std::unique_ptr<AidaNetwork> net, CaptureInfo infoIn, bool skip, float inGain, float outGain)
        : network (std::move (net)), inputSkip (skip), inputGain (inGain), outputGain (outGain)
    {
        captureInfo = std::move (infoIn);
        conditioning.fill (0.5f);
    }

    void prepare (double, int) override { network->reset(); }
    void reset() override { network->reset(); }

    void setConditioning (int index, float value) noexcept override
    {
        if (index >= 0 && index < (int) conditioning.size())
            conditioning[(size_t) index] = value;
    }

    void process (float* data, int numSamples) noexcept override
    {
        const int numInputs = network->inputSize();
        alignas (RTNEURAL_DEFAULT_ALIGNMENT) float in[4] {};
        for (int c = 1; c < numInputs && c < 4; ++c)
            in[c] = conditioning[(size_t) c - 1];

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = data[i] * inputGain;
            in[0] = x;
            const float y = network->forward (in);
            data[i] = (inputSkip ? (x + y) : y) * outputGain;
        }
    }

private:
    std::unique_ptr<AidaNetwork> network;
    bool inputSkip = false;
    float inputGain = 1.0f, outputGain = 1.0f;
    std::array<float, 3> conditioning {};
};

} // namespace

std::unique_ptr<CaptureModel> loadAidaCapture (const std::filesystem::path& file, std::string& error)
{
    try
    {
        std::ifstream stream (file, std::ios::binary);
        if (! stream)
        {
            error = "AIDA-X: nao foi possivel abrir o arquivo.";
            return nullptr;
        }

        json j;
        stream >> j;

        if (! j.contains ("in_shape") || ! j.contains ("layers"))
        {
            error = "AIDA-X: o arquivo nao parece ser um modelo AIDA-X (faltam in_shape/layers).";
            return nullptr;
        }

        const int inputs = j.at ("in_shape").back().get<int>();
        if (inputs < 1 || inputs > 3)
        {
            error = "AIDA-X: numero de entradas nao suportado (" + std::to_string (inputs) + ").";
            return nullptr;
        }

        int skip = 0;
        if (j.contains ("in_skip") && j["in_skip"].is_number())
            skip = j["in_skip"].get<int>();
        if (skip > 1)
        {
            error = "AIDA-X: in_skip > 1 nao e suportado.";
            return nullptr;
        }

        const float inGain = (j.contains ("in_gain") && j["in_gain"].is_number()) ? dbToGain (j["in_gain"].get<float>()) : 1.0f;
        const float outGain = (j.contains ("out_gain") && j["out_gain"].is_number()) ? dbToGain (j["out_gain"].get<float>()) : 1.0f;

        std::unique_ptr<AidaNetwork> network;
        if (auto factory = staticFactoryFor (j))
            network = factory();
        else
            network = std::make_unique<DynamicNetwork>();
        network->load (j);

        CaptureInfo info;
        info.path = file;
        info.name = file.stem().string();
        info.format = CaptureFormat::AidaX;
        info.architecture = j.at ("layers").at (0).at ("type").get<std::string>();
        for (auto& c : info.architecture)
            c = (char) std::toupper ((unsigned char) c);
        info.architecture += " " + std::to_string (j.at ("layers").at (0).at ("shape").back().get<int>());
        info.numConditioningInputs = inputs - 1;

        if (j.contains ("metadata") && j["metadata"].is_object())
        {
            for (auto it = j["metadata"].begin(); it != j["metadata"].end(); ++it)
                if (! it.value().is_null())
                    info.metadata[it.key()] = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();

            if (auto n = info.metadata.find ("name"); n != info.metadata.end() && ! n->second.empty())
                info.name = n->second;

            if (auto sr = info.metadata.find ("samplerate"); sr != info.metadata.end())
            {
                try { info.expectedSampleRate = std::stod (sr->second); }
                catch (...) {}
            }
        }

        return std::make_unique<AidaCapture> (std::move (network), std::move (info), skip == 1, inGain, outGain);
    }
    catch (const std::exception& e)
    {
        error = std::string ("AIDA-X: nao foi possivel ler o arquivo: ") + e.what();
        return nullptr;
    }
}

} // namespace gsp
