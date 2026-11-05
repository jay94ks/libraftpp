# Node.js·C# 런타임 바인딩

[English](../../en/modules/bindings.md) · [모듈 목록](index.md) · [I/O 조립](../composition.md)

`modules/bindings`는 core 노드와 선택 모듈을 조립해 실행하는 네이티브 런타임이다. C++ 구현은 이 모듈에, 언어 API는 `bindings/nodejs`와 `bindings/csharp/Raftpp`에 둔다. 공개 헤더는 `<raftpp/bindings/bindings.hpp>`와 `<raftpp/bindings/runtime.hpp>`, 타깃은 `raftpp::bindings`다. Node.js는 Node-API 8, C#은 P/Invoke를 사용한다. 인터롭 사양은 [Node-API](https://nodejs.org/api/n-api.html), [.NET 인터롭](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices)을 따른다.

## 구성과 로딩

`RAFTPP_MODULE_BINDINGS=ON`은 항상 공유 `libraftpp-bindings.so`를 만든다. core는 필수이고 UV는 선택이다. `RAFTPP_BINDINGS_NODEJS=ON`이면 `node_api.h`로 `raftpp_nodejs.node`를 추가한다. 두 옵션은 기본 OFF다. Node OFF에는 Node SDK가 필요 없다. 기본 조립은 UV 저장소·타이머·TCP이므로 UV가 필요하지만 SQLite/RocksDB+poll/Asio+사용자 transport, 또는 플러그인 런타임은 UV 없이 구성할 수 있다. SDK 모듈은 라이브러리 configure 시 선택하며 비활성 모듈을 요청하면 오류 19(`not_found`)다. 알 수 없는 조립 선택지는 오류 20(`invalid`)다.

Node.js 18 이상, C#은 .NET 8 이상을 사용한다. 다음은 저장소 루트·단일 구성 생성기 예다. 실제 구성에 맞는 산출물 경로를 사용한다.

```sh
cmake -S . -B build/bindings -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_MODULE_BINDINGS=ON -DRAFTPP_BINDINGS_NODEJS=ON \
  -DRAFTPP_WITH_UV=ON -DRAFTPP_WITH_LZ4=OFF -DRAFTPP_WITH_FIXTURE=OFF
cmake --build build/bindings --config Release --parallel
export RAFTPP_NODE_ADDON="$PWD/build/bindings/modules/bindings/raftpp_nodejs.node"
export LD_LIBRARY_PATH="$PWD/build/bindings/modules/bindings:${LD_LIBRARY_PATH:-}"
dotnet build bindings/csharp/Raftpp/Raftpp.csproj --configuration Release
```

SQLite/RocksDB·Asio·LZ4/zstd/zlib를 선택하려면 해당 `RAFTPP_MODULE_SQLITE/ROCKSDB/ASIO/LZ4/ZSTD/ZLIB=ON`과 준비된 `RAFTPP_DEPENDENCY_PREFIX`를 지정한다. SDK 준비는 [의존성](../dependencies.md)을 따른다. `RaftNode.features`/`RaftNode.Features`는 활성 UV/SDK 모듈을 조회한다. `asyncpp`·`mvfst`·`msquic`의 활성 표시는 SDK 모듈 존재를 뜻한다. 이들은 자동 연결 수립 factory가 아니며 아래 플러그인에서 실행기·연결된 스트림을 조립한다.

Node.js는 `require('./bindings/nodejs')`, C#은 `Raftpp.csproj` ProjectReference나 로컬 NuGet 패키지로 소비한다. TypeScript 선언은 `index.d.ts`이며 소비자는 자신의 `@types/node`를 제공한다. 네이티브 바이너리·SDK·런타임을 자동 다운로드하지 않는다. `cmake --install`은 브리지·활성 공개 헤더·선택 애드온을 설치한다. 설치 소비자는 `find_package(raftpp CONFIG REQUIRED COMPONENTS bindings)`를 사용한다. 애드온과 브리지는 같은 라이브러리 디렉터리에 두고 `$ORIGIN`으로 찾는다. 공유 core/모듈과 외부 SDK의 실행 라이브러리도 로더에서 찾을 수 있어야 한다. 관리 패키지는 별도 배포한다. [LICENSE.md](../../../LICENSE.md)를 따른다.

## 노드와 I/O 옵션

JS `NodeOptions`, C# `NodeOptions`는 `id/address/directory`(`Id/Address/Directory`)를 받는다. ID·term·인덱스는 JS `bigint`, C# `ulong`이며, 서버 ID는 양수다. 알려진 리더가 없으면 리더 ID가 0이다. 이전 대상 ID 0은 자동 선택이다. 주소는 UTF-8 NUL 없는 문자열이다. UV 디렉터리와 DB 부모 디렉터리는 호출자가 준비하며 SQLite는 파일 경로, RocksDB는 DB 디렉터리 경로다.

| Node.js | C# | 기본값 |
| --- | --- | --- |
| `electionTimeout` | `ElectionTimeout` | 1000 |
| `heartbeatTimeout` | `HeartbeatTimeout` | 100 |
| `installSnapshotTimeout` | `InstallSnapshotTimeout` | 30000 |
| `snapshotThreshold` / `snapshotTrailing` | `SnapshotThreshold` / `SnapshotTrailing` | 1024 / 2048 |
| `maxCatchUpRounds` / `maxCatchUpRoundDuration` | `MaxCatchUpRounds` / `MaxCatchUpRoundDuration` | 10 / 5000 |
| `maxInflightEntries` / `capacityThreshold` | `MaxInflightEntries` / `CapacityThreshold` | 32 / 4096 |
| `preVote` / `forceBarrier` | `PreVote` / `ForceBarrier` | false / false |
| `randomSeed` | `RandomSeed` | 1 |
| `payloadEncoding: 'bytes' / 'raw'` | `RawPayloads: false / true` | bytes / false |
| `pollInterval` | 해당 없음 | JS 자동 poll 5ms |
| `io` | `IO: IOOptions` | 아래 기본 조립 |

시간 값은 밀리초이고 heartbeat는 양수, election은 heartbeat보다 커야 한다. JS 정수 노드 옵션은 uint32 범위이며 pollInterval은 양수다. capacity는 uint16 범위다. `forceBarrier`는 생성 시 지정한다. `snapshotTrailing=0`도 지원한다. 노드는 로컬 스냅샷을 `snapshot_put_local`로 구분해 후속 로그를 보존하며 원격 설치는 기존 교체 경로를 사용한다.

| `io`의 JS 이름 (C# PascalCase) | 기본값·의미 |
| --- | --- |
| `factory` | `composed`; 사용자 플러그인 factory 이름으로 교체 가능 |
| `storage` | `uv`; `sqlite`, `rocksdb`도 선택 가능 |
| `timer` | `uv`; `asio`, 표준 monotonic clock의 `poll`도 가능 |
| `network` | `uv`; 사용자 `transport`/`Transport`를 지정하면 `custom` |
| `codec` | `none`; 스냅샷용 `lz4`, `zstd`, `zlib` |
| `configuration` | 플러그인에 전달하는 Buffer/byte[]; 기본 빈 배열 |
| `maxSnapshotSize` / `maxEncodedSnapshotSize` | 8MiB / 16MiB, core 조립 및 DB/UV 저장 한계 |
| `maxEntrySize` / `maxEntries` | DB 저장 한계 8MiB / 262144 |
| `segmentSize` / `blockSize` | UV segment 8MiB / 자동 감지 0 |
| `diskRetry` / `connectRetry` / `autoRecovery` | UV 5000ms / 1000ms / true |

JS 크기 값은 안전한 정수, C#은 ulong이다. segment/block 정렬 등 저장소 사전조건은 [UV](uv.md), DB 한계는 [SQLite](sqlite.md)/[RocksDB](rocksdb.md)를 따른다. 조립은 하나의 직렬 실행 영역에서 진행된다. DB 작업·초기화·bootstrap/복구는 동기로 호출 스레드를 막을 수 있다. `poll` 타이머는 Poll 호출 빈도에 따라 준비된 tick을 전달하며 누락된 tick을 한꺼번에 재생하지 않는다.

## 공개 노드 기능

| Node.js | C# | 계약 |
| --- | --- | --- |
| `bootstrap` / `recover` / `start` | `Bootstrap` / `Recover` / `Start` | [노드 API](../node.md)의 초기화·강제 복구·시작. 기존 저장소 bootstrap 오류 10은 별도 처리 |
| `apply` / `barrier` | `Apply` / `Barrier` | 명령 제출·선행 명령 순서 확인, 완료 콜백 |
| `add` / `assign` / `remove` | `Add` / `Assign` / `Remove` | spare 추가·역할 변경·서버 제거 |
| `transfer` | `Transfer` | 이전 요청, ID 0 자동 선택. 상태 0 완료도 성공 이전 보장은 아니며 leader를 확인 |
| `state`, `currentTerm`, `votedFor`, `lastIndex`, `commitIndex`, `lastApplied`, `timeout`, `localRole`, `leader`, `transferee` | 같은 이름의 PascalCase 속성 | 현재 상태·로그·리더·타이머 질의. 없는 role/leader는 null |
| `configuration` | `Configuration` | 서버 ID·주소·역할의 독립 복사본 |
| `peerStatus`, `matchIndex`, `catchUp` | `GetPeerStatus`, `MatchIndex`, `CatchUp` | peer 복제 인덱스·catch-up 상태, 사전조건과 오류는 node와 동일 |
| `error`, `errorMessage`, `errorDetails`, `lastMachineError` | PascalCase 속성 | Raft 코드·진단/원인 카테고리·소유 문자열·FSM 원래 예외 |
| `seed`, `setElectionTimeout`, `setHeartbeatTimeout`, `setInstallSnapshotTimeout`, `setPreVote`, `setSnapshotThreshold`, `setSnapshotTrailing`, `setMaxCatchUpRounds`, `setMaxCatchUpRoundDuration`, `setMaxInflightEntries`, `setCapacityThreshold` | PascalCase 메서드 | 대응하는 C++ seed/setter와 같은 범위·동작 |
| `setStepCallback`, `setTracer` | `SetStepCallback`, `SetTracer` | 네이티브 step 시점·tracer 시점에 동기 관찰. null로 해제 |
| `poll`, `close`, `closed` | `Poll`, `Close`, `Closed` | 준비된 작업 처리·비동기 종료·종료 완료 |

JS `Role.standby/voter/spare`, C# `ServerRole.Standby/Voter/Spare`는 0/1/2다. NodeState는 unavailable/follower/candidate/leader=0/1/2/3, CatchUpStatus는 none/running/aborted/finished=0/1/2/3이며 C# 열거값은 PascalCase다. `status`/`Status`는 주요 상태를 한 번에 제공한다. `details`(JS)는 votedFor·timeout·transferee·localRole·leaderAddress·진단·droppedEvents를 제공한다. `droppedEvents`/`DroppedEvents`는 네이티브 복사 이벤트 큐(최대 4096)의 유실 수다. 오류는 bootstrap 거부처럼 정상 실행 후에도 남을 수 있으며 종료 여부와 구분한다.

step/trace 함수에서는 상태 조회·콜백 교체·close가 가능하다. 현재 step 처리 중 명령 제출은 core의 busy 조건을 따른다. JS는 `setImmediate`, C# RaftHost는 `Post`로 다음 작업에 제출할 수 있다. 중첩 Poll은 금지한다. 관찰자 예외는 다른 완료 처리를 마친 뒤 JS microtask 또는 C# AggregateException으로 전달한다. TraceEvent는 type/level/message/file/functionName/line을 소유하며 저장해도 된다. 노드 tracer는 노드의 진단 경계이며 별도 네이티브 플러그인의 추적 설정은 해당 플러그인 계약을 따른다.

## FSM·요청·수명

JS FSM은 `apply(Buffer): Buffer`, `snapshot(): Buffer`, `restore(Buffer): void`를 제공한다. 선택 `snapshotAsync(Buffer): Buffer`는 두 번째 동기 스냅샷 단계이며 `snapshotFinalize(): void`는 정리 훅이다. C# `IStateMachine`의 PascalCase 메서드는 byte[]를 사용하며 두 선택 훅에 기본 구현이 있다. Promise/Task를 반환하는 FSM은 지원하지 않는다. FSM 훅에서도 상태 조회·Close가 가능하며 core가 호출 이후 종료 상태를 확인한다. 명령 제출·시작/복구의 재진입은 core의 busy/종료 조건을 따르고 중첩 Poll은 금지한다. 원래 예외는 LastMachineError에 남고 네이티브에는 오류 20을 반환한다. finalize는 오류를 전파하지 않는다.

입력·명령·결과는 경계에서 복사한다. JS 완료는 `(RaftError|null, Buffer)`, C#은 `CompletionCallback(RaftException?, byte[])`다. apply 외 결과는 빈 배열이다. 즉시 제출 실패는 code/Code가 있는 예외를 던지며 완료를 만들지 않는다. 받아들인 작업은 한 번 완료한다. 완료 함수가 던져도 다른 완료를 버리지 않는다. 종료 전에 요청 상태와 native 데이터가 유지된다. [오류 코드](../errors.md)를 따른다. 시간 초과는 명령 철회가 아니다.

기본 bytes 모드는 명령·스냅샷에 8바이트 little-endian 길이와 8배수 0 패딩의 애플리케이션 envelope를 사용한다. 원본 파일/RPC 형식은 바꾸지 않으며 사용자에는 벗긴 바이트만 전달한다. 빈 배열도 지원한다. raw 모드는 envelope 없이 C++ FSM과 같은 바이트를 주고받으며 해당 저장/통신 경계의 8바이트 명령 정렬·빈 스냅샷 제한을 지켜야 한다. 한 클러스터/저장소에서 모드를 섞지 않는다.

직접 RaftNode를 사용할 때 생성·호출·콜백은 같은 스레드에서 실행한다. JS start/close는 자동 interval Poll을 유지하며 ref/unref로 프로세스 유지 여부를 선택한다. C# 직접 노드는 Poll이 필요하다. Close 완료까지 계속 구동하고 Closed 이후 Dispose한다. finalizer는 외부 콜백을 끄고 native drain을 별도 스레드로 넘기며 완료 전달을 보장하지 않는다. 명시적 정상 종료를 사용한다. 자원 고갈이나 플러그인 정리 실패 시 살아 있는 자원을 보존한다.

## C# 자동 실행 호스트

`RaftHost(options, machine, bootstrap?, started?, onError?)`는 전용 스레드에서 노드를 생성·시작하고 Poll을 구동한다. `Post(Action<RaftNode>)`는 모든 공개 노드 기능을 그 스레드에서 실행하며 FSM·완료도 같은 스레드다. 생성자의 bootstrap 배열은 복사하며 기존 저장소의 오류 10은 허용한다. started/onError도 실행 스레드에서 호출한다. Post의 작업 예외는 onError로 전달한다. 시작 실패에는 onError를 통지하고 대기 작업을 실행하지 않는다.

Close는 선행 Post 뒤에 종료를 요청하고 정리 후 콜백을 실행한다. 종료를 요청한 뒤 Post는 ObjectDisposedException이다. Dispose는 Close를 요청하며 UI/event loop에서 동기 대기하지 않는다. 이미 종료된 호스트의 Close 콜백은 호출 스레드에서 바로 실행할 수 있다. 프로세스 유지 스레드이므로 정상 종료 시 Close/Dispose가 필요하다. 원시 RaftNode 참조를 다른 스레드로 옮기지 말고 Post에서 사용한다.

## 사용자 transport

JS는 `start(receive, local)`, `send(packet, done)`, `close(done)`, C#은 `IPacketTransport.Start/Send/Close`를 구현해 io.transport/IO.Transport로 전달한다. Packet은 대상/발신 `peerId`(C# PeerId), address, data를 소유한다. send의 packet은 목적지를, receive의 packet은 인증된 발신 ID·주소를 나타낸다. data는 한 개의 완전한 Raft RPC wire 메시지다. 연결·인증·분할/재조립·재연결 정책은 transport가 담당한다. partial/trailing bytes는 거부한다.

send는 실제 완료에 done을 한 번 호출하며 실패는 RaftError/RaftException으로 전달한다. 즉시 throw도 send 실패로 처리한다. Close는 수신을 중단하고 자신의 자원을 정리한 뒤 done을 호출한다. native close는 그 완료를 기다린다. 늦은/중복 send 완료는 안전하게 무시한다. JS는 같은 이벤트 루프, C# transport의 수신/완료는 다른 스레드에서도 전달할 수 있으며 Poll이 생성 스레드에서 큐를 소비한다. 직접 Receive는 생성 스레드에서만 호출한다. 종료가 시작된 수신은 버린다.

## 네이티브 런타임 플러그인

`RaftNode.loadBackendPlugin(path)`/`LoadBackendPlugin(path)`는 `raftpp_bindings_register_plugin()`을 호출한다. 플러그인은 `raftpp::bindings::register_runtime(name, factory)`로 이름을 등록하고 중복은 busy다. factory는 directory·configuration 바이트를 받아 `runtime`을 반환한다. backend는 core io, poll은 nonblocking, drain은 외부 콜백을 끈 finalizer 정리를 완료한다. owner는 모든 component/콜백보다 오래 살아야 한다. 선택 abandon은 관리 언어 호출을 중단한다. drain/abandon은 예외를 던지지 않는다.

상류 SDK에 맞는 connector를 만드는 이 경계에서 Asio·asyncpp 실행기, 연결된 mvfst/MsQuic 스트림과 framed_transport 등을 조립할 수 있다. TLS/연결 수립·서로 다른 실행 영역 간 dispatch는 애플리케이션 계약이며 플래그만으로 자동 구현되지 않는다. 사용자 io 구현도 여기서 선택하므로 공개 core I/O 기능을 생략할 필요가 없다. 플러그인은 브리지와 같은 C++ 헤더/ABI로 빌드하고 공유 라이브러리로 연결한다. 유효한 등록이 부분적으로 이뤄진 오류에도 안전하도록 로드한 플러그인은 프로세스 수명 동안 유지한다. factory 설정은 생성 중 소비하며 필요한 바이트는 직접 소유한다.

```cpp
#include <raftpp/bindings/runtime.hpp>
#include <raftpp/composed_io.hpp>
#include <raftpp/sqlite/sqlite.hpp>
#include <raftpp/uv/uv_timer.hpp>
#include <raftpp/uv/tcp_transport.hpp>
#include <uv.h>

namespace r = raftpp;
struct environment {
    uv_loop_t loop{};
    environment() { if (uv_loop_init(&loop)) throw r::system_error(r::errc::io_error); }
    ~environment() { uv_run(&loop, UV_RUN_DEFAULT); uv_loop_close(&loop); }
};
extern "C" int raftpp_bindings_register_plugin() {
    return r::bindings::register_runtime("application", [](r::string_view directory,
                                                         r::view<const r::byte> configuration) {
        // Parse application configuration and establish SDK connections here.
        (void)configuration;
        auto owner = std::make_shared<environment>();
        auto backend = r::make_io({
            .storage = std::make_shared<r::sqlite_storage>(r::fs::path(directory)),
            .timer = std::make_shared<r::uv_timer>(owner->loop),
            .network = std::make_shared<r::tcp_transport>(owner->loop)
        });
        return r::bindings::runtime{
            .owner = owner, .backend = std::move(backend),
            .poll = [owner] { uv_run(&owner->loop, UV_RUN_NOWAIT); },
            .drain = [owner] { uv_run(&owner->loop, UV_RUN_DEFAULT); }
        };
    }).value();
}
```

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS bindings uv sqlite)
add_library(application_backend SHARED backend.cpp)
target_link_libraries(application_backend PRIVATE raftpp::bindings raftpp::uv raftpp::sqlite)
```

언어 옵션의 `io.factory`/`IO.Factory`를 `application`으로 지정한다. 기본 selector는 플러그인 factory에서 사용하지 않으며 configuration은 애플리케이션이 해석한다.

## 네이티브 인터롭

ABI 버전은 2다. v1 create/status/completion 레이아웃과 심볼은 유지하며 새 `create_runtime`이 확장 옵션을 받는다. 헤더의 uint64_t/size_t/포인터·구조체 배치를 정확히 맞춘다. 일반 반환은 성공 0/Raft 오류 번호이며 peek 함수는 없음 0/있음 1/음수 오류다. 시스템 오류는 io_error로 정규화한다. `raftpp_binding_read(kind=0/1/2)`는 설정/복사 이벤트/전체 리더 주소를 크기 조회 후 복사하며 이벤트는 복사 성공 후 소비한다. FSM operation 0..6은 apply/snapshot/restore/snapshotAsync/finalize/step/trace다. output은 콜백 중 set_output으로만 복사한다.

transport peek 후 request ID에 status=-1을 보내 dispatch를 표시하고 실제 완료에 0..23을 보낸다. ID 0은 transport close다. request ID는 pop 전까지 중복할 수 없고, completion data는 pop/release 전에 복사한다. release는 핸들의 소유권을 이전하며 이후 재사용하거나 호출과 병렬 실행하지 않는다. 정상 종료는 close와 Poll을 사용한다.

## 사용 예제

다음 예제는 SQLite·zstd·UV 모듈을 활성화한 구성이다.

```js
const { RaftNode, Role } = require('./bindings/nodejs');
const fs = require('node:fs');
fs.mkdirSync('./data', { recursive: true });
let value = Buffer.alloc(0);
const node = new RaftNode({
  id: 1n, address: '127.0.0.1:9001', directory: './data/state.sqlite',
  snapshotThreshold: 16, snapshotTrailing: 0, preVote: true,
  io: { storage: 'sqlite', timer: 'poll', network: 'uv', codec: 'zstd' }
}, {
  apply(command) { value = Buffer.from(command); return value; },
  snapshot() { return value; },
  snapshotAsync(bytes) { return bytes; },
  snapshotFinalize() {},
  restore(bytes) { value = Buffer.from(bytes); }
});
node.bootstrap([{ id: 1n, address: '127.0.0.1:9001', role: Role.voter }]);
node.setTracer(event => console.log(event.message));
node.start();
// Access node.configuration, node.leader, node.peerStatus(peerId), and
// submit apply/barrier/membership/transfer operations with callbacks.
// During normal shutdown:
// node.close(() => console.log('closed'));
```

C#에서는 직접 Poll 대신 호스트를 사용할 수 있다.

```csharp
using Raftpp;

Directory.CreateDirectory("./data");
using var stopped = new ManualResetEventSlim();
var options = new NodeOptions(1, "127.0.0.1:9001", "./data/state.sqlite") {
    PreVote = true, SnapshotTrailing = 0,
    IO = new IOOptions { Storage = "sqlite", Timer = "poll", Network = "uv", Codec = "zstd" }
};
using var host = new RaftHost(options, new Machine(),
    [new Server(1, options.Address)], onError: Console.Error.WriteLine);
bool sent = false;
host.Post(node => node.SetStepCallback(() => {
    if (sent || node.State != NodeState.Leader) return;
    sent = true;
    // Post executes after the current native step has finished.
    host.Post(leader => leader.Apply("hello"u8.ToArray(), (error, result) => {
        Console.WriteLine(error?.Message ?? System.Text.Encoding.UTF8.GetString(result));
        host.Close(stopped.Set);
    }));
}));
stopped.Wait();
sealed class Machine : IStateMachine {
    private byte[] _value = [];
    public byte[] Apply(byte[] command) { _value = command; return _value; }
    public byte[] Snapshot() => _value;
    public void Restore(byte[] snapshot) => _value = snapshot;
}
```
