#include <raftpp.hpp>
#include <iostream>
#if RAFTPP_WITH_UV
#include <uv.h>
#endif
#if RAFTPP_WITH_FIXTURE
struct package_machine : raftpp::state_machine {
    raftpp::error_code apply(raftpp::view<const raftpp::byte>, raftpp::shared<void>&) override {
        return {};
    }
    raftpp::error_code restore(raftpp::shared_buffer) override {
        return {};
    }
    raftpp::error_code snapshot(raftpp::vector<raftpp::shared_buffer>&) override {
        return raftpp::errc::io_error;
    }
};
#endif

int main() {
    namespace r = raftpp;
    r::configuration configuration;
    if (configuration.add(1, "installed", r::role::voter))
        return 1;
    r::buffer encoded;
    if (configuration.encode(encoded))
        return 2;
    r::core_state core;
    core.id = 1;
    core.address = "installed";
    r::start_event start;
    start.term = 1;
    start.entries.push_back({1, r::entry_type::change, r::shared_buffer{std::move(encoded)}});
    if (r::step(core, {0, 8192, std::move(start)}) || r::state(core) != r::node_state::leader)
        return 3;
    if (r::step(core,
                {1, 8192,
                 r::submit_event{{{1, r::entry_type::command, r::shared_buffer{r::buffer{8}}}}}}) ||
        r::step(core, {2, 8192, r::persisted_entries_event{2}}) || core.commit_index != 2)
        return 4;
#if RAFTPP_WITH_UV
    // Pull the compression object from static archives, including private LZ4 links.
    r::buffer output;
    if (r::decompress({}, output) != (r::features::lz4 ? r::errc::io_error : r::errc::invalid))
        return 5;
#endif
#if RAFTPP_WITH_FIXTURE
    r::fixture fixture;
    if (fixture.size() != 0)
        return 6;
    if (fixture.grow(std::make_shared<package_machine>()))
        return 8;
    unsigned notifications = 0;
    r::configuration fixture_configuration;
    if (fixture.get(0).set_step_callback([&] { ++notifications; }) ||
        fixture.configuration(1, fixture_configuration) ||
        fixture.bootstrap(fixture_configuration) || fixture.start() || notifications == 0)
        return 9;
    fixture.close();
#endif
#if RAFTPP_WITH_UV
    uv_loop_t loop;
    if (uv_loop_init(&loop) || uv_loop_close(&loop))
        return 7;
#endif
    std::cout << "installed consumer: uv=" << r::features::uv << " lz4=" << r::features::lz4
              << " fixture=" << r::features::fixture << " backtrace=" << r::features::backtrace
              << '\n';
}
