#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

/** In-process list of OpenEQ instances, their processor pointers, their names and their latest spectrum
    summaries. Hosts that isolate plugins in another process cannot see this registry. Nothing here is for
    the audio thread: every call takes a mutex. Processors register in their constructor and remove
    themselves in the destructor (both on the message thread); entries carry a non-owning pointer that
    consumers must re-check against the registry on the message thread before use. Ids rise monotonically
    and are never reused, so an id names one instance for the life of the process even when a later
    instance gets the same address.

    Names: each entry has a fallback ("Instance <id>"), the track name the host reported, if any, and a name
    the user typed, if any. The display name is the typed name, else the track name, else the fallback. When
    a name is already taken by an earlier entry (registration order), " (2)", " (3)" and so on is tried until
    the name is unused, so every row is different. */
class InstanceRegistry
{
public:
    struct Summary
    {
        int id = 0;
        juce::String name;          // display name, filled in by snapshot()
        juce::String fallbackName;  // "Instance <id>", set at registration
        juce::String trackName;     // host track name, empty when the host gave none
        juce::String userName;      // typed by the user, saved with the session; empty when none
        bool hasTrackColour = false;
        juce::uint32 trackColour = 0; // ARGB, valid when hasTrackColour
        float peakHz = 0.0f;
        float peakDb = -100.0f;
        void* processor = nullptr;
    };

    static InstanceRegistry& get()
    {
        static InstanceRegistry registry;
        return registry;
    }

    /** Registers an instance. An empty fallback becomes "Instance <id>", which is unique for the process. */
    int add (const juce::String& fallbackName = {}, void* processorToRegister = nullptr)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        const int id = ++nextId;
        Summary entry;
        entry.id = id;
        entry.fallbackName = fallbackName.isNotEmpty() ? fallbackName : "Instance " + juce::String (id);
        entry.processor = processorToRegister;
        entries.push_back (entry);
        bumpGeneration();
        return id;
    }

    void remove (int id)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        entries.erase (std::remove_if (entries.begin(), entries.end(),
                                       [id] (const Summary& entry) { return entry.id == id; }),
                       entries.end());
        bumpGeneration();
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

    /** Sets the host track name. An empty name returns the entry to its fallback. */
    void setName (int id, const juce::String& trackName)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (auto& entry : entries)
            if (entry.id == id && entry.trackName != trackName)
            {
                entry.trackName = trackName;
                bumpGeneration();
            }
    }

    /** Sets the name the user typed. Empty returns to the track name or the fallback. */
    void setUserName (int id, const juce::String& userName)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (auto& entry : entries)
            if (entry.id == id && entry.userName != userName)
            {
                entry.userName = userName;
                bumpGeneration();
            }
    }

    /** Sets the host track name and colour together. */
    void setTrackProperties (int id, const juce::String& trackName, bool hasColour, juce::uint32 colourArgb)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (auto& entry : entries)
        {
            if (entry.id != id)
                continue;
            const juce::uint32 colour = hasColour ? colourArgb : 0;
            if (entry.trackName != trackName || entry.hasTrackColour != hasColour || entry.trackColour != colour)
            {
                entry.trackName = trackName;
                entry.hasTrackColour = hasColour;
                entry.trackColour = colour;
                bumpGeneration();
            }
        }
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

    /** True while the instance that registered as `id` at `processor` is still alive. A new instance
        created at a recycled address has a different id, so it never passes for the old one. */
    bool isRegistered (void* processorToCheck, int idToCheck) const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (const auto& entry : entries)
            if (entry.id == idToCheck && entry.processor == processorToCheck)
                return true;
        return false;
    }

    /** The processor registered under `id`, or nullptr once it is gone. Message thread: instances are
        destroyed there, so the pointer stays valid until the caller returns to the message loop. */
    void* processorFor (int id) const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        for (const auto& entry : entries)
            if (entry.id == id)
                return entry.processor;
        return nullptr;
    }

    /** Display name for `id` (track name, fallback, duplicate suffix), or an empty string when unknown. */
    juce::String displayName (int id) const
    {
        for (const auto& entry : snapshot())
            if (entry.id == id)
                return entry.name;
        return {};
    }

    /** Every entry, in registration order, with `name` set to the display name. */
    std::vector<Summary> snapshot() const
    {
        std::vector<Summary> copy;
        {
            const std::lock_guard<std::mutex> lock (mutex);
            copy = entries;
        }
        assignDisplayNames (copy);
        return copy;
    }

    /** Changes whenever an instance is added, removed or renamed. Editors poll it to refresh labels. */
    juce::uint32 getGeneration() const noexcept { return generation.load (std::memory_order_acquire); }

    /** The naming rule on its own, so it can be checked without processors. */
    static void assignDisplayNames (std::vector<Summary>& list)
    {
        juce::StringArray used;
        for (auto& entry : list)
        {
            const auto base = baseName (entry);
            auto candidate = base;
            for (int k = 2; used.contains (candidate); ++k)
                candidate = base + " (" + juce::String (k) + ")";
            used.add (candidate);
            entry.name = candidate;
        }
    }

private:
    InstanceRegistry() = default;

    static juce::String baseName (const Summary& entry)
    {
        if (const auto typed = entry.userName.trim(); typed.isNotEmpty())
            return typed;
        const auto track = entry.trackName.trim();
        return track.isNotEmpty() ? track : entry.fallbackName;
    }

    void bumpGeneration() noexcept { generation.fetch_add (1, std::memory_order_acq_rel); }

    mutable std::mutex mutex;
    std::vector<Summary> entries;
    int nextId = 0;
    std::atomic<juce::uint32> generation { 0 };
};
