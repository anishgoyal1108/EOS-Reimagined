#ifndef EOSR_CORE_RUNTIME_H
#define EOSR_CORE_RUNTIME_H

namespace eosr {

class sdk_client;
class sdk_platform;
class tracer;

// The single process-wide SDK client that backs the flat C ABI: one per loaded library, for
// the whole EOS_Initialize..EOS_Shutdown lifetime. Two game instances that need independent
// state load the library twice. Tests construct their own sdk_client in isolation.
sdk_client& global_client();

// The process-wide observability tracer, started by EOS_Initialize and stopped by EOS_Shutdown.
// Off until a run enables it, so it is inert for a game that does not opt into tracing. Tests build
// their own tracer in isolation rather than this one.
tracer& global_tracer();

// The live platform is created and destroyed with EOS_Platform_Create / EOS_Platform_Release.
// Each create allocates a fresh instance so its handle is a distinct pointer: a handle cached
// from a released platform can never match a later one, which is what makes the release
// contract hold. platform_current returns the live platform, or null when none exists.
sdk_platform* platform_create();
sdk_platform* platform_current();
void platform_destroy();

} // namespace eosr

#endif
