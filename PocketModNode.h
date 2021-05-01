

#ifndef POCKETMOD_NODE
#define POCKETMOD_NODE

#include <LabSound/core/AudioNode.h>

class PocketModNode : public lab::AudioNode
{
    struct Detail;
    Detail* _detail = nullptr;
    static bool s_registered;

public:
    PocketModNode(lab::AudioContext & ac);
    virtual ~PocketModNode();

    static const char* static_name() { return "PocketMod"; }
    virtual const char* name() const override { return static_name(); }

    // AudioNode
    virtual void process(lab::ContextRenderLock &, int bufferSize) override;
    virtual void reset(lab::ContextRenderLock &) override;

    void loadMOD(const char* path);

private:
    virtual bool propagatesSilence(lab::ContextRenderLock& r) const override { return false; }
    virtual double tailTime(lab::ContextRenderLock & r) const override { return 0; }
    virtual double latencyTime(lab::ContextRenderLock & r) const override { return 0; }
};

#endif
