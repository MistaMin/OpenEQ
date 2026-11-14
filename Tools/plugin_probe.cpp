/* plugin_probe: a black-box measurement host for any VST3.

   It loads a plug-in, sets parameters by index through the public host interface, plays a WAV file through it
   and writes what comes out. Nothing about the plug-in's internals is read: only its parameter list and its
   audio output, the same things a DAW sees.

   Usage:
     plugin_probe --plugin <path.vst3> list [--json]
         Every parameter: index, name, label, default, step count, and its text at 0, 0.25, 0.5, 0.75 and 1.
     plugin_probe --plugin <path.vst3> render --in <in.wav> --out <out.wav> [--set i=v,i=v,...]
                  [--block 512] [--warmup 1.0] [--pump 0.5] [--sidechain <sc.wav>] [--raw]
         Sets the listed parameters (normalised 0..1), processes the file and writes the output. The output is
         shifted by the reported latency so it lines up with the input, unless --raw is given. --warmup plays
         the start of the file (looped) for that many seconds first so adaptive processing has settled.
         --init runs silence and the message loop for that many seconds before parameters are set; --editor opens
         the editor off screen; --realtime leaves the plug-in in real-time mode. Some plug-ins need these to
         finish starting. "readback" in the output is the value the plug-in reports after the set.
         --pump runs the message loop for that many seconds after the parameters are set, for plug-ins that
         load data or apply changes from the message thread.
         Prints one JSON line: latency, sample rate, channel counts and the text of each parameter that was set.
*/
#include <JuceHeader.h>
#include <cstdio>

static std::unique_ptr<juce::AudioPluginInstance> loadPlugin (const juce::String& path, double sampleRate, int block, juce::String& error)
{
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile (types, juce::File (path).getFullPathName());
    if (types.isEmpty())
    {
        error = path + ": not a VST3, or it failed to describe itself";
        return {};
    }
    juce::AudioPluginFormatManager manager;
    manager.addDefaultFormats();
    return manager.createPluginInstance (*types.getFirst(), sampleRate, block, error);
}

static juce::String jsonEscape (const juce::String& s)
{
    return s.replace ("\\", "\\\\").replace ("\"", "\\\"");
}

static bool readWav (const juce::File& file, juce::AudioBuffer<float>& buffer, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return false;
    sampleRate = reader->sampleRate;
    buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
    return true;
}

static bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), sampleRate,
                                                                          (unsigned int) buffer.getNumChannels(), 32, {}, 0));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialise;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    auto option = [&] (const juce::String& name, const juce::String& fallback = {})
    {
        const int at = args.indexOf (name);
        return at >= 0 && at + 1 < args.size() ? args[at + 1] : fallback;
    };

    const auto pluginPath = option ("--plugin");
    const bool list = args.contains ("list");
    const bool render = args.contains ("render");
    if (pluginPath.isEmpty() || (! list && ! render))
    {
        std::fprintf (stderr, "usage: plugin_probe --plugin <path.vst3> list [--json] | render --in a.wav --out b.wav [--set i=v,...]\n");
        return 2;
    }

    const int block = juce::jmax (16, option ("--block", "512").getIntValue());
    juce::AudioBuffer<float> input;
    double sampleRate = 48000.0;
    if (render && ! readWav (juce::File::getCurrentWorkingDirectory().getChildFile (option ("--in")), input, sampleRate))
    {
        std::fprintf (stderr, "could not read input wav\n");
        return 2;
    }

    juce::String error;
    auto plugin = loadPlugin (pluginPath, sampleRate, block, error);
    if (plugin == nullptr)
    {
        std::fprintf (stderr, "load failed: %s\n", error.toRawUTF8());
        return 1;
    }

    const auto& params = plugin->getParameters();
    if (list)
    {
        const bool json = args.contains ("--json");
        if (json) std::printf ("[\n");
        for (int i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            const float def = p->getDefaultValue();
            juce::StringArray texts;
            for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                texts.add (p->getText (v, 64));
            if (json)
                std::printf ("  {\"index\": %d, \"name\": \"%s\", \"label\": \"%s\", \"default\": %.6f, \"default_text\": \"%s\", \"steps\": %d, \"discrete\": %s, \"automatable\": %s, \"text\": [\"%s\"]}%s\n",
                             i, jsonEscape (p->getName (128)).toRawUTF8(), jsonEscape (p->getLabel()).toRawUTF8(), def,
                             jsonEscape (p->getText (def, 64)).toRawUTF8(), p->getNumSteps(), p->isDiscrete() ? "true" : "false",
                             p->isAutomatable() ? "true" : "false",
                             jsonEscape (texts.joinIntoString ("\x01")).replace ("\x01", "\", \"").toRawUTF8(),
                             i + 1 < params.size() ? "," : "");
            else
                std::printf ("%4d  %-40s default %.4f (%s)  steps %d  [%s]\n", i, p->getName (128).toRawUTF8(), def,
                             p->getText (def, 64).toRawUTF8(), p->getNumSteps(), texts.joinIntoString (" | ").toRawUTF8());
        }
        if (json) std::printf ("]\n");
        return 0;
    }

    // Sidechain, when the plug-in offers a second input bus and a file was given.
    juce::AudioBuffer<float> sidechain;
    const auto sidechainPath = option ("--sidechain");
    bool useSidechain = false;
    if (sidechainPath.isNotEmpty())
    {
        double scRate = 0.0;
        useSidechain = readWav (juce::File::getCurrentWorkingDirectory().getChildFile (sidechainPath), sidechain, scRate)
                       && plugin->getBusCount (true) > 1;
        if (useSidechain)
            plugin->enableAllBuses();
    }

    plugin->setNonRealtime (! args.contains ("--realtime"));
    plugin->prepareToPlay (sampleRate, block);

    // Some plug-ins only finish initialising once their editor exists. --editor opens it off screen.
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    if (args.contains ("--editor") && plugin->hasEditor())
    {
        editor.reset (plugin->createEditorIfNeeded());
        if (editor != nullptr)
        {
            editor->addToDesktop (0);
            editor->setTopLeftPosition (-4000, -4000);
            editor->setVisible (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
        }
    }

    // --init gives a plug-in that finishes loading late (data files, a default preset) time to do so before
    // the parameters are set, so its own start-up does not overwrite them.
    const int initMs = (int) (option ("--init", "0").getDoubleValue() * 1000.0);
    if (initMs > 0)
    {
        // Audio has to be running for some engines to finish starting.
        juce::AudioBuffer<float> silence (juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels(), 2), block);
        juce::MidiBuffer noMidi;
        for (int waited = 0; waited < initMs; waited += 100)
        {
            silence.clear();
            plugin->processBlock (silence, noMidi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        }
    }

    struct Wanted { int index; float value; };
    std::vector<Wanted> wanted;
    for (const auto& item : juce::StringArray::fromTokens (option ("--set"), ",", {}))
    {
        const int index = item.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const float value = juce::jlimit (0.0f, 1.0f, item.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
        if (index < 0 || index >= params.size())
        {
            std::fprintf (stderr, "no parameter %d\n", index);
            return 2;
        }
        wanted.push_back ({ index, value });
    }

    // Set, let the message thread run, and set again whatever did not hold.
    const int pumpMs = (int) (option ("--pump", "0.5").getDoubleValue() * 1000.0);
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        bool allHeld = true;
        for (const auto& w : wanted)
            if (attempt == 0 || std::abs (params[w.index]->getValue() - w.value) > 0.002f)
            {
                params[w.index]->beginChangeGesture();
                params[w.index]->setValueNotifyingHost (w.value);
                params[w.index]->endChangeGesture();
                allHeld = false;
            }
        if (pumpMs > 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (pumpMs);
        if (allHeld)
            break;
    }

    juce::String setReport;
    for (const auto& w : wanted)
    {
        if (setReport.isNotEmpty()) setReport << ", ";
        setReport << "\"" << w.index << "\": {\"name\": \"" << jsonEscape (params[w.index]->getName (128)) << "\", \"value\": " << w.value
                  << ", \"readback\": " << params[w.index]->getValue()
                  << ", \"text\": \"" << jsonEscape (params[w.index]->getCurrentValueAsText()) << "\"}";
    }

    const int inChannels = plugin->getTotalNumInputChannels();
    const int outChannels = plugin->getTotalNumOutputChannels();
    const int mainIn = plugin->getBusCount (true) > 0 ? plugin->getChannelCountOfBus (true, 0) : inChannels;
    const int channels = juce::jmax (inChannels, outChannels, 2);
    const int total = input.getNumSamples();
    juce::AudioBuffer<float> work (channels, block);
    juce::MidiBuffer midi;

    auto fill = [&] (int start, int count)
    {
        work.clear();
        for (int ch = 0; ch < juce::jmin (mainIn, channels); ++ch)
        {
            const int src = juce::jmin (ch, input.getNumChannels() - 1);
            for (int i = 0; i < count; ++i)
                work.setSample (ch, i, start + i < total ? input.getSample (src, start + i) : 0.0f);
        }
        if (useSidechain)
            for (int ch = mainIn; ch < juce::jmin (inChannels, channels); ++ch)
            {
                const int src = juce::jmin (ch - mainIn, sidechain.getNumChannels() - 1);
                for (int i = 0; i < count; ++i)
                    work.setSample (ch, i, start + i < sidechain.getNumSamples() ? sidechain.getSample (src, start + i) : 0.0f);
            }
    };

    // Warm-up: loop the start of the file so smoothing and adaptive detection have settled before the take.
    const int warmupSamples = (int) (option ("--warmup", "1.0").getDoubleValue() * sampleRate);
    for (int done = 0; done < warmupSamples && total > 0; done += block)
    {
        fill (done % juce::jmax (1, total - block), block);
        plugin->processBlock (work, midi);
    }

    const int latency = plugin->getLatencySamples();
    const bool raw = args.contains ("--raw");
    const int shift = raw ? 0 : latency;
    const int outCount = juce::jmax (1, juce::jmin (outChannels, 2));
    juce::AudioBuffer<float> output (outCount, total);
    output.clear();
    for (int position = 0; position < total + shift; position += block)
    {
        fill (position, block);
        plugin->processBlock (work, midi);
        for (int ch = 0; ch < outCount; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const int dest = position + i - shift;
                if (dest >= 0 && dest < total)
                    output.setSample (ch, dest, work.getSample (ch, i));
            }
    }
    // Parameter texts as the plug-in reports them after the run, for plug-ins whose texts are only valid late.
    if (args.contains ("--dump-params"))
        for (int i = 0; i < params.size(); ++i)
            std::fprintf (stderr, "param %d %s = %.4f (%s)\n", i, params[i]->getName (64).toRawUTF8(), params[i]->getValue(),
                          params[i]->getCurrentValueAsText().toRawUTF8());
    editor.reset();
    plugin->releaseResources();

    if (! writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (option ("--out")), output, sampleRate))
    {
        std::fprintf (stderr, "could not write output wav\n");
        return 2;
    }
    std::printf ("{\"plugin\": \"%s\", \"latency_samples\": %d, \"aligned\": %s, \"sample_rate\": %.1f, \"inputs\": %d, \"outputs\": %d, \"sidechain\": %s, \"set\": {%s}}\n",
                 jsonEscape (plugin->getName()).toRawUTF8(), latency, raw ? "false" : "true", sampleRate, inChannels, outChannels,
                 useSidechain ? "true" : "false", setReport.toRawUTF8());
    return 0;
}
