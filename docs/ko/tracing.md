# 추적

[English](../en/tracing.md) · [노드](node.md) · [libuv I/O](uv_io.md) · [TCP](tcp_transport.md)

`<raftpp/tracing.hpp>`는 UV·LZ4·fixture 옵션과 무관하게 동기 diagnostic·metric 이벤트를 제공한다. `raftpp::raftpp`를 링크한다. `common.hpp`의 `source_location`은 C++20 `std::source_location`의 별칭이다.

## 관찰자

```cpp
#include <raftpp/tracing.hpp>
#include <iostream>

raftpp::tracer make_tracer()
{
    return raftpp::tracer{[](const raftpp::trace_event& event) {
        if (event.type != raftpp::trace_diagnostic || !event.info) return;
        const auto& value = *static_cast<
            raftpp::borrowed<const raftpp::diagnostic_trace>>(event.info);
        std::clog << value.location.file_name() << ':'
                  << value.location.line() << ' ' << value.message << '\n';
    }};
}
```

`tracer(trace_callback)`는 표준 공유 소유권으로 callable을 보관하며 복사본도 이를 공유한다. 생성과 `std::function` 변환은 할당 예외를 던질 수 있다. 빈 콜백이나 `tracer::noop()`는 추적을 비활성화하며 no-op 생성에는 할당이 없다. 기본 생성자는 프로세스 공통 stderr tracer를 선택한다.

| 메서드 | 계약 |
| --- | --- |
| `enabled() noexcept` | 사용자 관찰자 또는 활성 stderr sink가 선택되었는지 조회한다. |
| `log(trace_level, string_view, source_location = source_location::current()) noexcept` | 최대 1023바이트로 잘라 diagnostic을 보낸다. 기본 위치는 호출 지점이다. |
| `emit(uint32_t type, borrowed<const void> info = nullptr) noexcept` | 코드에 따라 데이터 형식이 정해지는 이벤트를 동기 전달한다. |

복사·내부 형식화·전달은 라이브러리 저장 공간을 할당하지 않는다. 관찰자 예외는 억제하며 연산 결과를 바꾸거나 종료를 시작하지 않는다. 전달 중 교체해도 실행 중인 callable은 생존한다. 다른 tracer를 통한 경우를 포함해 같은 스레드의 중첩 추적은 억제한다. 관찰자는 관찰 대상 노드·코어·I/O를 변경하거나 재진입하지 않으며, `set_tracer`로 tracer를 교체하는 것은 허용한다. 관찰 대상의 루프·스레드 계약을 따르며 공유 소유권이 서로 다른 루프의 capture 접근을 동기화하지는 않는다.

## 이벤트 데이터와 수명

| 코드 | 전달 중 데이터 |
| --- | --- |
| `trace_diagnostic` = 1 | `info`가 `trace_level level`·`string_view message`·`source_location location`을 담은 `diagnostic_trace`를 가리킨다. |
| `trace_write_submit` = 256 | null 데이터. native 파일 쓰기 제출 전이며 제출 실패 시 대응 완료가 없을 수 있다. |
| `trace_write_complete` = 512 | null 데이터. 취소를 포함해 수락한 native 쓰기 완료 콜백 전이다. |
| 애플리케이션 코드 | 호출자 정의 데이터. 1–255는 코어, 256–65535는 backend 예약 코드이며 애플리케이션은 65536부터 사용한다. |

수준은 C와 같은 error 1·warning 2·info 3·debug 4·trace 5다. 사용자 관찰자는 모든 수준을 받으며 직접 필터링할 수 있다. 이벤트·`info`·diagnostic 메시지는 전달 중에만 유효한 동기 대여 값이다. 레코드를 저장하거나 비동기 logger에 전달할 때 메시지를 `string`으로 복사하고 source-location 값을 보관한다. 이벤트·diagnostic만 복사해도 메시지를 소유하지 않는다. 애플리케이션 데이터도 송신자가 별도로 소유권을 유지하지 않는 한 같은 대여 계약을 따른다.

## 객체별 설정

생성 시 `node_options::tracing`·`uv_io_options::tracing`·`tcp_transport_options::tracing`을 설정한다. 노드·파일/타이머 backend·TCP는 독립 채널이므로 모두 수집하려면 같은 tracer를 각 옵션에 복사한다. 노드는 backend 관찰자를 바꾸지 않으며 `uv_io`도 전송 관찰자를 바꾸지 않는다.

이 클래스들은 각각 `set_tracer(tracer) noexcept`를 제공한다. UV setter는 완료 대기 중인 요청을 포함해 파일 coordinator와 기존·미래 native segment writer에 전달한다. 내부 `detail::file_writer`도 `writer_options::tracing`·`set_tracer`를 지원한다. 쓰기 중 교체하면 제출·완료가 서로 다른 관찰자에게 갈 수 있다. 설정은 교체 또는 객체 파괴까지 callable을 보관하며 close는 연산을 정리해도 설정을 지우지 않는다.

코어 diagnostic은 시작·모든 이벤트 종류·역할/term 전환·투표 결정·timeout 결정과 상태/update 요약을 설명한다. 노드 오류에는 연산 문맥과 숫자 상태가 들어간다. UV·TCP diagnostic은 수명·파일 쓰기·스냅샷 준비/공개·메시지·연결·재시도 큐를 설명한다. diagnostic 문구와 C++ 소스 경로는 관찰용이며 고정된 파싱 인터페이스가 아니다. native writer 이벤트 코드는 C 값을 보존한다.

## 기본 stderr

기본 sink는 비활성 상태로 시작한다. `maybe_enable_tracing(bool) noexcept`는 `LIBRAFT_TRACE`가 존재할 때만 프로세스 공통 atomic 스위치를 바꾸며 빈 문자열이나 `0`을 포함해 값은 무관하다. 변수가 없으면 스위치를 유지한다. `stderr_tracing_enabled() noexcept`로 조회한다. 노드 생성자는 C의 `raft_init`과 같이 `maybe_enable_tracing(true)`를 호출하며 독립 backend는 직접 호출할 수 있다. 환경 변수는 라이브러리를 병행 사용하기 전에 설정한다.

stderr는 데이터가 null이 아닌 모든 diagnostic 수준을 `LIBRAFT   <실시간 nanoseconds> <file>:<line> <message>` 형식과 끝 개행으로 출력한다. writer·애플리케이션 이벤트는 무시한다. 시간은 `std::chrono::system_clock`, 형식화는 표준 C I/O와 독립 스택 버퍼를 사용한다. no-op·사용자 tracer는 stderr 스위치의 영향을 받지 않는다.

## Assertion 호출 스택

`RAFTPP_WITH_BACKTRACE`는 기본 OFF인 CMake 옵션이다. ON이면 GNU libbacktrace의 헤더·라이브러리와 assertion 출력에 필요한 `__assert_fail`을 구성 시 검사한다. UV·LZ4·fixture와 독립이며, OFF에서는 libbacktrace를 요구하거나 링크하지 않는다. 라이브러리의 정적 링크에 필요한 의존성도 `raftpp::raftpp`를 통해 전달한다.

`<raftpp/config.hpp>`의 `RAFTPP_WITH_BACKTRACE`는 0 또는 1이며 `raftpp::features::backtrace`는 같은 선택을 나타내는 `inline constexpr bool`이다. 플래그는 빌드 선택을 나타낸다. 라이브러리에 `NDEBUG`가 정의되어 있으면 옵션이 ON이어도 assertion을 평가하거나 호출 스택을 출력하지 않는다. 일반적인 Debug 빌드는 assertion을 활성화하며 Release·RelWithDebInfo·MinSizeRel 빌드는 비활성화한다. 실제 동작은 라이브러리 컴파일 플래그를 따른다.

활성 assertion이 실패하면 식·소스 위치·함수와 호출 프레임을 stderr에 출력하고 `abort()`로 프로세스를 종료한다. 주소·심볼·파일·행의 해석은 바이너리와 디버그 정보에 따라 달라지며, 호출 스택 수집이 실패해도 assertion 종료는 유지한다. libbacktrace는 자체 메모리를 할당할 수 있으며 이 실패 경로는 사용자 PMR 자원을 사용하지 않는다. 출력 문구와 프레임 형식은 고정된 파싱 인터페이스가 아니다.

이 동작은 `LIBRAFT_TRACE`, stderr tracing 스위치와 사용자 `tracer`의 영향을 받지 않는다. 전역 signal handler를 설치하거나 예외·오류 코드로 변환하지 않는다. 옵션 OFF에서는 표준 C++ assertion 동작을 사용한다. 공개 API의 사전조건 위반을 복구 가능한 오류로 바꾸는 옵션은 아니다.
