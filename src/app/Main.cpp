#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace gsp::ui
{

class GuitarStudioProApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Guitar Studio Pro";
        options.folderName = "Guitar Studio Pro";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (options);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        settings = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, juce::PropertiesFile& settings)
            : DocumentWindow (name, colours::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (settings), true);
            setResizable (true, true);
            setResizeLimits (1320, 880, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace gsp::ui

START_JUCE_APPLICATION (gsp::ui::GuitarStudioProApplication)
