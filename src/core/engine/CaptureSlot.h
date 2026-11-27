#pragma once

#include "captures/CaptureModel.h"
#include "dsp/Resampler.h"

#include <cmath>
#include <memory>
#include <vector>

namespace gsp
{

// A loaded capture, prepared for a given device rate. If the capture was trained at a
// different rate (and cannot adapt), audio is converted to the training rate and back.
class CaptureSlot
{
public:
    explicit CaptureSlot (std::unique_ptr<CaptureModel> m) : model (std::move (m)) {}

    void prepare (double deviceRate, int maxBlock)
    {
        if (model == nullptr)
            return;

        const auto& info = model->info();
        const double trained = info.expectedSampleRate;
        resampling = trained > 0.0 && ! info.supportsAnySampleRate && std::abs (trained - deviceRate) > 0.5;

        if (! resampling)
        {
            model->prepare (deviceRate, maxBlock);
            return;
        }

        up.prepare (deviceRate, trained, maxBlock);
        const int maxModelBlock = up.maxOutputFor (maxBlock);
        down.prepare (trained, deviceRate, maxModelBlock);
        model->prepare (trained, maxModelBlock);

        modelBuffer.assign ((size_t) maxModelBlock, 0.0f);
        downBuffer.assign ((size_t) down.maxOutputFor (maxModelBlock), 0.0f);

        // Output FIFO primed with silence so it never runs dry (fixed, small latency).
        latency = 2 * StreamResampler::halfTaps + 8;
        fifo.assign ((size_t) (latency + 4 * maxBlock) + downBuffer.size(), 0.0f);
        fifoRead = 0;
        fifoCount = latency;
    }

    void process (float* data, int numSamples) noexcept
    {
        if (model == nullptr)
            return;

        if (! resampling)
        {
            model->process (data, numSamples);
            return;
        }

        const int n = up.process (data, numSamples, modelBuffer.data(), (int) modelBuffer.size());
        model->process (modelBuffer.data(), n);
        const int m = down.process (modelBuffer.data(), n, downBuffer.data(), (int) downBuffer.size());

        const int size = (int) fifo.size();
        for (int i = 0; i < m && fifoCount < size; ++i)
        {
            fifo[(size_t) ((fifoRead + fifoCount) % size)] = downBuffer[(size_t) i];
            ++fifoCount;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            if (fifoCount > 0)
            {
                data[i] = fifo[(size_t) fifoRead];
                fifoRead = (fifoRead + 1) % size;
                --fifoCount;
            }
            else
            {
                data[i] = 0.0f;
            }
        }
    }

    CaptureModel* get() const noexcept { return model.get(); }
    bool isResampling() const noexcept { return resampling; }
    int latencySamples() const noexcept { return resampling ? latency : 0; }

private:
    std::unique_ptr<CaptureModel> model;
    bool resampling = false;
    StreamResampler up, down;
    std::vector<float> modelBuffer, downBuffer, fifo;
    int fifoRead = 0, fifoCount = 0, latency = 0;
};

} // namespace gsp
