#include "runtime.hpp"
#include <raftpp/composed_io.hpp>
#include <raftpp/config.hpp>
#include <chrono>
#include <thread>
#include <map>
#include <mutex>
#include <dlfcn.h>
#if RAFTPP_WITH_UV
#include <uv.h>
#include <raftpp/uv/uv_storage.hpp>
#include <raftpp/uv/uv_timer.hpp>
#include <raftpp/uv/tcp_transport.hpp>
#endif
#if RAFTPP_MODULE_ASIO
#include <raftpp/asio/asio.hpp>
#include <asio/io_context.hpp>
#endif
#if RAFTPP_MODULE_SQLITE
#include <raftpp/sqlite/sqlite.hpp>
#endif
#if RAFTPP_MODULE_ROCKSDB
#include <raftpp/rocksdb/rocksdb.hpp>
#endif
#if RAFTPP_MODULE_LZ4
#include <raftpp/lz4/lz4.hpp>
#endif
#if RAFTPP_MODULE_ZSTD
#include <raftpp/zstd/zstd.hpp>
#endif
#if RAFTPP_MODULE_ZLIB
#include <raftpp/zlib/zlib.hpp>
#endif
namespace raftpp::bindings {
    namespace {
        struct registry {
            std::mutex mutex;
            std::map<string, runtime_factory> factories;
        };
        registry& registered() {
            static registry value;
            return value;
        }
        class poll_timer final : public timer {
        public:
            completion tick;
            close_callback on_close;
            uint64_t due = 0;
            unsigned interval = 0;
            bool inside = false, closing = false;
            uint64_t time() const noexcept override {
                return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count());
            }
            error_code start(unsigned value, completion callback) override {
                if (closing || tick)
                    return errc::busy;
                if (!value || !callback)
                    return errc::invalid;
                interval = value;
                due = time() + interval;
                tick = std::move(callback);
                return {};
            }
            void close(close_callback callback) noexcept override {
                closing = true;
                on_close = std::move(callback);
                if (!inside)
                    finish();
            }
            void finish() noexcept {
                tick = {};
                auto callback = std::move(on_close);
                try {
                    if (callback)
                        callback();
                } catch (...) {
                }
            }
            void poll() {
                if (!closing && tick && time() >= due) {
                    due = time() + interval;
                    inside = true;
                    try {
                        tick({});
                    } catch (...) {
                        inside = false;
                        throw;
                    }
                    inside = false;
                    if (closing)
                        finish();
                }
            }
        };
        struct environment {
#if RAFTPP_WITH_UV
            uv_loop_t loop{};
            environment() {
                if (uv_loop_init(&loop))
                    throw system_error(errc::io_error);
            }
            ~environment() {
                uv_run(&loop, UV_RUN_DEFAULT);
                uv_loop_close(&loop);
            }
#endif
#if RAFTPP_MODULE_ASIO
            asio::io_context context;
#endif
            shared<poll_timer> polling;
            void poll() {
#if RAFTPP_WITH_UV
                uv_run(&loop, UV_RUN_NOWAIT);
#endif
#if RAFTPP_MODULE_ASIO
                if (context.stopped())
                    context.restart();
                context.poll();
#endif
                if (polling)
                    polling->poll();
            }
            void drain() {
#if RAFTPP_WITH_UV
                uv_run(&loop, UV_RUN_DEFAULT);
#endif
#if RAFTPP_MODULE_ASIO
                if (context.stopped())
                    context.restart();
                context.run();
#endif
            }
        };
    }
    error_code register_runtime(string name, runtime_factory factory) noexcept {
        try {
            if (name.empty() || name.find('\0') != string::npos || name == "composed" || !factory)
                return errc::invalid;
            auto& registry = registered();
            std::lock_guard guard(registry.mutex);
            if (!registry.factories.emplace(std::move(name), std::move(factory)).second)
                return errc::busy;
            return {};
        } catch (const std::bad_alloc&) {
            return errc::out_of_memory;
        } catch (...) {
            return errc::invalid;
        }
    }
    namespace detail {
        uint64_t features() noexcept {
            return uint64_t(RAFTPP_WITH_UV) | (uint64_t(RAFTPP_MODULE_ASIO) << 1) |
                   (uint64_t(RAFTPP_MODULE_SQLITE) << 2) | (uint64_t(RAFTPP_MODULE_ROCKSDB) << 3) |
                   (uint64_t(RAFTPP_MODULE_LZ4) << 4) | (uint64_t(RAFTPP_MODULE_ZSTD) << 5) |
                   (uint64_t(RAFTPP_MODULE_ZLIB) << 6) | (uint64_t(RAFTPP_MODULE_ASYNCPP) << 7) |
                   (uint64_t(RAFTPP_MODULE_MVFST) << 8) | (uint64_t(RAFTPP_MODULE_MSQUIC) << 9);
        }
        runtime make_runtime(string_view directory, const raftpp_binding_runtime_options& options,
                             shared<packet_transport>& packets) {
            if (string_view(options.factory) != "composed") {
                runtime_factory factory;
                {
                    auto& registry = registered();
                    std::lock_guard guard(registry.mutex);
                    const auto found = registry.factories.find(options.factory);
                    if (found == registry.factories.end())
                        throw system_error(errc::not_found);
                    factory = found->second;
                }
                return factory(directory, {reinterpret_cast<const byte*>(options.configuration),
                                           options.configuration_size});
            }
            const string_view storage_kind(options.storage), timer_kind(options.timer),
                network_kind(options.network), codec_kind(options.codec);
            // Validate before allocating loop-backed objects; partial construction
            // must not strand live UV handles if a later selector is invalid.
            if ((storage_kind != "uv" && storage_kind != "sqlite" && storage_kind != "rocksdb") ||
                (timer_kind != "uv" && timer_kind != "asio" && timer_kind != "poll") ||
                (network_kind != "uv" && network_kind != "custom") ||
                (codec_kind != "none" && codec_kind != "lz4" && codec_kind != "zstd" &&
                 codec_kind != "zlib"))
                throw system_error(errc::invalid);
            const auto supported = features();
            auto require = [&](bool selected, unsigned bit) {
                if (selected && !(supported & (uint64_t(1) << bit)))
                    throw system_error(errc::not_found);
            };
            require(storage_kind == "uv" || timer_kind == "uv" || network_kind == "uv", 0);
            require(timer_kind == "asio", 1);
            require(storage_kind == "sqlite", 2);
            require(storage_kind == "rocksdb", 3);
            require(codec_kind == "lz4", 4);
            require(codec_kind == "zstd", 5);
            require(codec_kind == "zlib", 6);
            if (options.max_snapshot_size > SIZE_MAX ||
                options.max_encoded_snapshot_size > SIZE_MAX)
                throw system_error(errc::too_big);
            if (options.segment_size > SIZE_MAX || options.block_size > SIZE_MAX ||
                options.max_entry_size > SIZE_MAX || options.max_entries > SIZE_MAX ||
                options.auto_recovery > 1)
                throw system_error(errc::invalid);
            storage_limits limits{size_t(options.max_entry_size),
                                  size_t(options.max_encoded_snapshot_size),
                                  size_t(options.max_entries)};
            auto owner = std::make_shared<environment>();
            shared<storage> disk;
            shared<timer> clock;
            shared<transport> network;
            shared<compression_codec> codec;
#if RAFTPP_MODULE_LZ4
            if (codec_kind == "lz4")
                codec = std::make_shared<lz4_codec>();
#endif
#if RAFTPP_MODULE_ZSTD
            if (codec_kind == "zstd")
                codec = std::make_shared<zstd_codec>();
#endif
#if RAFTPP_MODULE_ZLIB
            if (codec_kind == "zlib")
                codec = std::make_shared<zlib_codec>();
#endif
#if RAFTPP_MODULE_SQLITE
            if (storage_kind == "sqlite")
                disk = std::make_shared<sqlite_storage>(fs::path(directory), limits);
#endif
#if RAFTPP_MODULE_ROCKSDB
            if (storage_kind == "rocksdb")
                disk = std::make_shared<rocksdb_storage>(fs::path(directory), limits);
#endif
#if RAFTPP_MODULE_ASIO
            if (timer_kind == "asio")
                clock = std::make_shared<asio_timer>(owner->context.get_executor());
#endif
#if RAFTPP_WITH_UV
            uv_io_options uv_options;
            uv_options.segment_size = size_t(options.segment_size);
            uv_options.block_size = size_t(options.block_size);
            uv_options.disk_retry = options.disk_retry;
            uv_options.auto_recovery = options.auto_recovery;
            uv_options.max_snapshot_size = size_t(options.max_encoded_snapshot_size);
            if (storage_kind == "uv")
                disk = std::make_shared<uv_storage>(owner->loop, fs::path(directory), uv_options);
            if (timer_kind == "uv")
                clock = std::make_shared<uv_timer>(owner->loop);
            if (network_kind == "uv") {
                tcp_transport_options settings;
                settings.connect_retry = options.connect_retry;
                network = std::make_shared<tcp_transport>(owner->loop, settings);
            }
#endif
            if (timer_kind == "poll") {
                owner->polling = std::make_shared<poll_timer>();
                clock = owner->polling;
            }
            if (network_kind == "custom") {
                packets = std::make_shared<packet_transport>();
                network = packets;
            }
            auto backend =
                make_io({.storage = std::move(disk),
                         .timer = std::move(clock),
                         .network = std::move(network),
                         .snapshot_codec = std::move(codec),
                         .max_snapshot_size = size_t(options.max_snapshot_size),
                         .max_encoded_snapshot_size = size_t(options.max_encoded_snapshot_size),
                         .random_seed = options.random_seed});
            return {.owner = owner,
                    .backend = std::move(backend),
                    .poll = [owner] { owner->poll(); },
                    .drain = [owner] { owner->drain(); },
                    .abandon =
                        [packets] {
                            if (packets)
                                packets->abandon();
                        }};
        }
    }
}
extern "C" int raftpp_binding_load_plugin(const char* path) noexcept {
    if (!path || !*path)
        return int(raftpp::errc::invalid);
    // Deliberately retain successful plugins for process lifetime: registered
    // factories/components may still hold plugin vtables after node release.
    auto handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return int(raftpp::errc::not_found);
    using initialize = int (*)();
    auto fn = reinterpret_cast<initialize>(dlsym(handle, "raftpp_bindings_register_plugin"));
    if (!fn) {
        dlclose(handle);
        return int(raftpp::errc::invalid);
    }
    // Even a partially failing initializer might have registered a factory.
    try {
        return fn();
    } catch (...) {
        return int(raftpp::errc::invalid);
    }
}
