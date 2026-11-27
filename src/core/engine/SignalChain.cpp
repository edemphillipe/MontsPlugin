#include "engine/SignalChain.h"

#include "captures/CaptureLoader.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace gsp
{

SignalChain::SignalChain() = default;

SignalChain::~SignalChain()
{
    delete current;
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
}

void SignalChain::prepare (double newSampleRate, int maxBlockSize)
{
    std::scoped_lock lock (prepareMutex);
    sampleRate = newSampleRate;
    maxBlock = juce::jmax (1, maxBlockSize);
    work.allocate ((size_t) maxBlock, true);
    workR.allocate ((size_t) maxBlock, true);
    pedalboard.prepare (sampleRate, maxBlock);

    gate.prepare (sampleRate);

    if (current != nullptr)
        current->prepare (sampleRate, maxBlock);
    if (auto* p = pending.load())
        p->prepare (sampleRate, maxBlock);

    cabinet.prepare ({ sampleRate, (juce::uint32) maxBlock, 1 });
    cabinet.reset();
    ampModel.prepare (sampleRate, maxBlock);
    cabinetSim.prepare (sampleRate);
    pathFade = PathFade();
    pathFade.step = (float) (1.0 / (0.01 * sampleRate)); // 10 ms out + 10 ms in when the amp/cabinet changes

    bass.reset(); middle.reset(); treble.reset();
    lastBass = lastMiddle = lastTreble = 1000.0f; // force coefficient update
    updateEq();

    inGain.reset (sampleRate, 0.02);
    outGain.reset (sampleRate, 0.02);
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainDb.load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb.load()));

    fadeStep = (float) (1.0 / (0.015 * sampleRate)); // 15 ms fades when swapping captures
}

void SignalChain::releaseResources() {}

void SignalChain::updateEq() noexcept
{
    const float b = bassDb.load(), m = middleDb.load(), t = trebleDb.load();
    if (! juce::exactlyEqual (b, lastBass))   { bass.setup (Biquad::Type::LowShelf, sampleRate, 120.0, 0.707, b); lastBass = b; }
    if (! juce::exactlyEqual (m, lastMiddle)) { middle.setup (Biquad::Type::Peak, sampleRate, 750.0, 0.8, m); lastMiddle = m; }
    if (! juce::exactlyEqual (t, lastTreble)) { treble.setup (Biquad::Type::HighShelf, sampleRate, 3500.0, 0.707, t); lastTreble = t; }
}

static float captureNormalisationGain (const CaptureSlot* slot)
{
    if (slot == nullptr || slot->get() == nullptr)
        return 1.0f;
    const auto& info = slot->get()->info();
    if (! info.hasLoudness)
        return 1.0f;
    // Same convention as the NAM plugin: bring every capture to -18 dB loudness.
    return juce::Decibels::decibelsToGain ((float) (-18.0 - info.loudnessDb));
}

void SignalChain::process (const float* monoIn, float* const* outputs, int numOutputs, int numSamples) noexcept
{
    if (numSamples > maxBlock)
    {
        // Should not happen (the device promised maxBlock); process in chunks.
        for (int start = 0; start < numSamples; start += maxBlock)
        {
            const int n = juce::jmin (maxBlock, numSamples - start);
            float* chunkOut[8] {};
            for (int c = 0; c < juce::jmin (numOutputs, 8); ++c)
                chunkOut[c] = outputs[c] + start;
            process (monoIn + start, chunkOut, juce::jmin (numOutputs, 8), n);
        }
        return;
    }

    float* x = work.get();
    juce::FloatVectorOperations::copy (x, monoIn, numSamples);

    // ---- input gain + meter
    inGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainDb.load (std::memory_order_relaxed)));
    inGain.applyGain (x, numSamples);
    {
        const auto r = juce::FloatVectorOperations::findMinAndMax (x, numSamples);
        const float peak = juce::jmax (std::abs (r.getStart()), std::abs (r.getEnd()));
        if (peak > inputPeak.load (std::memory_order_relaxed))
            inputPeak.store (peak, std::memory_order_relaxed);
    }

    tunerTap.push (x, numSamples);

    // ---- noise gate
    gate.setEnabled (gateEnabled.load (std::memory_order_relaxed));
    gate.setThresholdDb (gateThresholdDb.load (std::memory_order_relaxed));
    gate.process (x, numSamples);

    // ---- pedalboard (stereo) with the AMP block (capture + cabinet + EQ) where the user placed it
    float* right = workR.get();
    juce::FloatVectorOperations::copy (right, x, numSamples);
    pedalboard.process (x, right, numSamples, [this] (float* l, float* r, int n) noexcept
    {
        // the amp is mono: anything stereo before it is summed
        juce::FloatVectorOperations::add (l, r, n);
        juce::FloatVectorOperations::multiply (l, 0.5f, n);
        processAmp (l, n);
        juce::FloatVectorOperations::copy (r, l, n);
    });

    // ---- output gain + meter
    outGain.setTargetValue (muteOutput.load (std::memory_order_relaxed) ? 0.0f
                                                                     : juce::Decibels::decibelsToGain (outputGainDb.load (std::memory_order_relaxed)));
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outGain.getNextValue();
        x[i] *= g;
        right[i] *= g;
    }
    {
        const auto a = juce::FloatVectorOperations::findMinAndMax (x, numSamples);
        const auto b = juce::FloatVectorOperations::findMinAndMax (right, numSamples);
        const float peak = juce::jmax (juce::jmax (std::abs (a.getStart()), std::abs (a.getEnd())),
                                       juce::jmax (std::abs (b.getStart()), std::abs (b.getEnd())));
        if (peak > outputPeak.load (std::memory_order_relaxed))
            outputPeak.store (peak, std::memory_order_relaxed);
    }

    // L/R to the first two outputs; further outputs alternate L/R.
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::copy (outputs[c], (c % 2 == 0) ? x : right, numSamples);
}

namespace
{
// everything that changes the AMP block's signal path, packed into one key
constexpr int packPath (int source, int voicing, int cabinetFile, int cab, int mic) noexcept
{
    return source | (voicing << 2) | (cabinetFile << 7) | (cab << 8) | (mic << 13);
}
} // namespace

void SignalChain::processAmp (float* x, int numSamples) noexcept
{
    const int source = ampSource.load (std::memory_order_relaxed) == sourceAmpModel ? 1 : 0;
    const int cabinetFile = (cabinetMode.load (std::memory_order_relaxed) == cabinetImpulseResponse
                             && irLoaded.load (std::memory_order_acquire)) ? 1 : 0;
    const int desired = packPath (source, juce::jlimit (0, 31, ampModel.voicingIndex.load (std::memory_order_relaxed)), cabinetFile,
                                  juce::jlimit (0, 31, cabinetSim.cabinetIndex.load (std::memory_order_relaxed)),
                                  juce::jlimit (0, 31, cabinetSim.micIndex.load (std::memory_order_relaxed)));
    const int key = pathFade.update (desired);
    const int activeSource = key & 3, activeVoicing = (key >> 2) & 31, activeCabFile = (key >> 7) & 1,
              activeCab = (key >> 8) & 31, activeMic = (key >> 13) & 31;

    // ---- amp: capture or built-in model (the panel's on/off LED bypasses it)
    if (activeSource == 0)
        processCapture (x, numSamples);
    else if (captureEnabled.load (std::memory_order_relaxed))
        ampModel.process (x, numSamples, activeVoicing);

    // ---- cabinet: IR file or simulation
    if (irEnabled.load (std::memory_order_relaxed))
    {
        if (activeCabFile == 1)
        {
            float* channels[] = { x };
            juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
            cabinet.process (juce::dsp::ProcessContextReplacing<float> (block));
        }
        else
        {
            cabinetSim.process (x, numSamples, activeCab, activeMic);
        }
    }

    // ---- EQ (64-bit biquads)
    updateEq();
    for (int i = 0; i < numSamples; ++i)
        x[i] = treble.process (middle.process (bass.process (x[i])));

    pathFade.apply (x, numSamples);
    if (pathFade.swapDue())
    {
        const int previousSource = activeSource;
        pathFade.swap();
        if ((pathFade.active & 3) == 1 && previousSource != 1)
            ampModel.reset();
    }
}

void SignalChain::processCapture (float* x, int numSamples) noexcept
{
    // ---- capture handover: fade out, swap, fade in
    const bool swapRequested = pending.load (std::memory_order_acquire) != nullptr || pendingIsClear.load();
    if (fadeDirection == 0 && swapRequested && retired.load() == nullptr)
        fadeDirection = (current == nullptr) ? 2 : -1; // 2 = swap now (nothing to fade out)

    if (current != nullptr && captureEnabled.load (std::memory_order_relaxed))
    {
        if (auto* m = current->get())
        {
            m->setConditioning (0, conditioning1.load (std::memory_order_relaxed));
            m->setConditioning (1, conditioning2.load (std::memory_order_relaxed));
        }
        current->process (x, numSamples);
        juce::FloatVectorOperations::multiply (x, captureGain, numSamples);
    }

    if (fadeDirection != 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (fadeDirection == -1)
            {
                captureFade = juce::jmax (0.0f, captureFade - fadeStep);
                if (captureFade <= 0.0f)
                    fadeDirection = 2;
            }
            else if (fadeDirection == 1)
            {
                captureFade = juce::jmin (1.0f, captureFade + fadeStep);
                if (captureFade >= 1.0f)
                    fadeDirection = 0;
            }
            x[i] *= captureFade;
        }

        if (fadeDirection == 2)
        {
            pendingIsClear.store (false);
            auto* next = pending.exchange (nullptr, std::memory_order_acq_rel); // nullptr = clear
            retired.store (current, std::memory_order_release);
            current = next;
            captureGain = normaliseLoudness.load() ? captureNormalisationGain (current) : 1.0f;
            captureFade = 0.0f;
            fadeDirection = 1;
        }
    }
    else
    {
        captureGain = normaliseLoudness.load (std::memory_order_relaxed) ? captureNormalisationGain (current) : 1.0f;
    }
}

bool SignalChain::loadCapture (const juce::File& file, juce::String& error)
{
    std::string err;
    auto model = gsp::loadCapture (file.getFullPathName().toStdString(), err);
    if (model == nullptr)
    {
        error = juce::String (err);
        return false;
    }

    const CaptureInfo info = model->info(); // copy: the model is handed to the audio thread below
    juce::String description = (info.format == CaptureFormat::NAM ? "NAM" : "AIDA-X");
    description << " - " << juce::String (info.architecture);
    if (info.expectedSampleRate > 0)
        description << " - " << juce::String (info.expectedSampleRate / 1000.0, 1) << " kHz";

    auto slot = std::make_unique<CaptureSlot> (std::move (model));
    {
        std::scoped_lock lock (prepareMutex);
        slot->prepare (sampleRate, maxBlock);
        if (slot->isResampling())
            description << " (convertido para " << juce::String (sampleRate / 1000.0, 1) << " kHz)";

        collectGarbage();
        pendingIsClear.store (false);
        delete pending.exchange (slot.release()); // replace a not-yet-picked-up request
    }

    loadedCaptureName = juce::String (info.name);
    loadedConditioningInputs = info.numConditioningInputs;
    loadedCaptureDescription = description;
    return true;
}

void SignalChain::clearCapture()
{
    delete pending.exchange (nullptr);
    pendingIsClear.store (true);
    loadedCaptureName = {};
    loadedCaptureDescription = {};
    loadedConditioningInputs = 0;
}

bool SignalChain::loadImpulseResponse (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Arquivo de IR nao encontrado.";
        return false;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Nao foi possivel ler o IR (use WAV ou AIFF).";
        return false;
    }
    if (reader->lengthInSamples > (juce::int64) reader->sampleRate * 10)
    {
        error = "IR muito longo (maximo 10 segundos).";
        return false;
    }

    // Convolution resamples the IR to the device rate and swaps it in on a background thread.
    cabinet.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::yes, 0,
                                 juce::dsp::Convolution::Normalise::yes);
    irLoaded.store (true);
    irName = file.getFileNameWithoutExtension();
    return true;
}

void SignalChain::clearImpulseResponse()
{
    irLoaded.store (false);
    irName = {};
}

void SignalChain::collectGarbage()
{
    delete retired.exchange (nullptr, std::memory_order_acq_rel);
}

juce::String SignalChain::getCaptureName() const { return loadedCaptureName; }
juce::String SignalChain::getCaptureDescription() const { return loadedCaptureDescription; }

SignalChain::Levels SignalChain::readAndResetLevels() noexcept
{
    return { inputPeak.exchange (0.0f), outputPeak.exchange (0.0f) };
}

} // namespace gsp
