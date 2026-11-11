#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "ScratchEngine.h"
#include "Performance.h"
#include "Gamepad.h"
#include "LatencyMetrics.h"
#include "DjFx.h"
#include "MixerDsp.h"

class NeedleProcessor final : public juce::AudioProcessor, private juce::Thread, private juce::AsyncUpdater
{
  public:
    explicit NeedleProcessor(bool instrument = false);
    ~NeedleProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override
    {
        engineActive.store(false);
    }
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override
    {
        return true;
    }
    const juce::String getName() const override
    {
        return instrumentMode ? "SKRATCH.EXE Instrument" : "SKRATCH.EXE FX";
    }
    bool isInstrument() const noexcept
    {
        return instrumentMode;
    }
    LatencyMetrics latencyMetrics;
    std::atomic<bool> mixerFxBank{false};
    std::array<std::atomic<int>, dj::count> djCC;
    std::array<std::atomic<double>, 8> hotCues;
    float djValue(dj::Param parameter) const noexcept;
    void setDjParam(dj::Param parameter, float value);
    void clearDjOverride(dj::Param parameter) noexcept;
    struct ControlMonitor
    {
        std::atomic<bool> enabled{false}, rack{false}, strip{false};
        std::atomic<unsigned> activeModules{0};
        std::atomic<float> inputRms{0}, outputRms{0};
    } monitor;
    void requestDjAction(dj::Action action) noexcept
    {
        djActions.fetch_or(1u << action, std::memory_order_relaxed);
    }
    void requestHotCue(int index, bool down) noexcept;
    void storeHotCue(int index) noexcept;
    void publishPadAxes(bool active, float speed, float fader);
    bool acceptsMidi() const override
    {
        return true;
    }
    bool producesMidi() const override
    {
        return false;
    }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override
    {
        return 1;
    }
    int getCurrentProgram() override
    {
        return 0;
    }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override
    {
        return {};
    }
    void changeProgramName(int, const juce::String &) override {}
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;

    void loadFile(const juce::File &);
    void loadDemo(bool preserveHotCues = false);
    void setParam(const juce::String &, float);
    float param(const juce::String &id) const
    {
        return state.getRawParameterValue(id)->load();
    }
    float param(const char *id) const
    {
        for (const auto &p : parameterCache)
            if (std::strcmp(p.first, id) == 0)
                return p.second->load();
        return 0;
    }
    void returnToCue()
    {
        seek.store(cue.load());
    }
    void setCue()
    {
        cue.store(progress.load());
    }
    void stopDeck();
    void nextCue();
    void triggerPattern(int slot, float velocity = 1.0f, double beats = -1.0);
    void releasePattern(int slot);
    void editPattern(int slot, int lane, int node, float value);
    skratch::Pattern getPattern(int slot) const;
    void applyPreset(int index);
    static juce::StringArray presetNames();
    void startRecording();
    void stopRecording();
    void playRecording(bool play);
    void quantizeRecording(double grid);
    bool undoTake(bool redo = false);
    skratch::Take getTake(int displayPoints = 0) const;
    void replaceTake(const skratch::Take &);
    void editTakePoint(int lane, double beat, float value);
    juce::Result exportTake(const juce::File &, bool midi);
    juce::Result importTake(const juce::File &);
    juce::Result exportMapping(const juce::File &);
    juce::Result importMapping(const juce::File &);
    juce::Result savePreset(const juce::File &);
    juce::Result loadPreset(const juce::File &);
    void addSamples(const juce::StringArray &);
    void nextSample(int direction);
    juce::StringArray sampleFiles() const;
    skratch::PadConfig getPadConfig() const;
    void setPadConfig(const skratch::PadConfig &);
    skratch::PadState getPadState() const;
    void calibratePad(int stage);
    std::vector<GamepadBackend::Device> padDevices()
    {
        return gamepad.devices();
    }
    std::atomic<bool> recording{false}, takePlaying{false}, takeLoop{true}, engineActive{false},
        padConnected{false};
    std::atomic<double> takePosition{0}, bpmDisplay{120};
    std::atomic<int> learnTarget{-1}, lastMidiNote{-1}, lastMidiCC{-1}, selectedPattern{0}, presetIndex{0};
    // MIDI mappings are atomics so a learned CC can be committed on the audio thread.
    std::atomic<int> speedCC{16}, faderCC{1}, positionCC{20}, midiChannel{0};
    static constexpr int liveLearnBase = 19;
    inline static constexpr const char *liveIds[] = {"pitch",    "gain",  "sensitivity",
                                                     "response", "cutIn", "curve"};
    std::array<std::atomic<int>, 6> liveCC;
    std::array<std::atomic<float>, 6> effectiveLive;
    std::array<std::atomic<int>, 16> noteMap;
    std::atomic<float> padSpeed{0}, padFader{1};
    std::atomic<float> effectiveFader{1};
    std::atomic<bool> resetMidi{false};
    std::atomic<bool> padActive{false};
    struct View
    {
        juce::String name, status;
        std::vector<float> peaks;
        double seconds = 0;
        unsigned revision = 0;
    };
    View view() const;
    View view(unsigned knownRevision) const;
    juce::AudioProcessorValueTreeState state;
    std::atomic<bool> mouseHeld{false}, cutHeld{false};
    std::atomic<float> mouseSpeed{0.0f}, keySpeed{0.0f};
    std::atomic<double> progress{0.0}, cue{0.0}, seek{-1.0};
    std::atomic<double> platterTurns{0.0};
    std::atomic<unsigned> mouseSerial{0}, appliedMouseSerial{0};
    std::atomic<float> meter{0.0f}, actualSpeed{0.0f};
    std::atomic<bool> loading{false};

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout parameters(bool instrument);
    using SampleBytes = std::shared_ptr<const juce::MemoryBlock>;
    void install(juce::AudioBuffer<float> &&, double, const juce::String &, const juce::String &, int,
                 SampleBytes = {}, bool preserveHotCues = false);
    void loadSample(const juce::File &, SampleBytes = {}, bool restoreNow = false,
                    bool preserveHotCues = false);
    void applyMidi(const juce::MidiMessage &, bool recordAccess);
    void applyHotCue(int index, bool down, int midiSource = 0);
    void readDjControls();
    bool applyDjController(int number, int value, int learn);
    void writeDjState(juce::DynamicObject &, bool includeCues) const;
    void restoreDjState(const juce::var &, bool includeCues);
    void run() override;
    void handleAsyncUpdate() override;
    void rememberUndo();
    juce::var extraState() const;
    void restoreExtra(const juce::var &);
    void addRecordedEvent(int type, double value, double duration = 0, float velocity = 1);
    struct Clip
    {
        juce::AudioBuffer<float> audio;
        double rate;
    };
    const bool instrumentMode;
    // Producer owns the unpublished/replaced pointers. The audio thread owns activeClip.
    // The retired slot returns ownership without destruction on the audio thread.
    std::atomic<Clip *> pendingClip{nullptr}, retiredClip{nullptr};
    std::unique_ptr<Clip> activeClip;
    mutable juce::CriticalSection metadataMutex;
    mutable juce::SpinLock takeLock;
    double clipSeconds = 0;
    unsigned clipRevision = 0;
    juce::String clipName, filePath, status;
    SampleBytes embeddedSample;
    std::vector<float> peaks;
    juce::ThreadPool loader{1};
    std::atomic<int> generation{0};
    needle::Engine engine;
    const needle::SincReader &sinc;
    std::array<std::pair<const char *, std::atomic<float> *>, 24> parameterCache{};
    std::array<juce::NormalisableRange<float>, 6> liveRanges;
    std::array<float, 6> liveValues{}, previousLiveHost{};
    std::array<bool, 6> liveOverride{};
    bool midiBack = false, midiForward = false;
    std::atomic<bool> midiCut{false};
    skratch::Take take, undo, redo;
    bool canUndo = false, canRedo = false;
    std::array<skratch::Pattern, skratch::patternCount> patterns;
    std::array<skratch::Pattern, skratch::patternCount> renderPatterns;
    bool patternsDirty = false;
    double recordBeat = 0, playBeat = 0, rotation = 0;
    int recordCountdown = 0, activePattern = -1, activeNote = -1;
    double patternPhase = 0, patternLength = 1, patternPendingBeat = -1, absoluteBeat = 0;
    float patternVelocity = 1, midiSpeedValue = 0, midiFaderValue = 1;
    bool midiSpeedActive = false, midiFaderActive = false;
    int midiTransport = -1, takeTransport = -1;
    bool previousPlay = false;
    float previousHostFader = -1;
    double previousCue = -1, lastRecordedTempo = -1;
    bool previousCut = false, takeCut = false;
    double playbackTempo = 120;
    int faderMSB = 127, faderLSB = 127, positionMSB = 0, positionLSB = 0;
    size_t speedCursor = 0, faderCursor = 0, eventCursor = 0;
    std::atomic<int> patternRequest{-1}, patternRelease{-1};
    std::atomic<float> patternRequestVelocity{1};
    std::atomic<double> patternRequestLength{1};
    std::atomic<int> pendingActions{0};
    mutable juce::CriticalSection padMutex, listMutex;
    skratch::PadConfig padConfig;
    skratch::PadState padState;
    std::array<bool, 32> previousPadButtons{};
    GamepadBackend gamepad;
    juce::StringArray sampleList;
    int sampleIndex = -1;
    float previousOutput[2]{}, seekFrom[2]{};
    int seekFade = 0;
    int midiMode = 0, voiceNote = -1, voiceChannel = 0, activeNoteChannel = 0;
    bool voiceActive = false, voiceReleasing = false;
    float voiceVelocity = 1, voiceEnvelope = 0;
    std::array<uint8_t, 16 * 128> heldNotes{};
    size_t recordedNoteEvent = static_cast<size_t>(-1);
    int mouseFramesLeft = 0;
    unsigned mouseSeen = 0;
    int64_t padSeen = 0;
    juce::AudioBuffer<float> inputHistory;
    int historyWrite = 0, historyCount = 0;
    double historyRead = 0;
    bool inputScratching = false;
    dj::Rack djRack;
    dj::Motor djMotor;
    dj::InputStrip inputStrip, fileDeckStrip;
    dj::ManualCoast coast;
    dj::Morph morph;
    dj::Values djValues{}, djPreviousHost{};
    std::array<std::atomic<float> *, dj::count> djHost{};
    std::array<std::atomic<float>, dj::count> djEffective{};
    std::array<bool, dj::count> djOverridden{};
    std::atomic<unsigned> djActions{0}, cueRequests{0}, padCueRequests{0};
    std::array<std::atomic<uint64_t>, (dj::count + 63) / 64> djClearOverride{};
    int cueTransport = -1, activeHotCue = -1, activeHotCueSource = 0;
    int heldPadCue = -1;
    bool oldBrake = false, oldSpin = false;
    bool djChangedByMidi = false;
    float lastTakeSpeed = 0, lastTakeFader = 1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeedleProcessor)
};
