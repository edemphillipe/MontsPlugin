#pragma once

// Base class for every pedal / studio effect.
//
// Each effect declares its parameters (name, range, default, unit, optional choices);
// the UI builds knobs and selectors from that list, and presets save/restore it.
// Parameter values are atomics so the UI can change them while audio runs.
// All effects process stereo in place; mono "pre-amp" pedals process the left channel
// and the chain copies it before the amp.

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace gsp::fx
{

struct ParamInfo
{
    ParamInfo (std::string idIn, std::string nameIn, float minIn, float maxIn, float defIn, std::string unitIn = {},
               std::vector<std::string> choicesIn = {}, bool logIn = false)
        : id (std::move (idIn)), name (std::move (nameIn)), min (minIn), max (maxIn), def (defIn),
          unit (std::move (unitIn)), choices (std::move (choicesIn)), logarithmic (logIn) {}

    std::string id;           // stable key used in presets
    std::string name;         // label shown on the pedal
    float min = 0.0f, max = 1.0f, def = 0.5f;
    std::string unit;         // "dB", "ms", "%", "Hz", ""
    std::vector<std::string> choices; // non-empty = selector (value is the index)
    bool logarithmic = false;
};

// Musical subdivisions for tempo sync (fraction of a quarter note).
enum class Subdivision { Quarter = 0, Eighth, DottedEighth, EighthTriplet, Sixteenth, Half, DottedQuarter };
inline const std::vector<std::string>& subdivisionNames()
{
    static const std::vector<std::string> names { "1/4", "1/8", "1/8 pontuada", "1/8 tercina", "1/16", "1/2", "1/4 pontuada" };
    return names;
}
inline double subdivisionBeats (int s)
{
    switch ((Subdivision) s)
    {
        case Subdivision::Quarter:       return 1.0;
        case Subdivision::Eighth:        return 0.5;
        case Subdivision::DottedEighth:  return 0.75;
        case Subdivision::EighthTriplet: return 1.0 / 3.0;
        case Subdivision::Sixteenth:     return 0.25;
        case Subdivision::Half:          return 2.0;
        case Subdivision::DottedQuarter: return 1.5;
    }
    return 1.0;
}

// Shared tempo (set by the TAP button or typed BPM).
struct TempoClock
{
    std::atomic<double> bpm { 120.0 };
    double secondsFor (int subdivision) const noexcept { return 60.0 / bpm.load() * subdivisionBeats (subdivision); }
};

class Effect
{
public:
    enum class Placement { PreAmp, PostAmp };

    Effect (std::string typeIdIn, std::string displayName, Placement p)
        : typeId (std::move (typeIdIn)), name (std::move (displayName)), placement (p) {}
    virtual ~Effect() = default;

    const std::string& getTypeId() const noexcept { return typeId; }
    const std::string& getName() const noexcept { return name; }
    Placement getPlacement() const noexcept { return placement; }

    const std::vector<ParamInfo>& getParams() const noexcept { return params; }
    int numParams() const noexcept { return (int) params.size(); }
    int indexOf (const std::string& id) const noexcept
    {
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == id) return (int) i;
        return -1;
    }

    float get (int index) const noexcept { return values[(size_t) index].load (std::memory_order_relaxed); }
    float get (const std::string& id) const noexcept { const int i = indexOf (id); return i >= 0 ? get (i) : 0.0f; }
    void set (int index, float v) noexcept
    {
        const auto& p = params[(size_t) index];
        values[(size_t) index].store (juce::jlimit (p.min, p.max, v), std::memory_order_relaxed);
    }
    void set (const std::string& id, float v) noexcept { const int i = indexOf (id); if (i >= 0) set (i, v); }
    int choice (int index) const noexcept { return (int) std::lround (get (index)); }

    std::atomic<bool> enabled { true };

    void setTempoClock (const TempoClock* c) noexcept { tempo = c; }

    // --- called by the chain ---
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        blockSize = maxBlock;
        dryL.assign ((size_t) maxBlock, 0.0f);
        dryR.assign ((size_t) maxBlock, 0.0f);
        mixRamp.reset (sampleRate, 0.02);
        mixRamp.setCurrentAndTargetValue (enabled.load() ? 1.0f : 0.0f);
        prepareEffect (sampleRate, maxBlock);
        resetEffect();
    }

    // Processes with a click-free crossfade when the pedal is switched on/off ("true bypass" feel).
    void process (float* left, float* right, int n) noexcept
    {
        mixRamp.setTargetValue (enabled.load (std::memory_order_relaxed) ? 1.0f : 0.0f);

        if (! mixRamp.isSmoothing())
        {
            if (mixRamp.getTargetValue() < 0.5f)
            {
                if (tailWhenBypassed())
                    processEffect (left, right, n, true); // keep reverb/delay tails ringing
                return;
            }
            processEffect (left, right, n, false);
            return;
        }

        std::copy (left, left + n, dryL.begin());
        std::copy (right, right + n, dryR.begin());
        processEffect (left, right, n, false);
        for (int i = 0; i < n; ++i)
        {
            const float w = mixRamp.getNextValue();
            left[i] = dryL[(size_t) i] + w * (left[i] - dryL[(size_t) i]);
            right[i] = dryR[(size_t) i] + w * (right[i] - dryR[(size_t) i]);
        }
    }

    void reset() { resetEffect(); }

protected:
    int addParam (ParamInfo p)
    {
        params.push_back (p);
        values[params.size() - 1].store (p.def);
        return (int) params.size() - 1;
    }

    virtual void prepareEffect (double sampleRate, int maxBlock) = 0;
    virtual void resetEffect() = 0;
    // When `inputMuted` is true the pedal is bypassed but lets its tail decay: feed silence
    // into the effect and add only the wet tail to the dry signal.
    virtual void processEffect (float* left, float* right, int n, bool inputMuted) noexcept = 0;
    virtual bool tailWhenBypassed() const noexcept { return false; }

    double fs = 48000.0;
    int blockSize = 512;
    const TempoClock* tempo = nullptr;

private:
    std::string typeId, name;
    Placement placement;
    std::vector<ParamInfo> params;
    std::array<std::atomic<float>, 16> values {};
    std::vector<float> dryL, dryR;
    juce::SmoothedValue<float> mixRamp;
};

// ---------------------------------------------------------------- small helpers
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

// One-pole smoothing for parameters read once per block.
struct Smoothed
{
    float current = 0.0f, coeff = 0.001f;
    void prepare (double fs, double ms = 30.0, float start = 0.0f) { coeff = 1.0f - std::exp (-1.0f / (float) (ms * 0.001 * fs)); current = start; }
    float next (float target) noexcept { current += coeff * (target - current); return current; }
};

// Fractional delay line (cubic interpolation), power-of-two buffer.
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        write = 0;
    }
    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }
    void push (float x) noexcept { buffer[(size_t) write] = x; write = (write + 1) & mask; }
    // delay in samples (>= 1), measured from the most recent push
    float read (float delay) const noexcept
    {
        const float pos = (float) write - delay;
        const int i = (int) std::floor (pos);
        const float f = pos - (float) i;
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    float readInt (int delay) const noexcept { return at (write - delay); }
    int capacity() const noexcept { return mask; }

private:
    float at (int i) const noexcept { return buffer[(size_t) (i & mask)]; }
    std::vector<float> buffer { 0.0f };
    int mask = 0, write = 0;
};

struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setLowpass (double fs, double hz) { a = (float) std::exp (-2.0 * 3.14159265358979 * hz / fs); }
    float lp (float x) noexcept { z = x + a * (z - x); return z; }
    float hp (float x) noexcept { return x - lp (x); }
    void reset() { z = 0.0f; }
};

struct Lfo
{
    double phase = 0.0, inc = 0.0;
    void setRate (double fs, double hz) { inc = hz / fs; }
    float sine() noexcept { const float v = (float) std::sin (2.0 * 3.14159265358979 * phase); advance(); return v; }
    float triangle() noexcept { const float v = (float) (4.0 * std::abs (phase - 0.5) - 1.0); advance(); return v; }
    void advance() noexcept { phase += inc; if (phase >= 1.0) phase -= 1.0; }
};

} // namespace gsp::fx
