#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

#include "Lfo.h"
#include "Effects.h"

const char* MidiverbAudioProcessor::getDeviceName(int deviceIndex)
{
    if (deviceIndex >= 0 && deviceIndex < NUM_DEVICES)
        return devices[deviceIndex].name;
    return "Unknown";
}

int MidiverbAudioProcessor::getDeviceNumEffects(int deviceIndex)
{
    if (deviceIndex >= 0 && deviceIndex < NUM_DEVICES)
        return devices[deviceIndex].numEffects;
    return 0;
}

int MidiverbAudioProcessor::getDeviceDisplayOffset(int deviceIndex)
{
    if (deviceIndex >= 0 && deviceIndex < NUM_DEVICES)
        return devices[deviceIndex].displayOffset;
    return 0;
}

const char* MidiverbAudioProcessor::getEffectName(int deviceIndex, int effectIndex)
{
    if (deviceIndex >= 0 && deviceIndex < NUM_DEVICES) {
        const auto& dev = devices[deviceIndex];
        if (effectIndex >= 0 && effectIndex < dev.numEffects)
            return dev.effectNames[effectIndex];
    }
    return "Unknown";
}

static inline float saturate(float x)
{
    if (x <= -1.0f) return -1.0f;
    if (x >= 1.0f) return 1.0f;
    return x;
}

MidiverbAudioProcessor::MidiverbAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    DRAM.fill(0);
    lfo1 = new Lfo();
    lfo2 = new Lfo();
}

MidiverbAudioProcessor::~MidiverbAudioProcessor()
{
    delete lfo1;
    delete lfo2;
}

juce::AudioProcessorValueTreeState::ParameterLayout MidiverbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Device selection (0=MIDIVerb, 1=MidiFex, 2=MIDIVerb 2)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"device", 1}, "Device", 0, NUM_DEVICES - 1, 2));  // Default: MIDIVerb 2

    // Program selection (0-99 range to cover all devices)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"program", 1}, "Program", 0, MAX_PROGRAMS - 1, 1));  // Default: program 1

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"dryWet", 1}, "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"feedback", 1}, "Feedback",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    return { params.begin(), params.end() };
}

const juce::String MidiverbAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MidiverbAudioProcessor::acceptsMidi() const { return false; }
bool MidiverbAudioProcessor::producesMidi() const { return false; }
bool MidiverbAudioProcessor::isMidiEffect() const { return false; }
double MidiverbAudioProcessor::getTailLengthSeconds() const { return 2.0; }

int MidiverbAudioProcessor::getNumPrograms() { return MAX_PROGRAMS; }
int MidiverbAudioProcessor::getCurrentProgram()
{
    return static_cast<int>(*apvts.getRawParameterValue("program"));
}

void MidiverbAudioProcessor::setCurrentProgram(int index)
{
    apvts.getParameter("program")->setValueNotifyingHost(
        apvts.getParameter("program")->convertTo0to1(static_cast<float>(index)));
}

const juce::String MidiverbAudioProcessor::getProgramName(int index)
{
    int deviceIndex = static_cast<int>(*apvts.getRawParameterValue("device"));
    int displayNum = index + getDeviceDisplayOffset(deviceIndex);
    return juce::String(displayNum) + ": " + getEffectName(deviceIndex, index);
}

void MidiverbAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

void MidiverbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    oversampling = juce::jmax(1, (int) std::ceil(INTERNAL_MIN_RATE / sampleRate - 1e-9));
    internalRate = sampleRate * oversampling;

    inputModel.design(analog::midiverbInput(), internalRate);
    outputModelL.design(analog::midiverbOutput(), internalRate);
    outputModelR.design(analog::midiverbOutput(), internalRate);

    mv2Input.design(analog::midiverb2Input(), internalRate);
    mv2OutputL.design(analog::midiverb2Output(), internalRate);
    mv2OutputR.design(analog::midiverb2Output(), internalRate);

    const double decimationCutoff = juce::jmin(20000.0, 0.45 * sampleRate);
    decimateL.setCutoff(internalRate, decimationCutoff);
    interpolateIn.setCutoff(internalRate, decimationCutoff);
    decimateR.setCutoff(internalRate, decimationCutoff);

    resetSignalState();
}

void MidiverbAudioProcessor::resetSignalState()
{
    inputModel.reset();
    outputModelL.reset();
    outputModelR.reset();
    mv2Input.reset();
    mv2OutputL.reset();
    mv2OutputR.reset();
    decimateL.reset();
    decimateR.reset();
    interpolateIn.reset();
    prevAnalogIn = 0.0;
    heldL = heldR = 0.0;

    // Reset effect state
    DRAM.fill(0);
    memoryPointer = 0;
    lastWetL = 0.0;
    lastWetR = 0.0;
    phase = 0.0;

    // Reset LFO state
    currentLfoPatch = nullptr;
    lfo1Value = 0;
    lfo2Value = 0;
    lfoSampleCounter = 0;
    lastDeviceIndex = -1;
    lastEffectIndex = -1;
}

void MidiverbAudioProcessor::releaseResources()
{
}

bool MidiverbAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void MidiverbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    int deviceIndex = static_cast<int>(*apvts.getRawParameterValue("device"));
    int effectIndex = static_cast<int>(*apvts.getRawParameterValue("program"));
    float dryWet = *apvts.getRawParameterValue("dryWet");
    float feedback = *apvts.getRawParameterValue("feedback");

    // Clamp effect index to valid range for current device
    const auto& dev = devices[deviceIndex];
    if (effectIndex < 0) effectIndex = 0;
    if (effectIndex >= dev.numEffects) effectIndex = dev.numEffects - 1;

    // Get effect function pointer
    auto effectFn = dev.effects[effectIndex];

    // Check if device or effect changed - reinitialize LFOs if needed
    if (deviceIndex != lastDeviceIndex || effectIndex != lastEffectIndex)
    {
        if (deviceIndex != lastDeviceIndex)
        {
            // Different converter filters: start them from silence
            inputModel.reset(); outputModelL.reset(); outputModelR.reset();
            mv2Input.reset(); mv2OutputL.reset(); mv2OutputR.reset();
            prevAnalogIn = 0.0;
            heldL = heldR = 0.0;
        }
        lastDeviceIndex = deviceIndex;
        lastEffectIndex = effectIndex;

        // Find LFO patch for this effect (only MIDIVerb 2 effects 50-69 have LFO)
        if (init_lfo_for_program(effectIndex, lfo1, lfo2, &currentLfoPatch) && deviceIndex == 2)
        {
            lfoSampleCounter = 0;
            lfo1Value = lfo1->update(lfo1) | (static_cast<uint32_t>(currentLfoPatch->top1) << 16);
            lfo2Value = lfo2->update(lfo2) | (static_cast<uint32_t>(currentLfoPatch->top2) << 16);
        }
        else
        {
            currentLfoPatch = nullptr;
            lfo1Value = 0;
            lfo2Value = 0;
        }
    }

    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getWritePointer(1);
    int numSamples = buffer.getNumSamples();

    float angle = dryWet * 1.5707963f;  // pi/2
    float equalPowerDry = std::cos(angle);
    float equalPowerWet = std::sin(angle);

    const bool mv1Model = dev.analogModel == 0;
    const bool adc12bit = dev.adc12bit;
    const double phaseIncrement = dev.sampleRate / internalRate;

    for (int i = 0; i < numSamples; i++)
    {
        float dryL = leftChannel[i];
        float dryR = rightChannel[i];

        // Mono sum (the units sum L+R before the ADC), plus optional feedback
        const double monoIn = (dryL + dryR) * 0.5 + (lastWetL + lastWetR) * 0.5 * feedback;

        double wetL = 0.0, wetR = 0.0;
        for (int k = 1; k <= oversampling; ++k)
        {
            // Upsample to the internal rate (zero-stuffing + interpolation filter),
            // then the input (anti-alias) filter
            const double x = oversampling > 1
                ? interpolateIn.process(k == 1 ? monoIn * oversampling : 0.0)
                : monoIn;
            const double analogIn = mv1Model ? inputModel.process(x) : mv2Input.process(x);

            // Keep last value of the DAC hold for the part of this internal sample before an update
            double boxL = heldL, boxR = heldR;

            phase += phaseIncrement;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                // Time since the sampling instant, in internal samples (0..1)
                const double sinceEdge = phase / phaseIncrement;

                // ADC sample-and-hold: value at the exact sampling instant
                const double sampled = analogIn + (prevAnalogIn - analogIn) * sinceEdge;
                if (std::abs(sampled) > 1.0)
                    inputOverload.store(true, std::memory_order_relaxed);
                const float clipped = saturate(static_cast<float>(sampled));

                int16_t inputInt;
                if (adc12bit)
                {
                    // 12-bit successive approximation (truncating), shifted left one bit
                    // into the 13-bit DSP input range; bit 0 reads as 1 (as in MAME's driver)
                    const int code = juce::jlimit(-2048, 2047, (int) std::floor(clipped * 2047.0f));
                    inputInt = static_cast<int16_t>(code * 2 + 1);
                }
                else
                {
                    inputInt = static_cast<int16_t>(clipped * 0x0fff);
                }

                int16_t outLeftInt, outRightInt;
                effectFn(inputInt, &outLeftInt, &outRightInt, DRAM.data(), memoryPointer++, lfo1Value, lfo2Value);

                // Update LFO every 8 effect samples
                if (currentLfoPatch != nullptr)
                {
                    lfoSampleCounter++;
                    if (lfoSampleCounter >= LFO_UPDATE_INTERVAL)
                    {
                        lfoSampleCounter = 0;
                        lfo1Value = lfo1->update(lfo1) | (static_cast<uint32_t>(currentLfoPatch->top1) << 16);
                        lfo2Value = lfo2->update(lfo2) | (static_cast<uint32_t>(currentLfoPatch->top2) << 16);
                    }
                }

                if (adc12bit)
                {
                    // 13-bit DAC with saturation logic
                    outLeftInt = static_cast<int16_t>(juce::jlimit<int>(-4095, 4095, outLeftInt));
                    outRightInt = static_cast<int16_t>(juce::jlimit<int>(-4095, 4095, outRightInt));
                }
                const double newL = outLeftInt / static_cast<double>(0x0fff);
                const double newR = outRightInt / static_cast<double>(0x0fff);

                // DAC zero-order hold: the step happened sinceEdge before this sample,
                // so average the old and new level over the internal sample period
                boxL = heldL + (newL - heldL) * sinceEdge;
                boxR = heldR + (newR - heldR) * sinceEdge;
                heldL = newL;
                heldR = newR;
            }
            prevAnalogIn = analogIn;

            // Output (reconstruction) filter, then decimation filter
            double yL = mv1Model ? outputModelL.process(boxL) : mv2OutputL.process(boxL);
            double yR = mv1Model ? outputModelR.process(boxR) : mv2OutputR.process(boxR);
            if (oversampling > 1)
            {
                yL = decimateL.process(yL);
                yR = decimateR.process(yR);
            }
            wetL = yL;
            wetR = yR;
        }

        // Store for feedback
        lastWetL = wetL;
        lastWetR = wetR;

        // Dry/wet mix
        leftChannel[i] = static_cast<float>(dryL * equalPowerDry + wetL * equalPowerWet);
        rightChannel[i] = static_cast<float>(dryR * equalPowerDry + wetR * equalPowerWet);
    }
}

bool MidiverbAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* MidiverbAudioProcessor::createEditor()
{
    return new MidiverbAudioProcessorEditor(*this);
}

void MidiverbAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MidiverbAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidiverbAudioProcessor();
}
