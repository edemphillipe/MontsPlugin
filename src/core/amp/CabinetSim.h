#pragma once

// Filter-based speaker cabinet + microphone simulation (used when no IR file is loaded).
//
// Each cabinet is modelled by its low-frequency resonance (closed or open back), the box
// colouration, the cone break-up peaks and the steep top-end roll-off of a guitar speaker.
// The microphone adds its own curve (dynamic, ribbon, condenser); POSITION moves it from the
// centre of the cone (brighter) to the edge (darker), DISTANCE reduces the proximity bass and
// adds the first floor reflection (a short comb filter), as a real close/far mic does.
//
// This is a parametric model, not a measured IR: for a specific real cabinet, load an IR.

#include "dsp/Biquad.h"

#include <array>
#include <atomic>
#include <vector>

namespace gsp::amp
{

struct CabinetType
{
    const char* id;
    const char* name;
    float resonanceHz, resonanceQ, resonanceDb; // low-end bump (closed back is tighter and bigger)
    float lowCutHz;                             // below the resonance the speaker falls off
    float boxHz, boxDb;                         // low-mid box colouration
    float breakupHz, breakupDb;                 // cone break-up peak
    float breakup2Hz, breakup2Db;
    float topCutHz;                             // steep roll-off
    float trimDb;                               // level matching between cabinets
};

struct MicType
{
    const char* id;
    const char* name;
    float presenceHz, presenceDb;
    float highShelfHz, highShelfDb;
    float proximityDb;                          // bass boost when close (directional mics)
    float topCutHz;
};

int numCabinets() noexcept;
const CabinetType& cabinet (int index) noexcept;
int numMics() noexcept;
const MicType& mic (int index) noexcept;
int cabinetIndexForId (const char* id) noexcept;
int micIndexForId (const char* id) noexcept;

class CabinetSim
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* x, int numSamples) noexcept; // uses cabinetIndex / micIndex
    void process (float* x, int numSamples, int cabinet, int mic) noexcept;

    std::atomic<int> cabinetIndex { 2 }, micIndex { 0 };
    std::atomic<float> position { 0.3f }, distance { 0.1f }; // 0..1

private:
    void update (bool force, int cabinet, int mic) noexcept;

    double fs = 48000.0;
    static constexpr int numFilters = 12;
    std::array<Biquad, numFilters> filters;
    std::vector<float> reflection;
    int writePos = 0, reflectionDelay = 1;
    float reflectionGain = 0.0f, outputGain = 1.0f;
    int lastCab = -1, lastMic = -1;
    float lastPosition = -1.0f, lastDistance = -1.0f;
};

} // namespace gsp::amp
