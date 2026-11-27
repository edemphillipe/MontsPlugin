#pragma once

// Format-independent interface for neural captures (NAM, AIDA-X).
// This header deliberately has no JUCE, Eigen or nlohmann dependency so that
// each loader can be compiled against its own copy of those libraries.

#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace gsp
{

enum class CaptureFormat
{
    NAM,   // Neural Amp Modeler, .nam
    AidaX  // AIDA-X / RTNeural, .json (aidadsp format)
};

struct CaptureInfo
{
    std::string name;                 // display name (file stem unless metadata says otherwise)
    std::filesystem::path path;
    CaptureFormat format = CaptureFormat::NAM;
    std::string architecture;         // e.g. "WaveNet", "LSTM", "GRU"
    double expectedSampleRate = -1.0; // training sample rate, <= 0 if unknown
    bool supportsAnySampleRate = false;
    bool hasLoudness = false;         // NAM only: loudness of the model for a standard input
    double loudnessDb = 0.0;
    int numConditioningInputs = 0;    // AIDA-X models trained with 1 or 2 extra knob inputs
    std::map<std::string, std::string> metadata; // manufacturer, model, author, ...
};

class CaptureModel
{
public:
    virtual ~CaptureModel() = default;

    const CaptureInfo& info() const noexcept { return captureInfo; }

    // Not real-time safe: may allocate. Call before processing and on device changes.
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    // Real-time safe, in-place, mono. numSamples <= maxBlockSize given to prepare().
    virtual void process (float* data, int numSamples) noexcept = 0;

    // Clears the model's internal state (not real-time safe for NAM).
    virtual void reset() = 0;

    // AIDA-X conditioned models: value in [0, 1] for knob input `index` (0 or 1).
    virtual void setConditioning (int /*index*/, float /*value*/) noexcept {}

protected:
    CaptureInfo captureInfo;
};

// Loaders. On failure they return nullptr and fill `error` with a readable message.
std::unique_ptr<CaptureModel> loadNamCapture (const std::filesystem::path& file, std::string& error);
std::unique_ptr<CaptureModel> loadAidaCapture (const std::filesystem::path& file, std::string& error);

} // namespace gsp
