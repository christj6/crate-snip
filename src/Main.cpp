#include "EditorComponent.h"
#include "LaunchRequest.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace cratesnip
{

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow (const juce::StringArray& args)
        : DocumentWindow (JUCE_APPLICATION_NAME_STRING,
                          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (backgroundColourId),
                          allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new EditorComponent (LaunchRequest::parse (args)), true);
        setResizable (true, true);
        setResizeLimits (700, 420, 4096, 2160);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class CrateSnipApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // Each right-click opens its own window, so several tracks can be cut at once.
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getCommandLineParameterArray());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace cratesnip

START_JUCE_APPLICATION (cratesnip::CrateSnipApplication)
