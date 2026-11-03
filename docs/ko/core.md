# 이벤트 코어와 공통 조회

[English](../en/core.md) · [콜백 노드](node.md) · [버퍼, 엔트리와 스냅샷](storage.md) · [CMake 빌드·설치](cmake.md)

각 API는 `<raftpp/core.hpp>`, `<raftpp/random.hpp>`, `<raftpp/version.hpp>`를 포함하며 `<raftpp.hpp>`에서도 제공한다. `raftpp::raftpp`에만 연결한다. UV/LZ4/fixture 설정과 무관하게 사용할 수 있다.

## 상태와 이벤트

```cpp
error_code step(core_state& core, const core_event& event) noexcept;
uint64_t next_timeout(const core_state& core) noexcept;
```

이벤트 API는 `node`와 같은 합의 엔진을 사용하며 I/O나 FSM 적용을 실행하지 않는다. 드라이버가 영속화·전송·타이머·적용 순서·종료를 관리한다. 라이브러리가 I/O를 조정하도록 하려면 [콜백 노드](node.md)를 사용한다.

`core_state`는 소유 값이며 복사·이동할 수 없다. 시작 전에 0이 아닌 `id`, 빈 값이 아닌 `address`, 필요하면 `random` 시드와 설정을 지정한다. `state`는 `follower_state`, `candidate_state`, `leader_state`의 표준 variant이며 초기 상태는 follower다. `std::holds_alternative`/`std::get_if`로 조회하고 실행 중인 상태를 직접 교체하지 않는다. term·vote·클러스터 설정·commit/로그 경계·동작 설정은 코어의 필드다.

`core_event`는 단조 증가하는 ms 단위 `time`, KiB 단위 저장소 `capacity`, variant `payload`를 갖는다. 용량 임곗값 기본값은 0이며 `capacity_threshold`로 제출 용량 검사를 활성화한다. 호출/완료를 직렬화한다. `step`에 재진입하거나 tracer에서 코어를 변경하거나 호출 중 코어를 파괴하지 않는다.

`time`의 지원 상한은 `numeric_limits<uint64_t>::max() - 2 * uint64_t(core.election_timeout)`이다. 무작위 선거 기간 전체와 heartbeat/quorum 만료 시각을 표현할 여유를 둔다. 이 상한을 넘는 이벤트는 시계·용량·기존 update·코어 상태를 바꾸기 전에 `too_big`을 반환한다. 실행 중 시간 설정은 제공된 setter로 변경한다.

`term` 최댓값 자체는 영속 상태·수신 RPC·마지막으로 가능한 선거의 유효한 값이다. 다만 그 상태에서 다음 term을 요구하는 새 선거나 pre-vote, 인계 선거는 `too_big`을 반환하며 선거 역할·term·vote·타이머·난수 시드를 재설정하거나 투표 요청을 보내지 않는다. 이미 진행 중인 마지막 선거는 유효한 응답으로 승리할 수 있다. 단일 voter의 시작 시 자체 선출은 term을 증가시키지 않으므로 최댓값에서도 가능하다. 선거 오류는 이벤트 시계·용량·update 교체 이후에 발생할 수 있다.

로그·스냅샷의 실제 인덱스 상한은 `numeric_limits<log_index>::max() - 1`이다. 최댓값 자체는 다음 인덱스를 표현하는 데 사용한다. 상한을 넘는 시작 metadata·접미사나 새 제출은 `too_big`으로 거부하고, 수신 로그·스냅샷의 범위 검사에서 최댓값 자체를 참조하면 `malformed`로 거부한다. 더 이른 term/상태 처리로 무시한 RPC는 이 검사에 도달하지 않을 수 있다. 스냅샷이 상한에 있고 접미사가 비어 있으면 `start_index`는 최댓값일 수 있다. 이 범위 검사는 로그 추가나 스냅샷 교체 전에 수행하며, 이벤트 시계·리더/term 갱신까지 포함한 전체 상태 rollback을 약속하지는 않는다.

| Payload | 입력 |
| --- | --- |
| `start_event` | 영속화된 `term`, `voted_for`, 선택적 스냅샷 `metadata`, 로그 `start_index`, `entries`. 빈 상태는 term 0이고 vote/스냅샷/엔트리가 없다. bootstrap 로그는 인덱스 1의 설정 엔트리로 시작한다. 시작 성공은 한 번만 허용한다. |
| `receive_event` | 송신자 ID/주소와 로딩이 끝난 스냅샷 데이터를 가진 소유 `rpc`. |
| `persisted_entries_event` | `index`: 영속화에 성공한 연속 로그 prefix의 마지막 인덱스. |
| `persisted_snapshot_event` | 수신 스냅샷 설치 성공의 metadata. `offset`/`last`는 청크를 나타낸다. 엔진은 완전한 스냅샷을 설치한다. |
| `configuration_event` | commit된 설정의 적용을 `index`와 디코딩된 `configuration`으로 알린다. |
| `snapshot_event` | `metadata`의 로컬 스냅샷이 영속 공개되었음을 알리고 로그 `trailing`개를 보존한다. |
| `timeout_event` | 이벤트의 현재 시간/용량으로 만료를 전달한다. |
| `submit_event` | 현재 term의 명령·barrier·인코딩된 멤버십 변경 소유 `entries`. |
| `catch_up_event` | 서버 `id`의 승격 따라잡기를 시작한다. |
| `transfer_event` | 다른 투표자 `id`로 인계를 요청한다. ID 0은 대상을 자동 선택한다. |

`snapshot_metadata`는 `index`, `term`, `configuration`, `configuration_index`를 소유한다. 설정 값의 변경·바이트 형식은 [클러스터 설정](configuration.md)을 참고한다. 입력 이벤트는 소유 값/공유 payload 핸들을 사용한다. `step`은 호출 중 이벤트를 대여하고 필요한 데이터는 소유자로 유지한다. 호출 중 대여한 입력을 변경하지 않는다. C 할당을 수동으로 정리할 필요가 없다.

버전 2 이상인 `request_vote_result`의 `pre_vote=true` 응답은 미래의 제안 term을 담을 수 있다. 현재 candidate가 아닌 노드는 늦게 도착한 이 응답을 무시하며 term·vote를 바꾸거나 그 영속화를 요청하지 않는다. 실제 투표 응답과 pre-vote 표시를 지원하지 않는 버전 1 응답의 더 높은 term은 기존 term 갱신 규칙을 따른다. candidate에서는 현재 선거 단계에 맞는 응답 필터링과 투표 집계를 적용한다.

인계는 리더 상태에서만 요청한다. ID 0은 현재 로그 끝까지 복제된 첫 voter를 구성 순서대로 선택하며, 그런 voter가 없으면 마지막으로 확인한 다른 voter를 선택한다. 다른 voter가 없으면 `not_found`, 자신·미등록 ID·non-voter를 지정하면 `bad_id`다. 비리더이거나 이미 인계 중이면 대상 검사보다 먼저 `not_leader`를 반환한다. 인계 중 `leader(core)`는 비어 있고 새 제출은 `not_leader`로 거부된다.

이미 따라잡은 대상에는 즉시 `TimeoutNow`를 보낸다. 뒤처진 대상도 새 부분 복제를 확인한 응답에서 `TimeoutNow` 전송을 시작할 수 있다. RPC에는 리더의 전체 로그 끝을 담으며, 수신자는 그 경계가 자신의 영속 로그와 일치할 때만 pre-vote를 건너뛰는 선거를 시작한다. 전송을 시작한 뒤 후속 ACK만으로 같은 인계 힌트를 재전송하지 않는다. 따라서 인계 요청 수락이나 RPC 송신은 다른 리더의 선출을 뜻하지 않는다. 이벤트 드라이버는 선거·상태 변경을 계속 처리하고, `election_timeout` 경과를 전달해야 한다. 인계가 만료되면 대상을 해제하며 호출자는 다시 요청할 수 있다. 더 높은 term에 follower로 전환해도 인계 대상은 해제된다.

## Update, 소유권과 순서

접수된 각 단계는 소유 `core_update`인 `core.update`를 교체한다. 플래그가 지정한 필드만 사용한다. 플래그 숫자는 C와 같다.

| 플래그 | 값 | 드라이버 동작 |
| --- | --- | --- |
| `update_current_term` | 1 | `core.current_term`을 영속화하고 저장된 vote를 초기화한다. |
| `update_voted_for` | 2 | `core.voted_for`를 영속화한다. |
| `update_entries` | 4 | `entries.index`부터 `entries.batch`를 영속화한다. 교체/truncate는 append보다 앞에 둔다. |
| `update_snapshot` | 8 | 수신한 `snapshot.metadata`, `offset`, `chunk`, `last`를 영속화한다. |
| `update_messages` | 16 | `messages`의 소유 RPC를 순서대로 송신한다. |
| `update_state` | 32 | 새 `core.state`를 관찰한다. |
| `update_commit_index` | 64 | 새로 commit된 로그를 `core.commit_index`까지 순서대로 적용한다. |
| `update_timeout` | 128 | `next_timeout(core)`로 절대 만료 시각을 다시 계산한다. |

term/vote에 의존하는 응답을 보내기 전에 해당 값을 영속화한다. 영속 쓰기 성공 후에만 persisted 이벤트를 전달한다. 명령을 순서대로 적용하고 드라이버에서 적용 인덱스를 추적한다. commit된 설정을 적용하고 `configuration_event`를 전달한다. 로컬 스냅샷은 공개한 뒤 `snapshot_event`를 제출한다. 전송 오류는 영속화 완료가 아니다. 송신 메시지의 스냅샷 로딩 지시는 [노드 I/O 계약](node.md)처럼 전송 전에 영속 스냅샷을 로딩해야 한다.

`persisted_entries_event::index`는 현재 `last_stored`보다 크고 `trail.last_index()` 이하여야 한다. 로그 교체나 스냅샷 설치로 폐기된 쓰기의 늦은 완료를 그대로 전달하지 않는다. 드라이버는 요청이 보관한 엔트리의 인덱스·term을 현재 로그와 대조하여 여전히 유효한 연속 prefix만 보고하고, 스냅샷 설치 중인 이전 쓰기의 완료는 제외한다. 공개 `node`는 이 필터링을 수행한다. 원격 quorum으로 commit된 인덱스는 리더 자신의 `last_stored`보다 앞설 수 있다. follower의 성공 응답은 현재 리더와 로그 일치를 확인한 범위 및 실제 영속 prefix를 넘지 않으며, 알려진 리더가 없으면 쓰기 완료로 성공 응답을 보내지 않는다.

수신 스냅샷 설치 중에는 현재 또는 높은 term의 중복 스냅샷과 엔트리를 담은 AppendEntries의 데이터를 새로 저장하지 않고 선거를 시작하지 않는다. heartbeat·term/리더 갱신은 처리하며 설치 완료 시 현재 리더와 term으로 응답한다. 송신 측의 설치 재시도는 피어가 응답하는 동안 설치 제한 시간에 도달하면 가능하며, 응답 없는 피어는 probe로 돌아간다. catch-up은 한 라운드의 경과 시간이 `max_catch_up_round_duration`을 **초과**하면 중단된다. 정확히 같은 시각에는 이 조건만으로 중단하지 않으며, 중단 후 다시 요청할 수 있다.

비동기 작업에 데이터가 필요하면 다음 `step` 전에 `core.update`를 대기 작업으로 이동한다. update는 메시지 주소·배치·스냅샷·공유 payload를 소유하며 이후 step·압축·코어 자체보다 오래 생존할 수 있다. 드라이버/코어 상태를 사용하는 연산이 완료되거나 취소·정리될 때까지 해당 상태를 유지한다. update를 소유해도 코어의 수명은 연장되지 않는다.

오류는 `invalid`, `out_of_memory`, `too_big`, `not_leader`와 연산별 오류다. 호출 전 시간·시작·재진입 검사는 이전 update를 보존한다. 이후 오류는 상태 변경 후 발생할 수 있으며 상태 전체의 rollback을 보장하지 않는다. 제출된 작업이 철회되었다고 가정하지 말고 오류를 처리한다.

`next_timeout`은 이벤트 시계 기준 절대 ms 시각으로 선거 또는 heartbeat/quorum 만료를 조합한다. 타이머를 예약하지 않는다. 시작 전 기본 무작위 기간은 추출 전이므로 결과는 0이지만 election timeout을 설정하면 0이 아닐 수 있다. timeout 플래그가 없어도 시간 설정 변경 후 다시 계산한다.

## 이벤트 코어 조회

```cpp
node_state state(const core_state&) noexcept;
optional<role> local_role(const core_state&) noexcept;
optional<leader_info> leader(const core_state&);
error_code match_index(const core_state&, server_id, log_index&) noexcept;
error_code catch_up(const core_state&, server_id, catch_up_status&) noexcept;
server_id transferee(const core_state&) noexcept;
```

이 함수들은 I/O·상태 전이·이벤트 생성 없이 코어를 조회한다. clock·난수·기존 update를 바꾸지 않는다. term·vote·commit과 로그 경계는 기존 `core.current_term`, `core.voted_for`, `core.commit_index`, `core.trail.last_index()`에서 읽는다. 조회와 `step`을 같은 스레드에서 직렬화하며 동시에 코어를 변경하지 않는다.

| 조회 | 계약 |
| --- | --- |
| `state(core)` | 현재 variant의 follower/candidate/leader. 구성 직후에도 follower이며 valueless variant는 unavailable이다. |
| `local_role(core)` | 현재 configuration에서 core.id의 역할. 구성원이 아니면 빈 optional이다. |
| `leader(core)` | follower가 알고 있는 리더 또는 리더 자신을 값으로 반환한다. candidate·미확인 리더·인계 중인 리더는 빈 optional이다. |
| `match_index(core, id, index)` | 리더가 알고 있는 해당 구성원의 복제 인덱스를 출력한다. |
| `catch_up(core, id, status)` | 해당 구성원의 none/running/aborted/finished 상태를 출력한다. |
| `transferee(core)` | 리더의 인계 대상 ID이며, 비리더 또는 대상 없음은 0이다. |

`leader_info`는 `<raftpp/types.hpp>`에 선언한 `id`와 소유 `string address` 값이다. 코어의 주소·구성을 바꾸거나 코어를 해제해도 반환값은 유효하다. 주소를 복사하는 `leader`는 할당 예외가 발생할 수 있고 나머지 조회는 할당하지 않는다.

match/catch-up은 리더가 아니면 먼저 `not_leader`, 리더의 configuration에 ID가 없으면 `bad_id`를 반환한다. 수동으로 잘못 구성해 configuration과 progress가 일치하지 않는 코어에는 `invalid`를 반환한다. 모든 실패는 출력 인자를 보존한다. 인계 중이어도 코어의 state가 leader이면 progress 조회는 가능하지만 `leader(core)`는 리더를 알려주지 않는다. 코어의 변형·해제와 동시에 반환된 비소유 내부 표현을 읽는 대신 이 조회의 값 결과를 사용한다.

## 설정과 공통 조회

```cpp
error_code set_election_timeout(core_state&, unsigned msecs) noexcept;
error_code set_heartbeat_timeout(core_state&, unsigned msecs) noexcept;
error_code set_install_snapshot_timeout(core_state&, unsigned msecs) noexcept;
```

기본값은 1000, 100, 30000 ms다. election은 heartbeat보다 크고 `uint32_t` 최댓값의 절반 이하여야 한다. heartbeat는 0보다 크고 election보다 작아야 하며 snapshot timeout은 0보다 커야 한다. 잘못된 설정은 값/난수 시드를 보존하고 `invalid`를 반환한다. 새 election timeout의 위 시계 범위에 현재 `core.now`가 들어가지 않으면 `too_big`을 반환하며 설정·난수·기존 만료 시각을 보존한다. election setter는 `election_timer_start`를 초기화하지 않고 follower/candidate 기간을 다시 추출한다. leader는 다시 추출하지 않는다. setter는 update 플래그를 변경하거나 I/O를 실행하지 않는다. `pre_vote`, catch-up 제한, 용량 임곗값, `force_barrier` 등 다른 필드는 드라이버 순서가 허용할 때 설정한다.

이벤트 API는 지정한 150/15 ms 설정을 그대로 유지한다. 콜백 `node::set_election_timeout(150)`은 참조 legacy 보정을 유지한다. heartbeat가 15 ms이면 election/heartbeat가 450/45 ms가 된다. 명시적인 `node_options` 생성자 값은 그대로 적용한다. backend tick 간격을 위해 `start()` 전에 노드 heartbeat를 설정한다. 이후 변경은 해당 타이머를 재시작하지 않는다.

`<raftpp/types.hpp>`의 `state_name(node_state)`와 `role_name(role)`는 정적 `string_view`인 `follower`/`candidate`/`leader`, `stand-by`/`voter`/`spare`를 반환한다. 알 수 없는 값과 `unavailable`은 참조 구현에 이름이 없는 것처럼 빈 view를 반환한다.

`random_within_range(uint32_t& state, uint32_t minimum, uint32_t maximum) noexcept`는 호출자가 소유한 시드를 갱신하고 양끝을 포함한 값을 반환한다. 사전조건은 `minimum <= maximum`이다. 단일 값이 아닌 동일 범위/시드는 참조 난수열을 유지한다. 단일 값 범위는 시드를 진행하지 않고 해당 값을 반환한다. 합의/테스트용 결정적 난수이며 암호학적 난수가 아니다.

`reference_version`은 `array<unsigned, 3>{0, 22, 1}`, `reference_version_number`는 2201(`major * 10000 + minor * 100 + release`)이다. 동작 호환성 기준 버전을 식별하며 C++ 라이브러리 릴리스나 ABI 버전이 아니다.

## 단일 투표자 예제

완료 이벤트를 전달하기 전에 표시한 지점을 실제 영속 쓰기로 대체한다.

```cpp
#include <raftpp/core.hpp>

int main()
{
    namespace r = raftpp;
    r::core_state core;
    core.id = 1;
    core.address = "node-1";
    r::configuration members;
    if (members.add(1, "node-1", r::role::voter)) return 1;
    r::buffer encoded;
    if (members.encode(encoded)) return 2;
    r::start_event start;
    start.term = 1;
    start.entries.push_back({1, r::entry_type::change,
                             r::shared_buffer{std::move(encoded)}});
    if (r::step(core, {0, 65535, std::move(start)})) return 3;
    const r::array<r::byte, 1> bytes{r::byte{42}};
    r::submit_event command;
    command.entries.push_back({core.current_term, r::entry_type::command,
                               r::shared_buffer::copy(bytes)});
    if (r::step(core, {1, 65535, std::move(command)})) return 4;
    auto pending = std::move(core.update);
    if (!(pending.flags & r::update_entries)) return 5;
    // Persist pending.entries here; acknowledge only after durable success.
    const auto last = pending.entries.index + pending.entries.batch.size() - 1;
    if (r::step(core, {2, 65535, r::persisted_entries_event{last}})) return 6;
    return core.commit_index == last ? 0 : 7;
}
```
