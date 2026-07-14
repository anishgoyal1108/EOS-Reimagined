#ifndef EOSR_CORE_STUB_COMPLETION_H
#define EOSR_CORE_STUB_COMPLETION_H

#include <cstddef>

#include "eos_common.h"

#include "core/frame_result.h"

namespace eosr {

// What an unimplemented export does instead of nothing.
//
// A game that calls an EOS function we have not built must not hang and must not crash. These three
// give the honest answer through the ordinary async engine, so a stub behaves like any other EOS
// call from the game's side -- it just always says NotImplemented.
//
// `info_size` is the size of the specific EOS_*CallbackInfo the delegate expects. Every completion
// info begins with { EOS_EResult ResultCode; void* ClientData; }, which is all we fill; the rest of
// the payload is zeroed, so a game reading further fields sees empty values rather than garbage.

// Queue an EOS_NotImplemented completion, delivered on the next EOS_Platform_Tick. A game awaiting
// this callback gets it -- which is the whole point: waiting forever is worse than a clear refusal.
void stub_complete(void* client_data, completion_delegate delegate, std::size_t info_size);

// Register a notification that never fires, and hand back a real id. The game gets something it can
// pair a Remove call with, rather than an invalid id it may treat as an error.
EOS_NotificationId stub_add_notification(void* client_data, completion_delegate delegate,
                                         std::size_t info_size, const char* event);
void stub_remove_notification(EOS_NotificationId id);

trace_return stub_not_implemented_return();
trace_return stub_notification_return(EOS_NotificationId id);

} // namespace eosr

#endif
