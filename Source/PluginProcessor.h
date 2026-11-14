#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include "Parameters.h"
#include "EQBand.h"
#include "DynamicsEngine.h"
#include "LinearPhaseEngine.h"
#include "Analyzer.h"
#include "ResponseModel.h"
#include "Saturator.h"
#include "SpectralEngine.h"
#include "ResonanceEngine.h"
#include "SplitSeparator.h"
#include "InstanceRegistry.h"

struct BandParameterState
{
    bool active = false;
    bool solo = false;
    bool dynamic = false;
    FilterType type = FilterType::Bell;
    StereoMode stereoMode = StereoMode::Stereo;
    float frequency = 1000.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
    int slopeDb = 12;
    float dynThresholdDb = -24.0f;
    float dynRangeDb = 6.0f;
    float dynAttackMs = 15.0f;
    float dynReleaseMs = 150.0f;
    bool shown = false;
    int dynAction = 0;
    float dynKneeDb = 0.0f;
    int dynSource = 0;
    float dynRelative = 0.0f;
    float dynLookaheadMs = 0.0f;
    float dynTransient = 0.0f;
    bool dynAuto = false;
    bool dynDetLink = true;
    float dynDetFreq = 1000.0f;
    float dynDetQ = 1.0f;
    bool dynBelowOn = false;
    float dynBelowRangeDb = 6.0f;
    float dynBelowRatio = 1.0f;
    bool dynAboveOn = false;
    float dynAboveRangeDb = -6.0f;
    float dynAboveRatio = 1.0f;
    bool dynRelFree = false;
    float dynRelFreq = 1000.0f;
    float dynRelQ = 1.0f;
    int dynRelSource = 0;
    float pan = 0.0f; // -1 .. 1
    bool splitOn = false;      // Split mode: the band curve applies to the transient and the tail separately
    float splitTransDb = 0.0f; // transient-path gain
    float splitTailDb = 0.0f;  // tail-path gain
    float phase = 0.0f;        // free phase: -1 inverse, 0 linear, +1 minimum (kernel modes)
};

class OpenEQAudioProcessor : public juce::AudioProcessor
{
public:
    OpenEQAudioProcessor();
    ~OpenEQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void setNonRealtime (bool isNonRealtime) noexcept override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool supportsDoublePrecisionProcessing() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static constexpr int numBands = Params::numBands;
    static_assert (numBands == ResponseModel::graphBandCount);

    /** Processing chunks longer than this are split. Scratch buffers never grow on the audio thread. */
    static constexpr int maxProcessingChunk = 512;

    juce::AudioProcessorValueTreeState apvts;

    AnalyzerRing preL, preR, postL, postR, sideL, sideR;
    /** The two split streams after their band chains, for the two-colour spectrum. Fed only while a band is split. */
    AnalyzerRing splitTransL, splitTransR, splitTonalL, splitTonalR;
    bool isSplitSpectrumActive() const noexcept { return splitSpectrumActive.load(); }
    /** Host sample rate. The analyzer and anything that describes the host use this. The processing rate can be
        twice this when 2x oversampling is on. */
    double getSampleRate_() const noexcept { return baseSampleRate; }

    BandParameterState readBandParameter (int index) const noexcept;
    void readAllBandParameters (std::array<BandParameterState, numBands>& dest) const noexcept;

    /** Latest coefficients and targets for the graph. Does not touch live IIR objects. */
    ResponseModel::GraphSnapshot getGraphSnapshot() const;

    /** Message thread: while the audio callback is idle, rebuild the graph from parameters. */
    void serviceIdleGraph();

    juce::uint64 getDesignUpdateCount() const noexcept { return designUpdates.load(); }
    float getDynamicEnvelopeDb (int index) const noexcept;
    /** Gain offset last applied to this band. Zero while linear phase omits the band. */
    float getDynamicGainDb (int index) const noexcept;
    /** Threshold last used by the gain computer, including automatic threshold. */
    float getDynamicThresholdDb (int index) const noexcept;
    float getSplitTransientMeter() const noexcept { return splitTransientMeter.load(); }
    float getSplitTonalMeter() const noexcept { return splitTonalMeter.load(); }
    float getSplitRatioMeter() const noexcept { return splitRatioMeter.load(); }
    int getReportedLatencySamples() const noexcept { return reportedLatencyBase; }
    /** 2x oversampling. The processing runs at twice the host rate while it is on.
        Changing it re-prepares the plugin, so it runs with processing suspended. */
    void setOversampling2x (bool enabled);
    bool isOversampling2x() const noexcept { return oversampling; }
    juce::uint64 getLinearPhaseDesigns() const noexcept { return linearPhaseEngine.getCompletedDesigns(); }

    void beginEdit (const juce::String& name);
    void endEdit();
    void setRawFromUI (const juce::String& parameterID, float rawValue);
    bool undo();
    bool redo();

    void switchAB();
    void copyABToOther();
    int getAudibleABSlot() const noexcept { return abSlot; }

    juce::File userPresetDirectory() const;
    bool savePresetFile (const juce::File& file);
    bool loadPresetFile (const juce::File& file);
    /** User presets are .opeq files (legacy .godeq supported) in Application Support/OpenEQ/Presets. */
    juce::Array<juce::File> listUserPresets() const;
    juce::File defaultPresetFile() const;
    /** Saves the current state as the default. New instances load it. */
    bool saveAsDefault();
    void loadFactoryPreset (int index);

    /** Band slot operations. Each one is a single undoable edit. A band is "used" while it is active or shown. */
    bool isBandUsed (int index) const;
    int findFreeBandSlot() const;
    std::vector<float> captureBand (int index) const;
    void applyBandValues (int index, const std::vector<float>& values, bool invertGain);
    int duplicateBand (int index, bool invertGain);
    int splitBand (int index);
    void removeBands (const std::vector<int>& indices);
    void clearAllBands();
    void swapBands (int a, int b);
    void sortBands (bool byType);
    void copyBandToClipboard (int index);
    bool hasBandClipboard() const noexcept { return ! bandClipboard.empty(); }
    /** Pastes over a used band at `index`, or into the first free slot. Returns the slot, or -1. */
    int pasteBandClipboard (int index, bool invertGain);

    void setMonitorMode (int mode) noexcept;
    int getMonitorMode() const noexcept { return monitorMode.load(); }
    /** Analyzer display tilt in dB per octave around 1 kHz. Not saved. */
    void setDisplayTiltDbPerOctave (float dbPerOctave) noexcept;
    float getDisplayTiltDbPerOctave() const noexcept { return displayTiltDbPerOctave.load(); }
    /** 0 post, 1 pre, 2 external sidechain. Not saved. */
    void setSpectrumSource (int source) noexcept;
    int getSpectrumSource() const noexcept { return spectrumSource.load(); }
    void setAudition (bool holding, int bandIndex) noexcept;
    bool isAuditioning() const noexcept { return auditionBandIndex.load() >= 0; }
    /** Hear the detector band-pass. Temporary, and not saved. */
    void setDetectorAudition (bool holding, int bandIndex) noexcept;
    bool isDetectorAuditioning() const noexcept { return detectorAuditionIndex.load() >= 0; }

    float getMeterPeak (int channel) const noexcept;
    float getMeterRms (int channel) const noexcept;
    float getInputMeterPeak (int channel) const noexcept;
    float getInputMeterRms (int channel) const noexcept;
    /** Reduction the resonance suppressor is applying in one readout band, in dB; 0 while it is off. */
    float getSpectralReductionDb (int band) const noexcept { return spectral.getDisplayReductionDb (band); }
    bool isSpectralActive() const noexcept { return latched.spectral; }
    bool getMeterClipped() const noexcept { return meterClipped.load(); }
    /** Counts blocks that produced a non-finite sample and were recovered. Diagnostics only. */
    int getNonFiniteRecoveries() const noexcept { return nonFiniteRecoveries.load(); }
    /** Clears the clip latch after the user acknowledges the overload. */
    void clearMeterClip() noexcept { meterClipped.store (false); meterMaxPeak.store (0.0f); }
    /** Highest output sample magnitude since the last clearMeterClip, measured on every sample. */
    float getMeterMaxPeak() const noexcept { return meterMaxPeak.load(); }
    /** Highest input sample magnitude since the last clearInputMeterMax. */
    float getInputMeterMaxPeak() const noexcept { return inputMeterMaxPeak.load(); }
    void clearInputMeterMax() noexcept { inputMeterMaxPeak.store (0.0f); }

    bool linearPhaseOmitsDynamics() const noexcept { return lpOmitsDynamics.load(); }
    bool linearPhaseOmitsAllPass() const noexcept { return lpOmitsAllPass.load(); }
    juce::String restrictionNotice() const;

    int getInstanceId() const noexcept { return instanceId; }
    /** The linear-phase quality in use: the render quality while rendering offline, else the realtime choice. */
    int effectiveLinearPhaseQuality() const noexcept;

    /** Mirrors this instance's parameters with another OpenEQ instance, both ways: edits and host
        automation on either side are applied to the other. nullptr targets self (no relay).
        Message thread. The mirror is dropped automatically when the target is destroyed. */
    void setRemoteTarget (OpenEQAudioProcessor* target);
    OpenEQAudioProcessor* getRemoteTarget() const noexcept { return remoteTarget; }
    /** Human name for the instance menu ("OpenEQ 3"). */
    juce::String getInstanceName() const;

public:
    struct RelayListener; // defined in PluginProcessor.cpp; listens on an apvts state tree
private:
    struct CachedBandParams
    {
        std::atomic<float>* active = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* frequency = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* slope = nullptr;
        std::atomic<float>* slopeNorm = nullptr;
        std::atomic<float>* stereo = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* dynamic = nullptr;
        std::atomic<float>* dynThreshold = nullptr;
        std::atomic<float>* dynRange = nullptr;
        std::atomic<float>* dynAttack = nullptr;
        std::atomic<float>* dynRelease = nullptr;
        std::atomic<float>* shown = nullptr;
        std::atomic<float>* dynAction = nullptr;
        std::atomic<float>* dynKnee = nullptr;
        std::atomic<float>* dynSource = nullptr;
        std::atomic<float>* dynBlend = nullptr;
        std::atomic<float>* dynAhead = nullptr;
        std::atomic<float>* dynTransient = nullptr;
        std::atomic<float>* dynAuto = nullptr;
        std::atomic<float>* dynDetLink = nullptr;
        std::atomic<float>* dynDetFreq = nullptr;
        std::atomic<float>* dynDetQ = nullptr;
        std::atomic<float>* dynBelowOn = nullptr;
        std::atomic<float>* dynBelowRange = nullptr;
        std::atomic<float>* dynBelowRatio = nullptr;
        std::atomic<float>* dynAboveOn = nullptr;
        std::atomic<float>* dynAboveRange = nullptr;
        std::atomic<float>* dynAboveRatio = nullptr;
        std::atomic<float>* dynRelFree = nullptr;
        std::atomic<float>* dynRelFreq = nullptr;
        std::atomic<float>* dynRelQ = nullptr;
        std::atomic<float>* dynRelSource = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* splitOn = nullptr;
        std::atomic<float>* splitTrans = nullptr;
        std::atomic<float>* splitTail = nullptr;
        std::atomic<float>* phase = nullptr;
    };

    struct BandSmoother
    {
        juce::SmoothedValue<float> logFrequency;
        juce::SmoothedValue<float> gainDb;
        juce::SmoothedValue<float> logQ;
        juce::SmoothedValue<float> wetMix;
        juce::SmoothedValue<float> splitTransDb;
        juce::SmoothedValue<float> splitTailDb;
    };

    struct SliceMix
    {
        float start = 1.0f;
        float end = 1.0f;
    };

    void cacheParameterPointers();
    void ensureScratch();
    void snapSmoothers (const std::array<BandParameterState, numBands>& params, bool anySolo);
    void setSmootherTargets (const std::array<BandParameterState, numBands>& params, bool anySolo);
    void applyBandTargets (const std::array<BandParameterState, numBands>& params,
                           const float* gainOffsetDb, bool useTargets);
    void processSegment (juce::AudioBuffer<float>& buffer, int offset, int numSamples,
                         const std::array<BandParameterState, numBands>& params, bool anySolo, bool linearPhase);
    /** segmentPosition is the slice's first sample inside the current segment. The split streams are indexed by it. */
    void processBands (juce::AudioBuffer<float>& buffer, int offset, int numSamples,
                       const std::array<SliceMix, numBands>& mixes, bool linearPhase, int segmentPosition);
    void runBandChain (std::array<EQBand, numBands>& chain, std::array<bool, numBands>& wasDry,
                       float* left, float* right, int numSamples, int channels,
                       const std::array<SliceMix, numBands>& mixes);
    void processLane (EQBand& band, float* data, int numSamples, int lane, float mixStart, float mixEnd);
    void processLanePair (EQBand& band, float* dataA, float* dataB, int numSamples,
                          float mixStartA, float mixEndA, float mixStartB, float mixEndB);
    void updateSplitFlags (const std::array<BandParameterState, numBands>& params) noexcept;
    /** Designs one band for the transient chain and, while any band is split, for the tonal chain. */
    bool designBandPair (int index, const BandParameterState& band, float frequency, float gainDb, float q,
                         float transDb, float tailDb);
    void separateSplitStreams (juce::AudioBuffer<float>& buffer, int offset, int numSamples);
    /** A band's stream gain as the audio uses it: zero unless the band is on and split. */
    static float effectiveSplitDb (const BandParameterState& band, float streamDb) noexcept;
    /** Everything that decides the latency. The audio uses the latched copy, which changes only in silence. */
    struct LatencyConfig
    {
        int mode = 0;          // processing mode; 0 is Zero Latency, 4 is Analog ZL
        int lpOrder = 12;      // linear-phase FFT order
        int lookahead = 0;     // samples
        bool drive = false;
        bool spectral = false;
        bool split = false;

        bool sameLatencyAs (const LatencyConfig& other) const noexcept
        {
            return (mode != 0) == (other.mode != 0) && (mode == 4) == (other.mode == 4)
                && (mode == 0 || lpOrder == other.lpOrder)
                && lookahead == other.lookahead && drive == other.drive
                && spectral == other.spectral && split == other.split;
        }
    };
    LatencyConfig readLatencyConfig (const std::array<BandParameterState, numBands>& params, bool anySolo) const noexcept;
    void latchLatencyConfig (const LatencyConfig& config) noexcept;
    /** Output gain around a latency change: fade down, switch, stay silent for the new delay, fade up. */
    void applyLatencyTransition (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
    /** Message thread: records whether the A/B slot that is not playing has a split band. */
    void refreshOtherSlotSplit();
    void applyWidthAndGain (juce::AudioBuffer<float>& buffer, int offset, int numSamples);
    void publishAppliedGraph (const std::array<BandParameterState, numBands>& params, bool anySolo);
    ResponseModel::GraphSnapshot buildIdleGraph() const;
    void submitLinearPhaseRequest (const std::array<BandParameterState, numBands>& params, bool anySolo);
    void pushToAnalyzer (AnalyzerRing& ringL, AnalyzerRing& ringR, const juce::AudioBuffer<float>& buffer, int numSamples);
    static bool isAudible (const BandParameterState& band, bool anySolo) noexcept;
    static juce::uint64 hashCurveRequest (const LinearPhaseEngine::CurveRequest& request) noexcept;

    std::array<CachedBandParams, numBands> cachedBands;
    std::atomic<float>* processingMode = nullptr;
    std::atomic<float>* linearPhaseQuality = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* stereoWidth = nullptr;
    std::atomic<float>* gainScale = nullptr;
    std::atomic<float>* sideGain = nullptr;
    std::atomic<float>* outputPan = nullptr;
    std::atomic<float>* lookaheadOn = nullptr;
    std::atomic<float>* ultraPrecision = nullptr;
    std::atomic<float>* mixCrossover = nullptr;
    std::atomic<float>* lookaheadMs = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* polarity = nullptr;
    std::atomic<float>* autoGain = nullptr;
    std::atomic<float>* inputGain = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* spectralAmount = nullptr;
    std::atomic<float>* spectralThresh = nullptr;
    std::atomic<float>* spectralFreq = nullptr;
    std::atomic<float>* spectralOct = nullptr;
    std::atomic<float>* spectralSharp = nullptr;
    std::atomic<float>* spectralSelect = nullptr;
    std::atomic<float>* spectralAttack = nullptr;
    std::atomic<float>* spectralRelease = nullptr;
    std::atomic<float>* spectralMix = nullptr;
    std::atomic<float>* spectralLink = nullptr;
    std::atomic<float>* spectralHard = nullptr;
    std::atomic<float>* spectralDelta = nullptr;
    std::atomic<float>* spectralMS = nullptr;
    std::atomic<float>* spectralSidechain = nullptr;
    std::array<std::atomic<float>*, numBands> bandSuppress {};

    std::array<BandSmoother, numBands> smoothers;
    juce::SmoothedValue<float> inputGainDb;
    juce::SmoothedValue<float> outputGainDb;
    juce::SmoothedValue<float> widthPercent;
    juce::SmoothedValue<float> sideLinear;  // Side level as a linear multiplier.
    juce::SmoothedValue<float> panPosition; // -1 full left .. +1 full right.
    juce::SmoothedValue<float> bypassWet;

    std::array<EQBand, numBands> bands;
    /** The tonal stream's copy of every band. `bands` is the transient stream's copy, and the only one without Split. */
    std::array<EQBand, numBands> splitTailBands;
    std::array<DynamicsEngine, numBands> dynamics;
    std::array<bool, numBands> bandWasDry {};
    std::array<bool, numBands> tailWasDry {};
    std::array<bool, numBands> splitFlags {};
    bool anySplitBands = false;
    /** True while the audio runs as two streams: some band is split and the mode is Zero Latency. */
    bool splitEngaged = false;
    int splitDelaySamples = 0;
    std::atomic<bool> otherSlotSplit { false };
    std::atomic<bool> splitSpectrumActive { false };
    void pushSplitSpectrum (int numSamples);
    LatencyConfig latched;
    bool latencyPrimed = false;
    bool latencySwitchPending = false;
    float latencyFadeGain = 1.0f;
    int latencyMuteRemaining = 0;
    int kernelWaitSamples = 0;       // counts down while a mode change waits for its kernel
    float heldDrive = 0.0f;
    float heldSpectralAmount = 0.0f;
    SplitSeparator splitSeparator;

    juce::AudioBuffer<float> monoScratch, midBuffer, sideBuffer, bandDry, dryInput, floatFromDouble, detectorScratch, auditionCapture;
    juce::AudioBuffer<float> splitTransientBuffer, splitTonalBuffer;

    LinearPhaseEngine linearPhaseEngine;
    Saturator saturator;
    ResonanceEngine spectral;
    /** Auto Gain scratch: the audible bands' designs for one block. Audio thread only. */
    std::array<ResponseModel::DesignedBand, numBands> autoGainDesigns {};
    EQBand auditionBand;
    EQBand detectorMonitor;
    int lastLatencyKey = -1;
    int reportedLatency = 0;          // internal samples, used by the processing
    int reportedLatencyBase = 0;      // host samples, reported to the host
    double baseSampleRate = 44100.0;
    bool oversampling = false;
    int preparedBlockSize = 512;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::unique_ptr<juce::dsp::Oversampling<float>> sidechainOversampler;
    juce::AudioBuffer<float> oversampledMain, oversampledSidechain, sidechainBase;
    std::atomic<float>* oversample2x = nullptr;
    std::atomic<float>* splitSource = nullptr;
    std::atomic<float>* splitSeparation = nullptr;
    std::atomic<float>* splitDecay = nullptr;
    std::atomic<float>* splitSmoothingMs = nullptr;
    std::atomic<float>* splitSolo = nullptr;
    std::atomic<float>* windowShapeParam = nullptr;
    std::atomic<float>* windowParameterParam = nullptr;
    int lastWindowShape = -1;
    float lastWindowParameter = -1.0f;
    std::atomic<float>* renderQualityParam = nullptr;
    std::atomic<float>* renderOversampleParam = nullptr;
    std::atomic<float>* impulsePaddingParam = nullptr;
    std::atomic<float>* digitalPlusParam = nullptr;
    int lastImpulsePadding = -1;
    bool nonRealtime = false;
    void applyRenderDsp() noexcept;
    void processCore (juce::AudioBuffer<float>& buffer, int numSamples, const float* scLeft, const float* scRight);
    int lookaheadSamples = 0;
    int ioChannels = 2;

    std::vector<float> delayL, delayR, sideDelayL, sideDelayR;
    std::atomic<float> splitTransientMeter { 0.0f };
    std::atomic<float> splitTonalMeter { 0.0f };
    std::atomic<float> splitRatioMeter { 0.5f };
    void resetSplitEngine();
    int delayWrite = 0;
    const float* sidechainLeft = nullptr;
    const float* sidechainRight = nullptr;

    std::atomic<int> monitorMode { 0 };
    std::atomic<int> auditionBandIndex { -1 };
    std::atomic<int> detectorAuditionIndex { -1 };
    std::atomic<float> displayTiltDbPerOctave { 0.0f };
    std::atomic<int> spectrumSource { 0 };
    std::atomic<bool> lpOmitsDynamics { false };
    std::atomic<bool> lpOmitsAllPass { false };
    std::atomic<bool> lpOmitsSplit { false };
    std::atomic<float> meterPeak[2] {};
    std::atomic<float> meterRms[2] {};
    std::atomic<float> inputMeterPeak[2] {};
    std::atomic<float> inputMeterRms[2] {};
    std::atomic<bool> meterClipped { false };
    std::atomic<float> meterMaxPeak { 0.0f }, inputMeterMaxPeak { 0.0f };
    std::atomic<int> nonFiniteRecoveries { 0 };
    std::array<std::atomic<float>, numBands> appliedGainDb;
    std::array<std::atomic<float>, numBands> appliedThresholdDb;

    juce::UndoManager undoManager;
    std::vector<float> bandClipboard; // Not saved. Holds one band's parameter values in Params::bandParameterIDs order.
    juce::String gestureName;
    bool gestureOpen = false;
    bool applyingUndo = false;
    struct GestureChange
    {
        juce::String id;
        float start = 0.0f;
        float end = 0.0f;
        bool beganHostGesture = false;
    };
    std::vector<GestureChange> gestureChanges;
    juce::ValueTree abSlots[2];
    int abSlot = 0;
    int instanceId = 0;
    OpenEQAudioProcessor* remoteTarget = nullptr;
    RelayListener* ownRelay = nullptr;
    RelayListener* remoteRelay = nullptr;
    void relayParameter (const juce::String& parameterID, float normalisedValue, void* source);

    friend struct OpenEQParameterSetAction;
    void applyNormalised (const juce::String& parameterID, float normalised);
    void migrateLegacyDynamics();
    /** Maps the old 7-step slope choice onto the continuous slope for sessions saved before schema 3. */
    void migrateLegacySlopes();
public:
    /** A cut or band pass at slope 0 has order 0 and does nothing. Gives such a band 12 dB/oct (slope 0.5). Message thread. */
    void ensureCutSlope (int bandIndex);
private:
    int activeLookaheadSamples (const std::array<BandParameterState, numBands>& params, bool anySolo) const noexcept;
    void pushDelay (float left, float right) noexcept;
    float readDelay (int channel, int delaySamples) const noexcept;
    float readSideDelay (int channel, int delaySamples) const noexcept;
    void advanceDynamics (const std::array<BandParameterState, numBands>& params, bool anySolo, bool linearPhase,
                          const float* detectorL, const float* detectorR, int segmentOffset, int segmentSamples,
                          int sampleOffset, int count, float* gainOffset);
    void applyAudition (juce::AudioBuffer<float>& buffer, int numSamples,
                        const std::array<BandParameterState, numBands>& params);
    void applyDetectorAudition (juce::AudioBuffer<float>& buffer, int offset, int numSamples,
                                const std::array<BandParameterState, numBands>& params);
    void applyMonitor (juce::AudioBuffer<float>& buffer, int numSamples);
    void updateMeters (const juce::AudioBuffer<float>& buffer, int numSamples);
    void updateInputMeters (const juce::AudioBuffer<float>& buffer, int numSamples);
    void applyWetTail (juce::AudioBuffer<float>& buffer, int offset, int numSamples,
                       const std::array<BandParameterState, numBands>& params, bool anySolo);
    juce::uint64 lastCurveHash = 0;
    bool curveSubmitPending = false;
    bool wasFullyBypassed = false;
    bool prepared = false;

    ResponseModel::GraphSnapshot publishedGraph;
    mutable juce::CriticalSection graphLock;
    std::atomic<juce::uint32> lastAudioMs { 0 };
    std::atomic<juce::uint64> designUpdates { 0 };

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenEQAudioProcessor)
};

using GodEQAudioProcessor = OpenEQAudioProcessor;
