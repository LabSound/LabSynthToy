

#include "PocketModNode.h"
#include <LabSound/core/AudioBus.h>
#include <LabSound/core/AudioContext.h>
#include <LabSound/core/AudioNodeOutput.h>
#include <LabSound/extended/AudioContextLock.h>
#include <LabSound/extended/Registry.h>

#define POCKETMOD_IMPLEMENTATION
#include "pocketmod/pocketmod.h"

#include "concurrentqueue.h"
#include <algorithm>
#include <queue>

using namespace lab;

struct PocketModNodeEvent
{
    double when;
    int id;

    bool operator<(const PocketModNodeEvent& rhs) const
    {
        if (when > rhs.when)
            return true;
        if (when < rhs.when)
            return false;
        return id > rhs.id;
    }
};



struct PocketModNode::Detail
{
    std::priority_queue<PocketModNodeEvent> queue;
    moodycamel::ConcurrentQueue<PocketModNodeEvent> incoming;
    lab::AudioContext* ac = nullptr;
    
    float pocketmod_render_buffer[1024];
    
    pocketmod_context context;
    size_t mod_size = 0;
    char* mod_data = nullptr;
    bool mod_playing = false;

    Detail()
    {
        memset(&context, 0, sizeof(pocketmod_context));
    }
    ~Detail() = default;

    void clearSchedules()
    {
        PocketModNodeEvent s;
        while (incoming.try_dequeue(s)) {}
        while (!queue.empty())
            queue.pop();
    }
};

PocketModNode::PocketModNode(AudioContext& ac)
: AudioNode(ac)
, _detail(new Detail())
{
    _detail->ac = &ac;
    addOutput(std::unique_ptr<AudioNodeOutput>(new AudioNodeOutput(this, 2)));

    if (s_registered)
        initialize();
}

bool PocketModNode::s_registered = NodeRegistry::Register(PocketModNode::static_name(),
    [](AudioContext& ac)->AudioNode* { return new PocketModNode(ac); },
    [](AudioNode* n) { delete n; });

PocketModNode::~PocketModNode()
{
    _detail->clearSchedules();
    uninitialize();
    delete _detail;
}

void PocketModNode::loadMOD(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        printf("Couldn't open %s\n", path);
        return;
    }

    fseek(f, 0, SEEK_END);
    _detail->mod_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    _detail->mod_data = (char*) malloc(_detail->mod_size);
    fread(_detail->mod_data, 1, _detail->mod_size, f);
    fclose(f);
    
    if (!pocketmod_init(&_detail->context, _detail->mod_data, _detail->mod_size, _detail->ac->sampleRate()))
    {
        printf("Couldn't load MOD file\n");
        return;
    }

    
    _detail->mod_playing = true;
}

void PocketModNode::process(ContextRenderLock &r, int bufferSize)
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
        PocketModNodeEvent s;
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
    
    if (_detail->mod_playing)
    {
        int i = 0;
        while (i < bufferSize)
        {
            // over-rendering the buffer, as pocketmod doesn't seem to work with buffer sizes
            // other than 1024.
            i += pocketmod_render(&_detail->context, &_detail->pocketmod_render_buffer[i], 1024);//  AudioNode::ProcessingSizeInFrames - i);
        }
        
        float* dataL = outputBus->channel(0)->mutableData();
        float* dataR = outputBus->channel(1)->mutableData();
        for (i = 0; i < AudioNode::ProcessingSizeInFrames; ++i)
        {
            dataL[i] = _detail->pocketmod_render_buffer[i * 2];
            dataR[i] = _detail->pocketmod_render_buffer[i * 2 + 1];
        }
    }

    //if (events > 0)
        outputBus->clearSilentFlag();
}

void PocketModNode::reset(ContextRenderLock&)
{
    _detail->clearSchedules();
}
