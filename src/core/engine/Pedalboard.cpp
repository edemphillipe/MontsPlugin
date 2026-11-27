#include "engine/Pedalboard.h"

#include "effects/Delay.h"
#include "effects/DualDelay.h"
#include "effects/Drives.h"
#include "effects/Modulation.h"
#include "effects/Reverb.h"

#include <set>

namespace gsp
{

Pedalboard::Pedalboard()
{
    pedals.push_back (std::make_unique<fx::Compressor>());
    pedals.push_back (std::make_unique<fx::Boost>());
    pedals.push_back (std::make_unique<fx::Overdrive>());
    pedals.push_back (std::make_unique<fx::Distortion>());
    pedals.push_back (std::make_unique<fx::Fuzz>());
    pedals.push_back (std::make_unique<fx::Chorus>());
    pedals.push_back (std::make_unique<fx::Flanger>());
    pedals.push_back (std::make_unique<fx::Phaser>());
    pedals.push_back (std::make_unique<fx::Tremolo>());
    pedals.push_back (std::make_unique<fx::Delay>());
    pedals.push_back (std::make_unique<fx::DualDelay>());
    pedals.push_back (std::make_unique<fx::Reverb>());

    for (auto& p : pedals)
    {
        p->setTempoClock (&clock);
        p->enabled = false; // the board starts with everything off
    }

    // Default order: pre-amp pedals, AMP, post-amp pedals.
    std::vector<int> order;
    for (int i = 0; i < (int) pedals.size(); ++i)
        if (pedals[(size_t) i]->getPlacement() == fx::Effect::Placement::PreAmp)
            order.push_back (i);
    order.push_back (ampSlot);
    for (int i = 0; i < (int) pedals.size(); ++i)
        if (pedals[(size_t) i]->getPlacement() == fx::Effect::Placement::PostAmp)
            order.push_back (i);

    defaults = order;
    sharedOrder = order;
    activeOrder.reserve (pedals.size() + 1);
    activeOrder = order;
}

int Pedalboard::indexOfType (const std::string& typeId) const noexcept
{
    for (size_t i = 0; i < pedals.size(); ++i)
        if (pedals[i]->getTypeId() == typeId)
            return (int) i;
    return -1;
}

bool Pedalboard::isValidOrder (const std::vector<int>& order, int numPedals)
{
    if ((int) order.size() != numPedals + 1)
        return false;
    std::set<int> seen (order.begin(), order.end());
    if ((int) seen.size() != numPedals + 1 || ! seen.count (ampSlot))
        return false;
    for (int v : order)
        if (v != ampSlot && (v < 0 || v >= numPedals))
            return false;
    return true;
}

std::vector<int> Pedalboard::getOrder() const
{
    const juce::SpinLock::ScopedLockType lock (orderLock);
    return sharedOrder;
}

void Pedalboard::setOrder (const std::vector<int>& newOrder)
{
    if (! isValidOrder (newOrder, size()))
        return;
    const juce::SpinLock::ScopedLockType lock (orderLock);
    sharedOrder = newOrder;
    orderChanged = true;
}

void Pedalboard::movePedal (int pedalIndex, int newPosition)
{
    auto order = getOrder();
    const auto it = std::find (order.begin(), order.end(), pedalIndex);
    if (it == order.end())
        return;
    order.erase (it);
    newPosition = juce::jlimit (0, (int) order.size(), newPosition);
    order.insert (order.begin() + newPosition, pedalIndex);
    setOrder (order);
}

void Pedalboard::prepare (double sampleRate, int maxBlock)
{
    for (auto& p : pedals)
        p->prepare (sampleRate, maxBlock);
}

juce::var Pedalboard::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("bpm", clock.bpm.load());

    juce::Array<juce::var> order;
    for (int slot : getOrder())
        order.add (slot == ampSlot ? juce::var ("amp") : juce::var (juce::String (pedals[(size_t) slot]->getTypeId())));
    root->setProperty ("order", order);

    auto* pedalsObj = new juce::DynamicObject();
    for (auto& p : pedals)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("enabled", p->enabled.load());
        auto* params = new juce::DynamicObject();
        for (int i = 0; i < p->numParams(); ++i)
            params->setProperty (juce::Identifier (p->getParams()[(size_t) i].id), p->get (i));
        po->setProperty ("params", juce::var (params));
        pedalsObj->setProperty (juce::Identifier (p->getTypeId()), juce::var (po));
    }
    root->setProperty ("pedals", juce::var (pedalsObj));
    return juce::var (root);
}

void Pedalboard::fromVar (const juce::var& v)
{
    if (! v.isObject())
        return;

    if (v.hasProperty ("bpm"))
        clock.bpm = juce::jlimit (30.0, 300.0, (double) v["bpm"]);

    if (auto* pedalsObj = v["pedals"].getDynamicObject())
    {
        for (auto& p : pedals)
        {
            const auto pv = pedalsObj->getProperty (juce::Identifier (p->getTypeId()));
            if (! pv.isObject())
                continue;
            p->enabled = (bool) pv["enabled"];
            if (auto* params = pv["params"].getDynamicObject())
                for (int i = 0; i < p->numParams(); ++i)
                {
                    const juce::Identifier id (p->getParams()[(size_t) i].id);
                    if (params->hasProperty (id))
                        p->set (i, (float) params->getProperty (id));
                }
        }
    }

    if (auto* arr = v["order"].getArray())
    {
        std::vector<int> order;
        for (const auto& item : *arr)
        {
            const auto s = item.toString();
            const int slot = s == "amp" ? ampSlot : indexOfType (s.toStdString());
            if (slot != -1 || s == "amp")
                order.push_back (slot);
        }
        // pedals added in a newer version than the saved preset go in their default place:
        // right after the pedal that precedes them in the default order
        for (size_t d = 0; d < defaults.size(); ++d)
            if (std::find (order.begin(), order.end(), defaults[d]) == order.end())
            {
                auto pos = order.begin();
                if (d > 0)
                {
                    const auto prev = std::find (order.begin(), order.end(), defaults[d - 1]);
                    if (prev != order.end())
                        pos = prev + 1;
                }
                order.insert (pos, defaults[d]);
            }
        setOrder (order); // ignored if it doesn't list every pedal exactly once
    }
}

} // namespace gsp
