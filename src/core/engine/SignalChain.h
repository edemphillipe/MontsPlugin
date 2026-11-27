#pragma once

// Phase 1 signal chain, shared by macOS and Windows:
//
//   input gain -> noise gate -> capture (NAM / AIDA-X) -> cabinet IR -> EQ -> output gain
//
// Threading: the audio thread calls process(); the UI thread calls the setters and
// loadCapture/loadImpulseResponse. Captures are swapped without locks: a new capture is
// prepared on the UI thread, handed over through an atomic pointer, faded in on the audio
// thread, and the old one is freed later on the UI thread (collectGarbage()).

#include "captures/CaptureModel.h"
#include "dsp/Biquad.h"
#include "dsp/NoiseGate.h"
#include "engine/CaptureSlot.h"

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

    Levels readAndResetLevels() noexcept;

private:
    void updateEq() noexcept;

    double sampleRate = 48000.0;
    int maxBlock = 512;

    juce::HeapBlock<float> work;
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
