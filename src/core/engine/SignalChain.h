#pragma once

// Phase 1 signal chain, shared by macOS and Windows:
//
//   input gain -> noise gate -> [pedals] -> AMP -> [pedals] -> output gain
//   AMP = (NAM/AIDA-X capture  or  built-in amp model) -> cabinet (IR file or simulation) -> EQ
//   (the AMP block can sit anywhere in the pedal order; after it the chain is stereo)
//
// Threading: the audio thread calls process(); the UI thread calls the setters and
// loadCapture/loadImpulseResponse. Captures are swapped without locks: a new capture is
// prepared on the UI thread, handed over through an atomic pointer, faded in on the audio
// thread, and the old one is freed later on the UI thread (collectGarbage()).

#include "amp/AmpSim.h"
#include "amp/CabinetSim.h"
#include "captures/CaptureModel.h"
#include "dsp/Biquad.h"
#include "dsp/NoiseGate.h"
#include "engine/CaptureSlot.h"
#include "engine/Pedalboard.h"
#include "tuner/PitchDetector.h"

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <mutex>
#include <memory>

namespace gsp
{

class SignalChain
{
public:
    struct Levels
    {
        float inputPeak = 0.0f, outputPeak = 0.0f;
    };

    SignalChain();
    ~SignalChain();

    // --- audio thread (or before audio starts) ---
    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* monoIn, float* const* outputs, int numOutputs, int numSamples) noexcept;
    void releaseResources();

    // --- UI thread ---
    bool loadCapture (const juce::File& file, juce::String& error);
    void clearCapture();
    bool loadImpulseResponse (const juce::File& file, juce::String& error);
    void clearImpulseResponse();
    void collectGarbage();

    // Info about the active / most recently loaded capture (UI thread)
    juce::String getCaptureName() const;
    juce::String getCaptureDescription() const;
    juce::String getImpulseResponseName() const { return irName; }
    int getCaptureConditioningInputs() const { return loadedConditioningInputs; }

    // Parameters (any thread)
    std::atomic<float> inputGainDb { 0.0f }, outputGainDb { 0.0f };
    std::atomic<float> gateThresholdDb { -60.0f };
    std::atomic<bool> gateEnabled { true };
    std::atomic<bool> captureEnabled { true };
    std::atomic<bool> irEnabled { true };
    std::atomic<bool> normaliseLoudness { true };
    std::atomic<float> conditioning1 { 0.5f }, conditioning2 { 0.5f }; // AIDA-X knob inputs
    std::atomic<float> bassDb { 0.0f }, middleDb { 0.0f }, trebleDb { 0.0f };

    enum AmpSource { sourceCapture = 0, sourceAmpModel = 1 };
    enum CabinetMode { cabinetSimulated = 0, cabinetImpulseResponse = 1 };
    std::atomic<int> ampSource { sourceCapture };
    std::atomic<int> cabinetMode { cabinetSimulated };
    amp::AmpSim& getAmp() noexcept { return ampModel; }
    amp::CabinetSim& getCabinet() noexcept { return cabinetSim; }
    bool hasImpulseResponse() const noexcept { return irLoaded.load(); }

    Levels readAndResetLevels() noexcept;

    Pedalboard& getPedalboard() noexcept { return pedalboard; }

    // tuner: the input (after input gain, before the gate) is copied here for the UI
    TunerTap& getTunerTap() noexcept { return tunerTap; }
    double getSampleRate() const noexcept { return sampleRate; }
    std::atomic<bool> muteOutput { false }; // silent output while tuning (fades like the output gain)

private:
    void updateEq() noexcept;
    void processAmp (float* mono, int numSamples) noexcept;
    void processCapture (float* mono, int numSamples) noexcept;

    // Short fade-out / fade-in whenever the active path changes (amp source, amp model, cabinet)
    struct PathFade
    {
        int active = -1, pendingKey = -1;
        float gain = 1.0f, step = 0.002f;
        int direction = 0; // -1 out, +1 in
        // returns the key to use for this block
        int update (int desired) noexcept
        {
            if (active < 0) { active = desired; return active; }
            if (desired != active && direction == 0) { direction = -1; pendingKey = desired; }
            return active;
        }
        bool swapDue() const noexcept { return direction == -1 && gain <= 0.0f; }
        void apply (float* x, int n) noexcept
        {
            if (direction == 0) return;
            for (int i = 0; i < n; ++i)
            {
                gain = juce::jlimit (0.0f, 1.0f, gain + (float) direction * step);
                if (direction == 1 && gain >= 1.0f) direction = 0;
                x[i] *= gain;
            }
        }
        void swap() noexcept { active = pendingKey; direction = 1; }
    };
    PathFade pathFade;
    amp::AmpSim ampModel;
    amp::CabinetSim cabinetSim;

    double sampleRate = 48000.0;
    int maxBlock = 512;

    juce::HeapBlock<float> work, workR;
    Pedalboard pedalboard;
    TunerTap tunerTap;
    NoiseGate gate;

    // capture handover
    CaptureSlot* current = nullptr;            // owned, audio thread
    std::atomic<CaptureSlot*> pending { nullptr };
    std::atomic<CaptureSlot*> retired { nullptr };
    std::atomic<bool> pendingIsClear { false };
    float captureFade = 1.0f;                  // 0..1
    int fadeDirection = 0;                     // -1 fading out, +1 fading in
    float fadeStep = 0.001f;
    float captureGain = 1.0f;                  // loudness normalisation for the active capture
    juce::String loadedCaptureName, loadedCaptureDescription;
    int loadedConditioningInputs = 0;

    juce::dsp::Convolution cabinet;
    std::atomic<bool> irLoaded { false };
    juce::String irName;

    Biquad bass, middle, treble;
    float lastBass = 1000, lastMiddle = 1000, lastTreble = 1000;

    juce::SmoothedValue<float> inGain, outGain;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };

    std::mutex prepareMutex; // guards current/sampleRate between prepare() and UI-side preparation
};

} // namespace gsp
