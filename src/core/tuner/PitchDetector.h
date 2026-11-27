#pragma once

// Guitar tuner: YIN pitch detection (de Cheveigne & Kawahara, 2002) on a mono buffer,
// plus a lock-free tap that copies the input from the audio thread for the UI to analyse.

#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace gsp
{

struct PitchResult
{
    bool valid = false;
    double frequency = 0.0;   // Hz
    std::string note;         // e.g. "E", "A#"
    int octave = 0;
    double cents = 0.0;       // -50..+50 from the nearest equal-tempered note (A4 = reference)
};

class PitchDetector
{
public:
    // Analyses `n` samples (use about 4096 at 48 kHz for low E). Range 30..1200 Hz.
    PitchResult detect (const float* x, int n, double sampleRate, double a4 = 440.0);

private:
    std::vector<float> diff, cmnd;
};

// Single-producer (audio thread) / single-consumer (UI thread) ring buffer of the last samples.
class TunerTap
{
public:
    static constexpr int size = 8192;
    void push (const float* x, int n) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            buffer[(size_t) w] = x[i];
            w = (w + 1) & (size - 1);
        }
        writePos.store (w, std::memory_order_release);
    }
    // Copies the most recent `n` samples (n <= size) in time order.
    void latest (float* out, int n) const noexcept
    {
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            out[i] = buffer[(size_t) ((w - n + i) & (size - 1))];
    }

private:
    std::array<float, size> buffer {};
    std::atomic<int> writePos { 0 };
};

} // namespace gsp
