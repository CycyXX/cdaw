#include "MasterAnalyser.h"
#include <tracktion_graph/tracktion_graph.h>

namespace cdaw
{
    //==============================================================================
    namespace
    {
        /** Passes audio through unchanged and copies it into the analyser FIFO. */
        class AnalyserTapNode final : public tracktion::graph::Node
        {
        public:
            AnalyserTapNode (std::unique_ptr<tracktion::graph::Node> in, std::shared_ptr<AnalyserFifo> f)
                : input (std::move (in)), fifo (std::move (f))
            {
                setOptimisations ({ tracktion::graph::ClearBuffers::no, tracktion::graph::AllocateAudioBuffer::no });
            }

            tracktion::graph::NodeProperties getNodeProperties() override
            {
                auto props = input->getNodeProperties();

                if (props.nodeID != 0)
                    te::hash_combine (props.nodeID, static_cast<size_t> (0x43444157616e6c));   // "CDAWanl"

                return props;
            }

            std::vector<tracktion::graph::Node*> getDirectInputNodes() override   { return { input.get() }; }
            bool isReadyToProcess() override                                     { return input->hasProcessed(); }

            void prepareToPlay (const tracktion::graph::PlaybackInitialisationInfo& info) override
            {
                fifo->sampleRate = info.sampleRate;
            }

            void process (tracktion::graph::Node::ProcessContext& pc) override
            {
                auto source = input->getProcessedOutput();
                setAudioOutput (input.get(), source.audio);
                pc.buffers.midi.copyFrom (source.midi);

                if (source.audio.getNumChannels() > 0)
                    fifo->push (tracktion::graph::toAudioBuffer (source.audio));
            }

        private:
            std::unique_ptr<tracktion::graph::Node> input;
            std::shared_ptr<AnalyserFifo> fifo;
        };
    }

    MasterAnalyser::MasterAnalyser (te::Edit& e) : edit (e)
    {
        edit.getTransport().addListener (this);
        install();
    }

    MasterAnalyser::~MasterAnalyser()
    {
        edit.getTransport().removeListener (this);

        if (auto epc = edit.getTransport().getCurrentPlaybackContext())
            epc->setInsertOptionalLastStageNodeForDeviceCallback ({});
    }

    void MasterAnalyser::install()
    {
        auto epc = edit.getTransport().getCurrentPlaybackContext();

        if (epc == nullptr)
            return;

        // The fifo is shared with the graph, so it outlives this object if a graph is still playing.
        auto f = fifo;
        auto* engine = &edit.engine;

        epc->setInsertOptionalLastStageNodeForDeviceCallback (
            [f, engine] (te::OutputDevice& device, const te::CreateNodeParams&, std::unique_ptr<tracktion::graph::Node> node)
                -> std::unique_ptr<tracktion::graph::Node>
            {
                // Only the main output carries the master bus (a click routed elsewhere must not mix in).
                if (device.getDeviceID() != engine->getDeviceManager().getDefaultWaveOutDeviceID())
                    return node;

                return std::make_unique<AnalyserTapNode> (std::move (node), f);
            });

        epc->reallocate();
    }
}
