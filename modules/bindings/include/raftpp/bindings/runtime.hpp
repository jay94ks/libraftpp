#ifndef RAFTPP_BINDINGS_RUNTIME_HPP
#define RAFTPP_BINDINGS_RUNTIME_HPP
#include <raftpp/bindings/export.hpp>
#include <raftpp/node.hpp>

namespace raftpp::bindings {
    // Factories must return components in one serialized execution domain.
    // poll is nonblocking; drain runs only during finalizer cleanup, after
    // foreign callbacks have been disabled. Owners must outlive close/drain.
    struct runtime {
        shared<void> owner;
        shared<io> backend;
        function<void()> poll;
        function<void()> drain;
        function<void()> abandon;
    };
    using runtime_factory =
        function<runtime(string_view directory, view<const byte> configuration)>;
    // Register during plugin initialization. Existing names cannot be replaced.
    RAFTPP_BINDINGS_API error_code register_runtime(string name, runtime_factory factory) noexcept;
}
// Plugins export this entry point and register factories in it. Loaded plugins
// remain resident so factory closures, component vtables and deleters stay valid.
extern "C" RAFTPP_BINDINGS_API int raftpp_binding_load_plugin(const char* path) noexcept;
#endif
