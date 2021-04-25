
#ifndef LABSOUND_TEMPLATE_NODE
#define LABSOUND_TEMPLATE_NODE

#include <LabSound/core/AudioNode.h>

class LabSoundTemplateNode : public lab::AudioNode
{
    struct Detail;
    Detail* _detail = nullptr;
    static bool s_registered;

public:
    LabSoundTemplateNode(lab::AudioContext & ac);
    virtual ~LabSoundTemplateNode();

    static const char* static_name() { return "LabSoundTemplateNode"; }
    virtual const char* name() const override { return static_name(); }

    // AudioNode
    virtual void process(lab::ContextRenderLock &, int bufferSize) override;
    virtual void reset(lab::ContextRenderLock &) override;

    void realtimeEvent(float when, int identifier);

private:
    virtual bool propagatesSilence(lab::ContextRenderLock& r) const override { return false; }
    virtual double tailTime(lab::ContextRenderLock & r) const override { return 0; }
    virtual double latencyTime(lab::ContextRenderLock & r) const override { return 0; }
};

#endif
