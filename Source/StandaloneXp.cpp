#include "PluginEditor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
class SkratchStandalone final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String &) override {}

    void initialise(const juce::String &commandLine) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
#endif
        const bool smokeTest = commandLine.trim() == "--smoke-test";
        if (!smokeTest)
            properties.setStorageParameters(options);
        juce::LookAndFeel::setDefaultLookAndFeel(&look);
        auto holder = std::make_unique<juce::StandalonePluginHolder>(
            smokeTest ? nullptr : properties.getUserSettings(), false);
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            headlessHolder = std::move(holder);
            if (smokeTest)
            {
                setApplicationReturnValue(1);
                quit();
            }
            return;
        }
        window = std::make_unique<juce::StandaloneFilterWindow>(getApplicationName(), NeedleTheme::xpPanel,
                                                               std::move(holder));
        window->setVisible(true);
        if (smokeTest)
            juce::Timer::callAfterDelay(1200, [this] {
                auto *processor = window->getAudioProcessor();
                auto *editor = processor != nullptr ? processor->getActiveEditor() : nullptr;
                setApplicationReturnValue(editor != nullptr && editor->isShowing() ? 0 : 1);
                quit();
            });
    }

    void systemRequestedQuit() override
    {
        if (window)
            window->pluginHolder->savePluginState();
        if (headlessHolder)
            headlessHolder->savePluginState();
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay(100, [] {
                if (auto *app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

    void shutdown() override
    {
        window.reset();
        headlessHolder.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        properties.saveIfNeeded();
    }

private:
    NeedleLook look;
    juce::ApplicationProperties properties;
    std::unique_ptr<juce::StandalonePluginHolder> headlessHolder;
    std::unique_ptr<juce::StandaloneFilterWindow> window;
};
}

START_JUCE_APPLICATION(SkratchStandalone)
