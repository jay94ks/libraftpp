# raftpp Agent Instructions

[한국어](AGENTS.md)

## Objective and Scope

- Maintain the entire cowsql/raft feature set as an independent C++20 implementation.
- Preserve the consensus algorithm, state transitions, persistence and response ordering, and network and disk formats.
- The scope includes the consensus core, logs, batches, snapshots, configuration, memory management, errors, tracing, network and file I/O, public APIs, and fixtures. Adapt tests and examples as the port progresses.
- Keep existing external dependencies such as libuv and LZ4 initially. Do not introduce additional frameworks or dependencies without a task requirement.
- Enabled feature flags or a successful target build do not establish completion of the corresponding implementation or verification. Maintain current port status and completion decisions in `REWRITTEN.md` and its English counterpart.

## Instruction and Reference Scope

- This file's counterpart, the root `AGENTS.md`, applies throughout the repository. Also check ancestor instructions and any descendant `AGENTS.md` that applies to the path being modified. More specific instructions apply only to that directory and its descendants.
- Do not extend instructions from another subtree to the current work. Reading a reference file does not make that file's directory instructions apply to the files being modified.
- When following links, includes, or calls recursively, trace the dependencies needed to understand the feature's inputs, outputs, ownership, callbacks, cleanup, feature options, and tests. Do not infer behavior from direct dependencies alone.
- Track visited paths to avoid cyclic or duplicate reference traversal, and do not expand into unrelated directories or entire document collections. Distinguish the scope inspected from the scope actually modified and verified.
- Resolve document-relative links from the document's directory. Following a reference link does not automatically expand the task or instruction scope.
- Generated headers and feature options can change reference relationships. Check disabled configurations and their alternative paths and dependency requirements as well as enabled paths.

## Progress Records and API Documentation

- At the start of work, read the root [REWRITTEN.en.md](REWRITTEN.en.md) or its Korean counterpart and compare it with the actual sources. Maintain this pair as the project-wide port status record.
- Update the progress record when work starts or finishes, scope changes, or verification results change. Mark only work actually started as in progress; do not report planned work as implemented.
- Record mappings between original C sources and C++ implementations, the actual ported scope, remaining dependency layers, applicable feature options, verification performed, and limitations. Distinguish implementation completion from verification completion.
- Identify earlier verification results as historical. Do not present checks not run in the current task as newly passing.
- Keep enduring work rules in `AGENTS.md` and its English counterpart. Keep current status, work history, next steps, and verification results in `REWRITTEN.md` and its English counterpart. Do not create duplicate progress records in subdirectories.
- The repository-root `docs/` directory and all its descendants are exclusively for user-facing API documentation: public types, functions, parameters, return values, errors, ownership, callback contracts, feature-dependent usage conditions, and examples.
- Do not put work history, migration percentages or completion status, ongoing work, TODOs, roadmaps, next steps, internal work logs, or test execution history in `docs/`. Document API limitations and preconditions where relevant, without using them to report development status.
- Update relevant API documentation when public APIs or contracts change. Do not create documents in `docs/` solely to record progress. Do not reclassify original third-party licenses as API documentation.

## Documentation Languages and Counterparts

- Maintain project-authored documentation in Korean and English only. Both versions must convey the same content; neither is a summary nor a separate set of requirements.
- Preserve existing root filenames for Korean documents and use the `.en.md` suffix for English counterparts. Current pairs are `README.md` / `README.en.md`, `AGENTS.md` / `AGENTS.en.md`, `REWRITTEN.md` / `REWRITTEN.en.md`, and `LICENSE.md` / `LICENSE.en.md`.
- Place API documentation under `docs/ko/` and `docs/en/`, using matching relative paths and filenames. Both trees follow the API-documentation-only rule.
- Add, modify, or remove both language versions in the same task. Keep API signatures, behavior, errors, ownership, feature options, examples, progress status, and verification results consistent.
- Add relative links between counterparts. Do not translate code identifiers, paths, option names, or actual error messages into different values.
- Translations have the same instruction and reference scope as their counterparts; they are not additional descendant instructions. Read the language version needed and avoid recursively traversing reciprocal language links to reread the same content.
- Preserve the original upstream notice/legal text in `LICENSE.md` and third-party licenses in `deps/`. Do not translate or reorganize legal texts to enforce this language policy.

## Repository Layout

- Optional library modules implement core abstractions like the UV module. SDK include/link targets or declarations alone do not count as completed adapters. Implement stream, transport, executor or compression boundaries appropriate to each library and compose them with core.

- Do not reintroduce the original C implementation, C reference targets, or C comparison code into the repository/build. Maintain independent C++ tests using fixed format/random vectors, independent standard-container models, and state/lifetime/order contracts.
- `modules/core/include/raftpp/`: Authoritative core public headers, including support definitions required by public value types.
- `modules/<sdk>/`: optional adapters implementing core abstractions. They depend on core and are selected through `RAFTPP_MODULE_<NAME>`. Distinguish them from upstream checkouts/SDK preparation in `deps/`.
- `modules/uv/include/raftpp/uv/`: Authoritative built-in I/O/transport/stream/compression public headers.
- `modules/<module>/privates/`: Private implementation headers; never copy them into public or installed include trees.
- `modules/<module>/src/`, `modules/<module>/CMakeLists.txt`: Module sources and build rules. Core is always enabled; UV follows `RAFTPP_WITH_UV` and LZ4 is selected within UV.
- `include/`: Ignored generated mirror of the last configured active modules. Edit module originals, not this mirror. Compiler/installation inputs come from each build directory's own header copies.
- `modules/core/include/raftpp/common.hpp`: Standard aliases and common ownership templates.
- `modules/core/include/raftpp/types.hpp`: Fundamental Raft domain types/enumerations.
- `cmake/config.hpp.in`: Generated feature-configuration header template.
- `tests/`: Independent C++ regression/contract tests and test-only native syscall/error injection.
- `examples/`: native server/process-cluster examples using public C++ APIs, selected by `RAFTPP_BUILD_EXAMPLES`.
- `tools/benchmark/`: C++ measurement/reporting and native benchmark executable, selected by `RAFTPP_BUILD_BENCHMARKS`.
- `REWRITTEN.md`, `REWRITTEN.en.md`: Korean and English versions of the same overall port status, work history, verification results, and next steps.
- `docs/ko/`, `docs/en/`: Korean and English public API documentation. Exclude development status and progress records.
- `raftpp::core` and optional `raftpp::uv`: Module libraries. `raftpp::raftpp` is the interface target aggregating active modules. Enable only C++ for the project, tests, examples, and tools.
- Generate `config.hpp` and module export headers in each build directory; mirror them together with active public headers into root `include/`. Never edit generated copies. Building one configuration must not read another configuration's shared mirror.
- Preserve source attribution and references to `LICENSE.md` for derived implementations.

## Language and Naming

- Use C++20 without compiler language extensions for the library, tests, examples, and tools.
- Follow standard-library-style `snake_case` for types, functions, variables, namespaces, and enumerators.
- Prefer `inline constexpr` constants with `snake_case` names where applicable.
- Use `_type`, `_t`, and `_v` for traits and metafunctions when their meaning matches standard library conventions.
- Use the `RAFTPP_` prefix and uppercase names for project macros and header guards. Example: `RAFTPP_CONFIGURATION_HPP`.
- Do not introduce implementation-reserved identifiers, including names containing double underscores or starting with an underscore followed by an uppercase letter.
- Follow the root `.clang-format`: LLVM-based style, four-space indentation and a 100-column limit. Do not reorder includes/using declarations merely for formatting; distinguish semantic edits from formatting.

## Prefer Standard Implementations

- Do not reimplement a type or facility with an equivalent in C++20. Expose common standard types through `using` declarations or aliases in `modules/core/include/raftpp/common.hpp` and use those names.
- Do not recreate smart-pointer reference counts or control blocks, containers, strings, views, error transport, or general algorithms.
- Preserve the complete template interfaces and semantics of standard types. Do not add class wrappers merely to rename them.
- Do not assume C++23 or later facilities such as `std::expected`, `std::move_only_function`, or `std::byteswap` are available in C++20.
- When replacing containers, review element addresses, reference and iterator validity, removal order, batch sharing, and allocation-failure behavior. Do not mechanically replace intrusive queues.
- Do not indiscriminately replace aligned I/O buffers with ordinary `vector<byte>`. Use an allocator or deleter that preserves alignment and deallocation requirements.
- Serialization, CRC, SHA-1, and compression require compatibility. Retain necessary implementations or existing dependencies when the standard library has no equivalent; do not substitute `std::hash`.
- Changing random-number implementations can affect seeded output and reproducibility. Check the algorithm-preservation requirements and verification scope first.

## Ownership and Lifetime

Keep the following meanings for common types.

| Type | Meaning |
| --- | --- |
| `owned<T, Deleter>` | Exclusive ownership based on `std::unique_ptr`. Moving transfers ownership. |
| `unique<T, Deleter>` | Compatibility alias for `owned`. |
| `shared<T>` | Shared ownership based on `std::shared_ptr`. Use only when lifetime sharing is needed. |
| `weak<T>` | Non-owning observation based on `std::weak_ptr`. Use `lock()` to obtain a valid owning reference. |
| `borrowed<T>` | Nullable non-owning `T*`. Does not extend lifetime. |
| `ref<T>` | Required non-owning reference based on `std::reference_wrapper<T>`. The referent's lifetime must still be guaranteed. |
| `view<T, N>` | Contiguous view based on `std::span`. Use `view<const T>` for read-only access. |

- Do not add pointer ownership layers where values already own their data. For example, `buffer` owns its data as a move-only value.
- Prefer declarations and calls that make ownership, borrowing, and transfer readable. Do not require ordinary users to manipulate reference counts or repeatedly extend lifetimes manually.
- Non-owning pointers, references, and views do not guarantee their referents' lifetimes. `shared` does not synchronize concurrent access to the object.
- Keep asynchronous request state and required data alive until actual completion. Objects passed to callbacks must retain stable addresses rather than being moved or freed prematurely.
- If multiple entries reference a batch, distinguish batch ownership from entry views. Do not change the relationship into independent ownership per entry.
- Make ownership on failure explicit and honor the state-preservation and resource-cleanup guarantees of new APIs.

## Public APIs and Asynchronous Model

- Use return values for synchronous operations, request objects and completion callbacks for asynchronous operations, and completion callbacks for shutdown.
- Audit callback, setting, and user-context contracts in public structures as well as public function lists. Do not discard functional fields in conditional legacy extensions as mere reserved/ABI slots.
- Do not add coroutine-based methods, `task` or promise implementations, coroutine-framework adapters, or future-based replacement APIs. The plan to expose coroutines was withdrawn.
- Distinguish immediate submission failure from asynchronous completion failure and check each operation's callback contract. Do not confuse ownership transfer with successful completion.
- Distinguish initiating shutdown from completing cleanup. Do not block the event loop or free request or handle memory before shutdown completes.
- Do not treat a caller's timeout or canceled wait as withdrawal of a submitted command.
- Use standard `error_code` and the Raft error category for C++ errors. Preserve Raft status-code numeric values.
- Do not propagate exceptions across `noexcept` or C callback boundaries. Account separately for throwing constructors and standard-container operations that allocate.

## CMake and Optional Features

| Option | Defaults and constraints |
| --- | --- |
| `RAFTPP_WITH_UV` | Its initial default follows libuv detection. Enabling it requires Linux, libuv 1.18.0 or newer, and the necessary Linux headers. |
| `RAFTPP_WITH_LZ4` | Initially enabled when UV is enabled and liblz4 1.7.1 or newer is detected. Cannot be enabled without UV. |
| `RAFTPP_WITH_FIXTURE` | ON by default. Exclude fixture sources, public headers, and related test dependencies when disabled. |
| `RAFTPP_WITH_BACKTRACE` | OFF by default. Requires GNU libbacktrace headers/library and `__assert_fail` when enabled. Independent of UV/LZ4/fixture; prints a call stack on assertion failure in library builds without `NDEBUG`. |
| `RAFTPP_BUILD_TESTS` | OFF by default. Enables building CTest tests. |
| `RAFTPP_BUILD_EXAMPLES` | OFF by default. Builds C++ server/cluster examples and requires UV. Does not require tests, fixture, or LZ4. |
| `RAFTPP_BUILD_BENCHMARKS` | OFF by default. Builds the C++ native benchmark tool and requires UV. Does not require examples, tests, fixture, or LZ4. |
| `RAFTPP_INSTALL` | Initially ON at top level and OFF under `add_subdirectory`. Installs the C++ library, headers, and relocatable CMake package. |
| `BUILD_SHARED_LIBS` | Support both static and shared builds. Builds are static unless configured otherwise. |

- Apply feature options consistently to source selection, headers, link dependencies, and tests. Disabled features must not require their external libraries.
- Use the generated `raftpp/config.hpp` 0/1 macros and `raftpp::features`. Do not duplicate public feature-flag definitions elsewhere.
- Reconfiguring the same build directory with a feature disabled must not expose disabled headers/targets/symbols through active targets or installation.
- Export core and UV shared-library symbols with generated `RAFTPP_API` and `RAFTPP_UV_API` respectively; other modules use generated `RAFTPP_<MODULE>_API`.
- Configure source and generated include paths per target. Do not indiscriminately add internal directories to general include search paths where they can shadow system headers or libuv `<uv.h>`.
- Support build directories other than `build/` and consumption through a parent project's `add_subdirectory`.
- Installation exports the aggregate/module targets, generated public headers, and the upstream license. Do not embed producer source/build/optional-dependency absolute paths in the package. Changes to install/header/link rules require an independent `find_package` consumer after relocating the install prefix; reuse unchanged consumer results.

## Building and Verification

- Core is mandatory. Optional modules depend on `raftpp::core`; do not offer a core-disable option or a raftpp module configuration without core. Distinguish the SDK preparation build in `deps/` from raftpp modules.
- Build, run, and validate installed consumers for adapter tests only in Release mode. Use `CMAKE_BUILD_TYPE=Release` for single-configuration generators and select Release for both builds and CTest with multi-configuration generators. Preserve earlier Debug adapter results only as historical records. Core-only tests retain the existing policy.
- For each external library, use only APIs and dependency components required by its adapter. Build and verify only the related headers, linkage, and adapter behavior. Disable upstream tests, examples, benchmarks, documentation builds, and unrelated optional features. Do not run upstream full suites or unrelated framework functionality as adapter validation. Reuse unchanged verification records.

Default development commands for initial verification or changes affecting the full scope:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRAFTPP_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel
ctest --test-dir build -C Release --output-on-failure
```

- Before running tests, inspect the verification records in `REWRITTEN.md` and its English counterpart. Reuse existing results and skip checks when their related code and verification inputs have not changed. Elapsed time or a new work unit alone does not justify rerunning them. Skip builds/tests for documentation, translation, or progress-record-only changes.
- Related scope includes the implementation and test code being checked and relevant calls, shared headers, generated code, build rules, feature options, dependencies, compiler, and runtime environment that can affect the result. Sharing a directory or library alone does not invalidate every test. Match results per configuration; returning to a previously checked configuration alone does not require rerunning it.
- Verify only new/changed behavior or scopes lacking valid records. Select affected tests/configurations and run them once; do not repeat the full suite or option matrix by default. Apply this rule to builds, sanitizers, external consumers, and header/link checks as well. Honor an explicit user request to rerun its specified scope.
- Record the check name/scope, related sources/shared dependencies, reuse conditions such as feature options, build/link modes and tools/environment, command/time/result, and limitations. Mark a skipped rerun as `Skipped — reused existing result`, identifying the reused record and why related code is unchanged; do not report it as newly passing. Never reuse failed/interrupted/unverified results as passes or repeat a failed check without investigating and changing its related scope.
- Preserve CTest registration and test code; select execution targets according to the change. Invalidate a record when new changes affect its related scope and rerun only the necessary checks.
- For each work unit, record verification as `Check | Executed/Skipped/Unverified | Result or previous record to reuse | Change impact or reason for skipping`. Do not add skipped checks to the count of newly passing checks.
- Preserve the initial execution record. For subsequent work with no related changes, record only the section/check identifying the reused result and the reason for skipping. Do not rerun tests merely to refresh records or overwrite historical execution times/results.
- In C++ tests, check fixed serialization bytes/padding/random vectors, independent standard models, state transitions, log indices/terms, and state preservation on failure. Preserve past C comparisons as historical evidence without linking the C implementation again.
- Check truncated inputs, invalid lengths and counts, invalid IDs and roles, and size and index overflow. Do not rely solely on `assert` for tests, so verification still runs in Release builds.
- When changing feature selection, dependencies, or linking, verify affected combinations below that lack reusable results. Select static/shared modes by the same rule.

| UV | LZ4 | Fixture |
| --- | --- | --- |
| OFF | OFF | OFF |
| OFF | OFF | ON |
| ON | OFF | OFF |
| ON | OFF | ON |
| ON | ON | OFF |
| ON | ON | ON |

- Configuration checks include rejection of `UV=OFF, LZ4=ON` and disabling previously enabled features where relevant. Core-only configurations must build without optional dependencies. Reuse records when the related implementation/rules are unchanged.
- For public-header or link-requirement changes, check affected external consumers linking only `raftpp::raftpp`. Reuse results for consumer configurations with no related changes.
- Use ASan/UBSan as appropriate for lifetime and memory changes. If the environment cannot run LeakSanitizer, explicitly report the excluded leak check and do not claim leak verification passed.
- Completion reports must distinguish the actual ported scope, verified options and tests, remaining layers, and limitations. Explicitly state when the full library rewrite is incomplete.
