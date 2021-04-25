
#include "LabSynthToy.h"
#include "TinySoundFontNode.h"
#include "LabSoundTemplateNode.h"

#define TML_IMPLEMENTATION
#include "TinySoundFont/tml.h"

// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2020, The LabSound Authors. All rights reserved.

#if defined(_MSC_VER)
    #if !defined(_CRT_SECURE_NO_WARNINGS)
        #define _CRT_SECURE_NO_WARNINGS
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
#endif

#include "LabSound/LabSound.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace lab;

// Returns input, output
inline std::pair<AudioStreamConfig, AudioStreamConfig> GetDefaultAudioDeviceConfiguration(const bool with_input = false)
{
    AudioStreamConfig inputConfig;
    AudioStreamConfig outputConfig;

    const std::vector<AudioDeviceInfo> audioDevices = lab::MakeAudioDeviceList();
    const AudioDeviceIndex default_output_device = lab::GetDefaultOutputAudioDeviceIndex();
    const AudioDeviceIndex default_input_device = lab::GetDefaultInputAudioDeviceIndex();

    AudioDeviceInfo defaultOutputInfo, defaultInputInfo;
    for (auto& info : audioDevices)
    {
        if (info.index == default_output_device.index) defaultOutputInfo = info;
        else if (info.index == default_input_device.index) defaultInputInfo = info;
    }

    if (defaultOutputInfo.index != -1)
    {
        outputConfig.device_index = defaultOutputInfo.index;
        outputConfig.desired_channels = std::min(uint32_t(2), defaultOutputInfo.num_output_channels);
        outputConfig.desired_samplerate = defaultOutputInfo.nominal_samplerate;
    }

    if (with_input)
    {
        if (defaultInputInfo.index != -1)
        {
            inputConfig.device_index = defaultInputInfo.index;
            inputConfig.desired_channels = std::min(uint32_t(1), defaultInputInfo.num_input_channels);
            inputConfig.desired_samplerate = defaultInputInfo.nominal_samplerate;
        }
        else
        {
            throw std::invalid_argument("the default audio input device was requested but none were found");
        }
    }

    return { inputConfig, outputConfig };
}


std::shared_ptr<AudioBus> MakeBusFromSampleFile(char const* const name, int argc, char** argv)
{
    std::string path_prefix = synth_toy_asset_base;
    const std::string path = path_prefix + name;
    std::shared_ptr<AudioBus> bus = MakeBusFromFile(path, false);
    if (!bus)
        throw std::runtime_error("couldn't open " + path);

    return bus;
}

void tsf_two_notes(lab::AudioContext& ac)
{
    std::shared_ptr<TinySoundFontNode> tsfNode(new TinySoundFontNode(ac));
    ac.connect(ac.device(), tsfNode, 0, 0);
    tsfNode->noteOn(0.f, 0, 48, 1.0f); //C2
    tsfNode->noteOn(0.f, 0, 52, 1.0f); //E2
    std::this_thread::sleep_for(std::chrono::seconds(1));
    tsfNode->noteOff(0.f, 0, 48);
    tsfNode->noteOn(0.f, 0, 55, 1.0f);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    tsfNode->allNotesOff(0.f);
}

void tsf_test_sf2(lab::AudioContext& ac)
{
    std::string sf2_file = std::string(synth_toy_asset_base) + "florestan-subset.sf2";
    std::shared_ptr<TinySoundFontNode> tsfNode(new TinySoundFontNode(ac));
    tsfNode->load_sf2(sf2_file.c_str());
    ac.connect(ac.device(), tsfNode, 0, 0);

    int i, Notes[7] = { 48, 50, 52, 53, 55, 57, 59 };
    // Loop through all the presets in the loaded SoundFont

    int preset_count = tsfNode->presetCount();
    for (i = 0; i < preset_count; i++)
    {
        //printf("Play note %d with preset #%d '%s'\n", Notes[i % 7], i, tsf_get_presetname(g_TinySoundFont, i));
        tsfNode->noteOff(0.f, i - 1, Notes[(i - 1) % 7]);
        tsfNode->noteOn(0.f, i, Notes[i % 7], 1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    tsfNode->allNotesOff(0.f);
}

void tsf_test_tml(lab::AudioContext& ac)
{
    std::string midi_file = std::string(synth_toy_asset_base) + "venture.mid";
    tml_message* TinyMidiLoader = tml_load_filename(midi_file.c_str());
    if (!TinyMidiLoader)
        return;

    std::string sf2_file = std::string(synth_toy_asset_base) + "florestan-subset.sf2";
    std::shared_ptr<TinySoundFontNode> tsfNode(new TinySoundFontNode(ac));
    tsfNode->load_sf2(sf2_file.c_str());
    ac.connect(ac.device(), tsfNode, 0, 0);

    double g_Msec = 0;               //current playback time
    tml_message* curr_MidiMessage = TinyMidiLoader;  //next message to be played

    auto start = std::chrono::system_clock::now();

    while (curr_MidiMessage != NULL)
    {
        std::chrono::duration<double> elapsed_dur = std::chrono::system_clock::now() - start;
        double elapsed_ms = elapsed_dur.count() * 1e3;
        double until = elapsed_ms + 60000.;   // queue up anything within the next second

        while (curr_MidiMessage && curr_MidiMessage->time <= until)
        {
            float when = (float(curr_MidiMessage->time) - elapsed_ms) * 1e-3;
            //printf("%f\n", when);
            switch (curr_MidiMessage->type)
            {
            case TML_PROGRAM_CHANGE: //channel program (preset) change (special handling for 10th MIDI channel with drums)
                tsfNode->channelSetPreset(when, curr_MidiMessage->channel, curr_MidiMessage->program, curr_MidiMessage->channel == 9);
                break;
            case TML_NOTE_ON: //play a note
                tsfNode->channelNoteOn(when, curr_MidiMessage->channel, curr_MidiMessage->key, curr_MidiMessage->velocity / 127.0f);
                break;
            case TML_NOTE_OFF: //stop a note
                tsfNode->channelNoteOff(when, curr_MidiMessage->channel, curr_MidiMessage->key);
                break;
            case TML_PITCH_BEND: //pitch wheel modification
                tsfNode->channelSetPitchWheel(when, curr_MidiMessage->channel, curr_MidiMessage->pitch_bend);
                break;
            case TML_CONTROL_CHANGE: //MIDI controller messages
                tsfNode->channelMidiControl(when, curr_MidiMessage->channel, curr_MidiMessage->control, curr_MidiMessage->control_value);
                break;
            }
            curr_MidiMessage = curr_MidiMessage->next;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(60000));
        break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // wait for the last notes
    tml_free(TinyMidiLoader);
}

void test_timing(lab::AudioContext& ac)
{
    std::shared_ptr<LabSoundTemplateNode> timing(new LabSoundTemplateNode(ac));
    ac.connect(ac.device(), timing, 0, 0);
    int id[20] = {
        1,2,15,4,5,6,11,16,7,8,3,12,10,17,18,13,14,9,19,20
    };
    for (int i = 0; i < 20; ++i)
        timing->realtimeEvent(double(id[i]), i);

    std::this_thread::sleep_for(std::chrono::seconds(20));
}


int main(int argc, char *argv[]) try
{
    std::unique_ptr<lab::AudioContext> context;
    const auto defaultAudioDeviceConfigurations = GetDefaultAudioDeviceConfiguration();
    context = lab::MakeRealtimeAudioContext(defaultAudioDeviceConfigurations.second, defaultAudioDeviceConfigurations.first);
    lab::AudioContext& ac = *context.get();
    //tsf_two_notes(ac);
    //tsf_test_sf2(ac);
    tsf_test_tml(ac);
    //test_timing(ac);
    return EXIT_SUCCESS;
}
catch (const std::exception & e)
{
    std::cerr << "unhandled fatal exception: " << e.what() << std::endl;
    return EXIT_FAILURE;
}

