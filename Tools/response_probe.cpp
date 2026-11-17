/* response_probe: measures the explicitly selected VST3 response.

   A tiny JUCE measurement host. One band is set (type slot, frequency, gain,
   reference Q, slope), an impulse is processed, and the response is read with a direct DFT at
   200 log-spaced frequencies, printed as "frequency dB" pairs. This is the tool the fidelity
   docs (docs/FILTER_VALIDATION.md) were measured with.

   Usage:
     response_probe list                          print the plug-in's parameters with indices
     response_probe measure <out> <slot> <freq> <gain> <q> <slope>
                                          set band 1, render, write 200 rows "freq Hz, gain dB"
*/
#include <JuceHeader.h>
#include <cstdio>
#include <vector>

static juce::String pluginPath;

static std::unique_ptr<juce::AudioPluginInstance> loadPlugin (juce::AudioPluginFormatManager& fm, juce::String& error)
{
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile (types, juce::File (pluginPath).getFullPathName());
    if (types.isEmpty())
    {
        error = pluginPath + " VST3 not found or failed to describe";
        return {};
    }
    for (const auto* t : types)
        std::printf ("found: %s (%s)\n", t->name.toRawUTF8(), t->pluginFormatName.toRawUTF8());
    return fm.createPluginInstance (*types.getFirst(), 48000.0, 512, error);
}

/** Parameter index whose name equals a band token plus a key, or best substring match. */
static int findParam (const juce::Array<juce::AudioProcessorParameter*>& params,
                      const juce::StringArray& bandTokens, const juce::StringArray& keyTokens)
{
    // Exact names first: "Band1 Freq.", "Band1 Q", ...
    for (const auto& band : bandTokens)
        for (const auto& key : keyTokens)
            for (int i = 0; i < params.size(); ++i)
                if (params[(size_t) i]->getName (256) == band + key)
                    return i;
    // Fallback: substring score, longer keys weigh more.
    int best = -1;
    int bestScore = -1;
    for (int i = 0; i < params.size(); ++i)
    {
        const auto name = params[(size_t) i]->getName (256);
        int score = 0;
        for (const auto& t : bandTokens) if (name.containsIgnoreCase (t)) score += 2;
        for (const auto& t : keyTokens)  if (name.containsIgnoreCase (t)) score += 1;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    return best;
}

static void setNorm (juce::AudioProcessorParameter& param, float normalised)
{
    param.setValue (juce::jlimit (0.0f, 1.0f, normalised));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    juce::AudioPluginFormatManager formatManager;
    formatManager.addDefaultFormats();

    int argOffset = 1;
    for (int i = 1; i < argc - 1; ++i)
    {
        if (juce::String (argv[i]) == "--plugin")
        {
            pluginPath = argv[i + 1];
            argOffset = i + 2;
            break;
        }
    }

    if (pluginPath.isEmpty())
    {
        std::fprintf (stderr, "Pass --plugin <VST3 path> to select a measurement target.\n");
        return 1;
    }

    juce::String error;
    auto instance = loadPlugin (formatManager, error);
    if (instance == nullptr)
    {
        std::fprintf (stderr, "load failed: %s\n", error.toRawUTF8());
        return 1;
    }

    const auto params = instance->getParameters();
    std::printf ("parameters: %d, sample rate 48000\n", params.size());

    const juce::String mode = argc > argOffset ? argv[argOffset] : (argc > 1 && ! juce::String (argv[1]).startsWith ("--") ? argv[1] : "list");
    if (mode == "list")
    {
        for (int i = 0; i < params.size(); ++i)
            std::printf ("%3d  %-40s default %5.3f  text(0)=%s  text(1)=%s\n", i,
                         params[(size_t) i]->getName (256).toRawUTF8(),
                         params[(size_t) i]->getDefaultValue(),
                         params[(size_t) i]->getText (0.0f, 32).toRawUTF8(),
                         params[(size_t) i]->getText (1.0f, 32).toRawUTF8());
        return 0;
    }

    if (mode == "measure" && argc >= 8)
    {
        const juce::File outFile (argv[2]);
        const int slot = std::atoi (argv[3]);
        const float freq = (float) std::atof (argv[4]);
        const float gainDb = (float) std::atof (argv[5]);
        const float q = (float) std::atof (argv[6]);
        const float slope = (float) std::atof (argv[7]);

        // Disable every band except band 1, so only the one band under test sounds.
        for (int band = 2; band <= 32; ++band)
        {
            const auto bandToken = juce::StringArray { "Band" + juce::String (band) };
            const auto used = findParam (params, bandToken, { " Used" });
            const auto enabled = findParam (params, bandToken, { " Enabled" });
            if (used >= 0) setNorm (*params[(size_t) used], 0.0f);
            if (enabled >= 0) setNorm (*params[(size_t) enabled], 0.0f);
        }

        const auto band1 = juce::StringArray { "Band1" };
        const int used = findParam (params, band1, { " Used" });
        const int enabled = findParam (params, band1, { " Enabled" });
        const int type = findParam (params, band1, { " Type" });
        const int freqParam = findParam (params, band1, { " Freq." });
        const int gainParam = findParam (params, band1, { " Gain" });
        const int qParam = findParam (params, band1, { " Q" });
        const int slopeParam = findParam (params, band1, { " Slope" });
        const int bypass = findParam (params, {}, { "Bypass" });

        std::printf ("band1 used=%d enabled=%d type=%d freq=%d gain=%d q=%d slope=%d bypass=%d\n",
                     used, enabled, type, freqParam, gainParam, qParam, slopeParam, bypass);

        if (used >= 0) setNorm (*params[(size_t) used], 1.0f);
        if (enabled >= 0) setNorm (*params[(size_t) enabled], 1.0f);
        if (bypass >= 0) setNorm (*params[(size_t) bypass], 0.0f);
        if (type >= 0)     setNorm (*params[(size_t) type], (float) slot / 71.0f);       // reference has 72 type slots
        if (freqParam >= 0) setNorm (*params[(size_t) freqParam], (float) juce::jlimit (0.0, 1.0, std::log ((double) freq / 10.0) / std::log (3000.0)));
        if (gainParam >= 0) setNorm (*params[(size_t) gainParam], (gainDb + 30.0f) / 60.0f);
        if (qParam >= 0)     setNorm (*params[(size_t) qParam], (float) juce::jlimit (0.0, 1.0, 0.5 + std::log (q) / (2.0 * std::log (40.0))));
        if (slopeParam >= 0) setNorm (*params[(size_t) slopeParam], slope);
        std::printf ("after set: type='%s' freq='%s' gain='%s' q='%s' slope='%s'\n",
                     type >= 0 ? params[(size_t) type]->getText (params[(size_t) type]->getValue(), 64).toRawUTF8() : "?",
                     freqParam >= 0 ? params[(size_t) freqParam]->getText (params[(size_t) freqParam]->getValue(), 64).toRawUTF8() : "?",
                     gainParam >= 0 ? params[(size_t) gainParam]->getText (params[(size_t) gainParam]->getValue(), 64).toRawUTF8() : "?",
                     qParam >= 0 ? params[(size_t) qParam]->getText (params[(size_t) qParam]->getValue(), 64).toRawUTF8() : "?",
                     slopeParam >= 0 ? params[(size_t) slopeParam]->getText (params[(size_t) slopeParam]->getValue(), 64).toRawUTF8() : "?");

        instance->prepareToPlay (48000.0, 512);

        const int total = 1 << 18;
        juce::AudioBuffer<float> buffer (instance->getTotalNumInputChannels(), total);
        buffer.clear();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, 0, 1.0f);

        juce::MidiBuffer midi;
        const int block = 512;
        for (int offset = 0; offset < total; offset += block)
        {
            juce::AudioBuffer<float> slice (buffer.getNumChannels(), block);
            slice.clear();
            if (offset == 0)
                for (int channel = 0; channel < slice.getNumChannels(); ++channel)
                    slice.setSample (channel, 0, 1.0f);
            instance->processBlock (slice, midi);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.copyFrom (channel, offset, slice, channel, 0, block);
        }

        outFile.deleteFile(); // FileOutputStream appends when the file exists; start fresh.
        juce::FileOutputStream out (outFile);
        if (out.failedToOpen())
        {
            std::fprintf (stderr, "cannot open %s\n", outFile.getFullPathName().toRawUTF8());
            return 1;
        }
        out.writeText (juce::String ("freqHz\tgainDb\n"), false, false, nullptr);

        const float* left = buffer.getReadPointer (0);
        double maxDb = -400.0;
        for (int i = 0; i < 200; ++i)
        {
            const double f = 20.0 * std::pow (10.0, i * 3.0 / 199.0); // 20 Hz .. 20 kHz
            double re = 0.0, im = 0.0;
            for (int n = 0; n < total; ++n)
            {
                const double phase = juce::MathConstants<double>::twoPi * f * (double) n / 48000.0;
                re += (double) left[n] * std::cos (phase);
                im -= (double) left[n] * std::sin (phase);
            }
            const double mag = std::sqrt (re * re + im * im);
            const double db = juce::Decibels::gainToDecibels (mag, -300.0);
            maxDb = juce::jmax (maxDb, db);
            out.writeText (juce::String (f, 6) + "\t" + juce::String (db, 6) + "\n", false, false, nullptr);
        }
        std::printf ("wrote %s (peak %.2f dB)\n", outFile.getFullPathName().toRawUTF8(), maxDb);
        return 0;
    }

    std::fprintf (stderr, "usage: response_probe list | response_probe measure <out> <slot> <freq> <gain> <q> <slope>\n");
    return 1;
}
