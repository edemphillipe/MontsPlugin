#pragma once

// Noise gate with hysteresis, hold and smooth gain ramps (no clicks).
// Detection runs on the dry guitar signal before the amp/capture, where the
// signal-to-noise ratio is best.

#include <cmath>

namespace gsp
{

class NoiseGate
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        envCoeffAttack = coeff (0.5);
        envCoeffRelease = coeff (20.0);
        setTimes (1.0, 80.0, 60.0);
        reset();
    }

    void setThresholdDb (float db) noexcept { thresholdOpen = std::pow (10.0f, db / 20.0f); thresholdClose = thresholdOpen * 0.5f; }
    void setEnabled (bool e) noexcept { enabled = e; }

    void setTimes (double attackMs, double holdMs, double releaseMs) noexcept
    {
        gainUp = 1.0f / (float) std::fmax (1.0, attackMs * 0.001 * fs);
        gainDown = 1.0f / (float) std::fmax (1.0, releaseMs * 0.001 * fs);
        holdSamples = (int) (holdMs * 0.001 * fs);
    }

    void reset() noexcept { env = 0.0f; gain = 1.0f; holdCounter = 0; open = true; }

    void process (float* data, int n) noexcept
    {
        if (! enabled)
            return;

        for (int i = 0; i < n; ++i)
        {
            const float a = std::fabs (data[i]);
            env += (a > env ? envCoeffAttack : envCoeffRelease) * (a - env);

            if (env > thresholdOpen)
            {
                open = true;
                holdCounter = holdSamples;
            }
            else if (env < thresholdClose)
            {
                if (holdCounter > 0) --holdCounter;
                else open = false;
            }

            gain = open ? std::fmin (1.0f, gain + gainUp) : std::fmax (0.0f, gain - gainDown);
            data[i] *= gain * gain; // squared ramp sounds more natural
        }
    }

    bool isOpen() const noexcept { return open; }

private:
    float coeff (double ms) const noexcept { return 1.0f - (float) std::exp (-1.0 / (ms * 0.001 * fs)); }

    double fs = 48000.0;
    bool enabled = true, open = true;
    float thresholdOpen = 0.003f, thresholdClose = 0.0015f;
    float env = 0.0f, gain = 1.0f, gainUp = 0.01f, gainDown = 0.001f;
    float envCoeffAttack = 0.1f, envCoeffRelease = 0.01f;
    int holdSamples = 0, holdCounter = 0;
};

} // namespace gsp
