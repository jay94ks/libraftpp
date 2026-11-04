# asyncpp 모듈 API

[English](../../en/modules/asyncpp.md) · [모듈 목록](index.md)

`asyncpp_executor final : executor`는 `asyncpp::dispatcher::push`로 콜백을 제출한다. `RAFTPP_MODULE_ASYNCPP=ON`, 타깃 `raftpp::asyncpp`, 헤더 `<raftpp/asyncpp/asyncpp.hpp>`를 사용한다. core와 Threads에 의존하며 dispatcher 헤더는 체크아웃 또는 설치 SDK에서 찾는다. SDK 준비는 [의존성](../dependencies.md)을 따른다.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS asyncpp)
target_link_libraries(app PRIVATE raftpp::asyncpp)
```

## 생성과 소유권

```cpp
explicit asyncpp_executor(shared<asyncpp::dispatcher> dispatcher);
error_code post(function<void()> callback) noexcept override;
```

생성자는 dispatcher를 공동 소유한다. null이면 `std::invalid_argument("null asyncpp dispatcher")`를 던진다. 상류 `dispatcher`는 public 가상 소멸자를 제공하지 않으므로 `std::make_shared<ConcreteDispatcher>()`로 구체 타입의 owner를 생성한 뒤 변환해서 전달한다. 대여 포인터에서 별도 owner를 만들면 안 된다.

수락된 콜백도 dispatcher owner를 캡처한다. `asyncpp_executor` 객체를 파괴해도 큐에 수락된 작업과 dispatcher는 실행이 끝날 때까지 유지된다. 실행기 파괴는 큐 drain·취소·join이 아니다. 큐에 dispatcher 자신을 유지하는 작업이 남으면 owner도 남으므로 호출자가 큐를 계속 구동해야 한다.

## `post` 계약과 오류

| 반환 | 의미 |
| --- | --- |
| 빈 `error_code` | dispatcher가 콜백을 수락했다. 나중에 한 번 실행한다. |
| `errc::invalid` | 콜백이 비어 있다. |
| `errc::out_of_memory` | dispatcher 제출 중 `bad_alloc`이 발생했다. |
| `errc::io_error` | dispatcher 제출 중 다른 예외가 발생했다. |

즉시 실패 시 사용자 콜백을 호출하지 않는다. 전달된 callable의 캡처를 보존하려면 별도의 복사본/owner를 호출자가 유지한다. 큐가 실행한 사용자 콜백은 예외를 던지지 않아야 한다. `post`의 예외 매핑은 제출 단계에 적용되며 나중에 실행하는 콜백의 예외 처리기를 제공하지 않는다.

dispatcher는 `push`가 콜백을 나중에 실행하도록 큐에 넣고, 실패를 보고한 작업을 나중에 실행하지 않는 계약을 지켜야 한다. 호출 스레드 허용 여부·순서·큐 구동 방식은 구체 dispatcher를 따른다. 이 어댑터는 별도 스레드·동기화·Raft 콜백 전달을 생성하지 않는다.

## 사용 예제

```cpp
#include <raftpp/asyncpp/asyncpp.hpp>

raftpp::error_code increment_later(
    raftpp::shared<asyncpp::dispatcher> dispatcher,
    raftpp::shared<int> value)
{
    if (!value)
        return raftpp::errc::invalid;
    raftpp::asyncpp_executor scheduler{std::move(dispatcher)};
    return scheduler.post([value = std::move(value)] { ++*value; });
}
```

호출자는 구체 dispatcher의 shared owner와 유효한 값 owner를 제공하고 그 큐를 구동한다. 함수가 반환된 뒤 scheduler는 사라져도 캡처한 값과 dispatcher는 유지된다. 같은 값에 대한 다른 접근은 dispatcher의 실행 영역에서 직렬화한다. 노드 API와 자동 연결되는 실행기는 아니며 콜백 제출이 필요한 애플리케이션 코드에서 사용한다. [공통 실행기 계약](../streams.md)을 함께 따른다.
