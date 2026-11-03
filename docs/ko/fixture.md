# 가상 시간 클러스터 Fixture

[English](../en/fixture.md) · [노드](node.md) · [메모리](memory.md)

`<raftpp/fixture.hpp>`를 포함하고 `raftpp::raftpp`를 링크한다. `fixture`와 이벤트·메시지 열거형은 `RAFTPP_WITH_FIXTURE=ON`에서만 제공한다. UV·LZ4는 필요하지 않다. 독립적인 메모리 `io` 구현으로 공개 callback 기반 `node`를 실행하며 소켓·파일·백그라운드 스레드·실제 시간 sleep을 사용하지 않는다.

## 소유권과 구성

`fixture`는 이동 전용이며 `fixture::max_servers`인 8개까지 노드를 소유한다. `grow(shared<state_machine>, node_options = {})`는 ID가 `index + 1`이고 주소가 그 ID의 십진 문자열인 노드를 추가한다. 인덱스는 0부터 시작한다. fixture가 각 FSM을 보관하며 호출자도 관찰용 공유 핸들을 유지할 수 있다. fixture를 이동해도 node/backend 주소는 유지된다. `get(index)`는 대여한 노드 참조이며 fixture를 닫거나 파괴하면 무효다. `backend(index)`는 직접 모의 연산을 수행할 수 있는 공유 `io` 핸들을 반환한다. 보관한 backend는 종료나 fixture 파괴 뒤 새 작업을 `shutdown`으로 거부한다.

실행 중인 클러스터에도 `grow`를 호출할 수 있다. 새 노드는 기존 노드들과 시뮬레이션 연결을 가지지만 아직 시작되지 않고 Raft 구성에도 자동으로 추가되지 않는다. 새 인덱스를 `size() - 1`로 얻고 `get(index).start()`로 그 노드만 시작한 뒤, 현재 리더의 `add(id, address, callback)`와 `assign(id, role, callback)`로 멤버십을 변경한다. 클러스터 전체 `start()`는 모든 노드에 적용되므로 이미 실행 중인 노드를 다시 시작하는 용도로 사용하지 않는다. 각 비동기 구성 변경의 완료를 기다린 뒤 다음 변경을 제출한다. voter 승격은 필요한 로그 따라잡기를 포함한다. 멤버십에서 제거해도 fixture의 노드·FSM·인덱스는 유지되며 인덱스를 재사용하거나 나머지 ID를 바꾸지 않는다.

| 연산 | 계약 |
| --- | --- |
| `grow(machine, options)` | null FSM·잘못된 노드 옵션은 `invalid`, 서버 수 제한은 `too_many`, step·종료 도중에는 `busy`를 반환한다. 실패 시 클러스터 크기와 기존 연결을 보존한다. |
| `configuration(voters, output)` | 현재 모든 서버를 구성하고 앞의 `voters`개를 voter, 나머지를 standby로 설정한다. `1 <= voters <= size()`여야 하며 실패 시 출력을 보존한다. |
| `bootstrap(configuration)` / `start()` | 인덱스 순서로 노드에 적용한다. 노드가 하나 이상 필요하다. 첫 오류에서 중단하며 앞서 성공한 노드는 되돌리지 않는다. |
| `size()` / `time()` | 노드 수와 밀리초 단위의 공유 가상 시간이다. 둘 다 0에서 시작한다. |
| `leader_index()` / `commit_index()` | 마지막 step에서 관찰한 안정 리더의 인덱스 또는 리더가 없으면 `size()`, 그리고 안정 리더에서 관찰한 가장 큰 commit index다. 네트워크 제어의 영향은 다음 step에서 관찰에 반영된다. |
| `alive(index)` / `voted_for(index)` | 서버의 모의 생존 상태와 영속화한 투표 값이다. |
| `persisted(index, output)` | 서버에 영속화한 term·vote·snapshot·시작 index·entries를 깊은 복사한다. 실패 시 출력을 보존한다. |
| `error()` | 직접 I/O 완료·종료 콜백이 예외를 던졌을 때 기록한 오류다. Raft/드라이버 실패는 각 노드의 `error()`로 확인한다. |

일반 노드의 기본 timeout을 사용한다. heartbeat는 100 ms, election은 1000 ms, snapshot install은 30000 ms이며 backend capacity는 4096 KiB다. 기본 randomized election timeout은 1000, 1100, … ms다. 사용자 지정 election timeout에는 `timeout + index * (timeout / 10)`을 사용한다. 시작·step 전에 기존 Raft 난수 생성기의 seed를 설정해 지정한 timeout을 재현한다. `set_randomized_election_timeout(index, msecs)`의 값은 노드의 `[election_timeout, 2 * election_timeout]` 범위 안이어야 하며 양 끝을 포함한다. 이후 randomized timer 생성에 적용하고, `start_elect`는 현재 타이머를 직접 바꾼다. 노드 추적은 `node_options::tracing` 또는 `get(index).set_tracer(...)`로 선택한다.

## Step과 관찰

`step()`은 이벤트 하나를 실행하고 `optional<fixture_event>`를 값으로 반환한다. 남은 이벤트가 없거나 닫힌/moved-from fixture이면 비어 있다. `fixture_event`에는 `server_index`, `type`, 가상 `time`이 들어 있다. 이벤트 타입의 숫자는 C와 같아 `tick = 1`, `network = 2`, `disk = 3`, `work = 4`다. `work`는 예약 값이며 이 메모리 backend는 work 이벤트를 예약하지 않는다.

예정 시각이 가장 이른 이벤트를 선택한다. 같은 시각에는 서버 인덱스가 작은 쪽이 우선이며 같은 서버에서는 tick이 요청보다 우선하고 요청끼리는 제출 순서를 유지한다. send는 먼저 현재 시각에 완료하고, 성공하면 송신자의 network latency 후 별도 전달 이벤트를 만든다. 디스크 요청은 제출 시점의 disk latency를 사용하고 transmit latency는 send 완료 시점에 결정한다. 기본값은 네트워크 15 ms·디스크 10 ms다. 같은 시각에 여러 이벤트가 있어도 step 하나는 tick 또는 요청 하나만 처리한다. 죽은 서버의 tick 슬롯도 전진하지만 tick 콜백은 호출하지 않으며, 이미 대기 중인 디스크 요청은 완료될 수 있다.

이벤트 뒤 election safety를 검사하여 같은 term에 두 리더가 존재하는 것을 금지한다. 가장 높은 term의 살아 있는 리더에 대해 해당하는 살아 있고 포화되지 않은 follower가 모두 리더를 인정하고 C fixture의 non-spare 멤버 기준을 충족하면 안정 상태로 간주한다. 이 관찰 규칙에서 disconnected와 saturated는 서로 다르다. 안정 리더가 바뀌지 않았다면 캐시 엔트리가 스냅샷으로 압축된 경우를 제외하고 직전 관찰 로그와 일치해야 한다. 위반하면 `errc::corrupt`인 `system_error`를 던진다. 엔트리 타입·term·크기·바이트를 비교하며 Release에서도 검사한다. 관찰 준비 중 할당할 수 있으므로 이벤트가 이미 실행된 뒤 `step()`에서 할당 예외가 전파될 수 있다.

| 메서드 | 계약 |
| --- | --- |
| `step_n(count)` | 최대 `count`개 이벤트를 실행하고 마지막 것을 반환한다. 이벤트가 없으면 중단하며 count 0은 잘못된 인자다. |
| `step_until(stop, max_msecs)` | 매 step 전에 `stop(const fixture&)`를 확인한다. C fixture와 같이 가상 제한 시각보다 엄격히 이르게 중단해야 true다. 정확히 제한 시각에 조건을 달성하면 false이며 마지막 이벤트가 제한 시각을 초과할 수 있다. 빈 predicate는 잘못된 인자다. |
| `step_until_elapsed(msecs)` | 해당 가상 시간이 최소한 흐르거나 이벤트가 없어질 때까지 진행한다. |
| `step_until_has_leader(limit)` / `step_until_has_no_leader(limit)` | 캐시한 안정 리더 관찰을 기다린다. |
| `step_until_applied(index, entry, limit)` | `last_applied() >= entry`를 기다린다. index에 `size()`를 전달하면 모든 서버를 기다린다. |
| `step_until_state_is(index, state, limit)` / `step_until_term_is(index, term, limit)` | 지정한 노드 상태·현재 term을 기다린다. |
| `step_until_voted_for(index, candidate, limit)` | 해당 노드의 현재 투표가 서버 ID `candidate + 1`을 가리킬 때까지 기다린다. |
| `step_until_delivered(from, to, limit)` | `from`에서 `to`로 향하는 send·transmit이 큐에 없을 때까지 기다린다. 전달하거나 버리면 transmit을 제거한다. |
| `hook(callback)` | 관찰이 완료될 때마다 `callback(fixture&, const fixture_event&)`를 호출한다. 이벤트 참조는 그 호출 동안의 대여이므로 보관하려면 복사한다. 빈 콜백은 hook을 해제한다. |

fixture와 backend는 한 스레드에서 사용한다. 재귀 step은 `invalid_argument`를 던진다. hook에서 fixture를 닫을 수 있으며 hook·predicate의 예외는 호출자에게 전파된다. 조회·wait의 잘못된 인덱스는 `out_of_range`, 잘못된 메시지 열거값은 `invalid_argument`를 던진다. `error_code`를 반환하는 제어 함수는 잘못된 인덱스·인자에 `invalid`, 종료 뒤에는 `shutdown`을 반환한다. 이동된 원본은 size/time이 0이고 이벤트가 없으며 그 인덱스 조회는 잘못된 연산이다.

## 선거와 네트워크 제어

`start_elect(index)`는 안정 리더가 없고 모든 노드가 follower이며 대상이 voter여야 한다. 해당 follower의 현재 선거 타이머를 최소화하고 다른 타이머를 최대화한다. `elect(index, limit = 20000)`는 이를 호출하고 대상이 안정 리더가 될 때까지 기다린다. `depose(limit = 3000)`는 안정 리더가 있어야 하며 follower의 현재 타이머를 최대화하고 AppendEntries 결과를 일시적으로 버려 리더가 물러나게 한다. step에서 예외가 발생해도 기존 drop 설정을 복원한다. `elect`·`depose`는 시간 초과 시 false를 반환하며 잘못된 사전조건은 해당하는 `system_error` 또는 `invalid_argument`를 던진다.

`disconnect(from, to)`, `reconnect(from, to)`, `saturate(from, to)`, `desaturate(from, to)`는 **한 방향**을 바꾼다. 자기 연결은 잘못된 인자다. disconnected 대상은 send 완료 시 비동기 `no_connection`을 발생시킨다. saturated 연결은 send를 성공으로 완료하지만 전달을 버린다. disconnected 연결의 saturate/desaturate는 `no_connection`을 반환한다. `saturated(from, to)`로 그 플래그를 조회한다.

`kill(index)`는 노드의 tick 콜백을 억제하고 다른 모든 노드와의 양방향을 포화시킨다. 노드·FSM·영속 데이터를 유지하며 crash/restart 연산은 아니다. `revive(index)`는 콜백을 재개하고 다른 살아 있는 노드와의 양방향 포화를 해제한다. 명시적으로 끊은 연결은 disconnected 상태를 유지한다. `set_network_latency(index, msecs)`·`set_disk_latency(index, msecs)`는 이후 send 완료·디스크 제출의 지연에 적용하며 0도 허용한다. 이미 예약한 이벤트의 예정 시각은 유지된다.

`n_send(index, fixture_message_type)`는 성공으로 완료한 송신, `n_recv(...)`는 전달 횟수다. 열거형 코드는 AppendEntries 1, AppendEntriesResult 2, RequestVote 3, RequestVoteResult 4, InstallSnapshot 5, TimeoutNow 6이다. 버린 전달은 수신 횟수에 포함하지 않는다.

## 영속 상태와 장애

`set_term(index, term)`은 영속 term만 바꾼다. `set_snapshot(index, const snapshot&)`와 `add_entry(index, const entry&)`는 전달한 데이터를 깊은 복사한다. 영속 로그가 비어 있을 때 snapshot을 설정하면 suffix 시작 index를 `snapshot.index + 1`로 지정한다. 이 함수들은 실행 중인 노드의 메모리 상태를 바꾸지 않는다. backend `load`는 데이터를 깊은 복사한다. snapshot이 있으면 주입 순서와 무관하게 보관 중인 엔트리 전체를 `snapshot.index + 1`부터 시작하는 접미사로 반환한다. 기존 시작 index를 근거로 엔트리를 걸러내지 않으므로 주입하는 엔트리는 해당 snapshot에 대응하는 접미사로 구성해야 한다. load를 반복해도 저장한 snapshot·엔트리는 바뀌지 않으며, 할당 실패 시 호출자의 출력을 보존한다. trailing이 0인 snapshot put은 기존 로그를 버리고 snapshot 다음에 suffix를 시작하며, 0이 아니면 영속 엔트리를 유지한다. snapshot get은 독립적으로 합친 snapshot 또는 빈 optional을 반환한다. 메모리 backend의 `recover`는 기존 fixture의 미지원 계약과 같이 `io_error`를 반환한다.

`io_fault(index, delay, repeat)`는 load·bootstrap·start·term/vote 저장·실제 truncate·send 제출·append 제출·append 완료의 해당 I/O 호출을 센다. 음수 delay는 장애를 해제하고 delay 0은 즉시 실패한다. 양수 delay는 그만큼 정상 호출한 뒤 실패한다. 음수 repeat는 계속 실패하고, 양수 repeat는 그 횟수만 실패하며 0이면 추가 실패를 해제한다. 주입 오류는 `io_error`다. snapshot put/get 제출과 get 완료는 이 counter를 소비하지 않는다. trailing이 0인 snapshot 완료는 `truncate(1)`을 호출한다. 저장 로그가 비어 있지 않거나 suffix 시작 index가 1보다 크면 원본 C fixture의 내부 truncate처럼 counter를 소비한다. trailing이 0이 아니면 로그를 유지하고 truncate counter를 소비하지 않는다. 내부 truncate가 실패하면 `io_error`로 완료하고 이전 snapshot·로그를 보존한다. 유한 fault를 소진한 뒤에는 재시도할 수 있다. 즉시 제출 실패 시 완료 콜백을 호출하지 않으며 수락한 요청은 정확히 한 번 완료한다. 할당 실패도 해당 즉시·완료 오류 계약으로 전달한다. valueless 메시지 variant는 큐에 넣기 전에 `invalid`로 거부한다. 수락한 메시지·엔트리의 내용은 완료까지 변경하지 않아야 한다.

`close()`는 반복 호출할 수 있다. C fixture와 같이 먼저 서버·FIFO 순서로 대기 중인 디스크·send 요청을 완료하고 대기 중인 transmit을 버리며 가상 시간을 전진시키지 않는다. 그 뒤 모든 노드를 닫고 콜백이 끝나면 노드·FSM을 해제한다. 완료 콜백에서 flush한 영속 데이터를 관찰할 수 있다. 종료 전에 새 작업 제출을 멈추며 완료 콜백이 끝없는 요청을 만들지 않아야 한다. 노드나 backend를 직접 닫는 경우에는 해당 backend의 수락한 요청을 `canceled`로 완료한다. 취소 큐를 할당하거나 실제 I/O를 기다리지 않으며 소멸자는 fixture 종료를 호출한다. 진행 중인 이벤트는 반환할 때까지 자신의 backend·요청을 보관하므로 콜백에서 닫아도 안전하다. 보관한 닫힌 backend는 마지막 공유 핸들을 해제할 때까지 allocator resource를 유지할 수 있다. [메모리 할당 경계](memory.md)를 참고한다.

## 예제

```cpp
#include <raftpp/fixture.hpp>

struct counter final : raftpp::state_machine {
    unsigned count = 0;
    raftpp::error_code apply(raftpp::view<const raftpp::byte>,
                            raftpp::shared<void>& result) override
    { ++count; result.reset(); return {}; }
    raftpp::error_code restore(raftpp::shared_buffer) override
    { return raftpp::errc::io_error; }
    raftpp::error_code snapshot(raftpp::vector<raftpp::shared_buffer>&) override
    { return raftpp::errc::io_error; }
};

int main()
{
    raftpp::fixture cluster;
    auto first = std::make_shared<counter>();
    if (cluster.grow(first) ||
        cluster.grow(std::make_shared<counter>()) ||
        cluster.grow(std::make_shared<counter>())) return 1;
    raftpp::configuration members;
    if (cluster.configuration(3, members) ||
        cluster.bootstrap(members) || cluster.start()) return 2;
    if (!cluster.elect(0)) return 3;
    bool done = false;
    if (cluster.get(0).apply(raftpp::buffer{8},
        [&](raftpp::error_code error, raftpp::shared<void>) {
            done = !error;
        })) return 4;
    const auto index = cluster.get(0).last_index();
    if (!cluster.step_until_applied(cluster.size(), index, 2000)) return 5;
    return done && first->count == 1 ? 0 : 6;
}
```
