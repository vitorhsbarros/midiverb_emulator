#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Knob look shared by all three units.
class UnitKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider&) override;
};

// The whole faceplate, drawn in a fixed logical coordinate space and scaled
// by the editor. Redraws itself as whichever unit the "device" parameter selects.
class UnitPanel : public juce::Component
{
public:
    static constexpr int logicalWidth = 960;
    static constexpr int logicalHeight = 640;

    explicit UnitPanel(MidiverbAudioProcessor&);
    ~UnitPanel() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Called by the editor's timer.
    void refresh(bool overload);

    // Program chart collapse. When collapsed the chart is cut out of the
    // drawing and the panel is shorter; the editor resizes to match.
    bool isCollapsed() const { return collapsed; }
    void setCollapsed(bool shouldCollapse);
    int currentLogicalHeight() const;
    std::function<void()> onHeightChanged;

private:
    enum class HitType { Tab, Program, Defeat, Up, Down, Key, Collapse };
    struct Hit { juce::Rectangle<float> area; HitType type; int value; };

    int currentDevice() const;
    int currentProgram() const;
    void setIntParam(const juce::String& id, int value);
    void selectProgram(int index);
    void step(int delta);
    void pressKey(int digit);
    void layout();
    const Hit* hitAt(juce::Point<float>) const;
    bool isPressed(HitType, int value = 0) const;

    struct Cut { float start, end; };
    Cut cutFor(int device) const;
    float collapseShift() const;
    void paintContent(juce::Graphics&);
    void paintTabs(juce::Graphics&);
    void paintDesktopUnit(juce::Graphics&, bool isMidifex);
    void paintRackUnit(juce::Graphics&);
    void paintButton(juce::Graphics&, juce::Rectangle<float>, juce::Colour cap, bool pressed);
    void paintDisplay(juce::Graphics&, juce::Rectangle<float>);
    void paintLed(juce::Graphics&, juce::Point<float> centre, juce::Colour on, bool lit);
    void paintKnobLabel(juce::Graphics&, const juce::Slider&, const juce::String&, juce::Colour, bool above);
    juce::String displayText() const;

    MidiverbAudioProcessor& processor;
    UnitKnobLookAndFeel knobLook;

    juce::Slider mixKnob, feedbackKnob, inputKnob, outputKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment, feedbackAttachment,
                                                                          inputAttachment, outputAttachment;

    std::vector<Hit> hits;
    Hit pressedCopy {};
    bool hasPressed = false;
    bool collapsed = false;

    int shownDevice = -1;
    int shownProgram = -1;
    bool shownOverload = false;
    juce::uint32 overloadSince = 0;

    // Midiverb / Midifex DEFEAT button: remembers the program to return to.
    int programBeforeDefeat = 0;

    // Midiverb II keypad: first digit of a two-digit entry.
    int pendingDigit = -1;
    juce::uint32 pendingSince = 0;

    // Knob value briefly shown on the display while turning.
    int knobValueShown = -1;
    juce::uint32 knobValueSince = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UnitPanel)
};

class MidiverbAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    static constexpr int defaultWidth = 900;
    static constexpr int defaultHeight = 600;

    explicit MidiverbAudioProcessorEditor(MidiverbAudioProcessor&);
    ~MidiverbAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    int heightForWidth(int width) const;
    void applySizeLimits();

    MidiverbAudioProcessor& audioProcessor;
    UnitPanel panel;
    bool restoring = true;   // don't record sizes set while the editor is being built

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiverbAudioProcessorEditor)
};
