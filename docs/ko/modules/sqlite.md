# SQLite 저장소 모듈 API

[English](../../en/modules/sqlite.md) · [모듈 목록](index.md) · [I/O 조립](../composition.md)

`RAFTPP_MODULE_SQLITE=ON`에서 `raftpp::sqlite`와 `<raftpp/sqlite/sqlite.hpp>`를 제공한다. core는 필수이며 전이적으로 연결된다. SQLite 3.24 이상과 thread 지원이 필요하다.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS sqlite)
target_link_libraries(app PRIVATE raftpp::sqlite)
```

## 생성과 조립

```cpp
#include <raftpp/sqlite/sqlite.hpp>
#include <raftpp/composed_io.hpp>

raftpp::shared<raftpp::io> assemble(
    raftpp::fs::path database,
    raftpp::shared<raftpp::timer> timer,
    raftpp::shared<raftpp::transport> network,
    raftpp::shared<raftpp::compression_codec> codec = {})
{
    return raftpp::make_io({
        .storage = std::make_shared<raftpp::sqlite_storage>(std::move(database)),
        .timer = std::move(timer),
        .network = std::move(network),
        .snapshot_codec = std::move(codec),
    });
}
```

`sqlite_storage final : storage`의 생성자는 `explicit sqlite_storage(fs::path path, storage_limits limits = {})`다. path는 SQLite DB 파일이다. 부모 디렉터리는 미리 생성한다. 초기화 실패에도 빈 DB 파일/schema가 생성되어 남을 수 있다. 생성자는 아직 DB를 열지 않으며 할당/경로 변환 예외를 던질 수 있다. 빈/NUL 포함 경로·지원 범위 밖 limits는 `std::invalid_argument`다. 객체는 복사/이동할 수 없다.

타이머·transport·선택 snapshot codec은 사용자가 정하며 UV 의존성이 없다. `make_io`/노드가 저장소 수명을 관리한다. 파일 DB를 여는 한 객체가 독점 소유하므로 여러 노드/조립기에 공유하거나 다른 연결로 수정하지 않는다. 기존 UV 파일 디렉터리를 DB로 자동 이관하지 않는다.

## 메서드

모든 메서드는 [storage 계약](../composition.md)을 구현한다.

| 메서드 | 인자·반환·동작 |
| --- | --- |
| `init(server_id id, string_view address)` | 0 ID는 `bad_id`, 빈 주소는 `invalid`, 성공 후 반복은 `busy`. DB를 열거나 생성하고 저장된 형식/한도/연속 로그를 검사한다. 다른 owner의 잠금은 실패한다. 실패한 인스턴스는 close한 뒤 새 객체로 연다. |
| `bootstrap(const configuration&)` | voter가 필요하다. 빈 DB에 term 1/vote 0과 인덱스 1의 설정 엔트리를 한 트랜잭션으로 저장한다. 이미 term이 있으면 `cannot_bootstrap`. |
| `recover(const configuration&)` | 빈 이력은 `invalid`. 마지막 로그/snapshot term으로 설정 엔트리를 로그 끝에 추가한다. 현재 term/vote는 유지한다. |
| `load(io_state&)` | term/vote·snapshot·연속 엔트리를 읽는다. 원문 payload를 소유하며 실패 시 출력은 유지한다. trailing 로그를 포함한 시작 인덱스를 반환한다. |
| `set_term(term)` | 양수이며 현재 term보다 작을 수 없다. 동기 영속 커밋하고 vote를 0으로 만든다. |
| `set_vote(server_id)` | term이 있어야 한다. vote 0도 저장할 수 있다. 성공 반환 전에 영속화한다. |
| `truncate(log_index)` | `[start,next]` 범위이며 snapshot index보다 커야 한다. 해당 인덱스부터 suffix를 원자적으로 삭제한다. |
| `append(log_index, shared<const entry_batch>, completion)` | 현재 로그 끝 인덱스에 비어 있지 않은 배치를 추가한다. null 엔트리·0/현재 term 초과 term·미지원 type은 `invalid`. 전체 배치를 검증하고 한 트랜잭션으로 저장한다. 로그 payload에 8바이트 정렬은 필요하지 않다. |
| `snapshot_put(unsigned trailing, shared<const snapshot>, completion)` | 양수 index/term과 voter 설정, `1 <= configuration_index <= index`가 필요하다. index는 `UINT64_MAX`보다 작고 기존 snapshot보다 작을 수 없으며 term은 현재 term을 넘을 수 없다. 기존 snapshot과 같은 index를 다시 저장할 때는 term도 같아야 한다. 같은 index/term의 로그 또는 같은 기존 snapshot이 있으면 suffix와 snapshot이 덮는 엔트리 최대 trailing개를 유지한다. 일치하지 않으면 전체 로그를 교체하고 다음 append 인덱스를 `index+1`로 만든다. snapshot·시작 인덱스·로그 삭제는 원자적이다. |
| `snapshot_get(snapshot_callback)` | snapshot이 없으면 성공과 `nullopt`, 있으면 소유한 값을 전달한다. 저장소에서는 codec envelope를 해제하지 않는다. |
| `capacity() const noexcept` | DB 파일시스템의 가용 KiB를 65535로 제한한다. 초기화 전/종료 중/조회 실패는 0. |
| `error_details() const noexcept` | 복사 가능한 최근 실패 진단. SQLite native 결과의 오류 문자열을 포함한다. |
| `close(close_callback = {}) noexcept` | 새 작업은 `shutdown`. 활성 콜백이 끝나면 DB를 해제하고 첫 nonempty observer를 호출한다. 이미 닫혔으면 즉시 완료한다. |

DB 호출과 동기 커밋은 호출 스레드를 블로킹한다. 자체 worker/실행기를 만들거나 다른 스레드로 완료를 보내지 않는다. 모든 호출·파괴·노드/FSM·완료는 같은 직렬 실행 영역에서 수행한다. 타이머/네트워크 응답 지연에 DB 지연이 포함될 수 있다.

비동기 형태의 메서드는 성공 커밋 뒤 **인라인**으로 정확히 한 번 완료한다. 즉시 실패에는 콜백이 없다. 배치/snapshot은 콜백 반환까지 유지한다. 콜백에서 close하거나 facade를 해제할 수 있으며 close 완료는 콜백 밖으로 미룬다. 콜백 예외는 `io_error` 진단을 기록하고 종료하며 DB 커밋을 취소하지 않는다. 소멸자는 close를 시작한다. 동시 접근을 동기화하는 타입은 아니다.

## 저장 형식·한도·실패

`BEGIN EXCLUSIVE`/`COMMIT` 트랜잭션, `synchronous=FULL`, DELETE journal, `locking_mode=EXCLUSIVE`를 사용하며 native 오류/예외는 rollback한다. 잠금은 연결 수명 동안 유지하고 busy timeout은 0이다. 동기 커밋 성공만 완료로 알린다. 재시작 복구는 DB의 journal/WAL 복구를 사용한다. 파일시스템/장치의 동기화 보장에 의존하며 전원 손실에 대한 별도 하드웨어 보증을 추가하지 않는다.

DB 전용 v1 키 공간을 사용한다. `meta`는 `52 41 46 54 44 42 01 00` 뒤 little-endian term/vote/start 각 u64다. `e`+big-endian u64 index 키의 값은 little-endian term/type/payload 길이 각 u64와 payload다. `snapshot` 값은 little-endian index/term/configuration_index/설정 길이/payload 길이 각 u64, 기존 core 설정 인코딩과 원문 payload를 잇는다. DB 바깥의 RPC·UV 로그 형식은 변경하지 않는다. `make_io`의 선택 codec은 이 원문 snapshot에 envelope를 적용하며 같은 codec 설정으로 다시 연다.

core `storage_limits`는 `max_entry_size=8 MiB`, `max_snapshot_size=16 MiB`, `max_entries=262144`가 기본이다. 크기 필드는 각각 원문 저장 엔트리/snapshot payload의 한도다. codec 사용 시 저장소 snapshot 한도에는 envelope 전체를 포함한다. 설정 인코딩 한도는 별도 8 MiB다. 세 필드는 `INT_MAX/2` 이하, max_entries는 양수여야 한다. 전체 DB 바이트 한도가 아니므로 큰 로그는 메모리 사용량도 고려해 설정한다. 더 작은 한도로 재개하면 `too_big`가 될 수 있다.

한도/인덱스 오버플로는 `too_big`, 손상된 키·형식·길이·로그 구멍은 `corrupt`, 잘못된 요청은 `invalid`, 할당 실패는 `out_of_memory`, 기타 native/예외 실패는 `io_error`다. SQLite BUSY/LOCKED는 `busy`, FULL은 `no_space`, READONLY/PERM/AUTH는 `unauthorized`로 매핑한다. 검증·직렬화·커밋 전 준비 실패는 저장 상태를 바꾸지 않으며 cache는 성공한 커밋 뒤에만 갱신한다. native 쓰기/동기화 실패는 영속 결과가 불확실할 수 있으므로 후속 연산 전에 저장소를 닫고 다시 열어 상태를 확인한다.
