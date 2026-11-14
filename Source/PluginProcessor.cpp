#include "PluginProcessor.h"
#include "UI/ThemeFile.h"
#include "AppSettings.h"
#include "UI/Theme.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstring>

namespace
{
    constexpr double frequencyRampSeconds = 0.025;
    constexpr double gainRampSeconds = 0.025;
    constexpr double qRampSeconds = 0.025;
    constexpr double bandBypassRampSeconds = 0.015;
    constexpr double globalBypassRampSeconds = 0.015;
    constexpr double outputRampSeconds = 0.020;
    constexpr double widthRampSeconds = 0.020;
    constexpr int controlInterval = 64;
    constexpr int delayCapacity = 65536;
    // Fade down before the latency changes, and back up after.
    constexpr double latencyFadeSeconds = 0.005;
    constexpr juce::uint32 audioIdleMs = 500;

    float clampedFrequency (float frequency, double sampleRate) noexcept
    {
        return ResponseModel::clampFrequency (frequency, sampleRate);
    }

    float clampedQ (float q) noexcept
    {
        return ResponseModel::clampQ (q);
    }
}

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

void OpenEQAudioProcessor::setRemoteTarget (OpenEQAudioProcessor* target)
{
    if (target == remoteTarget)
        return;
    if (target != nullptr && ! InstanceRegistry::get().isRegistered (target))
        target = nullptr;

    if (remoteTarget != nullptr && remoteRelay != nullptr)
    {
        // A destroyed target took its listener lists with it; touching its state would be a use after free.
        if (InstanceRegistry::get().isRegistered (remoteTarget))
            detachRelayListener (remoteTarget->apvts, *remoteRelay);
        delete remoteRelay;
        remoteRelay = nullptr;
    }
    if (ownRelay != nullptr)
        detachRelayListener (apvts, *ownRelay);

    remoteTarget = target;
    if (remoteTarget != nullptr)
    {
        attachRelayListener (apvts, *ownRelay);
        remoteRelay = new RelayListener;
        remoteRelay->owner = this;
        remoteRelay->source = remoteTarget;
        attachRelayListener (remoteTarget->apvts, *remoteRelay);
    }
}

void OpenEQAudioProcessor::relayParameter (const juce::String& parameterID, float rawValue, void* source)
{
    if (remoteTarget == nullptr)
        return;
    if (! InstanceRegistry::get().isRegistered (remoteTarget))
    {
        // The target was destroyed: drop the mirror from the message thread.
        setRemoteTarget (nullptr);
        return;
    }

    const bool fromSelf = (source == this);
    auto& destination = fromSelf ? remoteTarget->apvts : apvts;
    if (auto* parameter = destination.getParameter (parameterID))
    {
        const float normalised = parameter->convertTo0to1 (rawValue);
        if (std::abs (normalised - parameter->getValue()) < 1.0e-9f)
            return; // echo of the value we just wrote
        parameter->setValueNotifyingHost (normalised);
    }
}

juce::String OpenEQAudioProcessor::getInstanceName() const
{
    if (const auto* entry = InstanceRegistry::get().findByProcessor ((void*) this))
        return entry->name;
    return "OpenEQ";
}

OpenEQAudioProcessor::OpenEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                           .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMETERS", Params::createParameterLayout())
{
    cacheParameterPointers();
    // A saved default (reference "Save as Default") becomes the starting state of new instances.
    if (defaultPresetFile().existsAsFile())
        loadPresetFile (defaultPresetFile());
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    sidechainOversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    publishedGraph = buildIdleGraph();
    abSlots[0] = apvts.copyState();
    abSlots[1] = abSlots[0].createCopy();
    instanceId = InstanceRegistry::get().add (juce::String ("Instance ") + juce::String (InstanceRegistry::get().snapshot().size() + 1), this);
    ownRelay = new RelayListener;
    ownRelay->owner = this;
    ownRelay->source = this;
}

OpenEQAudioProcessor::~OpenEQAudioProcessor()
{
    setRemoteTarget (nullptr);
    InstanceRegistry::get().remove (instanceId);
    delete ownRelay;
    ownRelay = nullptr;
}

void OpenEQAudioProcessor::cacheParameterPointers()
{
    for (int i = 0; i < numBands; ++i)
    {
        auto& cached = cachedBands[(size_t) i];
        cached.active = apvts.getRawParameterValue (Params::bandActiveID (i));
        cached.type = apvts.getRawParameterValue (Params::bandTypeID (i));
        cached.frequency = apvts.getRawParameterValue (Params::bandFreqID (i));
        cached.gain = apvts.getRawParameterValue (Params::bandGainID (i));
        cached.q = apvts.getRawParameterValue (Params::bandQID (i));
        cached.slope = apvts.getRawParameterValue (Params::bandSlopeID (i));
        cached.slopeNorm = apvts.getRawParameterValue (Params::bandSlopeNormID (i));
        cached.stereo = apvts.getRawParameterValue (Params::bandStereoID (i));
        cached.solo = apvts.getRawParameterValue (Params::bandSoloID (i));
        cached.dynamic = apvts.getRawParameterValue (Params::bandDynOnID (i));
        cached.dynThreshold = apvts.getRawParameterValue (Params::bandDynThreshID (i));
        cached.dynRange = apvts.getRawParameterValue (Params::bandDynRangeID (i));
        cached.dynAttack = apvts.getRawParameterValue (Params::bandDynAttackID (i));
        cached.dynRelease = apvts.getRawParameterValue (Params::bandDynReleaseID (i));
        cached.shown = apvts.getRawParameterValue (Params::bandShownID (i));
        cached.dynAction = apvts.getRawParameterValue (Params::bandDynActionID (i));
        cached.dynKnee = apvts.getRawParameterValue (Params::bandDynKneeID (i));
        cached.dynSource = apvts.getRawParameterValue (Params::bandDynSourceID (i));
        cached.dynBlend = apvts.getRawParameterValue (Params::bandDynBlendID (i));
        cached.dynAhead = apvts.getRawParameterValue (Params::bandDynAheadID (i));
        cached.dynTransient = apvts.getRawParameterValue (Params::bandDynTransID (i));
        cached.dynAuto = apvts.getRawParameterValue (Params::bandDynAutoID (i));
        cached.dynDetLink = apvts.getRawParameterValue (Params::bandDynDetLinkID (i));
        cached.dynDetFreq = apvts.getRawParameterValue (Params::bandDynDetFreqID (i));
        cached.dynDetQ = apvts.getRawParameterValue (Params::bandDynDetQID (i));
        cached.dynBelowOn = apvts.getRawParameterValue (Params::bandDynBelowOnID (i));
        cached.dynBelowRange = apvts.getRawParameterValue (Params::bandDynBelowRangeID (i));
        cached.dynBelowRatio = apvts.getRawParameterValue (Params::bandDynBelowRatioID (i));
        cached.dynAboveOn = apvts.getRawParameterValue (Params::bandDynAboveOnID (i));
        cached.dynAboveRange = apvts.getRawParameterValue (Params::bandDynAboveRangeID (i));
        cached.dynAboveRatio = apvts.getRawParameterValue (Params::bandDynAboveRatioID (i));
        cached.dynRelFree = apvts.getRawParameterValue (Params::bandDynRelFreeID (i));
        cached.dynRelFreq = apvts.getRawParameterValue (Params::bandDynRelFreqID (i));
        cached.dynRelQ = apvts.getRawParameterValue (Params::bandDynRelQID (i));
        cached.dynRelSource = apvts.getRawParameterValue (Params::bandDynRelSourceID (i));
        cached.pan = apvts.getRawParameterValue (Params::bandPanID (i));
        cached.splitOn = apvts.getRawParameterValue (Params::bandSplitOnID (i));
        cached.splitTrans = apvts.getRawParameterValue (Params::bandSplitTransID (i));
        cached.splitTail = apvts.getRawParameterValue (Params::bandSplitTailID (i));
        cached.phase = apvts.getRawParameterValue (Params::bandPhaseID (i));
        appliedThresholdDb[(size_t) i].store (-24.0f);
        jassert (cached.active != nullptr && cached.frequency != nullptr && cached.gain != nullptr);
    }

    processingMode = apvts.getRawParameterValue ("processingMode");
    linearPhaseQuality = apvts.getRawParameterValue ("linearPhaseQuality");
    outputGain = apvts.getRawParameterValue ("outputGain");
    stereoWidth = apvts.getRawParameterValue ("stereoWidth");
    gainScale = apvts.getRawParameterValue ("gainScale");
    sideGain = apvts.getRawParameterValue ("sideGain");
    outputPan = apvts.getRawParameterValue ("outputPan");
    lookaheadOn = apvts.getRawParameterValue ("lookaheadOn");
    ultraPrecision = apvts.getRawParameterValue ("ultraPrecision");
    mixCrossover = apvts.getRawParameterValue ("mixCrossover");
    oversample2x = apvts.getRawParameterValue ("oversample2x");
    splitSource = apvts.getRawParameterValue ("splitSource");
    splitSeparation = apvts.getRawParameterValue ("splitSeparation");
    splitDecay = apvts.getRawParameterValue ("splitDecay");
    splitSmoothingMs = apvts.getRawParameterValue ("splitSmoothingMs");
    splitSolo = apvts.getRawParameterValue ("splitSolo");
    windowShapeParam = apvts.getRawParameterValue ("windowShape");
    windowParameterParam = apvts.getRawParameterValue ("windowParameter");
    renderQualityParam = apvts.getRawParameterValue ("renderQuality");
    renderOversampleParam = apvts.getRawParameterValue ("renderOversample");
    impulsePaddingParam = apvts.getRawParameterValue ("impulsePadding");
    digitalPlusParam = apvts.getRawParameterValue ("digitalPlus");
    lookaheadMs = apvts.getRawParameterValue ("lookaheadMs");
    bypass = apvts.getRawParameterValue ("bypass");
    polarity = apvts.getRawParameterValue ("polarity");
    autoGain = apvts.getRawParameterValue ("autoGain");
    inputGain = apvts.getRawParameterValue ("inputGain");
    drive = apvts.getRawParameterValue ("drive");
    spectralAmount = apvts.getRawParameterValue ("spectralAmount");
    spectralThresh = apvts.getRawParameterValue ("spectralThresh");
    spectralFreq = apvts.getRawParameterValue ("spectralFreq");
    spectralOct = apvts.getRawParameterValue ("spectralOct");
    spectralSharp = apvts.getRawParameterValue ("spectralSharp");
    spectralSelect = apvts.getRawParameterValue ("spectralSelect");
    spectralAttack = apvts.getRawParameterValue ("spectralAttack");
    spectralRelease = apvts.getRawParameterValue ("spectralRelease");
    spectralMix = apvts.getRawParameterValue ("spectralMix");
    spectralLink = apvts.getRawParameterValue ("spectralLink");
    spectralHard = apvts.getRawParameterValue ("spectralHard");
    spectralDelta = apvts.getRawParameterValue ("spectralDelta");
    spectralMS = apvts.getRawParameterValue ("spectralMS");
    spectralSidechain = apvts.getRawParameterValue ("spectralSidechain");
    for (int i = 0; i < numBands; ++i)
        bandSuppress[(size_t) i] = apvts.getRawParameterValue (Params::bandSuppressID (i));
    jassert (processingMode != nullptr && outputGain != nullptr && stereoWidth != nullptr && bypass != nullptr);
}

BandParameterState OpenEQAudioProcessor::readBandParameter (int index) const noexcept
{
    std::array<BandParameterState, numBands> all;
    readAllBandParameters (all);
    if (index < 0 || index >= numBands)
        return {};
    return all[(size_t) index];
}

void OpenEQAudioProcessor::readAllBandParameters (std::array<BandParameterState, numBands>& dest) const noexcept
{
    // Every parameter is its own atomic and none orders another, so relaxed loads (plain loads on ARM) are
    // enough; this runs once per block over every band.
    // Global gain scale multiplies every band's dB gain (reference "Scale"). It is applied here so the
    // audio path, the graph, and the linear-phase kernel all see the same effective gain.
    // reference scale runs from -200% to 200%; a negative scale inverts every band's gain.
    const float scale = gainScale != nullptr ? gainScale->load (std::memory_order_relaxed) / 100.0f : 1.0f;
    const bool globalLookahead = lookaheadOn != nullptr && lookaheadOn->load (std::memory_order_relaxed) > 0.5f;
    const float globalLookaheadMs = lookaheadMs != nullptr ? lookaheadMs->load (std::memory_order_relaxed) : 0.0f;

    for (int i = 0; i < numBands; ++i)
    {
        const auto& cached = cachedBands[(size_t) i];
        auto& band = dest[(size_t) i];
        band.active = cached.active->load (std::memory_order_relaxed) > 0.5f;
        band.solo = cached.solo->load (std::memory_order_relaxed) > 0.5f;
        band.dynamic = cached.dynamic->load (std::memory_order_relaxed) > 0.5f;
        band.type = (FilterType) juce::jlimit (0, filterTypeCount - 1, (int) cached.type->load (std::memory_order_relaxed));
        band.stereoMode = (StereoMode) juce::jlimit (0, 5, (int) cached.stereo->load (std::memory_order_relaxed));
        band.pan = juce::jlimit (-1.0f, 1.0f, cached.pan->load (std::memory_order_relaxed) / 100.0f);
        band.splitOn = cached.splitOn->load (std::memory_order_relaxed) > 0.5f;
        band.splitTransDb = cached.splitTrans->load (std::memory_order_relaxed);
        band.splitTailDb = cached.splitTail->load (std::memory_order_relaxed);
        band.phase = juce::jlimit (-1.0f, 1.0f, cached.phase->load (std::memory_order_relaxed));
        band.frequency = cached.frequency->load (std::memory_order_relaxed);
        band.gainDb = cached.gain->load (std::memory_order_relaxed) * scale;
        band.q = cached.q->load (std::memory_order_relaxed);
        // Generic types carry reference continuous slope (encoded in slopeDb); the others keep plain dB per octave.
        if (ResponseModel::isBrickwall (band.type))
            band.slopeDb = ResponseModel::brickwallSlopeDb;
        else if (ResponseModel::usesContinuousSlope (band.type))
            band.slopeDb = ResponseModel::encodeSlopeNorm (cached.slopeNorm->load (std::memory_order_relaxed));
        else
            band.slopeDb = Params::slopeIndexToDb ((int) cached.slope->load (std::memory_order_relaxed));
        band.dynThresholdDb = cached.dynThreshold->load (std::memory_order_relaxed);
        band.dynRangeDb = cached.dynRange->load (std::memory_order_relaxed);
        band.dynAttackMs = cached.dynAttack->load (std::memory_order_relaxed);
        band.dynReleaseMs = cached.dynRelease->load (std::memory_order_relaxed);
        band.shown = cached.shown->load (std::memory_order_relaxed) > 0.5f;
        band.dynAction = juce::jlimit (0, 1, (int) cached.dynAction->load (std::memory_order_relaxed));
        band.dynKneeDb = cached.dynKnee->load (std::memory_order_relaxed);
        band.dynSource = juce::jlimit (0, 5, (int) cached.dynSource->load (std::memory_order_relaxed));
        band.dynRelative = cached.dynBlend->load (std::memory_order_relaxed);
        band.dynLookaheadMs = globalLookahead ? globalLookaheadMs : cached.dynAhead->load (std::memory_order_relaxed);
        band.dynTransient = cached.dynTransient->load (std::memory_order_relaxed);
        band.dynAuto = cached.dynAuto->load (std::memory_order_relaxed) > 0.5f;
        band.dynDetLink = cached.dynDetLink->load (std::memory_order_relaxed) > 0.5f;
        band.dynDetFreq = cached.dynDetFreq->load (std::memory_order_relaxed);
        band.dynDetQ = cached.dynDetQ->load (std::memory_order_relaxed);
        band.dynBelowOn = cached.dynBelowOn->load (std::memory_order_relaxed) > 0.5f;
        band.dynBelowRangeDb = cached.dynBelowRange->load (std::memory_order_relaxed);
        band.dynBelowRatio = cached.dynBelowRatio->load (std::memory_order_relaxed) / 100.0f;
        band.dynAboveOn = cached.dynAboveOn->load (std::memory_order_relaxed) > 0.5f;
        band.dynAboveRangeDb = cached.dynAboveRange->load (std::memory_order_relaxed);
        band.dynAboveRatio = cached.dynAboveRatio->load (std::memory_order_relaxed) / 100.0f;
        band.dynRelFree = cached.dynRelFree->load (std::memory_order_relaxed) > 0.5f;
        band.dynRelFreq = cached.dynRelFreq->load (std::memory_order_relaxed);
        band.dynRelQ = cached.dynRelQ->load (std::memory_order_relaxed);
        band.dynRelSource = juce::jlimit (0, 5, (int) cached.dynRelSource->load (std::memory_order_relaxed));
    }
}

bool OpenEQAudioProcessor::isAudible (const BandParameterState& band, bool anySolo) noexcept
{
    return band.active && (! anySolo || band.solo);
}

void OpenEQAudioProcessor::ensureScratch()
{
    auto ensure = [] (juce::AudioBuffer<float>& buffer, int channels, int samples)
    {
        if (buffer.getNumChannels() != channels || buffer.getNumSamples() != samples)
            buffer.setSize (channels, samples, false, true, false);
    };

    ensure (monoScratch, 1, maxProcessingChunk);
    ensure (midBuffer, 1, maxProcessingChunk);
    ensure (sideBuffer, 1, maxProcessingChunk);
    ensure (bandDry, 2, maxProcessingChunk);
    ensure (dryInput, 2, maxProcessingChunk);
    ensure (floatFromDouble, 2, maxProcessingChunk);
    ensure (detectorScratch, 2, maxProcessingChunk);
    ensure (auditionCapture, 2, maxProcessingChunk);
    ensure (splitTransientBuffer, 2, maxProcessingChunk);
    ensure (splitTonalBuffer, 2, maxProcessingChunk);
}

namespace
{
    /** Replaces every NaN or infinity with zero and says whether there was one. Written as a select and an
        unconditional store so the loop vectorises; the samples that come out are the same. */
    bool zeroNonFinite (float* samples, int numSamples) noexcept
    {
        bool found = false;
        for (int i = 0; i < numSamples; ++i)
        {
            const float sample = samples[i];
            const bool finite = std::isfinite (sample);
            found |= ! finite;
            samples[i] = finite ? sample : 0.0f;
        }
        return found;
    }

    /** Side Level is a dB trim on the side signal. The bottom of its range is silence. */
    float sideDbToLinear (float db) noexcept
    {
        return db <= -99.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
    }

    /** Output Pan in percent, -100 full left .. +100 full right, as a -1..1 position. */
    float panToPosition (float pan) noexcept
    {
        return juce::jlimit (-1.0f, 1.0f, pan / 100.0f);
    }
}

void OpenEQAudioProcessor::snapSmoothers (const std::array<BandParameterState, numBands>& params, bool anySolo)
{
    for (int i = 0; i < numBands; ++i)
    {
        const auto& band = params[(size_t) i];
        auto& smoother = smoothers[(size_t) i];
        smoother.logFrequency.reset (currentSampleRate, frequencyRampSeconds);
        smoother.gainDb.reset (currentSampleRate, gainRampSeconds);
        smoother.logQ.reset (currentSampleRate, qRampSeconds);
        smoother.wetMix.reset (currentSampleRate, bandBypassRampSeconds);
        smoother.splitTransDb.reset (currentSampleRate, gainRampSeconds);
        smoother.splitTailDb.reset (currentSampleRate, gainRampSeconds);
        smoother.logFrequency.setCurrentAndTargetValue (std::log (clampedFrequency (band.frequency, currentSampleRate)));
        smoother.gainDb.setCurrentAndTargetValue (band.gainDb);
        smoother.logQ.setCurrentAndTargetValue (std::log (clampedQ (band.q)));
        smoother.wetMix.setCurrentAndTargetValue (isAudible (band, anySolo) ? 1.0f : 0.0f);
        smoother.splitTransDb.setCurrentAndTargetValue (effectiveSplitDb (band, band.splitTransDb));
        smoother.splitTailDb.setCurrentAndTargetValue (effectiveSplitDb (band, band.splitTailDb));
        bandWasDry[(size_t) i] = smoother.wetMix.getCurrentValue() <= 1.0e-6f;
    }

    inputGainDb.reset (currentSampleRate, 0.02);
    inputGainDb.setCurrentAndTargetValue (inputGain->load());
    outputGainDb.reset (currentSampleRate, outputRampSeconds);
    widthPercent.reset (currentSampleRate, widthRampSeconds);
    sideLinear.reset (currentSampleRate, widthRampSeconds);
    panPosition.reset (currentSampleRate, widthRampSeconds);
    bypassWet.reset (currentSampleRate, globalBypassRampSeconds);
    outputGainDb.setCurrentAndTargetValue (outputGain->load());
    widthPercent.setCurrentAndTargetValue (stereoWidth->load());
    sideLinear.setCurrentAndTargetValue (sideDbToLinear (sideGain->load()));
    panPosition.setCurrentAndTargetValue (panToPosition (outputPan->load()));
    const bool bypassed = bypass->load() > 0.5f;
    bypassWet.setCurrentAndTargetValue (bypassed ? 0.0f : 1.0f);
    wasFullyBypassed = bypassed;
}

void OpenEQAudioProcessor::setSmootherTargets (const std::array<BandParameterState, numBands>& params, bool anySolo)
{
    for (int i = 0; i < numBands; ++i)
    {
        const auto& band = params[(size_t) i];
        auto& smoother = smoothers[(size_t) i];
        smoother.logFrequency.setTargetValue (std::log (clampedFrequency (band.frequency, currentSampleRate)));
        smoother.gainDb.setTargetValue (band.gainDb);
        smoother.logQ.setTargetValue (std::log (clampedQ (band.q)));
        smoother.wetMix.setTargetValue (isAudible (band, anySolo) ? 1.0f : 0.0f);
        smoother.splitTransDb.setTargetValue (effectiveSplitDb (band, band.splitTransDb));
        smoother.splitTailDb.setTargetValue (effectiveSplitDb (band, band.splitTailDb));
    }

    inputGainDb.setTargetValue (inputGain->load());
    outputGainDb.setTargetValue (outputGain->load());
    widthPercent.setTargetValue (stereoWidth->load());
    sideLinear.setTargetValue (sideDbToLinear (sideGain->load()));
    panPosition.setTargetValue (panToPosition (outputPan->load()));
    bypassWet.setTargetValue (bypass->load() > 0.5f ? 0.0f : 1.0f);
}

float OpenEQAudioProcessor::effectiveSplitDb (const BandParameterState& band, float streamDb) noexcept
{
    return band.splitOn && band.active ? streamDb : 0.0f;
}

void OpenEQAudioProcessor::updateSplitFlags (const std::array<BandParameterState, numBands>& params) noexcept
{
    bool anySplit = false;
    for (int i = 0; i < numBands; ++i)
    {
        splitFlags[(size_t) i] = params[(size_t) i].splitOn && params[(size_t) i].active;
        anySplit = anySplit || splitFlags[(size_t) i];
    }
    anySplitBands = anySplit;
}

bool OpenEQAudioProcessor::designBandPair (int index, const BandParameterState& band, float frequency, float gainDb,
                                           float q, float transDb, float tailDb)
{
    // While the split path is in, the audio runs as two streams and every band exists once per stream. A split
    // band differs between the two by its stream gains. Any other band is the same filter on both, and its
    // stream gains arrive here as zero.
    const bool changed = bands[(size_t) index].applyIfChanged (band.type, frequency, gainDb + transDb, q, band.slopeDb);
    if (anySplitBands || splitEngaged)
    {
        auto& tail = splitTailBands[(size_t) index];
        tail.setPan (band.pan);
        if (tail.getStereoMode() != band.stereoMode)
        {
            tail.setStereoMode (band.stereoMode);
            tail.resetState();
        }
        tail.applyIfChanged (band.type, frequency, gainDb + tailDb, q, band.slopeDb);
    }
    return changed;
}

void OpenEQAudioProcessor::applyBandTargets (const std::array<BandParameterState, numBands>& params,
                                             const float* gainOffsetDb, bool useTargets)
{
    updateSplitFlags (params);
    for (int i = 0; i < numBands; ++i)
    {
        const auto& band = params[(size_t) i];
        auto& smoother = smoothers[(size_t) i];
        const float frequency = (useTargets || ! smoother.logFrequency.isSmoothing())
            ? clampedFrequency (band.frequency, currentSampleRate)
            : std::exp (smoother.logFrequency.getCurrentValue());
        const float q = (useTargets || ! smoother.logQ.isSmoothing())
            ? clampedQ (band.q)
            : std::exp (smoother.logQ.getCurrentValue());
        const float gain = (useTargets ? band.gainDb : smoother.gainDb.getCurrentValue())
                         + (gainOffsetDb != nullptr ? gainOffsetDb[(size_t) i] : 0.0f);
        const float transDb = useTargets ? effectiveSplitDb (band, band.splitTransDb) : smoother.splitTransDb.getCurrentValue();
        const float tailDb = useTargets ? effectiveSplitDb (band, band.splitTailDb) : smoother.splitTailDb.getCurrentValue();

        bands[(size_t) i].setPan (band.pan);
        if (bands[(size_t) i].getStereoMode() != band.stereoMode)
        {
            bands[(size_t) i].setStereoMode (band.stereoMode);
            bands[(size_t) i].resetState();
        }

        if (designBandPair (i, band, frequency, gain, q, transDb, tailDb))
            designUpdates.fetch_add (1, std::memory_order_relaxed);
    }
}

void OpenEQAudioProcessor::prepareToPlay (double hostRate, int blockSize)
{
    // Everything inside the plugin runs at the internal rate. That is the host rate, or twice it with 2x on.
    baseSampleRate = hostRate;
    preparedBlockSize = blockSize;
    oversampling = oversample2x != nullptr && oversample2x->load() > 0.5f;
    const double sampleRate = hostRate * (oversampling ? 2.0 : 1.0);
    currentSampleRate = sampleRate;
    prepared = false;
    ensureScratch();

    if (oversampler != nullptr)
    {
        oversampler->reset();
        sidechainOversampler->reset();
        if (oversampling)
        {
            oversampler->initProcessing (512);
            sidechainOversampler->initProcessing (512);
        }
    }
    oversampledMain.setSize (2, 1024, false, true, false);
    oversampledSidechain.setSize (2, 1024, false, true, false);
    sidechainBase.setSize (2, 512, false, true, false);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxProcessingChunk;
    spec.numChannels = 1;

    for (auto& band : bands)
        band.prepare (spec);
    for (auto& band : splitTailBands)
        band.prepare (spec);

    for (auto& engine : dynamics)
        engine.prepare (sampleRate, maxProcessingChunk);

    std::array<BandParameterState, numBands> params;
    readAllBandParameters (params);
    bool anySolo = false;
    for (const auto& band : params)
        if (band.active && band.solo) { anySolo = true; break; }

    snapSmoothers (params, anySolo);
    float zeroOffset[(size_t) numBands] = {};
    applyBandTargets (params, zeroOffset, true);

    juce::dsp::ProcessSpec convolutionSpec = spec;
    convolutionSpec.numChannels = 2;
    linearPhaseEngine.prepare (convolutionSpec);
    saturator.prepare (sampleRate, maxProcessingChunk, 2);
    spectral.prepare (sampleRate);
    splitSeparator.prepare (sampleRate);
    resetSplitEngine();
    // The first block latches the whole configuration. The kernel request below needs the mode and quality now.
    latched = {};
    latched.mode = processingMode != nullptr ? (int) processingMode->load() : 0;
    latched.lpOrder = Params::linearPhaseQualityToFftOrder (effectiveLinearPhaseQuality());
    latencyPrimed = false;
    kernelWaitSamples = 0;
    latencySwitchPending = false;
    latencyFadeGain = 1.0f;
    latencyMuteRemaining = 0;
    {
        juce::dsp::ProcessSpec auditionSpec = spec;
        auditionSpec.numChannels = 1;
        auditionBand.prepare (auditionSpec);
        detectorMonitor.prepare (auditionSpec);
    }
    if ((int) delayL.size() < delayCapacity)
    {
        delayL.assign ((size_t) delayCapacity, 0.0f);
        delayR.assign ((size_t) delayCapacity, 0.0f);
        sideDelayL.assign ((size_t) delayCapacity, 0.0f);
        sideDelayR.assign ((size_t) delayCapacity, 0.0f);
        delayWrite = 0;
    }
    lastCurveHash = 0;
    curveSubmitPending = true;
    submitLinearPhaseRequest (params, anySolo);

    publishAppliedGraph (params, anySolo);
    prepared = true;
}

void OpenEQAudioProcessor::releaseResources()
{
    if (oversampler != nullptr)
    {
        oversampler->reset();
        sidechainOversampler->reset();
    }
}

bool OpenEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    if (mainIn != mainOut)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.inputBuses.getUnchecked (1);
        if (! sidechain.isDisabled()
            && sidechain != juce::AudioChannelSet::mono()
            && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void OpenEQAudioProcessor::resetSplitEngine()
{
    splitSeparator.reset();
    for (auto& band : splitTailBands)
        band.resetState();
    tailWasDry.fill (true);
    splitEngaged = false;
    splitDelaySamples = 0;
    splitTransientMeter.store (0.0f);
    splitTonalMeter.store (0.0f);
    splitRatioMeter.store (0.5f);
}

OpenEQAudioProcessor::LatencyConfig OpenEQAudioProcessor::readLatencyConfig (const std::array<BandParameterState, numBands>& params,
                                                                           bool anySolo) const noexcept
{
    LatencyConfig config;
    config.mode = processingMode != nullptr ? (int) processingMode->load() : 0;
    config.lpOrder = Params::linearPhaseQualityToFftOrder (effectiveLinearPhaseQuality());
    config.lookahead = activeLookaheadSamples (params, anySolo);
    config.drive = drive != nullptr && drive->load() > 1.0e-4f;
    config.spectral = spectralAmount != nullptr && spectralAmount->load() > 1.0e-4f;
    // The split path stays in while either A/B slot has a split band, so switching between the slots does not
    // change the latency.
    config.split = (anySplitBands || otherSlotSplit.load()) && config.mode == 0;
    return config;
}

void OpenEQAudioProcessor::latchLatencyConfig (const LatencyConfig& config) noexcept
{
    if (config.split != splitEngaged)
    {
        resetSplitEngine();
        splitEngaged = config.split;
    }
    if (config.spectral && ! latched.spectral)
        spectral.reset();
    latched = config;
}

void OpenEQAudioProcessor::applyLatencyTransition (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (! latencySwitchPending && latencyMuteRemaining <= 0 && latencyFadeGain >= 1.0f)
        return;

    const float step = 1.0f / (float) juce::jmax (1, (int) std::llround (latencyFadeSeconds * currentSampleRate));
    const int channels = juce::jmin (2, buffer.getNumChannels());
    for (int n = 0; n < numSamples; ++n)
    {
        if (latencySwitchPending)
            latencyFadeGain = juce::jmax (0.0f, latencyFadeGain - step);
        else if (latencyMuteRemaining > 0)
            --latencyMuteRemaining;
        else
            latencyFadeGain = juce::jmin (1.0f, latencyFadeGain + step);

        for (int channel = 0; channel < channels; ++channel)
            buffer.getWritePointer (channel)[n] *= latencyFadeGain;
    }
}

void OpenEQAudioProcessor::pushSplitSpectrum (int numSamples)
{
    // The analyzer works at the host rate. With 2x oversampling on, every second sample is enough for a display.
    const int stride = oversampling ? 2 : 1;
    const int count = numSamples / stride;
    const bool stereo = ioChannels > 1;
    auto* scratch = monoScratch.getWritePointer (0);
    auto push = [&] (AnalyzerRing& ring, const juce::AudioBuffer<float>& source, int channel)
    {
        const float* data = source.getReadPointer (channel);
        if (stride == 1)
        {
            ring.pushSamples (data, count);
            return;
        }
        for (int i = 0; i < count; ++i)
            scratch[i] = data[i * stride];
        ring.pushSamples (scratch, count);
    };
    push (splitTransL, splitTransientBuffer, 0);
    push (splitTransR, splitTransientBuffer, stereo ? 1 : 0);
    push (splitTonalL, splitTonalBuffer, 0);
    push (splitTonalR, splitTonalBuffer, stereo ? 1 : 0);
}

void OpenEQAudioProcessor::refreshOtherSlotSplit()
{
    // Message thread. The audio thread only reads the flag.
    bool uses = false;
    const auto& other = abSlots[1 - abSlot];
    auto valueOf = [&other] (const juce::String& id, float fallback)
    {
        const auto child = other.getChildWithProperty ("id", id);
        return child.isValid() ? (float) child.getProperty ("value", fallback) : fallback;
    };
    if (other.isValid())
        for (int i = 0; i < numBands && ! uses; ++i)
            uses = valueOf (Params::bandSplitOnID (i), 0.0f) > 0.5f && valueOf (Params::bandActiveID (i), 0.0f) > 0.5f;
    otherSlotSplit.store (uses);
}

void OpenEQAudioProcessor::separateSplitStreams (juce::AudioBuffer<float>& buffer, int offset, int numSamples)
{
    // Source presets scale the separation bias for the material. Higher asks more of a transient.
    static constexpr float sourceScale[5] = { 1.0f, 0.8f, 0.7f, 1.1f, 1.4f };
    SplitSeparator::Settings settings;
    settings.separation = splitSeparation != nullptr ? splitSeparation->load() : 0.5f;
    settings.decay = splitDecay != nullptr ? splitDecay->load() : 0.5f;
    settings.smoothingMs = splitSmoothingMs != nullptr ? splitSmoothingMs->load() : 5.0f;
    settings.sourceScale = sourceScale[(size_t) juce::jlimit (0, 4, splitSource != nullptr ? (int) splitSource->load() : 0)];

    const int channels = juce::jmax (1, juce::jmin (2, ioChannels, buffer.getNumChannels()));
    const float* inLeft = buffer.getReadPointer (0) + offset;
    const float* inRight = channels > 1 ? buffer.getReadPointer (1) + offset : nullptr;
    splitSeparator.process (inLeft, inRight,
                            splitTransientBuffer.getWritePointer (0), splitTransientBuffer.getWritePointer (1),
                            splitTonalBuffer.getWritePointer (0), splitTonalBuffer.getWritePointer (1),
                            numSamples, settings);

    // Stream meters: the peak of each stream, and the share of the energy that is transient.
    float transPeak = 0.0f, tonalPeak = 0.0f;
    double transEnergy = 0.0, tonalEnergy = 0.0;
    for (int channel = 0; channel < channels; ++channel)
    {
        const float* trans = splitTransientBuffer.getReadPointer (channel);
        const float* tonal = splitTonalBuffer.getReadPointer (channel);
        for (int n = 0; n < numSamples; ++n)
        {
            transPeak = juce::jmax (transPeak, std::abs (trans[n]));
            tonalPeak = juce::jmax (tonalPeak, std::abs (tonal[n]));
            transEnergy += (double) trans[n] * (double) trans[n];
            tonalEnergy += (double) tonal[n] * (double) tonal[n];
        }
    }
    const float prevT = splitTransientMeter.load();
    const float prevS = splitTonalMeter.load();
    splitTransientMeter.store (transPeak > prevT ? transPeak : prevT * 0.92f);
    splitTonalMeter.store (tonalPeak > prevS ? tonalPeak : prevS * 0.92f);
    const double total = transEnergy + tonalEnergy;
    if (total > 1.0e-12)
        splitRatioMeter.store ((float) (transEnergy / total));
}

void OpenEQAudioProcessor::processLane (EQBand& band, float* data, int numSamples, int lane, float mixStart, float mixEnd)
{
    const bool unity = mixStart >= 1.0f - 1.0e-6f && mixEnd >= 1.0f - 1.0e-6f;
    if (unity)
    {
        band.processChannel (data, numSamples, lane);
        return;
    }

    const int laneIndex = juce::jlimit (0, 1, lane);
    float* dry = bandDry.getWritePointer (laneIndex);
    std::memcpy (dry, data, sizeof (float) * (size_t) numSamples);
    band.processChannel (data, numSamples, laneIndex);

    for (int n = 0; n < numSamples; ++n)
    {
        const float t = (float) (n + 1) / (float) numSamples;
        const float mix = mixStart + (mixEnd - mixStart) * t;
        data[n] = dry[n] + mix * (data[n] - dry[n]);
    }
}

void OpenEQAudioProcessor::processLanePair (EQBand& band, float* dataA, float* dataB, int numSamples,
                                            float mixStartA, float mixEndA, float mixStartB, float mixEndB)
{
    // processLane on lane 0 (dataA) and lane 1 (dataB), with both filters run in one pass.
    const bool unityA = mixStartA >= 1.0f - 1.0e-6f && mixEndA >= 1.0f - 1.0e-6f;
    const bool unityB = mixStartB >= 1.0f - 1.0e-6f && mixEndB >= 1.0f - 1.0e-6f;
    float* dryA = bandDry.getWritePointer (0);
    float* dryB = bandDry.getWritePointer (1);
    if (! unityA)
        std::memcpy (dryA, dataA, sizeof (float) * (size_t) numSamples);
    if (! unityB)
        std::memcpy (dryB, dataB, sizeof (float) * (size_t) numSamples);

    band.processChannelPair (dataA, dataB, numSamples, 0, 1);

    auto blend = [numSamples] (float* data, const float* dry, float mixStart, float mixEnd)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float t = (float) (n + 1) / (float) numSamples;
            const float mix = mixStart + (mixEnd - mixStart) * t;
            data[n] = dry[n] + mix * (data[n] - dry[n]);
        }
    };
    if (! unityA)
        blend (dataA, dryA, mixStartA, mixEndA);
    if (! unityB)
        blend (dataB, dryB, mixStartB, mixEndB);
}

void OpenEQAudioProcessor::processBands (juce::AudioBuffer<float>& buffer, int offset, int numSamples,
                                         const std::array<SliceMix, numBands>& mixes, bool linearPhase, int segmentPosition)
{
    if (linearPhase)
    {
        const int mainChannels = juce::jmax (1, juce::jmin (2, ioChannels, buffer.getNumChannels()));
        auto block = juce::dsp::AudioBlock<float> (buffer)
                         .getSubBlock ((size_t) offset, (size_t) numSamples)
                         .getSubsetChannelBlock (0, (size_t) mainChannels);
        linearPhaseEngine.process (block);
        return;
    }

    const int channels = juce::jmax (1, juce::jmin (ioChannels, buffer.getNumChannels()));
    auto* left = buffer.getWritePointer (0) + offset;
    auto* right = channels > 1 ? buffer.getWritePointer (1) + offset : left;

    if (! splitEngaged)
    {
        runBandChain (bands, bandWasDry, left, right, numSamples, channels, mixes);
        return;
    }

    // Two streams, one band chain each: the transient stream through the transient gains, the tonal stream
    // through the tonal gains. The output is their sum, or one of them while it is soloed.
    auto* transLeft = splitTransientBuffer.getWritePointer (0) + segmentPosition;
    auto* transRight = channels > 1 ? splitTransientBuffer.getWritePointer (1) + segmentPosition : transLeft;
    auto* tonalLeft = splitTonalBuffer.getWritePointer (0) + segmentPosition;
    auto* tonalRight = channels > 1 ? splitTonalBuffer.getWritePointer (1) + segmentPosition : tonalLeft;
    runBandChain (bands, bandWasDry, transLeft, transRight, numSamples, channels, mixes);
    runBandChain (splitTailBands, tailWasDry, tonalLeft, tonalRight, numSamples, channels, mixes);

    const int soloMode = splitSolo != nullptr ? (int) splitSolo->load() : 0; // 0 off, 1 transient, 2 tonal
    const float transGain = soloMode == 2 ? 0.0f : 1.0f;
    const float tonalGain = soloMode == 1 ? 0.0f : 1.0f;
    for (int n = 0; n < numSamples; ++n)
        left[n] = transGain * transLeft[n] + tonalGain * tonalLeft[n];
    if (channels > 1)
        for (int n = 0; n < numSamples; ++n)
            right[n] = transGain * transRight[n] + tonalGain * tonalRight[n];
}

void OpenEQAudioProcessor::runBandChain (std::array<EQBand, numBands>& chain, std::array<bool, numBands>& wasDry,
                                         float* left, float* right, int numSamples, int channels,
                                         const std::array<SliceMix, numBands>& mixes)
{
    // Consecutive fully wet stereo bands are collected and run together through EQBand::processCascadePair,
    // which gives each of them exactly what processLanePair would, in less time. Any other band first
    // flushes the collected ones, so the order of the bands is kept.
    std::array<EQBand*, numBands> cascade {};
    int cascadeCount = 0;
    auto flushCascade = [&]
    {
        if (cascadeCount > 0)
            EQBand::processCascadePair (cascade.data(), cascadeCount, left, right, numSamples);
        cascadeCount = 0;
    };
    auto unity = [] (float start, float end) { return start >= 1.0f - 1.0e-6f && end >= 1.0f - 1.0e-6f; };

    for (int i = 0; i < numBands; ++i)
    {
        const float mixStart = mixes[(size_t) i].start;
        const float mixEnd = mixes[(size_t) i].end;
        if (mixStart <= 1.0e-6f && mixEnd <= 1.0e-6f)
        {
            wasDry[(size_t) i] = true;
            continue;
        }

        auto& band = chain[(size_t) i];
        if (wasDry[(size_t) i])
            band.resetState();
        wasDry[(size_t) i] = false;

        if (channels >= 2 && band.getStereoMode() == StereoMode::Stereo && band.canCascade())
        {
            double wetLeft = 1.0, wetRight = 1.0;
            ResponseModel::panWeights (band.getPan(), wetLeft, wetRight);
            if (unity (mixStart * (float) wetLeft, mixEnd * (float) wetLeft)
                && unity (mixStart * (float) wetRight, mixEnd * (float) wetRight))
            {
                cascade[(size_t) cascadeCount++] = &band;
                continue;
            }
        }
        flushCascade();

        if (channels < 2)
        {
            processLane (band, left, numSamples, 0, mixStart, mixEnd);
            continue;
        }

        switch (band.getStereoMode())
        {
            case StereoMode::Stereo:
            {
                // Pan scales the wet amount on each channel: a full pan to one side leaves the other channel dry.
                double wetLeft = 1.0, wetRight = 1.0;
                ResponseModel::panWeights (band.getPan(), wetLeft, wetRight);
                processLanePair (band, left, right, numSamples,
                                 mixStart * (float) wetLeft, mixEnd * (float) wetLeft,
                                 mixStart * (float) wetRight, mixEnd * (float) wetRight);
                break;
            }

            case StereoMode::Left:
                processLane (band, left, numSamples, 0, mixStart, mixEnd);
                break;

            case StereoMode::Right:
                processLane (band, right, numSamples, 1, mixStart, mixEnd);
                break;

            case StereoMode::MidSide:
            {
                // Mid and side both pass through the band. Pan sets how much of each gets the filter.
                auto* mid = midBuffer.getWritePointer (0);
                auto* side = sideBuffer.getWritePointer (0);
                for (int n = 0; n < numSamples; ++n)
                {
                    mid[n] = 0.5f * (left[n] + right[n]);
                    side[n] = 0.5f * (left[n] - right[n]);
                }

                double wetMid = 1.0, wetSide = 1.0;
                ResponseModel::panWeights (band.getPan(), wetMid, wetSide);
                processLanePair (band, mid, side, numSamples,
                                 mixStart * (float) wetMid, mixEnd * (float) wetMid,
                                 mixStart * (float) wetSide, mixEnd * (float) wetSide);

                for (int n = 0; n < numSamples; ++n)
                {
                    left[n] = mid[n] + side[n];
                    right[n] = mid[n] - side[n];
                }
                break;
            }

            case StereoMode::Mid:
            case StereoMode::Side:
            {
                auto* mid = midBuffer.getWritePointer (0);
                auto* side = sideBuffer.getWritePointer (0);
                for (int n = 0; n < numSamples; ++n)
                {
                    mid[n] = 0.5f * (left[n] + right[n]);
                    side[n] = 0.5f * (left[n] - right[n]);
                }

                if (band.getStereoMode() == StereoMode::Mid)
                    processLane (band, mid, numSamples, 0, mixStart, mixEnd);
                else
                    processLane (band, side, numSamples, 1, mixStart, mixEnd);

                for (int n = 0; n < numSamples; ++n)
                {
                    left[n] = mid[n] + side[n];
                    right[n] = mid[n] - side[n];
                }
                break;
            }
        }
    }
    flushCascade();
}

void OpenEQAudioProcessor::applyWidthAndGain (juce::AudioBuffer<float>& buffer, int offset, int numSamples)
{
    const int channels = juce::jmax (1, juce::jmin (ioChannels, buffer.getNumChannels()));
    const bool stereo = channels >= 2;

    // Width and Side Level both scale the side component, so they share one mid/side pass.
    const bool widthOrSideMoves = widthPercent.isSmoothing() || sideLinear.isSmoothing()
        || std::abs (widthPercent.getCurrentValue() - 100.0f) > 0.01f
        || std::abs (sideLinear.getCurrentValue() - 1.0f) > 1.0e-6f;
    if (stereo && widthOrSideMoves)
    {
        auto* left = buffer.getWritePointer (0) + offset;
        auto* right = buffer.getWritePointer (1) + offset;
        for (int i = 0; i < numSamples; ++i)
        {
            const float width = (widthPercent.isSmoothing() ? widthPercent.getNextValue() : widthPercent.getCurrentValue()) / 100.0f;
            const float sideScale = sideLinear.isSmoothing() ? sideLinear.getNextValue() : sideLinear.getCurrentValue();
            const float mid = 0.5f * (left[i] + right[i]);
            const float side = 0.5f * (left[i] - right[i]) * width * sideScale;
            left[i] = mid + side;
            right[i] = mid - side;
        }
    }

    // Output Pan is a balance: the side opposite the pan is attenuated, the near side is untouched.
    const bool panMoves = panPosition.isSmoothing() || std::abs (panPosition.getCurrentValue()) > 1.0e-6f;
    if (stereo && panMoves)
    {
        auto* left = buffer.getWritePointer (0) + offset;
        auto* right = buffer.getWritePointer (1) + offset;
        for (int i = 0; i < numSamples; ++i)
        {
            const float pan = panPosition.isSmoothing() ? panPosition.getNextValue() : panPosition.getCurrentValue();
            left[i] *= juce::jmin (1.0f, 1.0f - pan);
            right[i] *= juce::jmin (1.0f, 1.0f + pan);
        }
    }

    if (outputGainDb.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float gain = juce::Decibels::decibelsToGain (outputGainDb.getNextValue());
            for (int channel = 0; channel < channels; ++channel)
                buffer.getWritePointer (channel)[offset + i] *= gain;
        }
    }
    else
    {
        const float gain = juce::Decibels::decibelsToGain (outputGainDb.getCurrentValue());
        if (std::abs (gain - 1.0f) > 1.0e-6f)
            buffer.applyGain (offset, numSamples, gain);
    }
}

void OpenEQAudioProcessor::processSegment (juce::AudioBuffer<float>& buffer, int offset, int numSamples,
                                           const std::array<BandParameterState, numBands>& params,
                                           bool anySolo, bool linearPhase)
{
    const bool settledBypass = ! bypassWet.isSmoothing() && bypassWet.getCurrentValue() <= 1.0e-6f;

    if (settledBypass && reportedLatency == 0)
    {
        for (int i = 0; i < numBands; ++i)
        {
            auto& smoother = smoothers[(size_t) i];
            const auto& band = params[(size_t) i];
            smoother.logFrequency.setCurrentAndTargetValue (std::log (clampedFrequency (band.frequency, currentSampleRate)));
            smoother.gainDb.setCurrentAndTargetValue (band.gainDb);
            smoother.logQ.setCurrentAndTargetValue (std::log (clampedQ (band.q)));
            smoother.wetMix.setCurrentAndTargetValue (isAudible (band, anySolo) ? 1.0f : 0.0f);
            bandWasDry[(size_t) i] = smoother.wetMix.getCurrentValue() <= 1.0e-6f;
        }
        outputGainDb.setCurrentAndTargetValue (outputGain->load());
        inputGainDb.setCurrentAndTargetValue (inputGain->load());
        widthPercent.setCurrentAndTargetValue (stereoWidth->load());
        sideLinear.setCurrentAndTargetValue (sideDbToLinear (sideGain->load()));
        panPosition.setCurrentAndTargetValue (panToPosition (outputPan->load()));
        bypassWet.setCurrentAndTargetValue (0.0f);
        float zeroOffset[(size_t) numBands] = {};
        applyBandTargets (params, zeroOffset, true);
        wasFullyBypassed = true;
        return;
    }

    if (wasFullyBypassed)
    {
        for (auto& band : bands)
            band.resetState();
        for (auto& band : splitTailBands)
            band.resetState();
        wasFullyBypassed = false;
    }

    const int mainChannels = juce::jmin (2, buffer.getNumChannels());
    const bool needsDelayedDry = reportedLatency > 0;
    const bool fadeBypass = bypassWet.isSmoothing() || (needsDelayedDry && bypassWet.getCurrentValue() < 1.0f - 1.0e-6f);
    if (fadeBypass && ! needsDelayedDry)
    {
        for (int channel = 0; channel < mainChannels; ++channel)
            std::memcpy (dryInput.getWritePointer (channel),
                         buffer.getReadPointer (channel) + offset,
                         sizeof (float) * (size_t) numSamples);
    }

    const float* detectorL = buffer.getReadPointer (0) + offset;
    const float* detectorR = mainChannels > 1 ? buffer.getReadPointer (1) + offset : detectorL;
    if (lookaheadSamples > 0 && ! delayL.empty())
    {
        for (int channel = 0; channel < mainChannels; ++channel)
            std::memcpy (dryInput.getWritePointer (channel),
                         buffer.getReadPointer (channel) + offset,
                         sizeof (float) * (size_t) numSamples);
        detectorL = dryInput.getReadPointer (0);
        detectorR = mainChannels > 1 ? dryInput.getReadPointer (1) : detectorL;
        for (int i = 0; i < numSamples; ++i)
        {
            pushDelay (detectorL[i], detectorR[i]);
            if (sidechainLeft != nullptr && ! sideDelayL.empty())
            {
                int slot = delayWrite - 1;
                if (slot < 0)
                    slot += (int) sideDelayL.size();
                sideDelayL[(size_t) slot] = sidechainLeft[(size_t) (offset + i)];
                sideDelayR[(size_t) slot] = sidechainRight != nullptr ? sidechainRight[(size_t) (offset + i)]
                                                                      : sidechainLeft[(size_t) (offset + i)];
            }
            buffer.getWritePointer (0)[offset + i] = readDelay (0, lookaheadSamples);
            if (mainChannels > 1)
                buffer.getWritePointer (1)[offset + i] = readDelay (1, lookaheadSamples);
        }
    }
    else if (needsDelayedDry)
    {
        const bool holdSidechain = splitDelaySamples > 0 && sidechainLeft != nullptr && ! sideDelayL.empty();
        for (int i = 0; i < numSamples; ++i)
        {
            pushDelay (detectorL[i], detectorR[i]);
            if (holdSidechain)
            {
                int slot = delayWrite - 1;
                if (slot < 0)
                    slot += (int) sideDelayL.size();
                sideDelayL[(size_t) slot] = sidechainLeft[(size_t) (offset + i)];
                sideDelayR[(size_t) slot] = sidechainRight != nullptr ? sidechainRight[(size_t) (offset + i)]
                                                                      : sidechainLeft[(size_t) (offset + i)];
            }
        }
    }

    // Split: from here on the audio is two streams, both late by the separator's delay. The dynamics
    // detectors read the delay line at the same lateness, so they stay in step with the audio.
    if (splitEngaged)
        separateSplitStreams (buffer, offset, numSamples);

    bool anyDynamic = false;
    for (const auto& band : params)
        if (band.dynamic && ResponseModel::supportsDynamics (band.type) && isAudible (band, anySolo))
            anyDynamic = true;

    for (int i = 0; i < numBands; ++i)
    {
        // Pan changes every block, so it is set before the mode check. It does not reset the filter state.
        bands[(size_t) i].setPan (params[(size_t) i].pan);
        if (bands[(size_t) i].getStereoMode() == params[(size_t) i].stereoMode)
            continue;
        bands[(size_t) i].setStereoMode (params[(size_t) i].stereoMode);
        bands[(size_t) i].resetState();
    }

    const bool modulate = [&]
    {
        for (const auto& smoother : smoothers)
            if (smoother.logFrequency.isSmoothing() || smoother.gainDb.isSmoothing()
                || smoother.logQ.isSmoothing() || smoother.wetMix.isSmoothing()
                || smoother.splitTransDb.isSmoothing() || smoother.splitTailDb.isSmoothing())
                return true;
        return false;
    }();

    if (! modulate && ! anyDynamic)
    {
        for (auto& smoother : smoothers)
        {
            smoother.logFrequency.skip (numSamples);
            smoother.gainDb.skip (numSamples);
            smoother.logQ.skip (numSamples);
            smoother.wetMix.skip (numSamples);
            smoother.splitTransDb.skip (numSamples);
            smoother.splitTailDb.skip (numSamples);
        }

        float zeroOffset[(size_t) numBands] = {};
        applyBandTargets (params, zeroOffset, true);

        std::array<SliceMix, numBands> mixes;
        for (int i = 0; i < numBands; ++i)
            mixes[(size_t) i].start = mixes[(size_t) i].end = smoothers[(size_t) i].wetMix.getCurrentValue();

        processBands (buffer, offset, numSamples, mixes, linearPhase, 0);
    }
    else
    {
        int position = 0;
        while (position < numSamples)
        {
            const int count = juce::jmin (controlInterval, numSamples - position);
            std::array<SliceMix, numBands> mixes;
            float gainOffset[(size_t) numBands] = {};

            for (int i = 0; i < numBands; ++i)
            {
                auto& smoother = smoothers[(size_t) i];
                mixes[(size_t) i].start = smoother.wetMix.getCurrentValue();
                smoother.logFrequency.skip (count);
                smoother.gainDb.skip (count);
                smoother.logQ.skip (count);
                smoother.wetMix.skip (count);
                smoother.splitTransDb.skip (count);
                smoother.splitTailDb.skip (count);
                mixes[(size_t) i].end = smoother.wetMix.getCurrentValue();
            }

            if (anyDynamic)
                advanceDynamics (params, anySolo, linearPhase, detectorL, detectorR, offset, numSamples,
                                 position, count, gainOffset);

            for (int i = 0; i < numBands; ++i)
            {
                auto& smoother = smoothers[(size_t) i];
                // Once a ramp lands, use the parameter itself so settled coefficients stay bit-stable.
                const float frequency = smoother.logFrequency.isSmoothing()
                    ? std::exp (smoother.logFrequency.getCurrentValue())
                    : clampedFrequency (params[(size_t) i].frequency, currentSampleRate);
                const float q = smoother.logQ.isSmoothing()
                    ? std::exp (smoother.logQ.getCurrentValue())
                    : clampedQ (params[(size_t) i].q);
                const float gain = (smoother.gainDb.isSmoothing() ? smoother.gainDb.getCurrentValue()
                                                                  : params[(size_t) i].gainDb)
                                 + gainOffset[(size_t) i];

                if (designBandPair (i, params[(size_t) i], frequency, gain, q,
                                    smoother.splitTransDb.getCurrentValue(), smoother.splitTailDb.getCurrentValue()))
                    designUpdates.fetch_add (1, std::memory_order_relaxed);
            }

            processBands (buffer, offset + position, count, mixes, linearPhase, position);
            position += count;
        }
    }

    // The split streams now hold this segment after their band chains: the analyzer draws them as two layers.
    const bool showSplitSpectrum = splitEngaged && anySplitBands && ! linearPhase;
    splitSpectrumActive.store (showSplitSpectrum);
    if (showSplitSpectrum)
        pushSplitSpectrum (numSamples);

    applyWidthAndGain (buffer, offset, numSamples);
    applyWetTail (buffer, offset, numSamples, params, anySolo);

    if (fadeBypass)
    {
        const int channels = juce::jmax (1, juce::jmin (ioChannels, buffer.getNumChannels()));
        for (int i = 0; i < numSamples; ++i)
        {
            const float wet = bypassWet.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
            {
                const float dry = needsDelayedDry
                    ? readDelay (channel, (numSamples - 1 - i) + reportedLatency)
                    : dryInput.getReadPointer (channel)[i];
                float& sample = buffer.getWritePointer (channel)[offset + i];
                sample = dry + wet * (sample - dry);
            }
        }
    }
}

juce::uint64 OpenEQAudioProcessor::hashCurveRequest (const LinearPhaseEngine::CurveRequest& request) noexcept
{
    juce::uint64 hash = 1469598103934665603ULL;
    auto mix = [&hash] (juce::uint32 value)
    {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    auto mixFloat = [&mix] (float value)
    {
        juce::uint32 bits = 0;
        std::memcpy (&bits, &value, sizeof (bits));
        mix (bits);
    };

    mix ((juce::uint32) request.fftOrder);
    mix ((juce::uint32) request.target);
    mixFloat ((float) request.crossoverHz);
    mixFloat ((float) request.sampleRate);
    mix ((juce::uint32) request.count);
    mix (request.mono ? 1u : 0u);
    for (int i = 0; i < request.count; ++i)
    {
        const auto& band = request.bands[(size_t) i];
        mix ((juce::uint32) band.type);
        mix ((juce::uint32) band.placement);
        mixFloat (band.pan);
        mix ((juce::uint32) band.slopeDb);
        mix (band.audible ? 1u : 0u);
        mixFloat (band.frequency);
        mixFloat (band.gainDb);
        mixFloat (band.q);
    }
    return hash;
}

int OpenEQAudioProcessor::effectiveLinearPhaseQuality() const noexcept
{
    // While rendering offline, the Render Quality choice (Low..Maximum) overrides the realtime quality.
    if (nonRealtime && renderQualityParam != nullptr)
    {
        const int renderChoice = (int) renderQualityParam->load();
        if (renderChoice >= 1 && renderChoice <= 4)
            return juce::jlimit (0, 3, renderChoice - 1);
    }
    return linearPhaseQuality != nullptr ? (int) linearPhaseQuality->load() : 1;
}

void OpenEQAudioProcessor::applyRenderDsp() noexcept
{
    if (renderOversampleParam != nullptr && oversample2x != nullptr)
    {
        const int renderChoice = (int) renderOversampleParam->load();
        const bool realtimeOn = oversample2x->load() > 0.5f;
        const bool target = nonRealtime ? (renderChoice == 0 ? realtimeOn : renderChoice == 2)
                                        : realtimeOn;
        if (target != oversampling)
            setOversampling2x (target);
    }
}

void OpenEQAudioProcessor::setNonRealtime (bool isNonRealtime) noexcept
{
    juce::AudioProcessor::setNonRealtime (isNonRealtime);
    nonRealtime = isNonRealtime;
    // The effective quality is read at design time; force the kernel to rebuild with it.
    lastCurveHash = 0;
    curveSubmitPending = true;
    applyRenderDsp();
}

void OpenEQAudioProcessor::submitLinearPhaseRequest (const std::array<BandParameterState, numBands>& params, bool anySolo)
{
    const int mode = latched.mode;
    if (mode == 0)
        return;

    static constexpr double crossoverHz[] = { 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
    LinearPhaseEngine::CurveRequest request;
    request.target = mode == 2 ? LinearPhaseEngine::Target::Analog
                   : mode == 3 ? LinearPhaseEngine::Target::Mixed
                   : mode == 4 ? LinearPhaseEngine::Target::AnalogZeroLatency
                               : LinearPhaseEngine::Target::Linear;
    request.crossoverHz = crossoverHz[juce::jlimit (0, 4, mixCrossover != nullptr ? (int) mixCrossover->load() : 1)];
    request.sampleRate = currentSampleRate;
    request.fftOrder = latched.lpOrder;
    request.mono = getMainBusNumInputChannels() < 2;

    bool omitsDynamic = false;
    bool omitsAllPass = false;
    for (int i = 0; i < numBands && request.count < ResponseModel::graphBandCount; ++i)
    {
        const auto& band = params[(size_t) i];
        if (! isAudible (band, anySolo))
            continue;
        if (! ResponseModel::supportedInLinearPhase (band.type))
        {
            omitsAllPass = true;
            continue;
        }
        if (band.dynamic && ResponseModel::supportsDynamics (band.type))
        {
            omitsDynamic = true;
            continue;
        }

        auto& dest = request.bands[(size_t) request.count++];
        dest.type = band.type;
        dest.placement = band.stereoMode;
        dest.pan = band.pan;
        dest.frequency = clampedFrequency (band.frequency, currentSampleRate);
        dest.gainDb = band.gainDb;
        dest.q = clampedQ (band.q);
        dest.slopeDb = band.slopeDb;
        dest.phase = band.phase;
        dest.audible = true;
    }

    lpOmitsDynamics.store (omitsDynamic, std::memory_order_relaxed);
    lpOmitsAllPass.store (omitsAllPass, std::memory_order_relaxed);

    const auto hash = hashCurveRequest (request);
    if (hash == lastCurveHash && ! curveSubmitPending)
        return;

    if (linearPhaseEngine.trySubmit (request))
    {
        lastCurveHash = hash;
        curveSubmitPending = false;
    }
    else
    {
        curveSubmitPending = true;
    }
}

void OpenEQAudioProcessor::publishAppliedGraph (const std::array<BandParameterState, numBands>& params, bool anySolo)
{
    ResponseModel::GraphSnapshot snapshot;
    snapshot.sampleRate = currentSampleRate;

    for (int i = 0; i < numBands; ++i)
    {
        const auto& band = params[(size_t) i];
        auto& dest = snapshot.bands[(size_t) i];
        dest.commandedActive = band.active;
        dest.audible = isAudible (band, anySolo);
        dest.type = band.type;
        dest.stereoMode = band.stereoMode;
        dest.pan = band.pan;
        dest.splitOn = band.splitOn;
        dest.splitTransDb = band.splitTransDb;
        dest.splitTailDb = band.splitTailDb;
        dest.targetFrequency = band.frequency;
        dest.targetGainDb = band.gainDb;
        dest.appliedFrequency = bands[(size_t) i].getFrequency();
        dest.appliedGainDb = bands[(size_t) i].getGain();
        dest.appliedQ = bands[(size_t) i].getQ();
        dest.slopeDb = band.slopeDb;
        if (dest.audible)
        {
            bands[(size_t) i].exportStages (dest.stages, dest.stageCount);
            if (splitFlags[(size_t) i])
            {
                int tailCount = 0;
                splitTailBands[(size_t) i].exportStages (dest.tailStages, tailCount);
                if (tailCount != dest.stageCount)
                    dest.tailStages = dest.stages;
            }
            else
            {
                dest.tailStages = dest.stages;
            }
        }
    }

    if (! graphLock.tryEnter())
        return;

    publishedGraph = snapshot;
    graphLock.exit();
}

ResponseModel::GraphSnapshot OpenEQAudioProcessor::buildIdleGraph() const
{
    ResponseModel::GraphSnapshot snapshot;
    snapshot.sampleRate = currentSampleRate;

    std::array<BandParameterState, numBands> params;
    readAllBandParameters (params);
    bool anySolo = false;
    for (const auto& band : params)
        if (band.active && band.solo) { anySolo = true; break; }

    for (int i = 0; i < numBands; ++i)
    {
        const auto& band = params[(size_t) i];
        auto& dest = snapshot.bands[(size_t) i];
        dest.commandedActive = band.active;
        dest.audible = isAudible (band, anySolo);
        dest.type = band.type;
        dest.stereoMode = band.stereoMode;
        dest.pan = band.pan;
        dest.splitOn = band.splitOn;
        dest.splitTransDb = band.splitTransDb;
        dest.splitTailDb = band.splitTailDb;
        dest.targetFrequency = band.frequency;
        dest.targetGainDb = band.gainDb;
        dest.slopeDb = band.slopeDb;

        // A split band runs two designs, as the audio path does: the transient path and the tonal path.
        const bool split = band.splitOn && band.active;
        const auto designed = ResponseModel::designBand (band.type, band.frequency,
                                                           band.gainDb + (split ? band.splitTransDb : 0.0f),
                                                           band.q, band.slopeDb, currentSampleRate);
        dest.appliedFrequency = designed.frequency;
        dest.appliedGainDb = designed.gainDb;
        dest.appliedQ = designed.q;
        if (! dest.audible)
            continue;

        dest.stageCount = designed.stageCount;
        for (int stage = 0; stage < designed.stageCount; ++stage)
            dest.stages[(size_t) stage] = ResponseModel::normalizeBiquad (designed.raw[(size_t) stage]);
        if (split)
        {
            const auto tail = ResponseModel::designBand (band.type, band.frequency, band.gainDb + band.splitTailDb,
                                                           band.q, band.slopeDb, currentSampleRate);
            for (int stage = 0; stage < designed.stageCount; ++stage)
                dest.tailStages[(size_t) stage] = ResponseModel::normalizeBiquad (
                    tail.raw[(size_t) juce::jmin (stage, juce::jmax (0, tail.stageCount - 1))]);
        }
    }

    return snapshot;
}

ResponseModel::GraphSnapshot OpenEQAudioProcessor::getGraphSnapshot() const
{
    const juce::ScopedLock sl (graphLock);
    return publishedGraph;
}

void OpenEQAudioProcessor::serviceIdleGraph()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto last = lastAudioMs.load();
    if (last != 0 && (juce::uint32) (now - last) < audioIdleMs)
        return;

    auto snapshot = buildIdleGraph();
    const juce::ScopedLock sl (graphLock);
    const auto lastAfter = lastAudioMs.load();
    if (lastAfter != 0 && (juce::uint32) (juce::Time::getMillisecondCounter() - lastAfter) < audioIdleMs)
        return;

    publishedGraph = snapshot;
}

float OpenEQAudioProcessor::getDynamicEnvelopeDb (int index) const noexcept
{
    if (index < 0 || index >= numBands)
        return -100.0f;
    return dynamics[(size_t) index].getEnvelopeDb();
}

float OpenEQAudioProcessor::getDynamicGainDb (int index) const noexcept
{
    if (index < 0 || index >= numBands)
        return 0.0f;
    return appliedGainDb[(size_t) index].load();
}

float OpenEQAudioProcessor::getDynamicThresholdDb (int index) const noexcept
{
    if (index < 0 || index >= numBands)
        return -24.0f;
    return appliedThresholdDb[(size_t) index].load();
}

void OpenEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (! prepared || numSamples <= 0 || buffer.getNumChannels() <= 0)
        return;

    ioChannels = juce::jmax (1, juce::jmin (buffer.getNumChannels(), juce::jmax (1, getMainBusNumInputChannels())));
    sidechainLeft = nullptr;
    sidechainRight = nullptr;

    for (int channel = 0; channel < ioChannels; ++channel)
        zeroNonFinite (buffer.getWritePointer (channel), numSamples);

    // Input section: the meter reads the host signal before the trim, the trim then
    // feeds the analyzer and the rest of the chain.
    updateInputMeters (buffer, numSamples);
    inputGainDb.setTargetValue (inputGain->load());
    if (inputGainDb.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float gain = juce::Decibels::decibelsToGain (inputGainDb.getNextValue());
            for (int channel = 0; channel < ioChannels; ++channel)
                buffer.getWritePointer (channel)[i] *= gain;
        }
    }
    else
    {
        const float gain = juce::Decibels::decibelsToGain (inputGainDb.getCurrentValue());
        if (std::abs (gain - 1.0f) > 1.0e-6f)
            for (int channel = 0; channel < ioChannels; ++channel)
                buffer.applyGain (channel, 0, numSamples, gain);
    }

    if (getBusCount (true) > 1)
    {
        const int sideChannels = getChannelCountOfBus (true, 1);
        if (sideChannels > 0)
        {
            const int sideIndex = getChannelIndexInProcessBlockBuffer (true, 1, 0);
            if (sideIndex >= 0 && sideIndex < buffer.getNumChannels())
                sidechainLeft = buffer.getReadPointer (sideIndex);
            if (sideChannels > 1 && sideIndex + 1 < buffer.getNumChannels())
                sidechainRight = buffer.getReadPointer (sideIndex + 1);
        }
    }

    // The analyzer sees the host-rate signal, before any oversampling.
    lastAudioMs.store (juce::Time::getMillisecondCounter());
    pushToAnalyzer (preL, preR, buffer, numSamples);
    if (sidechainLeft != nullptr)
    {
        sideL.pushSamples (sidechainLeft, numSamples);
        sideR.pushSamples (sidechainRight != nullptr ? sidechainRight : sidechainLeft, numSamples);
    }

    if (! oversampling || oversampler == nullptr)
    {
        processCore (buffer, numSamples, sidechainLeft, sidechainRight);
    }
    else
    {
        // Upsample each host chunk, run the core at the internal rate, and downsample back into the host buffer.
        constexpr int hostChunk = 512;
        const int channels = ioChannels;

        for (int offset = 0; offset < numSamples; offset += hostChunk)
        {
            const int count = juce::jmin (hostChunk, numSamples - offset);

            float* hostPointers[2] = { buffer.getWritePointer (0) + offset,
                                       channels > 1 ? buffer.getWritePointer (1) + offset : buffer.getWritePointer (0) + offset };
            juce::dsp::AudioBlock<float> hostBlock (hostPointers, (size_t) channels, (size_t) count);
            auto up = oversampler->processSamplesUp (hostBlock);
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::copy (oversampledMain.getWritePointer (channel),
                                                   up.getChannelPointer ((size_t) juce::jmin (channel, channels - 1)),
                                                   2 * count);

            const float* scL = nullptr;
            const float* scR = nullptr;
            if (sidechainLeft != nullptr && sidechainOversampler != nullptr)
            {
                const float* sourceL = sidechainLeft + offset;
                const float* sourceR = sidechainRight != nullptr ? sidechainRight + offset : sourceL;
                juce::FloatVectorOperations::copy (sidechainBase.getWritePointer (0), sourceL, count);
                juce::FloatVectorOperations::copy (sidechainBase.getWritePointer (1), sourceR, count);
                float* scPointers[2] = { sidechainBase.getWritePointer (0), sidechainBase.getWritePointer (1) };
                juce::dsp::AudioBlock<float> scBlock (scPointers, 2, (size_t) count);
                auto scUp = sidechainOversampler->processSamplesUp (scBlock);
                for (int channel = 0; channel < 2; ++channel)
                    juce::FloatVectorOperations::copy (oversampledSidechain.getWritePointer (channel),
                                                       scUp.getChannelPointer ((size_t) channel), 2 * count);
                scL = oversampledSidechain.getReadPointer (0);
                scR = oversampledSidechain.getReadPointer (1);
            }

            processCore (oversampledMain, 2 * count, scL, scR);

            for (int channel = 0; channel < channels; ++channel)
                juce::FloatVectorOperations::copy (up.getChannelPointer ((size_t) channel),
                                                   oversampledMain.getReadPointer (channel), 2 * count);
            oversampler->processSamplesDown (hostBlock);
        }
    }

    // Host-safety net: a host must never receive a non-finite sample. Absurd but legal
    // parameter combinations (for example 32 overlapping +30 dB bells) can overflow the
    // float output even though the biquads run in double. Zero the block and recover the
    // filter state instead of letting NaN or inf escape into the host.
    bool recoveredNonFinite = false;
    for (int channel = 0; channel < ioChannels; ++channel)
        recoveredNonFinite = zeroNonFinite (buffer.getWritePointer (channel), numSamples) || recoveredNonFinite;
    if (recoveredNonFinite)
    {
        for (auto& band : bands)
            band.resetState();
        for (auto& band : splitTailBands)
            band.resetState();
        auditionBand.resetState();
        nonFiniteRecoveries.fetch_add (1);
    }

    updateMeters (buffer, numSamples);
    pushToAnalyzer (postL, postR, buffer, numSamples);
}

void OpenEQAudioProcessor::processCore (juce::AudioBuffer<float>& buffer, int numSamples, const float* scLeft, const float* scRight)
{
    sidechainLeft = scLeft;
    sidechainRight = scRight;

    std::array<BandParameterState, numBands> params;
    readAllBandParameters (params);
    bool anySolo = false;
    for (const auto& band : params)
        if (band.active && band.solo) { anySolo = true; break; }

    setSmootherTargets (params, anySolo);

    // 117-bit precision applies to every band's recursion. Changing it resets band state.
    const bool ultra = ultraPrecision != nullptr && ultraPrecision->load() > 0.5f;
    for (auto& band : bands)
        band.setUltraPrecision (ultra);
    for (auto& band : splitTailBands)
        band.setUltraPrecision (ultra);

    // Anything that changes the latency is held in `latched`. A change waits until the output has faded down,
    // is applied in silence, and the output fades back up once the new delay has passed.
    updateSplitFlags (params);
    const auto wanted = readLatencyConfig (params, anySolo);
    bool latchedNow = false;
    const bool wasPrimed = latencyPrimed;
    if (! latencyPrimed)
    {
        // First block after prepare: nothing is playing yet, so there is nothing to fade.
        latencyPrimed = true;
        latchLatencyConfig (wanted);
    }
    else if (! wanted.sameLatencyAs (latched))
    {
        if (! latencySwitchPending)
        {
            latencySwitchPending = true;
        }
        else if (latencyFadeGain <= 0.0f)
        {
            latchLatencyConfig (wanted);
            latencySwitchPending = false;
            latchedNow = true;
        }
    }
    else
    {
        // Same latency: the linear-phase modes that share a delay switch through the kernel crossfade.
        latencySwitchPending = false;
        latched = wanted;
    }

    const int mode = latched.mode;
    // Every mode except Zero Latency runs through the linear-phase kernel. Analog, Mix and Analog ZL change only the kernel's target.
    const bool linearPhase = mode != 0;
    if (linearPhase)
        linearPhaseEngine.setExpectedOrder (latched.lpOrder);

    // A mode change asks the engine for a kernel with a new target. The engine's delay follows the target of the
    // last request it accepted (Analog ZL has none), so stay silent until that request is in. The kernel itself
    // arrives a little later, through the engine's own crossfade.
    bool holdSilent = false;
    if (latchedNow && linearPhase)
    {
        linearPhaseEngine.reset();
        kernelWaitSamples = (int) (0.5 * currentSampleRate);
    }
    if (kernelWaitSamples > 0)
    {
        const int expectedTarget = mode == 2 ? (int) LinearPhaseEngine::Target::Analog
                                 : mode == 3 ? (int) LinearPhaseEngine::Target::Mixed
                                 : mode == 4 ? (int) LinearPhaseEngine::Target::AnalogZeroLatency
                                             : (int) LinearPhaseEngine::Target::Linear;
        kernelWaitSamples -= numSamples;
        if (! linearPhase || kernelWaitSamples <= 0)
        {
            kernelWaitSamples = 0;
        }
        else if (linearPhaseEngine.getPublishedTarget() == expectedTarget)
        {
            // Take the new kernel at once, with no handoff from the old one: nothing is audible to hand off.
            if (! latchedNow)
                linearPhaseEngine.reset();
            kernelWaitSamples = 0;
            latchedNow = true;
        }
        else
        {
            holdSilent = true;
        }
    }

    // Digital+ steps apply to the zero-latency biquads; re-design on change.
    const int digitalPlus = digitalPlusParam != nullptr ? (int) digitalPlusParam->load() : 0;
    for (auto& band : bands)
        band.setDigitalPlusSteps (digitalPlus);
    for (auto& band : splitTailBands)
        band.setDigitalPlusSteps (digitalPlus);

    // Split: while the split path is in, the audio is separated into a transient stream and a tonal stream.
    // The separation needs to see ahead, so it adds latency. The linear-phase modes do not use it.
    splitDelaySamples = splitEngaged ? splitSeparator.getLatencySamples() : 0;
    lpOmitsSplit.store (anySplitBands && linearPhase);

    // Impulse padding changes the kernel truncation; force a redesign on change.
    const int impulsePadding = impulsePaddingParam != nullptr ? (int) impulsePaddingParam->load() : 1;
    if (impulsePadding != lastImpulsePadding)
    {
        lastImpulsePadding = impulsePadding;
        linearPhaseEngine.setImpulsePadding (impulsePadding);
        lastCurveHash = 0;
    }

    // Window shape and parameter rebuild the FIR windows and force a kernel redesign.
    const int windowShape = windowShapeParam != nullptr ? (int) windowShapeParam->load() : 0;
    const float windowParameter = windowParameterParam != nullptr ? windowParameterParam->load() : 0.0f;
    if (windowShape != lastWindowShape || windowParameter != lastWindowParameter)
    {
        lastWindowShape = windowShape;
        lastWindowParameter = windowParameter;
        linearPhaseEngine.setWindowSettings (windowShape, windowParameter);
        lastCurveHash = 0; // force a resubmit so the kernel is rebuilt with the new window
    }
    submitLinearPhaseRequest (params, anySolo);

    lookaheadSamples = latched.lookahead;
    int latency = lookaheadSamples + splitDelaySamples;
    if (linearPhase)
        latency += linearPhaseEngine.getLatencySamples();
    if (latched.drive)
        latency += saturator.getLatencySamples();
    if (latched.spectral)
        latency += spectral.getLatencySamples();

    if (holdSilent)
    {
        latencyFadeGain = 0.0f;
        latencyMuteRemaining = juce::jmax (latencyMuteRemaining, numSamples + 1);
    }
    else if (latchedNow)
    {
        // The new path has nothing to play until its delay has passed. Stay silent for that long, then fade in.
        latencyMuteRemaining = latency;
    }
    else if (wasPrimed && latency != reportedLatency)
    {
        // The linear-phase engine changed its own delay (a kernel for a new target arrived). There was no
        // warning to fade down on, so cut to silence and fade back in.
        latencyFadeGain = 0.0f;
        latencyMuteRemaining = latency;
    }

    // Internal samples drive the processing. The host sees the same delay in host samples.
    reportedLatency = latency;
    const int baseLatency = oversampling
        ? (int) std::ceil (oversampler->getLatencyInSamples()) + (latency + 1) / 2
        : latency;
    if (baseLatency != reportedLatencyBase)
    {
        reportedLatencyBase = baseLatency;
        setLatencySamples (baseLatency);
    }

    for (int offset = 0; offset < numSamples;)
    {
        const int count = juce::jmin (maxProcessingChunk, numSamples - offset);
        const bool hearDetector = detectorAuditionIndex.load() >= 0;
        if (hearDetector)
        {
            const int channels = juce::jmin (2, buffer.getNumChannels(), auditionCapture.getNumChannels());
            for (int channel = 0; channel < channels; ++channel)
                auditionCapture.copyFrom (channel, 0, buffer, channel, offset, count);
        }
        processSegment (buffer, offset, count, params, anySolo, linearPhase);
        if (hearDetector)
            applyDetectorAudition (buffer, offset, count, params);
        offset += count;
    }

    if (detectorAuditionIndex.load() < 0)
        applyAudition (buffer, numSamples, params);
    applyMonitor (buffer, numSamples);
    applyLatencyTransition (buffer, numSamples);
    publishAppliedGraph (params, anySolo);
    sidechainLeft = nullptr;
    sidechainRight = nullptr;
}

void OpenEQAudioProcessor::setOversampling2x (bool enabled)
{
    if (auto* parameter = apvts.getParameter ("oversample2x"))
        parameter->setValueNotifyingHost (enabled ? 1.0f : 0.0f);

    // Re-preparing changes the internal rate and the delay lines, so the host must not process meanwhile.
    suspendProcessing (true);
    prepareToPlay (baseSampleRate, preparedBlockSize);
    suspendProcessing (false);
}

void OpenEQAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin (buffer.getNumChannels(), floatFromDouble.getNumChannels(), 2);
    if (! prepared || numSamples <= 0 || channels <= 0)
        return;

    // Conversion path: the biquad engine stays single precision. Samples are
    // copied through the prepared float scratch in chunks of maxProcessingChunk.
    for (int offset = 0; offset < numSamples;)
    {
        const int count = juce::jmin (maxProcessingChunk, numSamples - offset);
        floatFromDouble.setSize (channels, count, false, false, true);
        for (int channel = 0; channel < channels; ++channel)
        {
            const double* source = buffer.getReadPointer (channel) + offset;
            float* dest = floatFromDouble.getWritePointer (channel);
            for (int i = 0; i < count; ++i)
            {
                const double sample = source[i];
                dest[i] = std::isfinite (sample) ? (float) sample : 0.0f;
            }
        }

        processBlock (floatFromDouble, midi);

        for (int channel = 0; channel < channels; ++channel)
        {
            double* dest = buffer.getWritePointer (channel) + offset;
            const float* source = floatFromDouble.getReadPointer (channel);
            for (int i = 0; i < count; ++i)
                dest[i] = (double) source[i];
        }
        offset += count;
    }

    if (floatFromDouble.getNumChannels() != 2 || floatFromDouble.getNumSamples() != maxProcessingChunk)
        floatFromDouble.setSize (2, maxProcessingChunk, false, false, true);
}

void OpenEQAudioProcessor::pushToAnalyzer (AnalyzerRing& ringL, AnalyzerRing& ringR,
                                           const juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int channels = juce::jmax (1, juce::jmin (ioChannels, buffer.getNumChannels()));
    if (channels <= 1)
    {
        ringL.pushSamples (buffer.getReadPointer (0), numSamples);
        ringR.pushSamples (buffer.getReadPointer (0), numSamples);
        return;
    }

    ringL.pushSamples (buffer.getReadPointer (0), numSamples);
    ringR.pushSamples (buffer.getReadPointer (1), numSamples);
}

juce::AudioProcessorEditor* OpenEQAudioProcessor::createEditor()
{
    // Components copy the theme colours when they are built, so the theme is applied before the editor exists.
    Theme::applyTheme (AppSettings::get().themeIndex());
    if (const auto path = AppSettings::get().customThemeFile(); path.isNotEmpty())
        ThemeFile::load (juce::File (path));
    return new OpenEQAudioProcessorEditor (*this);
}

void OpenEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    state.setProperty ("schema", 3, nullptr);
    state.setProperty ("abSlot", abSlot, nullptr);
    if (auto otherXml = abSlots[1 - abSlot].createXml())
        state.setProperty ("abOther", otherXml->toString(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

/** True when the saved tree carries a parameter child, whatever its value. */
static bool stateContainsParameter (const juce::ValueTree& state, const juce::String& parameterID)
{
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        if (child.getProperty ("id").toString() == parameterID || child.getType().toString() == parameterID)
            return true;
    }
    return false;
}

/** Saved sessions from before the Below/Above panels have no panel children at all. */
static bool stateHasDynamicsPanels (const juce::ValueTree& state)
{
    for (int i = 0; i < Params::numBands; ++i)
        if (stateContainsParameter (state, Params::bandDynBelowOnID (i))
            || stateContainsParameter (state, Params::bandDynAboveOnID (i)))
            return true;
    return false;
}

void OpenEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const int schema = (int) tree.getProperty ("schema", 1);
    // Presence of the panel parameters is the migration test, not the live panel switches: a current
    // state may legitimately have both panels off, and must then keep the legacy single-action values out.
    const bool hasDynamicsPanels = stateHasDynamicsPanels (tree);
    const int slot = juce::jlimit (0, 1, (int) tree.getProperty ("abSlot", 0));
    const auto otherText = tree.getProperty ("abOther").toString();
    tree.removeProperty ("schema", nullptr);
    tree.removeProperty ("abSlot", nullptr);
    tree.removeProperty ("abOther", nullptr);
    apvts.replaceState (tree);
    abSlot = slot;
    abSlots[abSlot] = apvts.copyState();
    if (otherText.isNotEmpty())
        if (auto otherXml = juce::parseXML (otherText))
            abSlots[1 - abSlot] = juce::ValueTree::fromXml (*otherXml);
    if (! abSlots[1 - abSlot].isValid())
        abSlots[1 - abSlot] = abSlots[abSlot].createCopy();
    refreshOtherSlotSplit();

    if (schema < 2)
    {
        for (int i = 0; i < numBands; ++i)
        {
            const float defaultFreq = juce::jlimit (20.0f, 20000.0f, 60.0f * std::pow (1.75f, (float) i));
            const bool active = apvts.getRawParameterValue (Params::bandActiveID (i))->load() > 0.5f;
            const bool nonDefault = active
                || std::abs (apvts.getRawParameterValue (Params::bandFreqID (i))->load() - defaultFreq) > 0.5f
                || std::abs (apvts.getRawParameterValue (Params::bandGainID (i))->load()) > 0.01f
                || (int) apvts.getRawParameterValue (Params::bandTypeID (i))->load() != 0
                || std::abs (apvts.getRawParameterValue (Params::bandQID (i))->load() - 1.0f) > 0.001f
                || (int) apvts.getRawParameterValue (Params::bandSlopeID (i))->load() != 1
                || (int) apvts.getRawParameterValue (Params::bandStereoID (i))->load() != 0
                || apvts.getRawParameterValue (Params::bandSoloID (i))->load() > 0.5f
                || apvts.getRawParameterValue (Params::bandDynOnID (i))->load() > 0.5f;
            if (nonDefault)
                if (auto* shown = apvts.getParameter (Params::bandShownID (i)))
                    shown->setValueNotifyingHost (1.0f);
        }
    }

    if (! hasDynamicsPanels)
        migrateLegacyDynamics();
    if (schema < 3)
        migrateLegacySlopes();

    monitorMode.store (0);
    auditionBandIndex.store (-1);
    detectorAuditionIndex.store (-1);
}

void OpenEQAudioProcessor::ensureCutSlope (int bandIndex)
{
    if (bandIndex < 0 || bandIndex >= numBands)
        return;
    const auto type = (FilterType) juce::jlimit (0, filterTypeCount - 1, (int) apvts.getRawParameterValue (Params::bandTypeID (bandIndex))->load());
    const bool orderFromZero = type == FilterType::LowCut || type == FilterType::HighCut || type == FilterType::BandPass;
    if (! orderFromZero || apvts.getRawParameterValue (Params::bandSlopeNormID (bandIndex))->load() > 0.01f)
        return;
    if (auto* slope = apvts.getParameter (Params::bandSlopeNormID (bandIndex)))
        slope->setValueNotifyingHost (slope->convertTo0to1 (0.5f));
}

void OpenEQAudioProcessor::migrateLegacySlopes()
{
    // Sessions saved before schema 3 had a 7-step slope menu that only the cuts used. The continuous slope replaces it:
    // a cut keeps its dB per octave, and every other type starts at slope 0 (a second-order bell, a first-order shelf).
    for (int i = 0; i < numBands; ++i)
    {
        auto* slopeNorm = apvts.getParameter (Params::bandSlopeNormID (i));
        if (slopeNorm == nullptr)
            continue;
        const auto type = (FilterType) juce::jlimit (0, filterTypeCount - 1, (int) apvts.getRawParameterValue (Params::bandTypeID (i))->load());
        float value = 0.0f;
        if (type == FilterType::LowCut || type == FilterType::HighCut)
            value = Params::slopeNormForCutDb ((float) Params::slopeIndexToDb ((int) apvts.getRawParameterValue (Params::bandSlopeID (i))->load()));
        slopeNorm->setValueNotifyingHost (slopeNorm->convertTo0to1 (value));
    }
}

void OpenEQAudioProcessor::migrateLegacyDynamics()
{
    // Sessions saved before the Below and Above panels used one action with one range. Moving that into the
    // matching panel gives the same offsets, because the reference rule with a 100% ratio is the old limit.
    for (int i = 0; i < numBands; ++i)
    {
        const bool dynamic = apvts.getRawParameterValue (Params::bandDynOnID (i))->load() > 0.5f;
        const bool hasPanel = apvts.getRawParameterValue (Params::bandDynBelowOnID (i))->load() > 0.5f
                           || apvts.getRawParameterValue (Params::bandDynAboveOnID (i))->load() > 0.5f;
        if (! dynamic || hasPanel)
            continue;

        const float range = apvts.getRawParameterValue (Params::bandDynRangeID (i))->load();
        const int action = (int) apvts.getRawParameterValue (Params::bandDynActionID (i))->load();
        const bool above = action == 0;
        setRawFromUI (above ? Params::bandDynAboveOnID (i) : Params::bandDynBelowOnID (i), 1.0f);
        setRawFromUI (above ? Params::bandDynAboveRangeID (i) : Params::bandDynBelowRangeID (i), range);
        setRawFromUI (above ? Params::bandDynAboveRatioID (i) : Params::bandDynBelowRatioID (i), 100.0f);
    }
}

struct OpenEQParameterSetAction : public juce::UndoableAction
{
    OpenEQParameterSetAction (OpenEQAudioProcessor& owner, juce::String parameter, float fromNormalised, float toNormalised)
        : processor (owner), parameterID (std::move (parameter)), fromValue (fromNormalised), toValue (toNormalised) {}

    bool perform() override
    {
        if (alreadyApplied)
        {
            alreadyApplied = false;
            return true;
        }
        processor.applyNormalised (parameterID, toValue);
        return true;
    }

    bool undo() override
    {
        processor.applyNormalised (parameterID, fromValue);
        return true;
    }

    int getSizeInUnits() override { return 1; }

    OpenEQAudioProcessor& processor;
    juce::String parameterID;
    float fromValue = 0.0f;
    float toValue = 0.0f;
    bool alreadyApplied = true;
};

void OpenEQAudioProcessor::applyNormalised (const juce::String& parameterID, float normalised)
{
    if (auto* parameter = apvts.getParameter (parameterID))
        parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
}

void OpenEQAudioProcessor::beginEdit (const juce::String& name)
{
    if (gestureOpen)
        endEdit();
    gestureName = name;
    gestureOpen = true;
    gestureChanges.clear();
    // Each gesture is its own undo step. Without this, every edit since the start shares one transaction.
    undoManager.beginNewTransaction (name);
}

void OpenEQAudioProcessor::setRawFromUI (const juce::String& parameterID, float rawValue)
{
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (parameterID));
    if (parameter == nullptr)
        return;

    const float normalised = parameter->convertTo0to1 (rawValue);
    if (! gestureOpen || applyingUndo)
    {
        applyNormalised (parameterID, normalised);
        return;
    }

    for (auto& change : gestureChanges)
    {
        if (change.id == parameterID)
        {
            change.end = normalised;
            applyNormalised (parameterID, normalised);
            return;
        }
    }

    GestureChange change;
    change.id = parameterID;
    change.start = parameter->getValue();
    change.end = normalised;
    if (std::abs (change.start - change.end) > 1.0e-7f)
    {
        parameter->beginChangeGesture();
        change.beganHostGesture = true;
    }
    gestureChanges.push_back (change);
    applyNormalised (parameterID, normalised);
}

void OpenEQAudioProcessor::endEdit()
{
    if (! gestureOpen)
        return;
    gestureOpen = false;
    for (auto& change : gestureChanges)
    {
        if (change.beganHostGesture)
            if (auto* parameter = apvts.getParameter (change.id))
                parameter->endChangeGesture();
        if (std::abs (change.start - change.end) > 1.0e-7f)
            undoManager.perform (new OpenEQParameterSetAction (*this, change.id, change.start, change.end), gestureName);
    }
    gestureChanges.clear();
}

bool OpenEQAudioProcessor::undo()
{
    const juce::ScopedValueSetter<bool> guard (applyingUndo, true);
    return undoManager.undo();
}

bool OpenEQAudioProcessor::redo()
{
    const juce::ScopedValueSetter<bool> guard (applyingUndo, true);
    return undoManager.redo();
}

void OpenEQAudioProcessor::switchAB()
{
    abSlots[abSlot] = apvts.copyState();
    abSlot = 1 - abSlot;
    auto next = abSlots[abSlot].createCopy();
    if (next.isValid())
        apvts.replaceState (next);
    refreshOtherSlotSplit();
}

void OpenEQAudioProcessor::copyABToOther()
{
    abSlots[abSlot] = apvts.copyState();
    abSlots[1 - abSlot] = abSlots[abSlot].createCopy();
    refreshOtherSlotSplit();
}

juce::File OpenEQAudioProcessor::userPresetDirectory() const
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("OpenEQ")
                         .getChildFile ("Presets");
    directory.createDirectory();
    // One-time migration from the pre-rename folder (GodEQ): move presets and the saved default over.
    const auto legacy = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("GodEQ");
    if (legacy.isDirectory())
    {
        if (directory.findChildFiles (juce::File::findFiles, false).isEmpty())
            for (const auto& legacyPreset : juce::Array<juce::File> (legacy.getChildFile ("Presets").findChildFiles (juce::File::findFiles, false)))
                legacyPreset.copyFileTo (directory.getChildFile (legacyPreset.getFileName()).getFullPathName());
        const auto legacyDefault = legacy.getChildFile ("default.godeq");
        if (legacyDefault.existsAsFile() && ! defaultPresetFile().existsAsFile())
            legacyDefault.copyFileTo (defaultPresetFile());
    }
    return directory;
}

juce::Array<juce::File> OpenEQAudioProcessor::listUserPresets() const
{
    juce::Array<juce::File> files;
    userPresetDirectory().findChildFiles (files, juce::File::findFiles, false, "*.opeq");
    userPresetDirectory().findChildFiles (files, juce::File::findFiles, false, "*.godeq");
    files.sort();
    return files;
}

juce::File OpenEQAudioProcessor::defaultPresetFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenEQ")
               .getChildFile ("default.opeq");
}

bool OpenEQAudioProcessor::saveAsDefault()
{
    const auto file = defaultPresetFile();
    file.getParentDirectory().createDirectory();
    return savePresetFile (file);
}

bool OpenEQAudioProcessor::savePresetFile (const juce::File& file)
{
    auto state = apvts.copyState();
    state.setProperty ("schema", 3, nullptr);
    if (auto xml = state.createXml())
        return xml->writeTo (file);
    return false;
}

bool OpenEQAudioProcessor::loadPresetFile (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            juce::MemoryBlock block;
            copyXmlToBinary (*xml, block);
            setStateInformation (block.getData(), (int) block.getSize());
            return true;
        }
    }

    juce::MemoryBlock block;
    if (! file.loadFileAsData (block))
        return false;
    setStateInformation (block.getData(), (int) block.getSize());
    return true;
}

void OpenEQAudioProcessor::loadFactoryPreset (int index)
{
    beginEdit ("Factory Preset");
    for (auto* parameter : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr)
            continue;
        setRawFromUI (ranged->getParameterID(), ranged->convertFrom0to1 (ranged->getDefaultValue()));
    }

    if (index == 1)
    {
        setRawFromUI (Params::bandTypeID (0), (float) FilterType::TiltShelf);
        setRawFromUI (Params::bandFreqID (0), 800.0f);
        setRawFromUI (Params::bandGainID (0), 4.0f);
        setRawFromUI (Params::bandActiveID (0), 1.0f);
        setRawFromUI (Params::bandShownID (0), 1.0f);
    }
    else if (index == 2)
    {
        setRawFromUI (Params::bandTypeID (0), (float) FilterType::Bell);
        setRawFromUI (Params::bandFreqID (0), 3500.0f);
        setRawFromUI (Params::bandGainID (0), 3.0f);
        setRawFromUI (Params::bandQID (0), 0.8f);
        setRawFromUI (Params::bandActiveID (0), 1.0f);
        setRawFromUI (Params::bandShownID (0), 1.0f);
    }
    endEdit();
}

bool OpenEQAudioProcessor::isBandUsed (int index) const
{
    if (index < 0 || index >= numBands)
        return false;
    const auto& cached = cachedBands[(size_t) index];
    return cached.active->load() > 0.5f || cached.shown->load() > 0.5f;
}

int OpenEQAudioProcessor::findFreeBandSlot() const
{
    for (int i = 0; i < numBands; ++i)
        if (! isBandUsed (i))
            return i;
    return -1;
}

std::vector<float> OpenEQAudioProcessor::captureBand (int index) const
{
    std::vector<float> values;
    if (index < 0 || index >= numBands)
        return values;
    for (const auto& id : Params::bandParameterIDs (index))
    {
        auto* value = apvts.getRawParameterValue (id);
        values.push_back (value != nullptr ? value->load() : 0.0f);
    }
    return values;
}

void OpenEQAudioProcessor::applyBandValues (int index, const std::vector<float>& values, bool invertGain)
{
    const auto ids = Params::bandParameterIDs (index);
    if (index < 0 || index >= numBands || (int) values.size() != ids.size())
        return;

    const auto gainID = Params::bandGainID (index);
    for (int k = 0; k < ids.size(); ++k)
    {
        float value = values[(size_t) k];
        if (invertGain && ids[k] == gainID)
            value = -value;
        setRawFromUI (ids[k], value);
    }
}

int OpenEQAudioProcessor::duplicateBand (int index, bool invertGain)
{
    const int slot = findFreeBandSlot();
    if (! isBandUsed (index) || slot < 0)
        return -1;

    const auto values = captureBand (index);
    beginEdit ("Duplicate Band");
    applyBandValues (slot, values, invertGain);
    setRawFromUI (Params::bandActiveID (slot), 1.0f);
    setRawFromUI (Params::bandShownID (slot), 1.0f);
    endEdit();
    return slot;
}

int OpenEQAudioProcessor::splitBand (int index)
{
    if (! isBandUsed (index))
        return -1;

    // Left and Right split into each other, and Mid and Side too. Stereo has no opposite.
    const auto stereo = (StereoMode) juce::jlimit (0, 4, (int) apvts.getRawParameterValue (Params::bandStereoID (index))->load());
    StereoMode opposite = StereoMode::Stereo;
    switch (stereo)
    {
        case StereoMode::Left:  opposite = StereoMode::Right; break;
        case StereoMode::Right: opposite = StereoMode::Left;  break;
        case StereoMode::Mid:   opposite = StereoMode::Side;  break;
        case StereoMode::Side:  opposite = StereoMode::Mid;   break;
        case StereoMode::Stereo:
        default:
            return -1;
    }

    const int slot = findFreeBandSlot();
    if (slot < 0)
        return -1;

    const auto values = captureBand (index);
    beginEdit ("Split Band");
    applyBandValues (slot, values, false);
    setRawFromUI (Params::bandStereoID (slot), (float) (int) opposite);
    setRawFromUI (Params::bandActiveID (slot), 1.0f);
    setRawFromUI (Params::bandShownID (slot), 1.0f);
    endEdit();
    return slot;
}

void OpenEQAudioProcessor::removeBands (const std::vector<int>& indices)
{
    beginEdit ("Delete Band");
    for (int index : indices)
    {
        if (index < 0 || index >= numBands)
            continue;
        setRawFromUI (Params::bandShownID (index), 0.0f);
        setRawFromUI (Params::bandActiveID (index), 0.0f);
        setRawFromUI (Params::bandGainID (index), 0.0f);
    }
    endEdit();
}

void OpenEQAudioProcessor::clearAllBands()
{
    beginEdit ("Clear All Bands");
    for (int i = 0; i < numBands; ++i)
    {
        setRawFromUI (Params::bandShownID (i), 0.0f);
        setRawFromUI (Params::bandActiveID (i), 0.0f);
        setRawFromUI (Params::bandSoloID (i), 0.0f);
    }
    endEdit();
}

void OpenEQAudioProcessor::swapBands (int a, int b)
{
    if (a == b || a < 0 || b < 0 || a >= numBands || b >= numBands)
        return;
    const auto valuesA = captureBand (a);
    const auto valuesB = captureBand (b);
    beginEdit ("Move Band");
    applyBandValues (a, valuesB, false);
    applyBandValues (b, valuesA, false);
    endEdit();
}

void OpenEQAudioProcessor::sortBands (bool byType)
{
    struct Entry
    {
        std::vector<float> values;
        int type = 0;
        float frequency = 0.0f;
    };

    std::vector<Entry> entries;
    for (int i = 0; i < numBands; ++i)
    {
        if (! isBandUsed (i))
            continue;
        Entry entry;
        entry.values = captureBand (i);
        entry.type = (int) apvts.getRawParameterValue (Params::bandTypeID (i))->load();
        entry.frequency = apvts.getRawParameterValue (Params::bandFreqID (i))->load();
        entries.push_back (std::move (entry));
    }

    std::stable_sort (entries.begin(), entries.end(), [byType] (const Entry& x, const Entry& y)
    {
        if (byType && x.type != y.type)
            return x.type < y.type;
        return x.frequency < y.frequency;
    });

    beginEdit (byType ? "Sort Bands by Type" : "Sort Bands by Frequency");
    for (int i = 0; i < numBands; ++i)
    {
        if ((size_t) i < entries.size())
        {
            applyBandValues (i, entries[(size_t) i].values, false);
            setRawFromUI (Params::bandActiveID (i), 1.0f);
            setRawFromUI (Params::bandShownID (i), 1.0f);
        }
        else
        {
            setRawFromUI (Params::bandActiveID (i), 0.0f);
            setRawFromUI (Params::bandShownID (i), 0.0f);
        }
    }
    endEdit();
}

void OpenEQAudioProcessor::copyBandToClipboard (int index)
{
    if (index >= 0 && index < numBands)
        bandClipboard = captureBand (index);
}

int OpenEQAudioProcessor::pasteBandClipboard (int index, bool invertGain)
{
    if (bandClipboard.empty())
        return -1;

    const int slot = isBandUsed (index) ? index : findFreeBandSlot();
    if (slot < 0)
        return -1;

    beginEdit ("Paste Band");
    applyBandValues (slot, bandClipboard, invertGain);
    setRawFromUI (Params::bandActiveID (slot), 1.0f);
    setRawFromUI (Params::bandShownID (slot), 1.0f);
    endEdit();
    return slot;
}

void OpenEQAudioProcessor::setMonitorMode (int mode) noexcept
{
    monitorMode.store (juce::jlimit (0, 4, mode));
}

void OpenEQAudioProcessor::setDisplayTiltDbPerOctave (float dbPerOctave) noexcept
{
    displayTiltDbPerOctave.store (juce::jlimit (-12.0f, 12.0f, dbPerOctave));
}

void OpenEQAudioProcessor::setSpectrumSource (int source) noexcept
{
    // 0 post, 1 pre, 2 side, 3 waterfall.
    spectrumSource.store (juce::jlimit (0, 5, source));
}

void OpenEQAudioProcessor::setAudition (bool holding, int bandIndex) noexcept
{
    auditionBandIndex.store (holding && bandIndex >= 0 && bandIndex < numBands ? bandIndex : -1);
}

void OpenEQAudioProcessor::setDetectorAudition (bool holding, int bandIndex) noexcept
{
    detectorAuditionIndex.store (holding && bandIndex >= 0 && bandIndex < numBands ? bandIndex : -1);
}

float OpenEQAudioProcessor::getMeterPeak (int channel) const noexcept
{
    if (channel < 0 || channel > 1)
        return 0.0f;
    return meterPeak[(size_t) channel].load();
}

float OpenEQAudioProcessor::getMeterRms (int channel) const noexcept
{
    if (channel < 0 || channel > 1)
        return 0.0f;
    return meterRms[(size_t) channel].load();
}

float OpenEQAudioProcessor::getInputMeterPeak (int channel) const noexcept
{
    if (channel < 0 || channel > 1)
        return 0.0f;
    return inputMeterPeak[(size_t) channel].load();
}

float OpenEQAudioProcessor::getInputMeterRms (int channel) const noexcept
{
    if (channel < 0 || channel > 1)
        return 0.0f;
    return inputMeterRms[(size_t) channel].load();
}

juce::String OpenEQAudioProcessor::restrictionNotice() const
{
    juce::StringArray notes;
    if (processingMode != nullptr && (int) processingMode->load() != 0)
    {
        if (lpOmitsDynamics.load())
            notes.add ("Linear-phase modes leave dynamic bands out");
        if (lpOmitsAllPass.load())
            notes.add ("Linear phase leaves all-pass bands out");
    }
    if (getMonitorMode() != 0)
        notes.add ("Monitor is temporary and is not saved");
    if (isAuditioning())
        notes.add ("Frequency audition is temporary and is not saved");
    if (isDetectorAuditioning())
        notes.add ("Detector listen is temporary and is not saved");
    if (spectralAmount != nullptr && spectralAmount->load() > 1.0e-4f)
        notes.add ("Resonance suppressor is on and adds latency");
    if (lpOmitsSplit.load())
        notes.add ("Split needs Zero Latency mode. The linear-phase modes use the band gain alone");
    return notes.joinIntoString (". ");
}

int OpenEQAudioProcessor::activeLookaheadSamples (const std::array<BandParameterState, numBands>& params, bool anySolo) const noexcept
{
    // The global lookahead always adds a fixed 20 ms, even at 0 ms, by design.
    if (lookaheadOn != nullptr && lookaheadOn->load() > 0.5f)
        return (int) std::llround (0.020 * currentSampleRate);

    for (const auto& band : params)
        if (band.dynamic && band.dynLookaheadMs > 0.05f
            && ResponseModel::supportsDynamics (band.type) && isAudible (band, anySolo))
            return (int) std::llround (0.020 * currentSampleRate);
    return 0;
}

void OpenEQAudioProcessor::pushDelay (float left, float right) noexcept
{
    if (delayL.empty() || delayR.empty())
        return;
    delayL[(size_t) delayWrite] = left;
    delayR[(size_t) delayWrite] = right;
    delayWrite = (delayWrite + 1) % (int) delayL.size();
}

float OpenEQAudioProcessor::readDelay (int channel, int delaySamples) const noexcept
{
    const auto& line = channel == 0 ? delayL : delayR;
    if (line.empty())
        return 0.0f;
    const int length = (int) line.size();
    int age = juce::jlimit (0, length - 1, delaySamples);
    int index = delayWrite - 1 - age;
    index %= length;
    if (index < 0)
        index += length;
    return line[(size_t) index];
}

float OpenEQAudioProcessor::readSideDelay (int channel, int delaySamples) const noexcept
{
    const auto& line = channel == 0 ? sideDelayL : sideDelayR;
    if (line.empty())
        return 0.0f;
    const int length = (int) line.size();
    int age = juce::jlimit (0, length - 1, delaySamples);
    int index = delayWrite - 1 - age;
    index %= length;
    if (index < 0)
        index += length;
    return line[(size_t) index];
}

void OpenEQAudioProcessor::advanceDynamics (const std::array<BandParameterState, numBands>& params, bool anySolo,
                                           bool linearPhase, const float* detectorL, const float* detectorR,
                                           int segmentOffset, int segmentSamples, int sampleOffset, int count,
                                           float* gainOffset)
{
    if (detectorL == nullptr || count <= 0 || gainOffset == nullptr)
        return;

    for (int i = 0; i < numBands; ++i)
    {
        gainOffset[(size_t) i] = 0.0f;
        const auto& band = params[(size_t) i];
        if (! band.dynamic || ! ResponseModel::supportsDynamics (band.type) || ! isAudible (band, anySolo))
        {
            appliedGainDb[(size_t) i].store (0.0f);
            continue;
        }

        const auto& smoother = smoothers[(size_t) i];
        const float frequency = smoother.logFrequency.isSmoothing()
            ? std::exp (smoother.logFrequency.getCurrentValue())
            : clampedFrequency (band.frequency, currentSampleRate);
        const float q = smoother.logQ.isSmoothing()
            ? std::exp (smoother.logQ.getCurrentValue())
            : clampedQ (band.q);
        const float detectorFrequency = band.dynDetLink
            ? frequency
            : clampedFrequency (band.dynDetFreq, currentSampleRate);
        const float detectorQ = band.dynDetLink ? q : clampedQ (band.dynDetQ);
        dynamics[(size_t) i].setTriggerFilter (detectorFrequency, detectorQ);
        dynamics[(size_t) i].setRelativeFilter (clampedFrequency (band.dynRelFreq, currentSampleRate),
                                                clampedQ (band.dynRelQ));

        const float* left = detectorL + sampleOffset;
        const float* right = detectorR != nullptr ? detectorR + sampleOffset : left;
        const bool external = band.dynSource == 5 && sidechainLeft != nullptr;

        if ((lookaheadSamples > 0 || splitDelaySamples > 0) && ! delayL.empty() && detectorScratch.getNumSamples() >= count)
        {
            // The audio is late by the lookahead and by the split separator. The detector reads the delay line
            // late by the separator delay, and by whatever part of the lookahead this band does not use.
            const int lead = juce::jlimit (0, lookaheadSamples,
                                           (int) std::llround (band.dynLookaheadMs * 0.001 * currentSampleRate));
            const int extra = lookaheadSamples - lead + splitDelaySamples;
            auto* heldL = detectorScratch.getWritePointer (0);
            auto* heldR = detectorScratch.getWritePointer (1);
            for (int n = 0; n < count; ++n)
            {
                const int age = (segmentSamples - 1 - (sampleOffset + n)) + extra;
                if (external)
                {
                    heldL[n] = readSideDelay (0, age);
                    heldR[n] = readSideDelay (1, age);
                }
                else
                {
                    heldL[n] = readDelay (0, age);
                    heldR[n] = readDelay (1, age);
                }
            }
            left = heldL;
            right = heldR;
        }
        else if (external)
        {
            left = sidechainLeft + segmentOffset + sampleOffset;
            right = sidechainRight != nullptr ? sidechainRight + segmentOffset + sampleOffset : left;
        }

        DynamicsEngine::Mix mix = DynamicsEngine::Mix::Energy;
        if (! external)
        {
            switch (band.dynSource)
            {
                case 1: mix = DynamicsEngine::Mix::Left; break;
                case 2: mix = DynamicsEngine::Mix::Right; break;
                case 3: mix = DynamicsEngine::Mix::Mid; break;
                case 4: mix = DynamicsEngine::Mix::Side; break;
                default:
                    switch (band.stereoMode)
                    {
                        case StereoMode::Left: mix = DynamicsEngine::Mix::Left; break;
                        case StereoMode::Right: mix = DynamicsEngine::Mix::Right; break;
                        case StereoMode::Mid: mix = DynamicsEngine::Mix::Mid; break;
                        case StereoMode::Side: mix = DynamicsEngine::Mix::Side; break;
                        case StereoMode::Stereo:
                        default: mix = DynamicsEngine::Mix::Energy; break;
                    }
                    break;
            }
        }

        // Relative channel. Energy is the default. External reads the sidechain directly, without lookahead.
        DynamicsEngine::Mix relativeMix = DynamicsEngine::Mix::Energy;
        switch (band.dynRelSource)
        {
            case 1: relativeMix = DynamicsEngine::Mix::Left; break;
            case 2: relativeMix = DynamicsEngine::Mix::Right; break;
            case 3: relativeMix = DynamicsEngine::Mix::Mid; break;
            case 4: relativeMix = DynamicsEngine::Mix::Side; break;
            default: break;
        }
        const float* relativeLeft = nullptr;
        const float* relativeRight = nullptr;
        if (band.dynRelSource == 5 && sidechainLeft != nullptr)
        {
            relativeLeft = sidechainLeft + segmentOffset + sampleOffset;
            relativeRight = sidechainRight != nullptr ? sidechainRight + segmentOffset + sampleOffset : relativeLeft;
        }

        DynamicsEngine::Settings settings;
        settings.thresholdDb = band.dynThresholdDb;
        settings.kneeDb = band.dynKneeDb;
        settings.attackMs = band.dynAttackMs;
        settings.releaseMs = band.dynReleaseMs;
        settings.transientPercent = band.dynTransient;
        settings.automatic = band.dynAuto;
        settings.relativePercent = band.dynRelative;
        settings.relativeFree = band.dynRelFree;
        settings.modern = band.dynBelowOn || band.dynAboveOn;
        settings.belowOn = band.dynBelowOn;
        settings.belowRangeDb = band.dynBelowRangeDb;
        settings.belowRatio = band.dynBelowRatio;
        settings.aboveOn = band.dynAboveOn;
        settings.aboveRangeDb = band.dynAboveRangeDb;
        settings.aboveRatio = band.dynAboveRatio;
        settings.action = band.dynAction;
        settings.rangeDb = band.dynRangeDb;

        const float offsetDb = dynamics[(size_t) i].advance (left, right, count, mix,
                                                              relativeLeft, relativeRight, relativeMix, settings);
        appliedThresholdDb[(size_t) i].store (dynamics[(size_t) i].getLastThresholdDb());
        if (! linearPhase)
        {
            gainOffset[(size_t) i] = offsetDb;
            appliedGainDb[(size_t) i].store (offsetDb);
        }
    }
}

void OpenEQAudioProcessor::applyWetTail (juce::AudioBuffer<float>& buffer, int offset, int numSamples,
                                        const std::array<BandParameterState, numBands>& params, bool anySolo)
{
    const int channels = juce::jmax (1, juce::jmin (ioChannels, buffer.getNumChannels()));
    if (autoGain != nullptr && autoGain->load() > 0.5f)
    {
        double sum = 0.0;
        constexpr double probes[] = { 80.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 };
        // Each audible band is designed once and then read at every probe, in the same band order.
        int designedCount = 0;
        for (int i = 0; i < numBands; ++i)
        {
            const auto& band = params[(size_t) i];
            if (isAudible (band, anySolo))
                autoGainDesigns[(size_t) designedCount++] = ResponseModel::designBand (band.type, band.frequency, band.gainDb,
                                                                                      band.q, band.slopeDb, currentSampleRate);
        }
        for (double frequency : probes)
        {
            double magnitude = 1.0;
            for (int i = 0; i < designedCount; ++i)
                magnitude *= (double) ResponseModel::designedMagnitude (autoGainDesigns[(size_t) i], frequency, currentSampleRate);
            sum += magnitude;
        }
        const float compensation = juce::jlimit (0.25f, 4.0f, (float) ((double) std::size (probes) / juce::jmax (1.0e-6, sum)));
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* samples = buffer.getWritePointer (channel) + offset;
            for (int i = 0; i < numSamples; ++i)
                samples[i] *= compensation;
        }
    }

    // While a switch-off waits for the fade-down, drive and spectral keep their last amount. Dropping to zero
    // before the fade would be a jump of its own.
    if (latched.drive && drive != nullptr)
    {
        if (drive->load() > 1.0e-4f)
            heldDrive = drive->load();
        saturator.process (buffer, offset, numSamples, channels, heldDrive);
    }

    if (latched.spectral && spectralAmount != nullptr)
    {
        if (spectralAmount->load() > 1.0e-4f)
            heldSpectralAmount = spectralAmount->load();
        auto* left = buffer.getWritePointer (0) + offset;
        auto* right = channels > 1 ? buffer.getWritePointer (1) + offset : nullptr;
        ResonanceEngine::Settings settings;
        settings.depth = heldSpectralAmount;
        settings.floorDb = spectralThresh->load();
        settings.centreHz = spectralFreq->load();
        settings.widthOctaves = spectralOct->load();
        settings.sharpness = spectralSharp->load();
        settings.selectivity = spectralSelect->load();
        settings.attack = spectralAttack->load();
        settings.release = spectralRelease->load();
        settings.mix = spectralMix->load();
        settings.link = spectralLink->load();
        settings.hard = spectralHard->load() > 0.5f;
        settings.delta = spectralDelta->load() > 0.5f;
        settings.midSide = spectralMS->load() > 0.5f;
        // Bands with a suppressor focus amount shape where it works, using the band's own frequency and Q.
        for (int i = 0; i < numBands && settings.focusCount < ResonanceEngine::Settings::maxFocus; ++i)
        {
            const float amount = bandSuppress[(size_t) i]->load();
            const auto& band = cachedBands[(size_t) i];
            if (std::abs (amount) < 0.005f || band.active->load() < 0.5f)
                continue;
            settings.focus[(size_t) settings.focusCount++] = { band.frequency->load(), amount, band.q->load() };
        }
        const bool keyed = spectralSidechain->load() > 0.5f && sidechainLeft != nullptr;
        spectral.process (left, right, numSamples, settings,
                          keyed ? sidechainLeft + offset : nullptr,
                          keyed && sidechainRight != nullptr ? sidechainRight + offset : nullptr);
    }

    if (polarity != nullptr && polarity->load() > 0.5f)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* channelData = buffer.getWritePointer (channel) + offset;
            for (int i = 0; i < numSamples; ++i)
                channelData[i] = -channelData[i];
        }
    }
}

void OpenEQAudioProcessor::applyDetectorAudition (juce::AudioBuffer<float>& buffer, int offset, int count,
                                                  const std::array<BandParameterState, numBands>& params)
{
    const int index = detectorAuditionIndex.load();
    if (index < 0 || index >= numBands || count <= 0)
        return;

    const auto& band = params[(size_t) index];
    const float frequency = band.dynDetLink
        ? clampedFrequency (band.frequency, currentSampleRate)
        : clampedFrequency (band.dynDetFreq, currentSampleRate);
    const float q = band.dynDetLink ? clampedQ (band.q) : clampedQ (band.dynDetQ);
    detectorMonitor.applyIfChanged (FilterType::BandPass, frequency, 0.0f, q, 12);
    const int channels = juce::jmin (2, buffer.getNumChannels(), auditionCapture.getNumChannels());
    for (int channel = 0; channel < channels; ++channel)
    {
        buffer.copyFrom (channel, offset, auditionCapture, channel, 0, count);
        detectorMonitor.processChannel (buffer.getWritePointer (channel) + offset, count, channel);
    }
}

void OpenEQAudioProcessor::applyAudition (juce::AudioBuffer<float>& buffer, int numSamples,
                                         const std::array<BandParameterState, numBands>& params)
{
    const int index = auditionBandIndex.load();
    if (index < 0 || index >= numBands)
        return;

    const auto& band = params[(size_t) index];
    FilterType type = FilterType::BandPass;
    if (band.type == FilterType::LowCut)
        type = FilterType::HighCut;
    else if (band.type == FilterType::HighCut)
        type = FilterType::LowCut;

    // A band pass of the band's own slope could be order 0 (a no-op), so audition uses a second-order band pass.
    // A cut auditions through the complementary cut at the cut's own slope.
    const int auditionSlope = type == FilterType::BandPass ? ResponseModel::encodeSlopeNorm (0.5f) : band.slopeDb;
    auditionBand.applyIfChanged (type, clampedFrequency (band.frequency, currentSampleRate), 0.0f,
                                 clampedQ (band.q), auditionSlope);
    const int channels = juce::jmax (1, juce::jmin (ioChannels, buffer.getNumChannels(), 2));
    for (int channel = 0; channel < channels; ++channel)
        auditionBand.processChannel (buffer.getWritePointer (channel), numSamples, channel);
}

void OpenEQAudioProcessor::applyMonitor (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int mode = monitorMode.load();
    if (mode == 0)
        return;

    const int channels = juce::jmax (1, juce::jmin (ioChannels, buffer.getNumChannels()));
    auto* left = buffer.getWritePointer (0);
    auto* right = channels > 1 ? buffer.getWritePointer (1) : left;
    for (int i = 0; i < numSamples; ++i)
    {
        const float a = left[i];
        const float b = right[i];
        const float mid = 0.5f * (a + b);
        const float side = 0.5f * (a - b);
        float outL = a;
        float outR = b;
        if (mode == 1) { outL = a; outR = a; }
        else if (mode == 2) { outL = b; outR = b; }
        else if (mode == 3) { outL = mid; outR = mid; }
        else if (mode == 4) { outL = side; outR = side; }
        left[i] = outL;
        if (channels > 1)
            right[i] = outR;
    }
}

void OpenEQAudioProcessor::updateInputMeters (const juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int channels = juce::jmax (1, juce::jmin (ioChannels, buffer.getNumChannels(), 2));
    // Both channels in one pass: each channel's peak and sum keep their own order, the two chains overlap.
    float peak[2] = {};
    double sum[2] = {};
    const float* left = buffer.getReadPointer (0);
    if (channels > 1)
    {
        const float* right = buffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            peak[0] = juce::jmax (peak[0], std::abs (left[i]));
            sum[0] += (double) left[i] * (double) left[i];
            peak[1] = juce::jmax (peak[1], std::abs (right[i]));
            sum[1] += (double) right[i] * (double) right[i];
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            peak[0] = juce::jmax (peak[0], std::abs (left[i]));
            sum[0] += (double) left[i] * (double) left[i];
        }
    }
    for (int channel = 0; channel < channels; ++channel)
    {
        inputMeterPeak[(size_t) channel].store (peak[channel]);
        if (peak[channel] > inputMeterMaxPeak.load())
            inputMeterMaxPeak.store (peak[channel]);
        inputMeterRms[(size_t) channel].store ((float) std::sqrt (sum[channel] / (double) juce::jmax (1, numSamples)));
    }
}

void OpenEQAudioProcessor::updateMeters (const juce::AudioBuffer<float>& buffer, int numSamples)
{
    // 20 dB of peak release every 350 ms, applied per sample so the meter ballistics
    // are identical at every host buffer size.
    constexpr double releaseSecondsFor20Db = 0.35;
    const float releasePerSample = (float) std::pow (0.1, 1.0 / (releaseSecondsFor20Db * juce::jmax (1.0, currentSampleRate)));

    const int channels = juce::jmax (1, juce::jmin (ioChannels, buffer.getNumChannels(), 2));
    // Both channels in one pass: each channel's recursions keep their own order, the two chains overlap.
    float state[2] = { meterPeak[0].load(), channels > 1 ? meterPeak[1].load() : 0.0f };
    float blockPeak[2] = {};
    double sum[2] = {};
    auto step = [&] (int channel, float sample)
    {
        const float magnitude = std::abs (sample);
        blockPeak[channel] = juce::jmax (blockPeak[channel], magnitude);
        state[channel] = juce::jmax (magnitude, state[channel] * releasePerSample);
        sum[channel] += (double) sample * (double) sample;
    };
    const float* left = buffer.getReadPointer (0);
    if (channels > 1)
    {
        const float* right = buffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            step (0, left[i]);
            step (1, right[i]);
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
            step (0, left[i]);
    }
    for (int channel = 0; channel < channels; ++channel)
    {
        meterPeak[(size_t) channel].store (state[channel]);
        meterRms[(size_t) channel].store ((float) std::sqrt (sum[channel] / (double) juce::jmax (1, numSamples)));
        if (blockPeak[channel] > meterMaxPeak.load())
            meterMaxPeak.store (blockPeak[channel]);
        if (blockPeak[channel] >= 0.999f)
            meterClipped.store (true);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpenEQAudioProcessor();
}
