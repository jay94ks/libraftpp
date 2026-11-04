# libuv I/O와 전송

[English](../en/uv_io.md) · [노드 API](node.md) · [TCP 전송](tcp_transport.md) · [소유 stream](uv_stream.md) · [메시지 adapter](stream_transport.md) · [모듈별 API](modules/index.md)

`<raftpp/uv/uv_io.hpp>`의 `uv_io`는 `io`를 구현하며, Linux 파일 저장과 libuv 타이머를 `transport`에 연결한다. `RAFTPP_WITH_UV=ON`에서만 선언·빌드한다. `<raftpp/transport.hpp>`의 추상 메시지 전송 인터페이스는 UV 옵션과 무관하게 사용할 수 있다. 소비자는 `raftpp::raftpp`만 연결하며, UV 활성 시 이 타깃이 `<uv.h>`와 libuv 링크 사용 조건도 전달한다.

`error_details()`와 초기화 진단의 소유권은 [오류와 소유 진단 정보](errors.md)를 참고한다.

## 생성과 실행 순서

```cpp
#include <raftpp/uv/uv_io.hpp>
#include <uv.h>

raftpp::shared<raftpp::uv_io> make_io(
    uv_loop_t& loop, raftpp::fs::path directory,
    raftpp::shared<raftpp::transport> network)
{
    raftpp::uv_io_options options;
    options.disk_retry = 1000;
    return std::make_shared<raftpp::uv_io>(
        loop, std::move(directory), std::move(network), options);
}
```

생성자는 이벤트 루프를 빌리고 전송 객체를 공유 소유한다. 저장소 디렉터리는 이미 존재하고 읽기·쓰기 가능해야 한다. 생성자는 표준 할당 예외를 던질 수 있다. 객체는 복사·이동할 수 없으며 `shared<io>`로 노드에 전달할 수 있다.

순서는 `init` → 선택적인 `bootstrap` 또는 `recover` → `load` → `start` → 연산 → `close`다. `node`에 전달하면 노드가 초기화·로드·시작·종료를 호출한다. 초기화는 ID·주소·파일시스템 기능을 확인하고 전송 객체를 초기화한다. `load`는 저장 상태를 복구하고 로그 쓰기 계층을 연다. 한 객체에서 성공한 로드를 반복할 수 없다.

메서드와 콜백은 같은 이벤트 루프 스레드에서 실행한다. 루프는 소멸자나 `close` 호출 이후에도 실제 종료 완료까지 생존하고 실행되어야 한다. 초기화·로드·시작 실패 뒤의 정리도 포함한다. 실패한 객체는 종료를 완료하고 재사용하지 않는다.

## 옵션

| 필드 | 기본값 | 계약 |
| --- | --- | --- |
| `segment_size` | 8 MiB | 준비할 segment 파일 크기. 블록 크기 이상이고 `int64_t` 최댓값 이하여야 한다. 큰 단일 배치는 이를 넘을 수 있다. |
| `disk_retry` | 5000 ms | 파일 준비·로그 쓰기·스냅샷 준비 실패의 재시도 간격. 0도 허용한다. |
| `block_size` | 0 | 0은 probe 결과를 사용한다. 직접 지정하면 512 이상의 2의 거듭제곱이며 실제 파일시스템의 정렬 요구도 충족해야 한다. |
| `auto_recovery` | `true` | 로드 시 마지막 손상 segment 격리와 재시도를 허용한다. |
| `max_snapshot_size` | `size_t` 최댓값 | 제출하는 전체 스냅샷, 로드·조회하는 파일과 압축 해제 payload의 크기 제한. |
| `tracing` | 기본 stderr tracer | 파일·타이머 진단과 native 쓰기 이벤트. `set_tracer(tracer)`는 기존·미래 writer를 갱신한다. 전송 관찰자는 독립적이다. [추적](tracing.md)을 참고한다. |

파일 준비는 `posix_fallocate`로 공간을 예약한다. 이 호출이 `EOPNOTSUPP`를 반환하면 파일시스템 블록 크기를 기준으로 최대 4096바이트 간격의 1바이트 쓰기로 예약을 대체한다. native 예약의 `ENOSPC`는 `no_space`지만, 대체 경로의 stat/write 실패는 원본과 같은 `io_error`이며 소유 진단의 `cause`에 실제 원인을 보존한다. 준비가 실패하면 새 파일과 descriptor를 정리한다.

## 설정 변경

```cpp
error_code set_block_size(size_t size) noexcept;
error_code set_segment_size(size_t size) noexcept;
void set_disk_retry(unsigned msecs) noexcept;
void set_connect_retry_delay(unsigned msecs) noexcept;
void set_auto_recovery(bool enabled) noexcept;
error_code set_snapshot_compression(bool enabled) noexcept;
```

소유 루프 스레드에서 setter를 호출한다. `set_block_size`는 파일시스템 probe 결과로 복원하는 0 또는 512 이상인 2의 거듭제곱을 받는다. 초기화 후에는 segment 크기가 해당 block보다 작으면 안 된다. 정렬은 `load` 전에 변경한다. 열린 파일 writer에서는 제출된 버퍼와 offset을 보존하도록 `busy`를 반환한다. 잘못된 값은 설정을 바꾸지 않고 `invalid`, 종료 중에는 `shutdown`이다.

`set_segment_size`는 설정/probe block 크기 이상이며 `int64_t` 최댓값 이하인 양수를 받는다. 초기화 전과 쓰기 중 모두 사용할 수 있다. 초기화 전에 알 수 없는 probe 요구사항은 `init`에서 검사한다. 이후 append가 들어갈지 판단하거나 capacity를 보고할 때 새 크기를 사용한다. 기존 버퍼·prepare pool 파일·이미 제출한 준비 작업의 할당은 유지한다. 이후 준비는 block 배수로 내림한다. 크기를 넘는 단일 batch도 자체 segment에 저장한다. 잘못된 설정은 이전 크기를 보존하고 종료 중에는 `shutdown`이다.

`set_disk_retry`는 로그 할당/쓰기/finalize 재시도와 스냅샷 준비에 전파한다. `set_connect_retry_delay`는 전송 객체의 선택적 연결 정책에 전달한다. 둘 다 이후 예약에 적용하며 이미 예약한 타이머의 만료 시각은 유지한다. 0도 허용한다. `set_auto_recovery`는 `init` 후를 포함해 이후 load/recover 동작을 변경하며 이미 읽은 상태를 다시 쓰지 않는다. 이 void setter는 종료 중 아무 효과가 없다. 연결 정책을 전달할 때는 사용자 정책이 호출 도중 facade를 파괴해도 가상 메서드가 반환할 때까지 내부 backend/transport 소유권을 유지한다.

`set_snapshot_compression`은 아무 동작 없이 양쪽 값에 성공을 반환하며 종료 후와 LZ4 비활성 구성에서도 같다. 스냅샷 압축을 활성화하지 않는다. 기존 압축 스냅샷 읽기는 LZ4 기능 설정을 따른다.

디스크의 LZ4 스냅샷을 `load`나 `snapshot_get`으로 읽으면 압축을 해제한 payload를 반환한다. `node`가 이 스냅샷으로 뒤처진 노드를 따라잡게 할 때도 해제된 payload를 전송하며, 수신 측 `snapshot_put`은 전달받은 바이트를 그대로 저장한다. 이 과정에서 송신 측의 원래 압축 파일을 다시 쓰지 않는다. LZ4 비활성 구성에서 압축 파일 자체를 읽으려 하면 `invalid`다. `load`의 이전 스냅샷 대체 정책은 아래 계약을 따른다.

## 저장·스냅샷 메서드

| 메서드 | 계약 |
| --- | --- |
| `bootstrap`, `recover` | 로드 전에 동기적으로 초기 설정을 생성하거나 강제 복구한다. |
| `load(io_state&)` | term·vote·스냅샷·로그를 로드한다. 최신 스냅샷을 읽지 못하면 이전 것도 확인한다. 오류 시 출력은 유지하고 종료를 시작한다. |
| `set_term`, `set_vote` | 메타데이터를 동기적으로 영속화한다. 성공한 기록만 캐시에 반영한다. |
| `truncate(index)` | 해당 인덱스부터 접미사를 제거하는 blocking barrier와 worker를 제출한다. 후속 append의 순서를 보장한다. 제출 이후 오류는 `last_error()`와 후속 요청 결과에 반영된다. |
| `append(index, entries, callback)` | 현재 다음 인덱스부터 기록하고 받아들인 batch·payload를 완료까지 소유한다. 즉시 반환 오류에는 콜백을 호출하지 않는다. |
| `snapshot_put(trailing, value, callback)` | 임시 파일을 준비하고 선행 로그를 마친 barrier 이후 공개·정리한다. 한 번에 하나만 제출할 수 있으며 중복은 `busy`다. |
| `snapshot_get(callback)` | worker에서 최신 스냅샷을 읽는다. 없으면 성공과 `nullopt`, 읽기 실패면 오류와 `nullopt`를 전달한다. 여러 조회를 함께 제출할 수 있다. |

`recover(configuration)`은 현재 자동 복구 정책으로 로컬 snapshot·로그를 읽고 복구 가능한 open segment를 닫은 뒤, 복구된 다음 인덱스에 전달한 설정을 추가한다. 새 엔트리는 마지막 저장 엔트리의 term을 사용하며 로그가 없으면 snapshot의 term을 사용한다. term·vote 메타데이터와 기존의 유효한 이력을 유지한다. snapshot만 있는 저장소도 지원하며 로그·snapshot이 모두 없으면 `invalid`다. `load` 전에 반복하면 설정 엔트리를 하나 더 추가한다. 로드로 파일 coordinator를 연 뒤에는 `bootstrap`·`recover`가 `busy`를 반환한다.

자동 복구를 끄면 손상된 suffix 파일의 격리·재시도를 하지 않지만, open segment의 유효한 batch prefix를 살리는 동작은 유지한다. 오류를 반환하기 전에 open 파일을 닫거나 제거하고 손상 파일을 격리할 수 있으므로 실패 시 디렉터리 rollback을 약속하지 않는다. `bootstrap`은 첫 설정 엔트리를 만들기 전에 term 1을 저장하므로 이후 실패해도 term 메타데이터가 남을 수 있다.

자동 복구가 격리할 수 있는 손상은 open segment 또는 마지막 closed segment의 `corrupt` 오류다. 손상 파일 이후의 open 파일도 함께 격리하며, 뒤에 다른 closed segment가 남아 있는 중간 손상은 `corrupt`로 거부한다. 스냅샷이 있어도 이 조건을 적용한다. 첫 배치의 format·preamble·header·data를 읽는 중 실제 파일 끝에서 발생한 짧은 읽기는 `io_error`이며 자동 복구를 켜도 손상 suffix로 격리하지 않는다. format이 0인 파일은 전체가 0일 때만 비어 있는 준비 파일로 취급한다.

닫힌 segment의 파일명은 양 끝을 포함하는 로그 인덱스 범위를 선언한다. 디코딩한 엔트리 개수는 32비트를 넘는 경우에도 전체 범위와 일치해야 하며, 불일치는 `corrupt`다. 격리·재시도는 위 자동 복구 정책을 따른다. 시작 인덱스가 커도 일치하는 범위가 작으면 그 자체로 큰 할당을 의미하지 않는다.

스냅샷 term·index는 양수이고 index는 10¹⁶ 미만이어야 한다. 합친 payload는 비어 있지 않아야 한다. 잘못된 값은 `invalid`, 크기 초과는 `too_big`다. 제출한 snapshot·batch·message와 payload는 완료까지 변경하지 않는다.

`trailing > 0`인 로컬 스냅샷은 barrier 이후 로그 쓰기와 공개를 함께 진행할 수 있다. `trailing == 0`인 설치용 스냅샷은 다음 append 인덱스를 `snapshot.index + 1`로 바꾸고 공개·기존 segment 정리가 끝날 때까지 후속 쓰기를 막는다. 최신 두 스냅샷을 보관하며 segment는 trailing 정책을 따른다.

로컬 `snapshot_put` 뒤에 `truncate`와 새 인덱스의 `append`를 연속 제출할 수 있다. 각 요청은 barrier와 파일 작업의 순서에 따라 진행한다. `truncate`의 즉시 성공은 실제 잘라내기 완료를 뜻하지 않는다. 후속 append의 성공 콜백은 그 쓰기에 선행하는 truncate가 끝났음을 포함하며, 로컬 스냅샷의 공개 완료 콜백과의 상대 순서는 별도로 기다려야 한다.

스냅샷 준비 실패는 성공하거나 종료할 때까지 재시도한다. 공개·정리 오류는 콜백으로 전달하며 재시도하지 않는다. 오류가 반환되어도 이미 공개된 파일이 있을 수 있으므로 rollback으로 해석하지 않는다. 성공은 파일·디렉터리 동기화를 포함하지만 원자적인 다중 파일 트랜잭션을 의미하지 않는다.

## 전송 인터페이스

| `transport` 메서드 | 구현자의 계약 |
| --- | --- |
| `init(id, address)` | 로컬 ID·주소를 초기화한다. 주소를 보관하려면 뷰를 복사한다. |
| `start(receive)` | 수신을 시작하고 송신자의 ID·주소를 담은 `message` 소유 값을 전달한다. |
| `send(value, callback)` | 목적지 ID·주소를 담은 메시지를 전송한다. 즉시 실패에는 콜백을 호출하지 않으며 받아들인 전송은 정확히 한 번 완료한다. |
| `set_connect_retry_delay(unsigned) noexcept` | 이후 연결 재시도를 위한 선택적 정책. 기본 구현은 아무 동작도 하지 않아 기존 사용자 전송 객체가 override할 필요는 없다. `tcp_transport`는 연결 재시도 간격을 갱신한다. |
| `error_details() const noexcept` | 선택적 소유 진단 조회. 기본 구현은 빈 값이다. 즉시 실패를 반환하거나 실패 완료 콜백을 호출하기 전에 해당 코드의 정보를 공개한다. 할당·예외·노드 재진입 없이 조회하며 이전 요청의 문맥을 반환하지 않는다. |
| `close(callback)` | 수신을 중단하고 받아들인 전송·native 핸들을 정리한 뒤 완료한다. `init`·`start` 실패 뒤에도 호출할 수 있어야 한다. |

전송 구현은 연결·직렬화·수신 버퍼의 수명을 관리한다. `send`의 반환 오류나 예외는 요청을 수락하지 않았음을 뜻해야 한다. 수락한 뒤 예외로 제출 실패를 보고해서는 안 된다. 메시지는 [노드 API](node.md)의 계약을 따른다. `uv_io`도 받아들인 전송 메시지를 완료까지 보관한다. 전송 오류의 복제 재시도는 노드가 담당한다.

`uv_io`는 transport의 init/start 실패와 즉시/비동기 send 실패 진단을 외부 콜백 전에 복사한다. 제공된 코드가 일치하지 않거나 구현이 진단을 제공하지 않으면 해당 연산의 Raft 코드로 설명한다. transport가 예외로 제출을 거절하면 이전 transport 진단을 사용하지 않는다. 전송 실패는 기존 결과·재시도 정책을 유지하며 그 자체로 치명적인 디스크 오류가 되지 않는다.

라이브러리·애플리케이션·사용자 transport 플러그인은 같은 버전의 C++ 인터페이스 헤더로 빌드한다. 선택적 메서드의 기본 구현은 소스 수준의 호환성을 제공하며 서로 다른 버전의 virtual 인터페이스 바이너리를 혼용하는 계약은 아니다.

## 타이머·종료·진단

`start(interval, tick, receive)`의 interval은 양수다. libuv 타이머가 tick을 호출하고 전송 객체가 수신 메시지를 전달한다. `time()`은 루프의 캐시된 단조 시간을 밀리초로 반환한다. `random(minimum, maximum)`은 기존 C backend와 같이 `std::rand`를 사용하며 maximum을 제외한다. `minimum >= maximum`이면 minimum을 반환한다. 초기화는 프로세스 공용 난수 상태를 seed한다.

`capacity()`는 준비 파일과 활성 segment의 남은 공간을 원본과 같은 unsigned `size_t` 산술로 합산한 뒤 1024로 나누고 `uint16_t`로 변환한 KiB 값이다. 최댓값에서 포화시키지 않으며 큰 값은 16비트 범위로 순환한다. 예를 들어 활성 segment 없이 준비 파일 2개의 보고 크기가 각각 32 MiB이면 65536 KiB가 0으로 보고된다. 이 값은 공간 부족 임계값 판정과 RPC의 capacity 필드에도 사용한다. runtime segment 크기를 바꾸면 기존 파일을 재할당하지 않아도 보고 값이 바뀐다. 종료 중에는 0이다. `last_error()`는 치명적인 backend 오류를 조회하며 개별 콜백 결과를 대신하지 않는다.

종료 중 새 제출은 `shutdown`이다. 준비·재시도·barrier 대기 중 스냅샷은 취소한다. 이미 공개 worker를 실행 중인 스냅샷과 받아들인 조회는 완료를 기다린다. 종료 콜백은 요청 완료, 라이브러리의 요청 데이터 참조 해제와 native 핸들 정리 이후에 호출한다. 콜백에서 종료하거나 마지막 `uv_io` 소유자를 파괴해도 내부 수명은 유지된다.

미완료 종료 콜백은 하나만 등록한다. 빈 콜백으로 반복 종료할 수 있으며 종료가 이미 끝났으면 전달한 콜백을 인라인 호출한다. tick·receive·완료 콜백의 예외는 native 경계를 넘지 않으며 backend의 종료를 시작한다.
