
#ifndef TINYSOUNDFONTNODE_H
#define TINYSOUNDFONTNODE_H

#include <LabSound/core/AudioNode.h>


class TinySoundFontNode : public lab::AudioNode
{
    struct Detail;
    Detail* _detail = nullptr;
    static bool s_registered;

public:
    TinySoundFontNode(lab::AudioContext & ac);
    virtual ~TinySoundFontNode();

    static const char* static_name() { return "TinySoundFont"; }
    virtual const char* name() const override { return static_name(); }

    // AudioNode
    virtual void process(lab::ContextRenderLock &, int bufferSize) override;
    virtual void reset(lab::ContextRenderLock &) override;

    void load_sf2(char const*const path);
    int presetCount() const;

    void noteOn(float when, int preset_index, int key, float vel);
    void noteOff(float when, int preset_index, int key);

    void channelNoteOn(float when, int channel, int key, float vel);
    void channelNoteOff(float when, int channel, int key);

    void channelSetPreset(float when, int channel, int program, bool midi_drums);
    void channelSetPitchWheel(float when, int channel, int bend);
    void channelMidiControl(float when, int channel, int control, int value);

    void allNotesOff(float when);

private:
    virtual bool propagatesSilence(lab::ContextRenderLock& r) const override { return false; }
    virtual double tailTime(lab::ContextRenderLock & r) const override { return 0; }
    virtual double latencyTime(lab::ContextRenderLock & r) const override { return 0; }
};

#endif
