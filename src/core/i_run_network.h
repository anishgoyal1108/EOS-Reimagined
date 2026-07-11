#ifndef EOSR_CORE_I_RUN_NETWORK_H
#define EOSR_CORE_I_RUN_NETWORK_H

namespace eosr {

struct net_envelope;

// Implemented by every interface that listens for inbound peer messages.
// Spec: IRunNetwork / per-interface OnNetworkMessage (docs/architecture.md §4)
class i_run_network {
public:
    virtual ~i_run_network() = default;

    // Handle one decoded envelope. Called on the tick thread during message dispatch, never
    // concurrently; `message` is valid only for the duration of the call, so do not store it.
    // Returns true when the message was consumed.
    virtual bool on_network_message(const net_envelope& message) = 0;
};

} // namespace eosr

#endif
