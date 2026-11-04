# 노드, I/O와 상태 머신

[English](../en/node.md) · [이벤트 코어](core.md) · [버퍼, 엔트리와 스냅샷](storage.md) · [메시지 인코딩](encoding.md) · [가상 시간 Fixture](fixture.md) · [서버·클러스터 예제](examples.md) · [I/O 조립](composition.md)

`<raftpp.hpp>`를 포함하고 CMake 타깃 `raftpp::raftpp`에 연결한다. 이름은 `raftpp` 네임스페이스에 있다. 아래 API는 C++20을 사용하며 `RAFTPP_WITH_UV`, `RAFTPP_WITH_LZ4`, `RAFTPP_WITH_FIXTURE`와 무관하게 사용할 수 있다. 사용자는 `io`와 `state_machine`을 구현해 노드에 전달한다.

`RAFTPP_WITH_UV=ON` 구성에서는 `raftpp::raftpp`가 libuv의 헤더 검색 경로와 링크 의존성을 전달한다. 소비자가 libuv 이벤트 루프를 사용할 때도 해당 타깃에 연결하고 `<uv.h>`를 포함하면 된다. `RAFTPP_WITH_UV=OFF` 구성에는 libuv 사용 조건이 추가되지 않는다.

파일 저장·libuv 타이머와 전송 객체를 연결하는 backend는 [uv_io API](uv_io.md)를 참고한다.

`error_details()`·고정 메시지 저장·네이티브 원인·진단 수명은 [오류와 소유 진단 정보](errors.md)를 참고한다.

## 생성과 수명

```cpp
node(server_id id, string address, shared<io> backend,
     shared<state_machine> fsm, node_options options = {});
error_code bootstrap(const raftpp::configuration& configuration) noexcept;
error_code recover(const raftpp::configuration& configuration) noexcept;
error_code start() noexcept;
error_code close(close_callback callback = {}) noexcept;
```

`node`는 복사할 수 없고 이동할 수 있다. `id`는 0이 아니며 `address`는 비어 있지 않아야 한다. backend와 FSM은 필수이며 노드가 공동 소유한다. `owned<io>`와 `owned<state_machine>`도 이동해 전달할 수 있다. 생성자는 잘못된 인자에 `invalid_argument`, 할당 실패에 `bad_alloc`, `io::init` 오류에 `system_error`를 던질 수 있다. `io::init`에 전달되는 주소 뷰를 호출 이후에 보관하려면 backend가 복사해야 한다.

`bootstrap`은 새 저장소에 초기 설정을 기록하며, 기존 저장소에는 `cannot_bootstrap`을 반환한다. `recover`는 backend의 복구 계약에 따라 강제로 새 설정을 기록한다. 두 호출 모두 `start` 전에 수행한다. 새로 합류하는 노드는 bootstrap 없이 빈 저장소에서 시작할 수 있다. `start`는 저장된 term·vote·스냅샷·로그를 불러오고 스냅샷을 FSM에 복원한 뒤 이벤트 수신을 시작한다. 시작 실패 시 노드는 종료한다. 이미 시작한 노드의 중복 `start`는 `invalid`를 반환하며 기존 실행을 유지한다.

종료가 요청되지 않은 시작이 진행 중인 동안 사용자 backend/FSM에서 다시 `start`·`bootstrap`·`recover`를 호출하면 `busy`를 반환하고, 중첩 로드·복원이나 영속 설정 변경을 수행하지 않는다. 진행 중인 시작과 완료된 시작의 중복 호출은 구분한다. `io::load`, 시작 시 FSM `restore`, `io::start` 안에서 노드를 종료하거나 핸들을 파괴할 수 있다. 해당 호출이 정상 반환해도 종료가 요청됐으면 `start`는 `shutdown`을 반환하고 후속 시작 단계나 수신 구독을 재개하지 않는다. 이 명시적 종료 자체는 치명적 시작 오류로 기록하지 않는다. 내부 상태·backend·FSM은 실행 중인 호출이 돌아올 때까지 유지되며, 종료 콜백은 즉시 실행될 수도 있다.

`close`는 종료를 시작하고, 받아들인 I/O가 완료되거나 취소되어 정리된 다음 콜백을 실행한다. 종료 요청 시 미완료 클라이언트 요청은 `leadership_lost`로 완료될 수 있다. 종료 콜백은 즉시 실행될 수도 있다. 콜백 등록 메모리 할당에 실패하면 `out_of_memory`를 반환하고 해당 콜백을 호출하거나 새 종료를 시작하지 않는다. 인자 없이 `close()`를 다시 호출할 수 있다. 이미 종료한 노드의 종료 콜백은 즉시 실행된다.

실행 중인 사용자 I/O 또는 FSM 호출에서도 노드를 종료하거나 핸들을 파괴할 수 있다. 호출이 정상 반환해도 드라이버는 종료 여부를 다시 확인하고 새 I/O 작업·FSM 적용/복원을 시작하지 않는다. 이미 받아들인 I/O의 완료·취소와 스냅샷 정리 콜백은 계속 처리한다. 클라이언트 요청은 성공을 반환하더라도 완료 콜백에 `leadership_lost`가 전달될 수 있으며(제출 함수의 반환 전에도 가능), 그 완료 콜백은 종료 완료보다 먼저 실행된다. FSM `apply`·`restore` 안에서 종료한 경우 반환 후 `last_applied`를 추가로 진행하지 않는다. 이미 실행 중인 사용자 함수의 부작용이나 영속화된 명령을 되돌리는 동작은 아니다. 정상적인 명시 종료 자체를 새 치명적 오류로 기록하지 않는다.

소멸자와 이동 대입은 기존 노드의 비동기 종료를 시작한다. 소멸자는 기다리지 않는다. 내부 요청·데이터·backend·FSM은 실제 정리에 필요한 동안 생존한다. 이벤트 루프도 종료 완료까지 진행해야 한다. 노드 핸들을 완료 콜백에서 파괴해도 내부 작업의 수명은 유지된다. 이동한 원본은 `unavailable`이며 작업 제출에 `shutdown`을 반환한다.

노드 메서드와 backend/FSM 콜백은 하나의 이벤트 루프 스레드에서 직렬로 호출한다. 공동 소유권은 스레드 동기화를 제공하지 않는다.

노드는 [이벤트 코어의 term·시계 범위](core.md)를 따른다. backend의 `time()`은 현재 election timeout을 기준으로 그 범위 안에 있어야 한다. 상한을 넘는 시계로 시작하면 `start()`가 `too_big`을 반환하고 종료한다. 실행 중 timer/수신 이벤트에서 시계나 다음 선거 term의 표현 범위를 소진하면 노드는 `too_big` 오류를 기록하고 비동기 종료를 시작한다. 최대 term 자체의 수신과 단일 voter 자체 선출은 허용된다.

## 명령과 완료 콜백

```cpp
using completion = function<void(error_code)>;
using apply_callback = function<void(error_code, shared<void>)>;
using close_callback = function<void()>;

error_code apply(shared_buffer command, apply_callback callback = {}) noexcept;
error_code barrier(completion callback = {}) noexcept;
error_code add(server_id id, string_view address, completion callback = {}) noexcept;
error_code assign(server_id id, role role, completion callback = {}) noexcept;
error_code remove(server_id id, completion callback = {}) noexcept;
error_code transfer(server_id id = 0, close_callback callback = {}) noexcept;
```

반환 오류는 즉시 제출 실패를 뜻하며 콜백을 호출하지 않는다. 성공 반환은 요청을 받아들였다는 뜻이다. 제공한 완료 콜백은 나중에 성공 또는 실패로 정확히 한 번 호출하며, 제출 함수가 반환하기 전에 실행될 수도 있다. 일반적인 즉시 오류는 `not_leader`, `shutdown`, FSM 호출 안에서 재진입해 제출할 때의 `busy`, `out_of_memory`, `no_space`다. 콜백에서는 예외를 던지지 않는다. 일반 완료 콜백에서 새 요청을 제출할 수 있다.

`apply`는 명령 하나를 로그에 추가하고 commit 후 FSM에 적용한다. `buffer`와 `aligned_buffer`를 값으로 받는 오버로드도 있으므로 `std::move`로 직접 전달할 수 있다. `shared_buffer`의 복사본은 같은 읽기 전용 저장 공간을 공동 소유한다. 함수 인자로 이동한 버퍼는 즉시 오류가 발생해도 자동 정리되며 호출자에게 되돌아오지 않는다. FSM 결과는 `shared<void>`이며 필요한 타입으로 캐스팅해 보관할 수 있다. 명령 바이트와 콜백 상태는 요청 완료에 필요한 동안 라이브러리가 소유한다.

완료 콜백은 생략하거나 `nullptr`로 전달할 수 있다. null 함수 포인터도 콜백 생략과 같다. 모든 요청 메서드는 일반 함수·함수 포인터와 이동 전용 캡처가 있는 콜백도 받는다. 내부에서는 표준 `shared_ptr`로 콜백의 수명을 관리한다. 인자 생성이나 `std::function` 인자의 호출 전 복사는 여전히 표준 C++의 예외 규칙을 따른다.

```cpp
#include <raftpp.hpp>

raftpp::error_code submit(raftpp::node& node, raftpp::buffer command)
{
    auto context = std::make_unique<unsigned>(42);
    return node.apply(std::move(command),
        [context = std::move(context)](raftpp::error_code status,
                                       raftpp::shared<void> result) {
            // status를 처리한다. context와 result는 이 호출 동안 생존한다.
            (void)status;
            (void)result;
            (void)context;
        });
}
```

노드가 사용하는 로그·스냅샷 인덱스 상한은 `numeric_limits<log_index>::max() - 1`이다. 리더의 새 엔트리 제출이 이를 넘으면 `too_big`으로 즉시 거부하며 해당 요청의 완료 콜백을 호출하지 않는다. 수신·시작 상태에도 [이벤트 코어의 인덱스 범위](core.md)가 적용된다. native 저장소의 파일명 한도는 이보다 더 작을 수 있다.

`barrier`는 명령을 FSM에 적용하지 않고 로그의 barrier가 commit되면 완료한다. `add`는 서버를 `spare`로 추가한다. `assign`은 `voter`, `standby`, `spare`를 지정하며 voter 승격 전에 필요한 catch-up을 기다린다. 멤버십 변경을 동시에 제출하면 `cannot_change`가 발생할 수 있다. `assign`의 없는 서버는 `not_found`, 잘못되거나 이미 지정된 역할은 `bad_role`이다. `remove`는 설정에서 서버를 제거하며 없는 ID는 `bad_id`다.

종료 상태 등 공통 진입 검사를 통과한 뒤, `add`와 `remove`는 설정 복사본의 ID·주소 검증을 리더/변경 가능 여부보다 먼저 수행한다. 따라서 변경이 진행 중이어도 중복 ID·주소나 없는 제거 대상의 오류가 우선할 수 있다. `assign`은 리더/리더십 이전 여부를 먼저 확인하고, 요청한 역할의 유효성, 변경 가능 여부, 대상 존재·기존 역할 순서로 검사한다. 즉시 거절은 진행 중인 요청을 변경하거나 완료 콜백을 호출하지 않는다.

`transfer`는 다른 voter로 리더십을 이전한다. ID 0은 구성 순서에서 현재 로그 끝까지 복제된 첫 voter를 선택하며, 그런 voter가 없으면 마지막 다른 voter를 선택한다. 다른 voter가 없으면 `not_found`, 자신·미등록 ID·non-voter는 `bad_id`다. 콜백에는 상태 인자가 없으며 완료 후 `leader()`로 관찰한 리더를 확인한다. 종료나 이전 제한 시간 만료로도 콜백이 실행될 수 있어 성공 이전을 뜻하지 않는다. 이전 중에는 새 명령을 거부할 수 있다.

[이벤트 코어의 인계 계약](core.md)도 적용된다. 부분 복제 응답으로 시작한 인계 힌트는 수신자의 영속 로그가 일치하지 않으면 무시되며, 후속 ACK가 이를 자동 재전송하지 않는다. 제한 시간이 지나면 대기 중인 인계를 해제하고 명시적으로 다시 요청할 수 있다.

리더십 상실이나 I/O 장애로 받아들인 요청을 완료할 수 없으면 `leadership_lost`가 전달될 수 있다. 근본적인 노드 오류는 `error()`와 `error_message()`로 조회한다. 실패 콜백이나 호출자의 시간 제한은 이미 제출된 명령의 철회를 보장하지 않는다.

## Step 알림

```cpp
using step_callback = function<void()>;
error_code set_step_callback(step_callback callback = {}) noexcept;
```

`set_step_callback`은 원본 `raft.legacy.step_cb`에 대응하는 알림을 등록한다. 기본값은 알림 없음이다. 성공한 코어 step마다 상태 전이·요청 검사 이후, 해당 step의 term/vote/로그/스냅샷·송신 I/O와 FSM 적용 전에 같은 루프 스레드에서 호출한다. 따라서 새 commit 인덱스를 관찰해도 `last_applied`는 아직 이전 값일 수 있다. 이 알림 자체는 명령의 완료나 영속화를 뜻하지 않는다. 한 외부 이벤트가 내부 configuration/persistence 이벤트를 만들면 여러 번 호출될 수 있으며 상태가 같아도 알림을 생략하지 않는다. 실패한 step에는 호출하지 않는다. `bootstrap`·`recover`·조회·setter·직접 `close`는 자체적으로 코어 step 알림을 만들지 않는다. 완료 전달을 기다리는 apply 응답의 상태가 `no_space`이면 클라이언트 오류가 먼저 전달되도록 해당 step 알림을 생략한다. 생략한 알림을 자동 재생하지 않으며 이후 성공한 step에서 다시 검사한다.

콜백과 캡처는 표준 공동 소유로 보관한다. 일반 함수·함수 포인터·이동 전용 캡처를 허용한다. 실행 중 콜백을 교체하거나 해제해도 현재 호출은 끝까지 생존하며 새 콜백은 이후 step에 적용된다. 빈 함수, `{}` 또는 `nullptr`는 할당 없이 해제한다. 등록 할당 실패는 `out_of_memory`이며 기존 콜백을 유지한다. 이동한 원본 노드는 `shutdown`을 반환한다. 종료한 노드도 등록/해제를 허용하지만 새 step은 실행되지 않으며 캡처는 교체·해제·객체 파괴까지 보관된다. 인자 생성·`std::function`의 호출 전 복사는 표준 C++ 예외 규칙을 따른다.

알림에서 상태를 조회하거나 콜백을 교체/해제하고 노드를 종료·파괴할 수 있다. 종료하면 후속 I/O/FSM 단계를 시작하지 않고 이미 받아들인 요청은 기존 종료 계약으로 완료한다. 종료 전 알림 안의 새 요청 제출은 `busy`다. 콜백에서 예외를 던지지 않는다. 예외는 노드의 이벤트 경계에서 오류로 변환되어 종료를 시작하며, 일반 예외는 `io_error`, `bad_alloc`은 `out_of_memory`, `length_error`는 `too_big`다.

```cpp
#include <raftpp/node.hpp>

raftpp::error_code count_steps(raftpp::node& node,
                              raftpp::shared<unsigned> notifications)
{
    return node.set_step_callback(
        [notifications = std::move(notifications)] { ++*notifications; });
}
```

## 옵션과 상태 조회

| `node_options` 필드 | 기본값 | 의미 |
| --- | --- | --- |
| `election_timeout` | 1000 | 선거 제한 시간, ms. heartbeat보다 크며 `uint32_t` 최댓값의 절반 이하다. |
| `heartbeat_timeout` | 100 | heartbeat 간격, ms. 0보다 커야 한다. |
| `install_snapshot_timeout` | 30000 | 스냅샷 설치 제한 시간, ms. 0보다 커야 한다. |
| `snapshot_threshold` | 1024 | 스냅샷 생성 간격을 결정하는 적용된 엔트리 수. |
| `snapshot_trailing` | 2048 | 로컬 스냅샷 이후 보관할 로그 엔트리 수. |
| `max_catch_up_rounds` | 10 | 승격 catch-up 라운드 한도. |
| `max_catch_up_round_duration` | 5000 | catch-up 라운드 제한 시간, ms. |
| `max_inflight_entries` | 32 | 서버별 응답 대기 중인 복제 엔트리 수 한도. |
| `capacity_threshold` | 4096 | 투표자 저장 용량 기준, KiB. 0이면 용량 검사를 끈다. |
| `pre_vote` | false | pre-vote 사용 여부. |
| `force_barrier` | false | 여러 투표자가 있는 설정의 리더 전환 시 초기 barrier 강제 추가. |
| `tracing` | 기본 stderr tracer | Diagnostic 관찰자. [추적](tracing.md)을 참고한다. |

각 옵션은 같은 이름의 `set_...` 메서드로 변경할 수 있다. `force_barrier`는 생성 시에만 설정하며 `tracing`은 `set_tracer(tracer)`로 바꾼다. 세 timeout setter는 `error_code`를 반환하며 위 조건 위반 시 `invalid`다. 나머지 setter는 `void`다. `seed(uint32_t)`는 내부 선거 난수 상태를 설정한다.

`force_barrier`는 이미 commit된 로그에서도 여러 투표자가 있는 리더 전환에 barrier를 추가한다. 단일 투표자는 기존 self-election/commit 경로를 따르며 이 옵션으로 barrier를 추가하지 않는다. 라이브러리는 실행 파일명을 검사하지 않는다. 실행 환경에 따른 호환 정책이 필요하면 호출자가 생성 옵션으로 명시한다.

election setter는 시작 시각을 초기화하지 않고 follower/candidate 만료 시간을 다시 추출한다. 새 timeout이 코어에 기록된 현재 시계의 표현 범위를 초과하면 `too_big`을 반환하고 설정·난수·만료 시각을 보존한다. legacy 호환을 위해 heartbeat가 15 ms일 때 `set_election_timeout(150)`은 election/heartbeat를 450/45 ms로 변경한다. 명시적인 생성자 옵션은 그대로 적용한다. backend tick 간격을 위해 `start()` 전에 heartbeat를 설정한다. 이후 setter는 해당 타이머를 재시작하지 않는다. 이름·독립 난수·참조 버전 정보는 [이벤트 코어와 공통 조회](core.md)를 참고한다.

| 조회 | 반환 또는 출력 |
| --- | --- |
| `state()` | `node_state::{unavailable,follower,candidate,leader}`. 시작 전에는 기준 `raft_init`과 같이 `follower`, 종료 중에는 `unavailable`. |
| `current_term()`, `voted_for()` | 현재 term과 투표한 서버 ID. |
| `last_index()`, `commit_index()`, `last_applied()` | 마지막 로그, commit, FSM 적용 인덱스. |
| `timeout()` | backend ms 시계 기준 다음 절대 만료 시각. 종료 중이거나 이동된 핸들은 0이다. 시작 전 추출하지 않은 기본 타이머도 0이다. |
| `local_role()` | 현재 설정의 역할인 `optional<role>`. 설정에 없으면 빈 값. |
| `leader()` | 알려진 리더의 `optional<leader_info>` 값 복사. `leader_info`는 `id`, `address`를 갖는다. |
| `configuration()` | 현재 [설정](configuration.md)의 값 복사. |
| `match_index(id, index)` | 리더가 확인한 해당 서버의 복제 인덱스를 출력. |
| `catch_up(id, status)` | `catch_up_status::{none,running,aborted,finished}`를 출력. |
| `transferee()` | 이전 대상 ID. 대상이 없으면 0. |
| `error()`, `error_message()` | 기록한 노드 오류와 문맥 메시지. 오류가 없으면 성공 코드·빈 문자열. |

`match_index`와 `catch_up`은 `error_code`를 반환하며 종료 시 `shutdown`, 리더가 아니면 `not_leader`, 없는 ID는 `bad_id`다. 실패 시 출력 인자를 보존한다. 문자열·설정 값을 복사하는 조회는 할당 예외를 던질 수 있다. `set_pre_vote`와 `seed`처럼 반환값이 없는 설정도 같은 스레드에서 수행한다.

`leader_info`는 `<raftpp/types.hpp>`의 소유 값이며 이벤트 코어 조회와 공유한다. 노드의 리더·역할·복제 진행 조회는 같은 코어 의미를 사용하고, 이동·종료에 대한 노드의 상태 검사를 먼저 적용한다.

## I/O 구현 계약

`io`는 가상 소멸자가 있는 추상 클래스다. 다음 메서드를 구현한다. `error_code`를 반환하는 비동기 제출 메서드는 즉시 오류 시 콜백을 호출하지 않고, 성공 시 종료·취소를 포함해 정확히 한 번 완료한다. 인라인 완료도 허용한다.

| 메서드 | 계약 |
| --- | --- |
| `init(server_id, string_view)` | 저장소와 주소를 초기화한다. 실패한 초기화 자원도 backend가 정리한다. |
| `bootstrap(const configuration&)`, `recover(const configuration&)` | 시작 전 초기 설정·강제 복구를 동기적으로 기록한다. |
| `load(io_state&)` | term, voted_for, optional snapshot, start_index, entries를 값으로 반환한다. 첫 로그 인덱스 기본값은 1이다. |
| `start(unsigned interval, function<void()> tick, function<void(message)> receive)` | ms 간격의 타이머와 수신 콜백을 등록한다. |
| `close(close_callback) noexcept` | 새 타이머·수신을 중단하고 받아들인 연산을 완료·취소한 뒤 종료 콜백을 호출한다. |
| `set_term(term)`, `set_vote(server_id)` | 응답 전 안정적으로 기록한다. 새 term 기록은 저장된 vote도 0으로 초기화한다. |
| `truncate(log_index)` | 해당 인덱스부터 로그 접미사를 제거한다. 후속 append와 진행 중인 쓰기의 순서를 보장한다. |
| `append(log_index, shared<const entry_batch>, completion)` | 지정 인덱스부터 엔트리를 기록하고 안정적인 저장 이후 완료한다. |
| `send(shared<const message>, completion)` | 메시지와 payload를 전송 완료까지 유지한다. |
| `snapshot_put(unsigned trailing, shared<const snapshot>, completion)` | 스냅샷을 저장한다. trailing은 snapshot이 덮는 로그의 보존 수이며 0은 보존을 요구하지 않는다. 설치에도 0을 사용한다. 충돌하는 이력은 교체하며 저장소에 따라 일치하는 suffix는 유지할 수 있다. |
| `snapshot_get(snapshot_callback)` | 안정된 스냅샷을 `optional<snapshot>` 값으로 반환한다. 콜백 타입은 `function<void(error_code, optional<snapshot>)>`다. |
| `time() const noexcept` | 단조 증가하는 ms 시간. |
| `random(uint32_t minimum, uint32_t maximum) noexcept` | 양 끝을 포함하는 범위의 난수. |
| `capacity() const noexcept` | 로그 저장에 보장하는 용량, KiB. 기본 구현은 0이다. |

용량을 제공하지 않는 backend는 `capacity_threshold = 0`으로 검사를 끌 수 있다. 전송 실패는 복제 프로토콜이 재시도한다. 지속적인 저장 실패는 노드를 종료할 수 있다. 참조·뷰 인자를 backend에서 비동기로 사용하려면 그 데이터의 소유권을 별도로 확보해야 한다. 소유 핸들로 전달한 batch·message·snapshot은 완료 시점까지 보관할 수 있다.

`entry_batch`는 `vector<shared<const entry>>`다. `message`는 `server_id`, 소유 문자열 `server_address`, `message_payload`를 갖는다. 송신에서는 목적지, 수신에서는 송신자의 ID와 주소다. payload는 순서대로 `append_entries`, `append_entries_result`, `request_vote`, `request_vote_result`, `install_snapshot`, `timeout_now`의 표준 variant다. 이 순서의 `index() + 1`이 원본 RPC 코드다. 메시지의 version, term, 로그 인덱스·term, vote, pre-vote, features, capacity, 설정과 payload는 [message.hpp](../../include/raftpp/message.hpp)의 필드로 표현한다. `message_feature_capacity`는 용량 보고 비트다. `install_snapshot::needs_data`는 내부의 스냅샷 조회 지시이며 노드 드라이버가 처리한다. backend가 전송하거나 수신에 전달하는 메시지에서는 `false`여야 한다.

| payload 타입 | 필드와 의미 |
| --- | --- |
| `request_vote` | `version` 기본값 2. `term`은 선거 term, `candidate_id`는 후보 ID다. `last_log_index`·`last_log_term`은 후보의 영속 로그 경계다. `disrupt_leader`는 리더십 이전 시 기존 리더가 있다는 이유로 투표를 거부하는 검사를 건너뛰며, `pre_vote`는 실제 term·vote 기록 전의 사전 투표다. |
| `request_vote_result` | `version` 기본값 2. `term`, `vote_granted`, `pre_vote`는 투표 응답을 표현한다. `features`는 지원 기능 비트, `capacity`는 KiB 단위의 로그 저장 보장 용량이다. |
| `append_entries` | `version` 기본값 0. `term`은 리더 term, `prev_log_index`·`prev_log_term`은 새 엔트리 직전의 일치 경계, `leader_commit`은 리더의 commit 인덱스다. `entries`는 순서대로 추가할 공유 엔트리 핸들이며, 빈 배치는 heartbeat다. |
| `append_entries_result` | `version` 기본값 2. `term`은 응답 term이다. `rejected`는 성공 시 0, 거절 시 거부한 이전 로그 인덱스다. `last_log_index`는 성공 시 확인한 영속 일치 구간의 끝이며, 거절 시 재시도 위치를 위한 로그 힌트다. `features`·`capacity`는 투표 응답과 같은 단위·의미다. |
| `install_snapshot` | `version` 기본값 0. `term`은 리더 term, `last_index`·`last_term`은 스냅샷 경계다. `configuration`·`configuration_index`는 스냅샷의 설정과 그 로그 인덱스, `data`는 스냅샷 바이트를 소유하는 공유 버퍼다. `needs_data`의 backend 계약은 위 설명을 따른다. |
| `timeout_now` | `version` 기본값 0. `term`과 `last_log_index`·`last_log_term`은 리더십 이전에 사용하는 term과 로그 경계다. 메시지를 받은 대상이 즉시 선거를 시작하도록 요청한다. |

현재 candidate가 아닌 노드는 버전 2 이상인 `request_vote_result`의 `pre_vote=true` 응답을 무시한다. 이 늦은 응답의 제안 term 때문에 현재 term·vote를 바꾸거나 I/O에 영속화를 요청하지 않는다. 실제 투표 응답과 pre-vote 표시를 지원하지 않는 버전 1 응답의 더 높은 term은 기존 갱신 규칙을 따른다.

## 상태 머신 구현 계약

```cpp
virtual error_code apply(view<const byte> command, shared<void>& result) = 0;
virtual error_code restore(shared_buffer data) = 0;
virtual error_code snapshot(vector<shared_buffer>& buffers) = 0;
virtual error_code snapshot_async(vector<shared_buffer>& buffers);
virtual void snapshot_finalize(vector<shared_buffer>& buffers) noexcept;
```

FSM 메서드는 노드의 이벤트 루프에서 동기적으로 실행된다. `apply`는 commit된 명령을 로그 순서대로 적용한다. 명령 뷰를 호출 이후 보관하려면 복사한다. `result`에 지정한 공유 객체가 해당 명령 완료 콜백으로 전달된다. `restore`는 하나로 합친 공유 버퍼로 전달한 스냅샷 상태를 복원한다. 저장 공간은 핸들을 보관해 유지할 수 있다. `apply`나 `restore` 실패는 노드를 종료할 수 있다.

`snapshot`은 그 시점의 상태를 읽기 전용 버퍼들로 고정한다. 성공한 준비 작업에는 저장 성공·실패·취소와 무관하게 `snapshot_finalize`가 정확히 한 번 대응한다. 준비 자체의 실패는 정리 대상인 성공으로 취급하지 않는다. 선택적인 두 번째 단계 `snapshot_async`도 동기 반환이며 기본 구현은 성공이다. 기본 `snapshot_finalize`는 버퍼를 비운다. `snapshot_async` 또는 즉시 스냅샷 제출 실패 시 준비한 상태를 정리하고 다음 생성 기회를 기다린다. 저장 완료 전에 FSM이 버퍼를 변경하거나 해제해서는 안 된다.

로컬 스냅샷의 메타데이터·요청 준비나 `snapshot`에서 `bad_alloc` 또는 `length_error`가 발생하면 해당 생성 시도를 건너뛰고 이후 이벤트에서 다시 시도한다. 이미 commit·적용된 명령과 성공 콜백은 유지하며 이 오류만으로 노드를 종료하지 않는다. 고정에 성공한 이후 제출 준비가 실패하면 `snapshot_finalize`로 정리한다. 그 밖의 첫 단계 FSM 예외와 이미 수락된 스냅샷 저장의 완료 오류는 노드를 종료할 수 있다.

로컬 snapshot 저장이 성공하면 그 완료 이벤트가 코어에 반영되기 전에도 동일 commit index의 저장을 중복 제출하지 않는다. 즉시 제출 실패는 이후 snapshot 기회에 다시 시도할 수 있다. 인라인 완료도 같은 계약을 따른다.
