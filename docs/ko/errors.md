# 오류와 소유 진단 정보

[English](../en/errors.md) · [노드 API](node.md) · [libuv I/O](uv_io.md)

`<raftpp/error.hpp>` 또는 `<raftpp.hpp>`를 포함하고 `raftpp::raftpp`에 연결한다. 오류 코드와 길이가 제한된 진단 값은 모든 기능 구성에서 사용할 수 있다.

## 코드와 메시지

`errc`는 Raft 상태 코드의 숫자 값을 정의한다. `make_error_code(errc)`는 이름이 `raft`인 `raft_category()`의 표준 `error_code`를 반환한다. 표준 error-code-enum 특수화를 제공하므로 `status == errc::not_leader`처럼 비교할 수 있다.

`error_message(errc) noexcept`는 할당 없이 정적 `string_view`를 반환한다. 오류 값 1~23은 `resource name too long`, `operation can't be performed at this time`을 포함해 C 참조 메시지를 사용한다. 성공의 C++ 메시지는 `success`, 알 수 없는 값은 `unknown error`다. 표준 `error_code::message()`는 소유 문자열을 반환하며 할당할 수 있다.

## 진단 값

```cpp
struct error_info {
    error_code code;
    error_code cause;
    array<char, 256> message;
    string_view text() const noexcept;
};
error_info make_error_info(error_code code, string_view operation = {},
                           string_view resource = {}, error_code cause = {}) noexcept;
error_info wrap_error_info(error_info info, string_view prefix) noexcept;
```

`error_info`는 표준 오류 코드와 고정 배열을 소유한다. 복사·이 helper를 통한 생성·감싸기·`text()`는 저장소를 할당하지 않는다. `text()`는 해당 값의 배열을 대여하므로 view를 보관하려면 값을 먼저 저장한다. 복사본은 콜백 반환·backend 파괴·노드 종료 후에도 유효하다. 기본 값은 오류 없음을 나타낸다.

`make_error_info`는 연산·자원·Raft 메시지를 조합한다. 비어 있지 않은 문맥은 `: `로 구분한다. 전달한 원인은 별도의 표준 `error_code`로 유지하고, 설명 문자열을 할당하지 않도록 메시지에 카테고리 이름과 숫자를 덧붙인다. 다른 카테고리의 상위 코드도 카테고리/숫자를 유지한다. 소유 시스템 설명이 필요하고 할당을 허용한다면 `cause.message()`를 별도로 호출한다. 성공은 빈 진단을 생성한다.

helper는 최대 255바이트와 NUL을 기록한다. 감싸기는 참조 C의 잘림 경계를 따른다. 접두사가 253바이트 이상이면 구분자/뒷부분을 생략하고, 짧으면 들어가는 만큼 뒷부분을 보존한다. `wrap_error_info`에 명시적으로 빈 접두사를 전달하면 오류 앞에 `: `가 붙는다. 잘림은 표시 문자열에만 영향을 주며 코드/원인은 보존한다. 공개 배열은 표준 값이다. 직접 변경해 종단 문자가 없어져도 `text()`는 배열 범위 안에서만 검색한다. 진단 문구는 사람이 읽는 용도이며 고정 파싱 규약이 아니다. 바이트 단위 잘림은 다중 바이트 문자를 나눌 수 있다.

## Backend와 노드 계약

`io::error_details() const noexcept`는 소유 진단을 반환하는 선택적 가상 조회이며 기본 구현은 빈 값이다. 사용자 backend는 즉시 실패를 반환하거나 실패 완료 콜백을 호출하기 전에 해당 정보를 공개한다. getter는 할당·예외·노드 재진입을 수행하지 않는다. 소유 이벤트 루프 스레드에서 접근을 직렬화한다. backend 정보가 비어 있거나 코드가 다르면 연산의 Raft 코드로 대체한다.

`node::error_details() const noexcept`는 기록된 노드 진단을 복사한다. 비동기 I/O 완료는 대기 처리나 이후 backend 변경 전에 콜백 진입 시 정보를 복사한다. 즉시 load·metadata·truncate·제출 실패도 backend 문맥을 캡처한다. 정리나 바깥쪽 시작 함수가 추가 오류를 보고해도 종료를 시작한 실패는 유지한다. 이동된 노드는 shutdown 진단을 반환한다. `node::error_message()`는 기록한 문구의 소유 문자열을 만들며 할당할 수 있다. `error()`는 계속 Raft 상태 조회다. bootstrap/recover backend 실패는 종료를 시작하지 않고 문맥을 기록한다. 그 밖의 즉시 검증/제출 제한 오류는 해당 연산으로 반환하며 노드 실패로 기록되지 않을 수 있다.

생성자 초기화 실패는 원래 상태와, 제공된 backend 정보의 코드가 일치하면 해당 문구를 포함한 `system_error`를 던진다. 생성/예외 형식화 중 할당은 표준 예외 규칙을 따른다. I/O 오류가 rollback이나 접수된 명령의 철회를 뜻하지 않는다.

UV 활성 시 `uv_io::error_details()`는 기록한 backend 진단을 반환한다. 디렉터리 stat 실패는 경로와 POSIX 원인(예: `std::errc::no_such_file_or_directory`)을 보존한다. 내부 파일 핸들 연산은 진단 출력으로 네이티브 원인을 소유 값으로 캡처할 수 있다. 진단은 연산/코드만 있고 원인이 비어 있을 수도 있다. 모든 backend나 네이티브 오류가 시스템 상세 정보를 제공한다고 가정하지 않는다. 취소·정리는 [노드](node.md)와 [UV](uv_io.md)의 완료 계약을 따른다.

## TCP 진단

`transport::error_details() const noexcept`는 선택적 가상 조회이며 기본값은 빈 진단이다. 기존 사용자 transport는 override 없이 사용할 수 있다. 구현은 실패 반환 또는 완료 전에 해당 요청의 정보를 공개한다. `tcp_transport::error_details()`는 연산·상대 ID·설정된 상대 주소와 제공 가능한 네이티브 원인을 소유 값으로 반환한다. 아직 송신자의 handshake를 읽지 못한 수신 연결은 상대 ID가 0이며 주소 대신 로컬 endpoint를 표시할 수 있다. 조회·복사는 할당하지 않고 이벤트 루프 스레드에서 수행한다.

TCP의 libuv 원인은 `libuv` 카테고리와 원래 음수 상태 값을 보존한다. Linux errno에 대응하는 값은 표준 오류 조건에 연결되어 `info.cause == std::errc::connection_refused`처럼 비교할 수 있다. `UV_EAI_*`·`UV_EOF`·`UV_UNKNOWN` 등 libuv 고유 값은 같은 카테고리를 유지하며 errno인 것처럼 변환하지 않는다. listener의 동기 `getaddrinfo` 실패는 `getaddrinfo` 카테고리에 그 함수의 코드를 보관한다. `EAI_SYSTEM`은 당시 errno를 `generic_category`로 보관한다. 원인 문구의 소유 문자열이 필요하면 `cause.message()`를 별도로 호출하며 할당될 수 있다.

연결 시도는 자기 진단을 보관하고 대기열에서 완료할 각 전송에 복사한다. 쓰기 요청도 자기 결과·peer 문맥을 완료까지 보관한다. 콜백 진입 시 정보를 복사한다. 다른 요청이나 재진입 호출이 이후 조회 값을 바꿀 수 있으나 이미 복사한 값은 연결·transport 정리 뒤에도 유효하다. 취소와 새로운 유효성 검사 실패는 이전 DNS·쓰기 오류의 원인을 물려받지 않는다. 노드는 UV가 복사한 정보를 기존 I/O 실패 처리에 사용하며, 전송 실패만으로 노드가 종료된다고 가정하지 않는다.

진단은 관찰 정보이며 `last_error()`나 연산 결과를 대신하지 않는다. 예를 들어 연결 재시도 중 실패 뒤에도 수락한 send가 성공할 수 있다. 즉시 거절된 send와 정상 EOF의 관찰 진단은 기존 `last_error()` 값을 변경하지 않는다. EOF는 `io_error`/`UV_EOF` 문맥으로 조회할 수 있지만 정상 연결 종료의 기존 상태·재시도·완료 동작은 유지한다. 상세 문구는 안정된 파싱 형식이 아니다. [TCP 전송 계약](tcp_transport.md)을 함께 따른다.

## 디스크 진단

UV 활성 시 metadata·스냅샷·segment 실패는 실패한 파일 또는 디렉터리 경로, 연산과 제공 가능한 POSIX 원인을 보존한다. 네이티브 원인은 Raft 상태와 별개다. 예를 들어 metadata 슬롯을 여는 데 실패하면 `io_error`와 `std::errc::is_a_directory`를 함께 보고할 수 있다. 경로·연산 문구도 다른 진단과 같은 길이 제한을 따른다.

내부 동기 디스크 연산은 선택적 `error_info*` 출력을 받는다. 공용 또는 스레드 지역 마지막 오류 대신 각 호출의 출력을 기록한다. 스냅샷 fallback 성공과 정상적으로 허용하는 metadata 슬롯 부재를 포함해, 성공하면 해당 출력을 비운다. 동시 worker는 서로 다른 출력을 제공하고 디스크 연산의 기존 동기화 요구사항을 따라야 한다. 스냅샷·로그 coordinator는 각 worker의 진단을 완료 콜백까지 보관하며 metadata 실패는 `uv_io`를 거쳐 노드에 전달된다.

최선 노력 정리는 정리를 유발한 실패를 덮어쓰지 않는다. 이후 독립적인 실패는 이전 네이티브 원인을 물려받지 않고 자기 문맥을 갖는다. 유효성 검사 실패와 시스템 오류를 제공할 수 없는 실패의 원인은 비어 있을 수 있다. 파일시스템 probe가 허용하는 미지원 기능은 진단이 빈 성공 결과이며 I/O 실패가 아니다. 진단은 영속성·복구·rollback 보장을 바꾸지 않는다.

파일시스템 probe는 AIO context 정리가 성공한 뒤 capability 결과를 공개한다. 정상 완료 또는 미지원 기능 판정 중 `io_destroy`가 실패하면 `io_error`를 반환하고 기존 capability 출력을 보존하며, 진단에 정리 연산·디렉터리·POSIX 원인을 기록한다. `ENOMEM`도 이 네이티브 정리 경로에서는 `io_error`다. 제출 등 앞선 단계가 이미 실패했다면 그 실패와 원인을 유지하고 정리는 최선 노력으로 수행한다.

## Native Writer 진단

UV 활성 시 native writer 실패는 연산·FD 번호·offset과 제공 가능한 POSIX 원인을 보존한다. 쓰기 결과 진단은 보고된 결과와 예상 바이트 수도 포함한다. 로그 계층에서는 `open-N` segment 이름을 덧붙인다. 스냅샷 worker 실패는 준비·공개·조회를 식별하고 제공 가능한 스냅샷 인덱스를 포함한다. 문구 길이는 위 제한을 따른다.

| 네이티브 관찰 결과 | 반환/완료 코드 | 진단 원인 |
| --- | --- | --- |
| `-EIO` 등의 음수 쓰기 결과 | `io_error` | `generic_category`의 대응 양수 POSIX 코드. |
| 음수 쓰기 결과 `-ENOSPC` | 기존 writer 상태 매핑을 유지한 `io_error` | `std::errc::no_space_on_device`. |
| 0 이상의 short write 결과 | `no_space` | 빈 값. 보고된 바이트 수로 실패를 설명한다. |
| AIO 초기화 제한 `-EAGAIN` | `too_many` | `std::errc::resource_unavailable_try_again`. |
| 즉시 네이티브 제출 실패 | 기존 매핑의 Raft 오류. 완료 콜백 없음 | 표현 가능한 경우 캡처한 네이티브 원인. |

각 worker는 자기 요청의 진단만 기록한다. 소유 이벤트 루프는 해당 완료 콜백 전에 이를 공개한다. backend API를 직접 사용할 때는 콜백 진입 시 `error_details()`를 복사한다. 다른 연산이나 재진입 호출이 backend의 저장 정보를 바꿀 수 있다. 노드는 완료 정보를 자동으로 복사한다. 스냅샷 완료 콜백 중에는 취소를 포함해 그 요청의 진단을 이전 writer 실패보다 우선한다. 콜백 사이에는 coordinator가 치명적인 writer 문맥을 유지할 수 있다. 요청·native 핸들이 해제된 뒤에도 진단 복사본은 문구와 코드를 소유한다. 기록된 FD 번호는 과거 연산을 설명하며 파일을 열린 상태로 유지하지 않는다.

재시도는 기존 완료·종료 동작을 유지한다. 재시도가 성공해 연산이 성공 완료된 뒤에도 앞서 관찰한 진단이 남을 수 있으며 `last_error()`는 기존 상태 의미를 유지한다. 연산 결과와 `node::error()`로 성공/실패를 판단한다. `error_details()`는 관찰한 일시적 실패를 포함한 문맥을 제공한다. native 종료·정리도 대기 요청 버퍼를 일찍 해제하지 않고 해당 실패를 캡처한다.

```cpp
#include <raftpp/node.hpp>

raftpp::error_info inspect_failure(const raftpp::node& node) noexcept
{
    auto info = node.error_details();
    if (info.code) {
        // info.text() borrows info; info.cause is a standard error_code.
        return raftpp::wrap_error_info(info, "application");
    }
    return info;
}
```
