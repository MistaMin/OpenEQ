#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <mutex>
#include <vector>

/** In-process list of OpenEQ instances, their processor pointers and their latest spectrum summaries.
    Hosts that isolate plugins in another process cannot see this registry. Publishing is for the
    editor timer, not the audio thread. Processors register in their constructor and remove
    themselves in the destructor; entries carry a non-owning pointer that consumers must re-check
    against the registry snapshot before use. */
class InstanceRegistry
{
public:
    struct Summary
    {
        int id = 0;
        juce::String name;
        float peakHz = 0.0f;
        float peakDb = -100.0f;
        void* processor = nullptr;
    };

    static InstanceRegistry& get()
    {
        static InstanceRegistry registry;
        return registry;
    }

    int add (const juce::String& name, void* processorToRegister = nullptr)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        const int id = ++nextId;
        entries.push_back ({ id, name, 0.0f, -100.0f, processorToRegister });
        return id;
    }

    void remove (int id)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        entries.erase (std::remove_if (entries.begin(), entries.end(),
                                       [id] (const Summary& entry) { return entry.id == id; }),
                       entries.end());
    }

    void publish (int id, float peakHz, float peakDb)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (auto& entry : entries)
        {
            if (entry.id == id)
            {
                entry.peakHz = peakHz;
                entry.peakDb = peakDb;
                return;
            }
        }
    }

    void setName (int id, const juce::String& name)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (auto& entry : entries)
            if (entry.id == id)
                entry.name = name;
    }

    /** True while `processor` is still registered (the entry survives until its destructor removes it). */
    bool isRegistered (void* processorToCheck) const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (const auto& entry : entries)
            if (entry.processor == processorToCheck)
                return true;
        return false;
    }

    /** The registry entry for a processor, or nullptr when it is not registered. */
    const Summary* findByProcessor (void* processorToFind) const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (const auto& entry : entries)
            if (entry.processor == processorToFind)
                return &entry;
        return nullptr;
    }

    std::vector<Summary> snapshot() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return entries;
    }

private:
    InstanceRegistry() = default;
    mutable std::mutex mutex;
    std::vector<Summary> entries;
    int nextId = 0;
};
