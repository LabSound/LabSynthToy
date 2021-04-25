
#include "LabSoundTemplateNode.h"
#include <LabSound/core/AudioBus.h>
#include <LabSound/core/AudioContext.h>
#include <LabSound/core/AudioNodeOutput.h>
#include <LabSound/extended/AudioContextLock.h>
#include <LabSound/extended/Registry.h>

#include "concurrentqueue.h"
#include <algorithm>
#include <queue>

using namespace lab;

struct LabSoundTemplateNodeEvent
{
    double when;
    int id;

    bool operator<(const LabSoundTemplateNodeEvent& rhs) const
    {
        if (when > rhs.when)
            return true;
        if (when < rhs.when)
            return false;
        return id > rhs.id;
    }
};



struct LabSoundTemplateNode::Detail
{
    std::priority_queue<LabSoundTemplateNodeEvent> queue;
    moodycamel::ConcurrentQueue<LabSoundTemplateNodeEvent> incoming;
    lab::AudioContext* ac = nullptr;

    Detail() = default;
    ~Detail() = default;

    void clearSchedules()
    {
        LabSoundTemplateNodeEvent s;
        while (incoming.try_dequeue(s)) {}
        while (!queue.empty())
            queue.pop();
    }
};

LabSoundTemplateNode::LabSoundTemplateNode(AudioContext& ac)
: AudioNode(ac)
, _detail(new Detail())
{
    _detail->ac = &ac;
    addOutput(std::unique_ptr<AudioNodeOutput>(new AudioNodeOutput(this, 1)));

    if (s_registered)
        initialize();
}

bool LabSoundTemplateNode::s_registered = NodeRegistry::Register(LabSoundTemplateNode::static_name(),
    [](AudioContext& ac)->AudioNode* { return new LabSoundTemplateNode(ac); },
    [](AudioNode* n) { delete n; });

LabSoundTemplateNode::~LabSoundTemplateNode()
{
    _detail->clearSchedules();
    uninitialize();
    delete _detail;
}

void LabSoundTemplateNode::realtimeEvent(float when, int identifier)
{
    double now = _detail->ac->currentTime();
    _detail->incoming.enqueue({when + now, identifier});
}

void LabSoundTemplateNode::process(ContextRenderLock &r, int bufferSize)
{
    AudioBus * outputBus = output(0)->bus(r);

    if (!isInitialized())
    {
        if (outputBus)
            outputBus->zero();

        _detail->clearSchedules();
        return;
    }

    // move incoming commands to the internal schedule
    {
        LabSoundTemplateNodeEvent s;
        while (_detail->incoming.try_dequeue(s))
        {
            _detail->queue.push(s);
        }
    }

    auto& ac = *r.context();
    double quantumStart = ac.currentTime();
    double quantumEnd = quantumStart + (double) bufferSize / ac.sampleRate();

    // any events to service now?

    int events = 0;
    outputBus->zero();
    while (!_detail->queue.empty() && _detail->queue.top().when < quantumEnd)
    {
        auto& top = _detail->queue.top();

        // compute the exact sample the event occurs at
        int offset = (top.when < quantumStart) ? 0 : static_cast<int>((top.when - quantumStart) * ac.sampleRate());

        // sanity to guard against rounding problems
        if (offset > bufferSize - 1)
            offset = bufferSize - 1;

        // make a little popping sound
        float* data = outputBus->channel(0)->mutableData();
        for (int i = std::max(0, offset - 4); i < std::min(offset + 4, bufferSize); ++i)
            data[i] = 1;

        _detail->queue.pop();
        ++events;
    }

    if (events > 0)
        outputBus->clearSilentFlag();
}

void LabSoundTemplateNode::reset(ContextRenderLock&)
{
    _detail->clearSchedules();
}
