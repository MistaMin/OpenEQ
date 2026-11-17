#include "PluginProcessor.h"
#include <cmath>

// Instance link: the two-way parameter mirror between two OpenEQ instances in one process, the instance
// names the host reports, and the closing notice that editors use when they control another instance.
//
// Threads. Parameter listeners run on whatever thread changed the parameter, the audio thread included.
// On the message thread a relayed change is checked against the registry and written at once: instances are
// created and destroyed on the message thread, so the check and the write cannot overlap a destruction.
// On any other thread the relay only stores the value into a preallocated slot and raises a flag; a
// message-thread timer writes the marked slots. The audio path never locks, allocates, deletes, posts a
// message or dereferences the other instance. Link setup and teardown happen on the message thread only.

struct OpenEQAudioProcessor::RelayListener final : public juce::AudioProcessorValueTreeState::Listener
{
    OpenEQAudioProcessor* owner = nullptr;
    void* source = nullptr;
    void parameterChanged (const juce::String& parameterID, float newValue) override
    {
        if (owner != nullptr)
            owner->relayParameter (parameterID, newValue, source);
    }
};

struct OpenEQAudioProcessor::RelayPump final : public juce::Timer
{
    explicit RelayPump (OpenEQAudioProcessor& ownerToPump) : owner (ownerToPump) {}
    void timerCallback() override { owner.drainRelay(); }
    OpenEQAudioProcessor& owner;
};

namespace
{
    constexpr int relayPumpIntervalMs = 15;

    bool onMessageThread() noexcept
    {
        auto* manager = juce::MessageManager::getInstanceWithoutCreating();
        return manager != nullptr && manager->isThisTheMessageThread();
    }
}

static void attachRelayListener (juce::AudioProcessorValueTreeState& state, OpenEQAudioProcessor::RelayListener& listener)
{
    for (int i = 0; i < state.state.getNumChildren(); ++i)
        state.addParameterListener (state.state.getChild (i).getProperty ("id").toString(), &listener);
}

static void detachRelayListener (juce::AudioProcessorValueTreeState& state, OpenEQAudioProcessor::RelayListener& listener)
{
    for (int i = 0; i < state.state.getNumChildren(); ++i)
        state.removeParameterListener (state.state.getChild (i).getProperty ("id").toString(), &listener);
}

void OpenEQAudioProcessor::initInstanceLink()
{
    relaySlotCount = getParameters().size();
    const auto slots = (size_t) relaySlotCount * 2;
    relayValues = std::make_unique<std::atomic<float>[]> (slots);
    relayFlags = std::make_unique<std::atomic<bool>[]> (slots);
    for (size_t i = 0; i < slots; ++i)
    {
        relayValues[i].store (0.0f, std::memory_order_relaxed);
        relayFlags[i].store (false, std::memory_order_relaxed);
    }
    ownRelay = new RelayListener;
    ownRelay->owner = this;
    ownRelay->source = this;
}

void OpenEQAudioProcessor::shutdownInstanceLink()
{
    // Some hosts destroy plug-ins on a worker thread. The editor and link work below touches components and
    // other instances, so it runs on the message thread or while this thread holds the message manager lock.
    std::unique_ptr<juce::MessageManagerLock> lock;
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    if (manager != nullptr && ! manager->currentThreadHasLockedMessageManager())
        lock = std::make_unique<juce::MessageManagerLock>();
    const bool locked = manager == nullptr || manager->currentThreadHasLockedMessageManager();
    jassert (locked);
    // Editors that control this instance from another window drop their views of it now, while its state is
    // still intact; they rebuild later, on the message thread.
    if (locked)
        closingListeners.call ([this] (ClosingListener& listener) { listener.instanceClosing (*this); });
    remoteTarget.store (nullptr, std::memory_order_release);
    remoteTargetId.store (0, std::memory_order_release);
    releaseLink();
    delete relayPump;
    relayPump = nullptr;
    InstanceRegistry::get().remove (instanceId);
    delete ownRelay;
    ownRelay = nullptr;
}

void OpenEQAudioProcessor::releaseLink()
{
    if (remoteRelay != nullptr)
    {
        // A destroyed target took its listener lists with it; touching its state would be a use after free.
        // The id check also rejects a new instance that was created at the destroyed target's address.
        // Removing a listener waits for a call in progress on another thread, so the delete below is safe.
        if (attachedTarget != nullptr && InstanceRegistry::get().isRegistered (attachedTarget, attachedTargetId))
            detachRelayListener (attachedTarget->apvts, *remoteRelay);
        delete remoteRelay;
        remoteRelay = nullptr;
    }
    if (ownRelay != nullptr)
        detachRelayListener (apvts, *ownRelay);
    attachedTarget = nullptr;
    attachedTargetId = 0;
    if (relayPump != nullptr)
        relayPump->stopTimer();
}

void OpenEQAudioProcessor::setRemoteTarget (OpenEQAudioProcessor* target)
{
    jassert (onMessageThread() || juce::MessageManager::getInstanceWithoutCreating() == nullptr);
    if (target == this || (target != nullptr && ! InstanceRegistry::get().isRegistered (target)))
        target = nullptr;
    // Same address and same id: already linked. Same address with another id is a new instance.
    const int targetId = target != nullptr ? target->getInstanceId() : 0;
    if (target == attachedTarget && targetId == attachedTargetId
        && target == remoteTarget.load (std::memory_order_acquire) && targetId == remoteTargetId.load (std::memory_order_acquire))
        return;

    remoteTarget.store (nullptr, std::memory_order_release);
    remoteTargetId.store (0, std::memory_order_release);
    releaseLink();
    ++linkGeneration;
    for (int i = 0; i < relaySlotCount * 2; ++i)
        relayFlags[(size_t) i].store (false, std::memory_order_relaxed);
    relayPending.store (false, std::memory_order_release);

    if (target == nullptr)
        return;

    attachedTarget = target;
    attachedTargetId = targetId;
    remoteTargetId.store (targetId, std::memory_order_release);
    remoteTarget.store (target, std::memory_order_release);
    attachRelayListener (apvts, *ownRelay);
    remoteRelay = new RelayListener;
    remoteRelay->owner = this;
    remoteRelay->source = target;
    attachRelayListener (target->apvts, *remoteRelay);
    if (relayPump == nullptr)
        relayPump = new RelayPump (*this);
    relayPump->startTimer (relayPumpIntervalMs);
}

void OpenEQAudioProcessor::relayParameter (const juce::String& parameterID, float rawValue, void* source)
{
    if (remoteTarget.load (std::memory_order_acquire) == nullptr)
        return;
    const bool fromSelf = (source == this);

    if (! onMessageThread())
    {
        // Audio thread or another worker: mark the slot. The parameter lookup is a map search on this
        // instance's own state and does not allocate.
        if (auto* parameter = apvts.getParameter (parameterID))
        {
            const int index = parameter->getParameterIndex();
            if (index >= 0 && index < relaySlotCount)
            {
                const auto slot = (size_t) ((fromSelf ? 0 : relaySlotCount) + index);
                relayValues[slot].store (rawValue, std::memory_order_relaxed);
                relayFlags[slot].store (true, std::memory_order_release);
                relayPending.store (true, std::memory_order_release);
            }
        }
        return;
    }

    auto* target = remoteTarget.load (std::memory_order_acquire);
    const int targetId = remoteTargetId.load (std::memory_order_acquire);
    if (target == nullptr)
        return;
    if (! InstanceRegistry::get().isRegistered (target, targetId))
    {
        // The target was destroyed (a new instance at the same address has another id): stop relaying now
        // and detach on a later message-loop turn, outside the listener call that brought us here.
        remoteTarget.store (nullptr, std::memory_order_release);
        remoteTargetId.store (0, std::memory_order_release);
        const int generation = linkGeneration;
        const int selfId = instanceId;
        juce::MessageManager::callAsync ([self = this, selfId, generation]
        {
            if (! InstanceRegistry::get().isRegistered (self, selfId))
                return; // this instance has gone as well
            if (self->linkGeneration == generation)
                self->releaseLink();
        });
        return;
    }

    writeRelayed (fromSelf ? *target : *this, parameterID, rawValue);
}

void OpenEQAudioProcessor::writeRelayed (OpenEQAudioProcessor& destination, const juce::String& parameterID, float rawValue)
{
    if (auto* parameter = destination.apvts.getParameter (parameterID))
    {
        const float normalised = parameter->convertTo0to1 (rawValue);
        if (std::abs (normalised - parameter->getValue()) < 1.0e-9f)
            return; // echo of the value we just wrote
        parameter->setValueNotifyingHost (normalised);
    }
}

void OpenEQAudioProcessor::drainRelay()
{
    auto* target = remoteTarget.load (std::memory_order_acquire);
    const int targetId = remoteTargetId.load (std::memory_order_acquire);
    if (target == nullptr || ! InstanceRegistry::get().isRegistered (target, targetId))
    {
        remoteTarget.store (nullptr, std::memory_order_release);
        remoteTargetId.store (0, std::memory_order_release);
        releaseLink(); // also stops this timer
        return;
    }
    if (! relayPending.exchange (false, std::memory_order_acq_rel))
        return;

    const auto& parameters = getParameters();
    for (int direction = 0; direction < 2; ++direction)
    {
        for (int index = 0; index < relaySlotCount && index < parameters.size(); ++index)
        {
            const auto slot = (size_t) (direction * relaySlotCount + index);
            if (! relayFlags[slot].exchange (false, std::memory_order_acq_rel))
                continue;
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameters[index]))
                writeRelayed (direction == 0 ? *target : *this, ranged->getParameterID(),
                              relayValues[slot].load (std::memory_order_relaxed));
            // A write can reach a listener that drops the link; stop if it did.
            if (remoteTarget.load (std::memory_order_acquire) != target)
                return;
        }
    }
}

void OpenEQAudioProcessor::setUserInstanceName (const juce::String& name)
{
    const auto trimmed = name.trim().substring (0, 64);
    {
        const juce::ScopedLock lock (userNameLock);
        userInstanceName = trimmed;
    }
    if (instanceId != 0)
        InstanceRegistry::get().setUserName (instanceId, trimmed);
}

juce::String OpenEQAudioProcessor::getUserInstanceName() const
{
    const juce::ScopedLock lock (userNameLock);
    return userInstanceName;
}

juce::String OpenEQAudioProcessor::getInstanceName() const
{
    const auto name = InstanceRegistry::get().displayName (instanceId);
    return name.isNotEmpty() ? name : juce::String ("OpenEQ");
}

void OpenEQAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    const auto name = properties.name.has_value() ? properties.name->trim() : juce::String();
    const bool hasColour = properties.colour.has_value();
    InstanceRegistry::get().setTrackProperties (instanceId, name, hasColour, hasColour ? properties.colour->getARGB() : 0u);
}
