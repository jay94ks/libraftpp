# 사용자 선택형 I/O 조립

[English](../en/composition.md) · [모듈 목록](modules/index.md) · [노드](node.md)

`<raftpp/composed_io.hpp>`의 `make_io(io_components)`는 사용자가 선택한 저장소·타이머·메시지 transport를 `shared<io>`로 조립한다. 필수 core API이며 UV나 특정 SDK에 의존하지 않는다. 반환값은 기존 `node`의 backend 인자로 사용한다. 구체 부품의 모듈 타깃도 함께 연결한다.

```cpp
#include <raftpp/composed_io.hpp>

raftpp::shared<raftpp::io> assemble(
    raftpp::shared<raftpp::storage> disk,
    raftpp::shared<raftpp::timer> clock,
    raftpp::shared<raftpp::transport> network,
    raftpp::shared<raftpp::compression_codec> codec = {})
{
    return raftpp::make_io({
        .storage = std::move(disk),
        .timer = std::move(clock),
        .network = std::move(network),
        .snapshot_codec = std::move(codec),
    });
}
```

## 구성 요소와 옵션

| 필드 | 기본값·계약 |
| --- | --- |
| `storage` | 필수 `shared<storage>`. 메타데이터·로그·스냅샷과 저장 용량을 담당한다. |
| `timer` | 필수 `shared<timer>`. 주기적 tick과 단조 증가하는 밀리초 시계를 담당한다. |
| `network` | 필수 `shared<transport>`. Raft 메시지를 송수신한다. 단일 노드는 peer가 없는 `framed_transport`를 사용할 수 있다. |
| `snapshot_codec` | 기본 null. 지정하면 스냅샷 payload를 해당 코덱으로 압축해서 저장하고 읽을 때 해제한다. 로그/RPC에는 적용하지 않는다. |
| `max_snapshot_size` | 기본 8 MiB. 저장 전 원문과 읽은 논리 스냅샷 payload의 최대 크기다. |
| `max_encoded_snapshot_size` | 기본 16 MiB. 압축/escape 프레임의 헤더를 포함한 최대 크기다. 원문 그대로인 payload에는 `max_snapshot_size`를 적용한다. 저장소의 파일 읽기 상한을 대신 설정하지는 않는다. |
| `random_seed` | 기본 1. 이 I/O 객체가 소유하는 재현 가능한 난수 상태의 초기값이다. 여러 노드는 서로 다른 seed를 지정한다. |

저장소·타이머·network 중 하나라도 null이면 `std::invalid_argument`를 던진다. 조립과 내부 콜백 생성은 할당 예외를 던질 수 있다. 팩토리는 부품의 init/start를 호출하지 않는다. 생성 실패 시 전달받은 shared owner를 해제하며 다른 owner의 소유권을 빼앗지 않는다.

부품은 각 추상 인터페이스를 구현하는 사용자 객체로 교체할 수 있다. 선택 모듈이 켜졌다는 이유로 특정 구현·코덱을 자동 선택하지 않는다. `executor`는 별도 콜백 제출 인터페이스이며 그 자체로 저장소나 타이머가 되지는 않는다.

## 저장소 계약

`<raftpp/storage.hpp>`의 `storage`는 다음 순서를 사용한다.

| 메서드 | 계약 |
| --- | --- |
| `init(server_id, string_view)` | 로컬 식별자로 초기화한다. 주소 뷰를 보관하려면 복사한다. 실패 후에도 close할 수 있어야 한다. |
| `bootstrap(const configuration&)`, `recover(const configuration&)` | 동기 설정 생성/복구. 실제 파일 변경·실패 시 상태 보존 범위는 구현이 정의한다. |
| `load(io_state&)` | term·vote·snapshot·로그를 읽는다. 조립기는 임시 상태에 읽고 필요한 압축 해제까지 성공한 뒤 호출자 출력으로 옮긴다. |
| `set_term(term)`, `set_vote(server_id)` | 성공을 반환하기 전에 해당 메타데이터의 영속화를 보장한다. |
| `truncate(log_index)` | 그 인덱스부터 suffix를 제거하며 이후 append와 저장 순서를 유지한다. |
| `append(log_index, shared<const entry_batch>, completion)` | 수락한 배치를 완료까지 유지하고 영속화 결과로 한 번 완료한다. |
| `snapshot_put(unsigned trailing, shared<const snapshot>, completion)` | 선행 로그와의 순서 및 trailing 보존 정책을 지켜 스냅샷을 게시한다. |
| `snapshot_put_local(unsigned trailing, shared<const snapshot>, completion)` | 로컬 생성 스냅샷. 기본은 snapshot_put으로 전달한다. UV는 trailing=0에서도 더 새로운 로그 suffix를 보존하며 원격 설치와 구분한다. |
| `snapshot_get(snapshot_callback)` | 스냅샷이 없으면 성공과 `nullopt`, 있으면 소유한 값을 전달한다. |
| `close(close_callback) noexcept` | 새 요청을 중단하고 수락한 작업/정리를 완료한 뒤 종료를 알린다. 초기화 전에도 호출할 수 있어야 한다. |
| `capacity() const noexcept` | KiB 단위 용량. 기본값 0이며 `io::capacity()`로 전달한다. |
| `error_details() const noexcept` | 오류 반환/완료 전에 해당 오류에 맞는 소유 진단을 공개한다. 기본값은 빈 진단이다. |

동기 메서드와 비동기 제출은 `error_code`를 반환한다. 즉시 실패에는 완료 콜백이 없고 수락한 작업은 한 번 완료하며 인라인 완료도 허용한다. 저장소는 snapshot payload를 소유한 바이트로 취급하며 조립기의 프레임을 변환하지 않아야 한다. `uv_storage`는 이 프레임을 그대로 전달하면서 기존 LZ4 snapshot 읽기 계약도 유지한다.

내장 영속 구현은 [UV의 `uv_storage`](modules/uv.md), [RocksDB의 `rocksdb_storage`](modules/rocksdb.md), [SQLite의 `sqlite_storage`](modules/sqlite.md)다. RocksDB/SQLite는 UV 없이 선택할 수 있고 `storage_limits`로 엔트리/snapshot 크기·엔트리 수 한도를 지정한다. DB 호출·커밋은 블로킹하며 완료는 인라인이다. 다른 저장소도 `storage`를 구현해 조립할 수 있다. Asio·QUIC 스트림 어댑터 자체는 파일 영속화를 제공하지 않는다.

UV 저장소의 로그 엔트리 payload 길이는 기존 디스크 형식대로 8바이트의 배수여야 한다. 조립기는 명령에 padding을 추가하지 않는다. 스냅샷 payload에는 이 정렬 조건을 적용하지 않는다.

## 타이머 계약

`<raftpp/timer.hpp>`의 `timer`는 `start(unsigned interval, completion tick)`, `time() const noexcept`, `close(close_callback) noexcept`, `error_details() const noexcept`를 제공한다. interval은 양수 밀리초다. 정상 tick은 빈 오류 코드를 전달한다. 실패 tick은 구독을 끝내며 조립기가 전체 I/O 종료를 시작한다. 초기 제출 실패에는 tick이 없다. close는 새 tick을 막고 활성 콜백과 native 정리를 완료한 뒤 완료 콜백을 호출한다. 초기화/시작 전 close도 지원한다.

[UV의 `uv_timer`](modules/uv.md)와 [Asio의 `asio_timer`](modules/asio.md)를 선택할 수 있다. loop/context는 native 종료 완료까지 유지한다.

## 실행 영역과 수명

모든 부품 호출, 노드/FSM 호출과 콜백은 **같은 직렬 실행 영역**에서 실행한다. 팩토리는 SDK 스레드를 비교하거나 자동 bridge를 생성하지 않는다. 서로 다른 loop를 섞는 경우 사용자가 같은 스레드에서 함께 구동하거나 이 계약을 만족하는 명시적 어댑터를 제공한다. `asio_timer`와 UV 저장소를 한 스레드에서 같이 구동하려면 아래와 같이 poll할 수 있다.

```cpp
// loop와 context는 부품을 생성할 때 전달한 실행 영역이다.
uv_run(&loop, UV_RUN_NOWAIT);
if (context.stopped()) context.restart();
context.poll();
```

각 부품의 init/start/close 수명은 조립기가 독점 관리한다. 다른 노드나 조립기에 같은 부품의 수명 관리를 동시에 맡기지 않는다. `framed_transport`에 peer를 등록한 뒤 조립하고, transport의 read/close를 따로 시작하지 않는다. QUIC 연결·TLS·peer 인증은 여전히 connector가 담당한다.

노드가 `init → bootstrap/recover(선택) → load → start → close`를 호출한다. 직접 I/O를 사용할 때도 같은 순서다. init/load/start 실패는 전체 부품 정리를 시작한다. init 반복은 `busy`, ID 0은 `bad_id`, 빈 주소는 `invalid`다. load 반복/재진입은 `busy`; start에는 성공한 load와 양수 interval, 비어 있지 않은 tick/receive 콜백이 필요하다. 종료 중 요청은 `shutdown`이다.

조립기는 저장소·타이머·network를 모두 닫고 수락한 요청과 활성 사용자 콜백이 끝난 뒤 종료를 완료한다. 대기 중 첫 nonempty close observer를 유지하며 반복된 빈 close는 안전하다. 완료 후 close observer는 즉시 실행할 수 있다. 소멸자는 종료를 시작하고 동기 대기하지 않는다. loop/context를 계속 구동한다.

배치·메시지·원본/인코딩된 스냅샷·코덱과 내부 요청 상태는 실제 완료까지 유지한다. 호출자 callback 실행 동안도 유지하며 조립기가 보유한 요청 payload는 close 완료 전 해제한다. 사용자 콜백이 예외를 던지면 native 경계로 전파하지 않고 종료한다. 제출/동기 동작의 `bad_alloc`은 `out_of_memory`, 길이 오류는 `too_big`, 기타 예외는 `io_error`다. `error_details()`는 최근 소유 진단이며 제출 예외에 이전 native 원인을 재사용하지 않는다. 조립 후 `time()`은 타이머 시계, `random(min,max)`는 상한을 제외한 범위이며 `min>=max`이면 min이다.

## 스냅샷 코덱과 형식

선택 코덱은 동기 동작한다. 입력 snapshot은 수정하지 않고 별도 소유 snapshot의 payload만 인코딩한다. term/index/configuration은 유지한다. 읽은 값은 노드/FSM에 전달하기 전에 해제하므로 전송 RPC와 로그 레코드 형식을 변경하지 않는다.

저장 payload는 다음 v1 envelope를 사용한다. 정수는 little endian이다.

| 오프셋 | 크기 | 내용 |
| --- | --- | --- |
| 0 | 8 | `52 41 46 54 50 50 01 00` |
| 8 | 8 | encoding: 0은 압축 없는 escape, 1은 선택 codec의 프레임 |
| 16 | 8 | 원래 payload의 바이트 수 |
| 24 | 나머지 | 원문 또는 codec 프레임 |

codec이 없으면 보통 기존 원문을 그대로 저장한다. 원문이 envelope magic으로 시작하면 encoding 0으로 escape하여 임의 바이트의 왕복을 보장한다. codec 지정 시 encoding 1을 쓴다. envelope magic으로 시작하지 않는 이전 원문 snapshot도 읽을 수 있다. 이전 원문 자체가 이 magic으로 시작하면 envelope로 해석하므로 이 경로로 열기 전에 별도 변환이 필요하다. encoding 1을 읽는데 codec이 없으면 `invalid`; 잘린 헤더·알 수 없는 encoding·해제 길이 불일치는 `corrupt`; 한도 초과는 `too_big`다. codec 자체의 오류 코드는 유지한다. `snapshot_get` 실패에는 `nullopt`를 전달하며 load 실패에는 호출자 상태를 유지한다.

디렉터리를 다시 열 때 같은 snapshot codec 설정을 제공한다. envelope는 codec 식별자나 사전을 저장하지 않으므로 코덱을 임의로 바꾸지 않는다. legacy `uv_io`는 새 envelope를 해제하지 않으므로 해당 디렉터리는 `make_io`로 열어야 한다. 기존 `uv_io`의 원문/LZ4 읽기와 이전 snapshot fallback 정책은 바꾸지 않는다. 저장소/스냅샷 디렉터리를 다른 형식 설정으로 공유하지 않는다. envelope는 암호화나 인증 형식이 아니다.

## 구체적인 조합

```cpp
#include <raftpp/composed_io.hpp>
#include <raftpp/uv/uv_storage.hpp>
#include <raftpp/uv/uv_timer.hpp>
#include <raftpp/uv/tcp_transport.hpp>
#include <raftpp/zstd/zstd.hpp>

raftpp::shared<raftpp::io> make_backend(uv_loop_s& loop, raftpp::fs::path directory)
{
    return raftpp::make_io({
        .storage = std::make_shared<raftpp::uv_storage>(loop, std::move(directory)),
        .timer = std::make_shared<raftpp::uv_timer>(loop),
        .network = std::make_shared<raftpp::tcp_transport>(loop),
        .snapshot_codec = std::make_shared<raftpp::zstd_codec>(),
    });
}
```

이 예제는 UV와 `RAFTPP_MODULE_ZSTD=ON`을 요구한다. `find_package(raftpp CONFIG REQUIRED COMPONENTS uv zstd)`와 `raftpp::uv`, `raftpp::zstd`를 연결한다. 디렉터리는 미리 생성한다. 타이머를 `asio_timer`, network를 연결된 Asio/mvfst/MsQuic 스트림을 등록한 `framed_transport`로 바꿀 수 있으며 실행 영역 계약도 함께 만족시킨다.

`RAFTPP_BUILD_EXAMPLES=ON`의 `raftpp-example-composed DIRECTORY [none|lz4|zstd|zlib] [uv|asio]`는 선택 가능한 코덱·타이머와 `uv_storage`를 사용한다. 각 선택 SDK 모듈을 빌드할 때 활성화한다. 단일 투표자/peer 없는 transport 예제이며 카운터를 한 번 증가시키고 snapshot 게시/종료를 기다린다. 같은 인자로 같은 디렉터리를 다시 열면 값을 복원한다. 기존 `uv_io`는 별도 API로 유지되며 이 팩토리를 사용할 필요가 없다.
