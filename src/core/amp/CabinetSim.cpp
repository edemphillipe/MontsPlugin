#include "amp/CabinetSim.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gsp::amp
{
namespace
{
const CabinetType cabinets[] = {
    // id, name, resHz, resQ, resDb, lowCut, boxHz, boxDb, bu1Hz, bu1Db, bu2Hz, bu2Db, topCut, trimDb
    { "1x12_open", "1x12 ABERTO", 85.0f, 0.7f, 2.0f, 70.0f, 400.0f, 1.5f, 2200.0f, 3.0f, 4200.0f, 2.0f, 6000.0f, -2.7f },
    { "2x12_open", "2x12 ABERTO", 90.0f, 0.9f, 3.0f, 65.0f, 300.0f, 2.0f, 2000.0f, 3.5f, 3800.0f, 2.0f, 5800.0f, -3.6f },
    { "4x12_vintage", "4x12 FECHADO VINTAGE", 100.0f, 1.4f, 5.0f, 75.0f, 250.0f, 2.0f, 2600.0f, 5.0f, 4400.0f, 3.0f, 5200.0f, -4.5f },
    { "4x12_green", "4x12 FECHADO GREEN", 95.0f, 1.2f, 4.5f, 70.0f, 220.0f, 3.0f, 1900.0f, 4.0f, 3500.0f, 1.5f, 4800.0f, -5.0f },
    { "2x12_closed", "2x12 FECHADO", 110.0f, 1.1f, 4.0f, 80.0f, 280.0f, 1.5f, 2400.0f, 4.0f, 4000.0f, 2.5f, 5500.0f, -3.9f },
};

const MicType mics[] = {
    // id, name, presenceHz, presenceDb, shelfHz, shelfDb, proximityDb, topCut
    { "dynamic", "DIN\xc3\x82MICO", 5000.0f, 4.0f, 9000.0f, -2.0f, 4.0f, 14000.0f },
    { "ribbon", "FITA", 2500.0f, 0.0f, 3500.0f, -6.0f, 6.0f, 11000.0f },
    { "condenser", "CONDENSADOR", 8000.0f, 1.5f, 10000.0f, 2.0f, 2.0f, 18000.0f },
};
} // namespace

int numCabinets() noexcept { return (int) (sizeof (cabinets) / sizeof (cabinets[0])); }
const CabinetType& cabinet (int index) noexcept { return cabinets[std::clamp (index, 0, numCabinets() - 1)]; }
int numMics() noexcept { return (int) (sizeof (mics) / sizeof (mics[0])); }
const MicType& mic (int index) noexcept { return mics[std::clamp (index, 0, numMics() - 1)]; }
int cabinetIndexForId (const char* id) noexcept
{
    for (int i = 0; i < numCabinets(); ++i)
        if (std::strcmp (cabinets[i].id, id) == 0)
            return i;
    return -1;
}
int micIndexForId (const char* id) noexcept
{
    for (int i = 0; i < numMics(); ++i)
        if (std::strcmp (mics[i].id, id) == 0)
            return i;
    return -1;
}

void CabinetSim::prepare (double sampleRate)
{
    fs = sampleRate;
    reflection.assign ((size_t) std::ceil (fs * 0.005) + 2, 0.0f);
    lastCab = -1;
    reset();
    update (true, cabinetIndex.load(), micIndex.load());
}

void CabinetSim::reset()
{
    for (auto& f : filters)
        f.reset();
    std::fill (reflection.begin(), reflection.end(), 0.0f);
    writePos = 0;
}

void CabinetSim::update (bool force, int cabinetIn, int micIn) noexcept
{
    const int ci = std::clamp (cabinetIn, 0, numCabinets() - 1);
    const int mi = std::clamp (micIn, 0, numMics() - 1);
    const float pos = std::clamp (position.load (std::memory_order_relaxed), 0.0f, 1.0f);
    const float dist = std::clamp (distance.load (std::memory_order_relaxed), 0.0f, 1.0f);
    if (! force && ci == lastCab && mi == lastMic && std::abs (pos - lastPosition) < 1e-6f && std::abs (dist - lastDistance) < 1e-6f)
        return;
    if (ci != lastCab || mi != lastMic)
        reset();
    lastCab = ci; lastMic = mi; lastPosition = pos; lastDistance = dist;

    const auto& c = cabinets[ci];
    const auto& m = mics[mi];
    using T = Biquad::Type;

    // centre of the cone = brightest; towards the edge the top end and the break-up fade
    const float edgeDarken = 1.0f - 0.55f * pos;
    const float top = c.topCutHz * edgeDarken;

    filters[0].setup (T::HighPass, fs, c.lowCutHz, 0.8, 0.0);
    filters[1].setup (T::Peak, fs, c.resonanceHz, c.resonanceQ, c.resonanceDb);
    filters[2].setup (T::Peak, fs, c.boxHz, 1.0, c.boxDb);
    filters[3].setup (T::Peak, fs, c.breakupHz * (1.0f - 0.15f * pos), 2.0, c.breakupDb * (1.0f - 0.5f * pos));
    filters[4].setup (T::Peak, fs, c.breakup2Hz, 3.0, c.breakup2Db * (1.0f - 0.8f * pos));
    filters[5].setup (T::LowPass, fs, top, 0.9, 0.0);
    filters[6].setup (T::LowPass, fs, top * 1.15f, 0.6, 0.0);
    filters[7].setup (T::LowPass, fs, top * 1.6f, 0.55, 0.0);
    // microphone
    filters[8].setup (T::Peak, fs, m.presenceHz, 1.2, m.presenceDb * (1.0f - 0.6f * pos));
    filters[9].setup (T::HighShelf, fs, m.highShelfHz, 0.7, m.highShelfDb);
    filters[10].setup (T::LowShelf, fs, 150.0, 0.7, m.proximityDb * (1.0f - dist) - 2.0f * dist);
    filters[11].setup (T::LowPass, fs, m.topCutHz * (1.0f - 0.3f * dist), 0.707, 0.0);

    // first floor reflection: 0.3 ms (close) .. 4 ms (far), stronger when far
    reflectionDelay = std::max (1, (int) std::lround (fs * (0.0003 + 0.0037 * dist)));
    reflectionDelay = std::min (reflectionDelay, (int) reflection.size() - 1);
    reflectionGain = 0.45f * dist;
    // keep the loudness about the same for every cabinet and mic distance
    outputGain = std::pow (10.0f, (c.trimDb - 3.0f * dist) / 20.0f) / (1.0f + reflectionGain);
}

void CabinetSim::process (float* x, int numSamples) noexcept
{
    process (x, numSamples, cabinetIndex.load (std::memory_order_relaxed), micIndex.load (std::memory_order_relaxed));
}

void CabinetSim::process (float* x, int numSamples, int cabinetIn, int micIn) noexcept
{
    update (false, cabinetIn, micIn);
    const int size = (int) reflection.size();
    for (int i = 0; i < numSamples; ++i)
    {
        float s = x[i];
        for (auto& f : filters)
            s = f.process (s);

        reflection[(size_t) writePos] = s;
        int readPos = writePos - reflectionDelay;
        if (readPos < 0)
            readPos += size;
        const float y = s + reflectionGain * reflection[(size_t) readPos];
        writePos = (writePos + 1) % size;
        x[i] = y * outputGain;
    }
}

} // namespace gsp::amp
