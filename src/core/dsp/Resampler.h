#pragma once

// Streaming windowed-sinc sample-rate converter, used to run a capture at the rate it
// was trained at (usually 48 kHz) when the audio interface runs at another rate.
// Real-time safe after prepare().

#include <vector>

namespace gsp
{

class StreamResampler
{
public:
    // ratio = inputRate / outputRate. maxInputBlock bounds a single process() call.
    void prepare (double inputRate, double outputRate, int maxInputBlock);
    void reset();

    // Consumes all nIn samples, writes up to maxOut samples, returns how many were written.
    int process (const float* in, int nIn, float* out, int maxOut) noexcept;

    // Upper bound of outputs for nIn inputs.
    int maxOutputFor (int nIn) const noexcept;

    static constexpr int halfTaps = 16;   // 32-tap kernel
    static constexpr int phases = 512;    // kernel table resolution

private:
    float kernelAt (int tap, double frac) const noexcept;

    double ratio = 1.0;      // input samples advanced per output sample
    double cutoff = 1.0;     // normalised cutoff (1 = input Nyquist)
    double position = 0.0;   // read position relative to buffer start
    std::vector<float> buffer;
    int buffered = 0;
    std::vector<float> table; // (2*halfTaps) * (phases + 1)
};

} // namespace gsp
