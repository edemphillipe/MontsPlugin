#pragma once

// Built-in amplifier models (Phase 2). Each model is a "voicing" of the same structure:
//
//   input -> bright/coupling filters -> N triode stages (asymmetric soft clipping, coupling
//   high-pass and Miller low-pass between stages, GAIN pot after the first stage)
//   -> passive tone stack (BASS/MIDDLE/TREBLE, circuit model) -> VOLUME
//   -> power amp (PRESENCE/resonance in the feedback loop, push-pull saturation, supply sag)
//   -> MASTER
//
// The non-linear part runs at 4x oversampling. Names and voicings are our own; they are
// inspired by the sound of classic amp families, not copies of specific circuits.

#include "amp/ToneStack.h"
#include "dsp/Biquad.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

namespace gsp::amp
{

struct Voicing
{
    const char* id;          // stable id used in saved settings
    const char* name;        // shown in the UI
    const char* description; // one line for the UI
    int stages;              // number of triode stages (2..5)
    float inputHpfHz;        // input coupling (tightness)
    float brightDb;          // bright cap: treble boost that fades as GAIN goes up
    float firstStageGain;    // linear gain of stage 1
    float stageGain;         // linear gain of the following stages
    float maxGainDb;         // GAIN pot range (dB of attenuation at 0)
    float bias;              // operating point offset (asymmetry -> even harmonics)
    float interstageHpfHz;
    float interstageLpfHz;
    ToneStackParts toneStack;
    float powerDrive;        // how hard VOLUME pushes the power amp
    float powerAsymmetry;    // 0 = push-pull (odd), >0 = class-A flavour
    float sag;               // supply sag amount
    float resonanceHz, resonanceDb;
    float presenceHz;
    float outputTrimDb;      // level matching between voicings
};

int numVoicings() noexcept;
const Voicing& voicing (int index) noexcept;
int voicingIndexForId (const juce::String& id) noexcept;

class AmpSim
{
public:
    AmpSim();
    ~AmpSim();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (float* x, int numSamples) noexcept; // mono, in place, uses voicingIndex
    void process (float* x, int numSamples, int voicing) noexcept; // explicit model (signal chain cross-fades changes)

    // parameters (any thread); knobs 0..10 like on a real amp
    std::atomic<int> voicingIndex { 1 };
    std::atomic<float> gain { 5.0f }, volume { 5.0f }, bass { 5.0f }, middle { 5.0f }, treble { 5.0f },
        presence { 5.0f }, master { 5.0f };

    int latencySamples() const noexcept;

private:
    void updateCoefficients (const Voicing& v, bool force) noexcept;

    double fs = 48000.0, osFs = 192000.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int activeVoicing = -1;

    struct Stage
    {
        Biquad hpf, lpf;
        float dcOffset = 0.0f;
    };
    std::array<Stage, 5> stages;
    Biquad inputHpf, bright, resonance, presenceShelf, outputLpf, outputHpf;
    ToneStack toneStack;

    float lastTreble = -1, lastMiddle = -1, lastBass = -1, lastPresence = -1, lastGain = -1;
    float sagEnv = 0.0f, sagAttack = 0.0f, sagRelease = 0.0f;
    float gainPot = 1.0f, gainPotTarget = 1.0f;
    float volumeGain = 1.0f, masterGain = 1.0f;
};

} // namespace gsp::amp
