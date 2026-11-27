#pragma once

// The pedalboard: every available pedal plus an "AMP" slot (capture + cabinet + EQ),
// in a user-defined order. Pedals before AMP feed the amp; pedals after it run in stereo.
//
// Order changes come from the UI thread; the audio thread picks them up at the start of a
// block with a try-lock (never waits). Pedal parameters are atomics inside each Effect.

#include "effects/Effect.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>

#include <memory>
#include <vector>

namespace gsp
{

class Pedalboard
{
public:
    static constexpr int ampSlot = -1;

    Pedalboard();

    fx::TempoClock& tempo() noexcept { return clock; }

    int size() const noexcept { return (int) pedals.size(); }
    fx::Effect& pedal (int index) noexcept { return *pedals[(size_t) index]; }
    int indexOfType (const std::string& typeId) const noexcept;

    // Order = list of pedal indices with exactly one ampSlot entry.
    std::vector<int> getOrder() const;
    const std::vector<int>& defaultOrder() const noexcept { return defaults; }
    void setOrder (const std::vector<int>& newOrder); // UI thread; invalid orders are ignored
    void movePedal (int pedalIndex, int newPosition);  // UI helper (position in the order list)

    // State for presets / settings (JSON)
    juce::var toVar() const;
    void fromVar (const juce::var&);

    // --- audio thread ---
    void prepare (double sampleRate, int maxBlock);
    // Runs pedals before the amp, calls ampProcess(L, R, n) for the AMP slot, then pedals after.
    template <typename AmpFn>
    void process (float* l, float* r, int n, AmpFn&& ampProcess) noexcept
    {
        {
            const juce::SpinLock::ScopedTryLockType lock (orderLock);
            if (lock.isLocked() && orderChanged)
            {
                activeOrder = sharedOrder;
                orderChanged = false;
            }
        }
        for (int slot : activeOrder)
        {
            if (slot == ampSlot)
                ampProcess (l, r, n);
            else
                pedals[(size_t) slot]->process (l, r, n);
        }
    }

private:
    static bool isValidOrder (const std::vector<int>& order, int numPedals);

    std::vector<std::unique_ptr<fx::Effect>> pedals;
    fx::TempoClock clock;

    juce::SpinLock orderLock;
    std::vector<int> defaults;      // order on first start
    std::vector<int> sharedOrder;   // guarded by orderLock
    bool orderChanged = false;      // guarded by orderLock
    std::vector<int> activeOrder;   // audio thread only (capacity reserved, never reallocates)
};

// TAP tempo: average of the last taps; a pause longer than 2 s starts a new measurement.
class TapTempo
{
public:
    // Returns the new BPM, or 0 if more taps are needed.
    double tap (double timeSeconds) noexcept
    {
        if (count > 0 && timeSeconds - last > 2.0)
            count = writeIndex = 0;
        if (count > 0)
        {
            intervals[(size_t) (writeIndex++ % (int) intervals.size())] = timeSeconds - last;
        }
        last = timeSeconds;
        ++count;
        const int n = std::min<int> (count - 1, (int) intervals.size());
        if (n < 1)
            return 0.0;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += intervals[(size_t) i];
        return juce::jlimit (30.0, 300.0, 60.0 / (sum / n));
    }

private:
    std::array<double, 4> intervals {};
    double last = 0.0;
    int count = 0, writeIndex = 0;
};

} // namespace gsp
