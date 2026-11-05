#include <raftpp/bindings/runtime.hpp>
#include "composition_support.hpp"

extern "C" int raftpp_bindings_register_plugin() {
    using namespace raftpp;
    return int(bindings::register_runtime("test-memory", [](string_view, view<const byte>) {
                   auto disk = std::make_shared<composition_testing::memory_storage>();
                   auto clock = std::make_shared<composition_testing::manual_timer>();
                   auto network = std::make_shared<composition_testing::controlled_transport>();
                   auto backend = make_io({.storage = disk, .timer = clock, .network = network});
                   return bindings::runtime{
                       .backend = backend, .poll = [clock] { clock->advance(50); }, .drain = [] {}};
               }).value());
}
