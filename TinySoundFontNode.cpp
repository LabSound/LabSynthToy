
#include "TinySoundFontNode.h"
#include <LabSound/core/AudioBus.h>
#include <LabSound/core/AudioContext.h>
#include <LabSound/extended/AudioContextLock.h>
#include <LabSound/core/AudioNodeOutput.h>
#include <LabSound/extended/Registry.h>
#include <memory>
#include <queue>

#include "concurrentqueue.h"

#define TSF_IMPLEMENTATION
#include "TinySoundFont/tsf.h"

/*

This file is copyright the LabSound Authors, and under the LabSound license.

Portions of TinySoundFont are reproduced in this file; those portions are named
here, and the license for those fragments attached:

MinimalSoundFont bore the license:

Copyright (C) 2017-2018 Bernhard Schelling (Based on SFZero, Copyright (C) 2012 Steve Folta, https://github.com/stevefolta/SFZero)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

namespace {

// From TinySoundFont/example1.c
// This is a minimal SoundFont with a single loopin saw-wave sample/instrument/preset (484 bytes)
const static unsigned char MinimalSoundFont[] =
{
    #define TEN0 0,0,0,0,0,0,0,0,0,0
    'R','I','F','F',220,1,0,0,'s','f','b','k',
    'L','I','S','T',88,1,0,0,'p','d','t','a',
    'p','h','d','r',76,TEN0,TEN0,TEN0,TEN0,0,0,0,0,TEN0,0,0,0,0,0,0,0,255,0,255,0,1,TEN0,0,0,0,
    'p','b','a','g',8,0,0,0,0,0,0,0,1,0,0,0,'p','m','o','d',10,TEN0,0,0,0,'p','g','e','n',8,0,0,0,41,0,0,0,0,0,0,0,
    'i','n','s','t',44,TEN0,TEN0,0,0,0,0,0,0,0,0,TEN0,0,0,0,0,0,0,0,1,0,
    'i','b','a','g',8,0,0,0,0,0,0,0,2,0,0,0,'i','m','o','d',10,TEN0,0,0,0,
    'i','g','e','n',12,0,0,0,54,0,1,0,53,0,0,0,0,0,0,0,
    's','h','d','r',92,TEN0,TEN0,0,0,0,0,0,0,0,50,0,0,0,0,0,0,0,49,0,0,0,34,86,0,0,60,0,0,0,1,TEN0,TEN0,TEN0,TEN0,0,0,0,0,0,0,0,
    'L','I','S','T',112,0,0,0,'s','d','t','a','s','m','p','l',100,0,0,0,86,0,119,3,31,7,147,10,43,14,169,17,58,21,189,24,73,28,204,31,73,35,249,38,46,42,71,46,250,48,150,53,242,55,126,60,151,63,108,66,126,72,207,
        70,86,83,100,72,74,100,163,39,241,163,59,175,59,179,9,179,134,187,6,186,2,194,5,194,15,200,6,202,96,206,159,209,35,213,213,216,45,220,221,223,76,227,221,230,91,234,242,237,105,241,8,245,118,248,32,252
};

const int command_note_on = 0;
const int command_note_off = 2;
const int command_note_all_off = 3;
const int command_clear_schedule = 4;
const int command_channel_note_on = 5;
const int command_channel_note_off = 6;
const int command_set_drums_preset = 7;
const int command_set_preset = 8;
const int command_channel_pitchbend = 9;
const int command_channel_midi_control = 10;

    struct Scheduled
    {
        double when;
        int preset_index; int key; int aux; float vel;
        int command;
        int id; // id enforces total order, if two midi commands occur simultaneously, their total enqueue order will be respected

        bool operator<(const Scheduled& rhs) const
        {
            if (when > rhs.when)
                return true;
            if (when < rhs.when)
                return false;
            return id > rhs.id;
        }
    };



} // anon



struct TinySoundFontNode::Detail
{
    tsf* sound_font = nullptr;
    moodycamel::ConcurrentQueue<Scheduled> incoming;
    std::priority_queue<Scheduled> queue;
    int rate = 0;
    int id = 0;

    Detail(float rate)
    : rate((int) rate)
    {
        // by default have the MinimalSoundFont loaded.
        sound_font = tsf_load_memory(MinimalSoundFont, sizeof(MinimalSoundFont));
        if (sound_font)
        {
            tsf_set_output(sound_font, TSF_MONO, (int) rate, -10);
            tsf_set_max_voices(sound_font, 128);
        }
    }

    ~Detail()
    {
        if (sound_font)
        {
            tsf_close(sound_font);
            sound_font = nullptr;
        }
    }

    void load_sf2(char const*const path)
    {
        if (sound_font)
        {
            tsf_close(sound_font);
            sound_font = nullptr;
        }

        sound_font = tsf_load_filename(path);
        if (sound_font)
        {
            tsf_set_output(sound_font, TSF_MONO, rate, -10);
            tsf_set_max_voices(sound_font, 128);
        }
    }

    void clearSchedules()
    {
        Scheduled s;
        while (incoming.try_dequeue(s)) {}
        while (!queue.empty())
            queue.pop();
        incoming.enqueue({ 0., 0,0,0, command_clear_schedule, ++id });
    }


};

using namespace lab;

TinySoundFontNode::TinySoundFontNode(AudioContext& ac)
: AudioNode(ac)
, _detail(new Detail(ac.sampleRate()))
{
    addOutput(std::unique_ptr<AudioNodeOutput>(new AudioNodeOutput(this, 1)));

    if (s_registered)
        initialize();
}

bool TinySoundFontNode::s_registered = NodeRegistry::Register(TinySoundFontNode::static_name(),
    [](AudioContext& ac)->AudioNode* { return new TinySoundFontNode(ac); },
    [](AudioNode* n) { delete n; });

TinySoundFontNode::~TinySoundFontNode()
{
    _detail->clearSchedules();
    uninitialize();
    delete _detail;
}

void TinySoundFontNode::load_sf2(char const*const path)
{
    _detail->load_sf2(path);
}

int TinySoundFontNode::presetCount() const
{
    if (!_detail->sound_font)
        return 0;

    return tsf_get_presetcount(_detail->sound_font);
}


void TinySoundFontNode::process(ContextRenderLock &r, int bufferSize)
{
    AudioBus * outputBus = output(0)->bus(r);
    if (!isInitialized())
    {
        if (outputBus)
            outputBus->zero();

        _detail->clearSchedules();
        return;
    }

    // move requested starts to the internal schedule if there's a source bus.
    // if there's no source bus, the schedule requests are discarded.
    {
        Scheduled s;
        while (_detail->incoming.try_dequeue(s))
        {
            if (s.command == command_clear_schedule)
            {
                while (!_detail->queue.empty())
                    _detail->queue.pop();
            }
            else
            {
                _detail->queue.push(s);
            }
        }
    }

    auto& ac = *r.context();
    double quantumStart = ac.currentTime();
    double quantumEnd = quantumStart + (double)bufferSize / ac.sampleRate();

    // any events to service now?

    int events = 0;
    outputBus->zero();
    while (!_detail->queue.empty() && _detail->queue.top().when < quantumEnd)
    {
        auto& s = _detail->queue.top();

        // note, although we could calculate an exact note start time,
        // tinysoundfont doesn't offer that granularity.
        //size_t offset = (s.when < quantumStartTime) ? 0 : static_cast<size_t>(s.when * r.context()->sampleRate());
        if (s.command == command_note_on)
        {
            tsf_note_on(_detail->sound_font, s.preset_index, s.key, s.vel);
        }
        else if (s.command == command_note_off)
        {
            tsf_note_off(_detail->sound_font, s.preset_index, s.key);
        }
        else if (s.command == command_note_all_off)
        {
            tsf_note_off_all(_detail->sound_font);
        }
        else if (s.command == command_channel_note_on)
        {
            tsf_channel_note_on(_detail->sound_font, s.preset_index, s.key, s.vel);
        }
        else if (s.command == command_channel_note_off)
        {
            tsf_channel_note_off(_detail->sound_font, s.preset_index, s.key);
        }
        else if (s.command == command_set_drums_preset)
        {
            tsf_channel_set_presetnumber(_detail->sound_font, s.preset_index, s.key, true);
        }
        else if (s.command == command_set_preset)
        {
            tsf_channel_set_presetnumber(_detail->sound_font, s.preset_index, s.key, false);
        }
        else if (s.command == command_channel_pitchbend)
        {
            tsf_channel_set_pitchwheel(_detail->sound_font, s.preset_index, s.key);
        }
        else if (s.command == command_channel_midi_control)
        {
            tsf_channel_midi_control(_detail->sound_font, s.preset_index, s.key, s.aux);
        }
        _detail->queue.pop();
    }

    tsf_render_float(_detail->sound_font, outputBus->channel(0)->mutableData(), bufferSize, 0);
    outputBus->clearSilentFlag();
}

void TinySoundFontNode::reset(ContextRenderLock & r)
{
    _detail->clearSchedules();
}

void TinySoundFontNode::noteOn(float when, int preset_index, int key, float vel)
{
    if (_detail->sound_font)
    {
        _detail->incoming.enqueue({ when, preset_index, key, 0, vel, command_note_on, ++_detail->id });
    }
}

void TinySoundFontNode::noteOff(float when, int preset_index, int key)
{
    if (_detail->sound_font)
    {
        _detail->incoming.enqueue({ when, preset_index, key, 0, 0, command_note_off, ++_detail->id });
    }
}

void TinySoundFontNode::channelNoteOn(float when, int channel, int key, float vel)
{
    if (_detail->sound_font)
    {
        _detail->incoming.enqueue({ when, channel, key, 0, vel, command_channel_note_on, ++_detail->id });
    }
}

void TinySoundFontNode::channelNoteOff(float when, int channel, int key)
{
    if (_detail->sound_font)
    {
        _detail->incoming.enqueue({ when, channel, key, 0, 0, command_channel_note_off, ++_detail->id });
    }
}

void TinySoundFontNode::channelSetPreset(float when, int channel, int program, bool midi_drums)
{
    if (_detail->sound_font)
    {
        if (midi_drums)
            _detail->incoming.enqueue({ when, channel, program, 0, 0, command_set_drums_preset, ++_detail->id });
        else
            _detail->incoming.enqueue({ when, channel, program, 0, 0, command_set_preset, ++_detail->id });
    }
}

void TinySoundFontNode::channelSetPitchWheel(float when, int channel, int bend)
{
    if (_detail->sound_font)
    {
        _detail->incoming.enqueue({ when, channel, bend, 0, 0, command_channel_pitchbend, ++_detail->id });
    }
}

void TinySoundFontNode::channelMidiControl(float when, int channel, int control, int value)
{
    if (_detail->sound_font)
    {
        _detail->incoming.enqueue({ when, channel, control, value, 0, command_channel_midi_control, ++_detail->id });
    }
}


void TinySoundFontNode::allNotesOff(float when)
{
    if (_detail->sound_font)
    {
        _detail->incoming.enqueue({ when, 0, 0, 0, 0, command_note_all_off, ++_detail->id });
    }
}


