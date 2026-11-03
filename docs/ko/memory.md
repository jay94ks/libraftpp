# 메모리 Resource와 소유권

[English](../en/memory.md) · [버퍼](storage.md) · [노드](node.md) · [Native I/O](uv_io.md) · [Fixture](fixture.md)

`<raftpp/memory.hpp>`를 포함하고 `raftpp::raftpp`를 링크한다. 이 API는 표준 C++20 PMR·스마트 포인터를 사용하며 UV·LZ4·fixture에 의존하지 않는다. `common.hpp`의 `pmr`는 `std::pmr`의 별칭이다.

## 소유권 의미를 나타내는 표준 별칭

`<raftpp/common.hpp>`는 다음 별칭을 제공한다. 별도 포인터 클래스나 참조 계수 구현을 추가하지 않으며 대응하는 표준 타입의 생성·변환·이동·해제 계약을 그대로 따른다.

| 별칭 | 대응 타입과 소유권 |
| --- | --- |
| `owned<T, Deleter = std::default_delete<T>>` | `std::unique_ptr<T, Deleter>`. 단독 소유하며 이동으로 소유권을 이전한다. native 자원에는 짝이 맞는 deleter를 사용한다. |
| `unique<T, Deleter = std::default_delete<T>>` | `owned<T, Deleter>`와 같은 타입인 호환 이름이다. |
| `shared<T>` | `std::shared_ptr<T>`. 실제 소유 핸들을 복사하면 대상의 수명을 공유한다. |
| `weak<T>` | `std::weak_ptr<T>`. 소유하지 않으며 `lock()`의 반환 소유자를 유지하는 동안 대상에 접근한다. |
| `borrowed<T>` | `T*`. null을 허용하는 비소유 포인터이며 수명을 연장하지 않는다. |
| `ref<T>` | `std::reference_wrapper<T>`. 살아 있는 lvalue를 참조하며 `get()`으로 접근한다. 대상의 수명은 별도로 보장해야 한다. |
| `view<T, Extent = std::dynamic_extent>` | `std::span<T, Extent>`. 연속 메모리의 비소유 뷰이며 읽기 전용 접근은 `view<const T>`로 표현한다. |

`borrowed`, `ref`, `view`를 복사하거나 콜백에 캡처해도 대상의 생존을 보장하지 않는다. 비동기 호출에서는 필요한 소유 값을 캡처하거나 해당 연산이 요구하는 소유 버퍼를 전달한다. `shared`의 수명 공유는 대상에 대한 동시 접근을 동기화하지 않는다. 데이터 자체를 소유하는 `buffer` 같은 값에는 수명 공유가 필요할 때만 추가 소유 핸들을 사용한다.

`array`, `vector`, `deque`, `list`, `map`, `string`, `string_view`, `optional`, `variant`, `function`, 오류 타입과 공통 타입 특성도 표준 타입의 `using` 선언으로 제공한다. 표준 템플릿 인자와 연산은 그대로 사용한다. `chrono`, `pmr`, `fs`는 각각 `std::chrono`, `std::pmr`, `std::filesystem`의 네임스페이스 별칭이다.

## Resource 선택

| 함수 | 계약 |
| --- | --- |
| `get_memory_resource() noexcept` | 라이브러리가 선택한 `pmr::memory_resource`의 공유 핸들을 반환한다. 기본값은 표준 `new_delete_resource()`다. |
| `set_memory_resource(shared<pmr::memory_resource>) noexcept` | 선택을 atomic 교체하고 이전 resource를 반환한다. null은 내장 기본값을 선택한다. |
| `reset_memory_resource() noexcept` | 내장 기본값으로 복원한다. |

선택은 allocator·버퍼·allocator 연동 라이브러리 객체를 만들 때 적용한다. 기존 할당은 원래 resource를 유지한다. 기존 버퍼의 resize는 원래 allocator를 사용하며 선택을 초기화해도 해제 경로를 바꾸지 않는다. 이 선택은 `std::pmr::set_default_resource`와 독립적이고 프로세스 공통 `operator new`나 libuv·LZ4 allocator를 교체하지 않는다. 정적 내장 resource에는 별도 공유 제어 블록 할당이 없다.

내장 핸들은 소유 제어 블록 없이 정적 resource를 alias한다. 포인터는 null이 아니고 `use_count`는 0이며 그 weak 핸들은 만료 상태다. 이는 정적 resource의 수명을 뜻하지 않는다. 소유 공유 포인터로 전달한 사용자 resource는 일반 공유·weak 소유권 의미를 유지한다.

## Resource 수명을 자동 보관하는 버퍼

```cpp
#include <raftpp/buffer.hpp>

raftpp::shared_buffer make_payload()
{
    auto resource = std::make_shared<
        raftpp::pmr::synchronized_pool_resource>();
    raftpp::buffer bytes{4096,
        raftpp::resource_allocator<raftpp::byte>{resource}};
    bytes.bytes()[0] = raftpp::byte{42};
    return raftpp::shared_buffer{std::move(bytes)};
}
```

반환 값이 pool을 보관하므로 호출자가 별도 resource 소유자를 유지할 필요는 없다. `shared_buffer`의 slice는 같은 payload·resource를 유지하며, 표준 공유 제어 블록도 payload 파괴 이후 마지막 weak 참조가 해제될 때까지 resource를 보관한다.

`buffer(size, resource_allocator<byte> = {})`와 `buffer::copy(bytes, allocator = {})`는 명시한 또는 현재 resource를 선택한다. allocator만 받는 생성자는 빈 버퍼를 만든다. `get_allocator()`는 소유 allocator를 반환한다. `resize`는 기존 prefix를 보존하고 추가 바이트를 0으로 초기화하며 할당 실패 시 버퍼를 보존한다. `clear`는 표준 vector처럼 capacity를 유지한다. 이동 대입·swap은 서로 다른 resource 사이에서도 저장 공간과 함께 allocator를 전달하며 이동된 원본 버퍼도 다시 사용할 수 있다.

`aligned_buffer(size, alignment, resource_allocator<byte> = {})`의 alignment는 `alignof(max_align_t)` 이상인 2의 거듭제곱이어야 한다. size는 alignment의 배수일 필요가 없다. 저장 공간을 0으로 초기화하며 deleter가 정확한 resource·크기·정렬을 보관한다. `get_allocator()`로 소유 allocator를 조회한다. 이동·swap은 짝이 맞는 할당 정보를 유지한다. `shared_buffer::copy(bytes, allocator = {})`도 같은 resource 선택을 지원한다. 생성은 `bad_alloc`·`length_error`·정렬/인자 예외를 던질 수 있으며 실패 가능한 라이브러리 연산은 문서의 `error_code`·출력 계약을 유지한다.

## 표준 Allocator와 객체 생성

표준 Allocator 계약에 따라 `T`는 cv 한정이 없는 객체 타입이다. const 객체를 만들 때는 `std::allocate_shared<const T>(resource_allocator<T>{}, args...)`처럼 allocator의 value type은 비-const로 전달한다.

`resource_allocator<T>`는 표준 Allocator 인터페이스를 따르며 원시 할당·해제를 `std::pmr::polymorphic_allocator<T>`에 위임한다. 일반 polymorphic allocator와 달리 추가 상태가 `shared_ptr`로 resource를 소유한다. rebinding·allocator traits·표준 컨테이너를 지원한다. 복사 대입·이동 대입·swap은 resource를 전달하며 allocator를 이동해도 원본의 resource를 사용할 수 있다. 동등성은 원래 resource 포인터로 판단한다. 표준 allocator 크기 제한을 넘는 개수는 `bad_array_new_length`를 던진다.

```cpp
#include <raftpp/memory.hpp>

raftpp::shared<int> make_value()
{
    auto resource = std::make_shared<
        raftpp::pmr::synchronized_pool_resource>();
    raftpp::resource_allocator<int> allocator{resource};
    auto unique = raftpp::allocate_owned<int>(allocator, 42);
    raftpp::resource_vector<int> values{allocator};
    values.push_back(*unique);
    return std::allocate_shared<int>(allocator, values.front());
}
```

공유 객체에는 표준 `std::allocate_shared<T>`를 사용한다. `allocate_owned<T>(resource_allocator<T>, args...)`는 표준 PMR `new_object`·`delete_object`로 scalar 소유 할당을 제공한다. 반환하는 `resource_owned<T>`는 `unique_ptr<T, resource_deleter<T>>`의 별칭이며 생성자가 예외를 던지면 저장 공간을 해제한다. deleter가 원래 타입·resource를 보관하며 배열은 지원하지 않는다. 이 단독 소유자를 호환되는 `shared<Base>`로 변환하면 원래 deleter를 유지한다. 그 포인터를 다른 allocator로 수동 해제하지 않는다.

## Allocator를 지원하는 표준 컨테이너

`common.hpp`는 resource 선택 함수·`resource_allocator`와 다음 별칭을 제공한다. 소유 객체 생성 함수와 deleter도 사용하려면 `memory.hpp`를 포함한다. 각 별칭은 대응 표준 컨테이너의 템플릿 인자와 연산을 유지한다.

| 별칭 | 표준 타입과 기본값 |
| --- | --- |
| `resource_vector<T, Allocator = resource_allocator<T>>` | `std::vector<T, Allocator>` |
| `resource_deque<T, Allocator = resource_allocator<T>>` | `std::deque<T, Allocator>` |
| `resource_list<T, Allocator = resource_allocator<T>>` | `std::list<T, Allocator>` |
| `resource_map<Key, Value, Compare = std::less<Key>, Allocator = resource_allocator<pair<const Key, Value>>>` | `std::map<Key, Value, Compare, Allocator>` |
| `resource_basic_string<Char, Traits = std::char_traits<Char>, Allocator = resource_allocator<Char>>` | `std::basic_string<Char, Traits, Allocator>` |
| `resource_string` | `resource_basic_string<char>` |

기본 allocator는 컨테이너 생성 시 라이브러리 선택을 보관하며 이후 확장에도 같은 allocator를 사용한다. 복사 생성은 원본 allocator를 유지하고 복사·이동 대입과 swap은 allocator를 전달한다. 각 resource 객체는 컨테이너의 모든 사용에 적합해야 한다. 표준 allocator 사전조건도 적용하므로 `splice`로 교환하는 리스트의 allocator는 같아야 한다. allocator 타입이 다른 컨테이너와 문자열은 서로 다른 타입이므로 경계에서는 뷰나 명시적 값 복사를 사용한다.

## 할당 경계와 병행 실행

라이브러리 바이트 버퍼·정렬 staging/수신 버퍼·공유 payload 제어 블록·할당된 entry 핸들·보관하는 tracer/callback 객체·노드 요청/상태 객체·native disk/file/TCP 요청/상태 객체는 이 resource를 사용한다. 로그 캐시·참조 인덱스와 term trail 저장 공간, 코어 주소·후보 투표·리더 진행 상태·rollback 백업·코어 송신 메시지 배열도 resource를 지원하는 표준 컨테이너를 사용한다. Fixture 상태·요청 객체와 대기 요청·리더 로그 컨테이너도 선택한 resource를 사용한다. 노드 요청·operation·이벤트·종료 큐, native writer의 요청·버퍼·iovec·완료 배열, log writer의 segment·preparation·truncate·barrier 큐, 스냅샷 get·UV send 큐와 TCP listener·peer·incoming·send 큐, stream 버퍼 descriptor·내부 주소도 같은 방식을 사용한다.

각 기본 allocator는 생성 시점의 선택을 보관하며 기존 노드가 미래 요청 객체·역할 상태·이벤트 update의 선택을 고정하지 않는다. 기존 큐의 확장은 새 resource로 만든 요청을 삽입할 때도 원래 resource로 할당한다. splice로 연결한 큐 쌍과 log writer의 barrier 요청 그룹은 대응하는 원래 큐와 allocator를 명시적으로 공유하며 완료·취소 임시 리스트도 이를 유지한다. 선택 변경은 기존 저장 공간을 옮기지 않는다. 닫힌 핸들도 파괴될 때까지 빈 컨테이너와 그 resource 소유자를 유지할 수 있다. 적용할 연산 전에 resource를 선택하거나 값에 명시적 allocator를 전달한다.

일반 `vector`·`string`·`list` 별칭은 명시적 resource allocator가 없으면 표준 기본 allocator를 유지한다. 공개 entry batch·메시지 주소·configuration/snapshot 값 컨테이너와 파일 목록 결과도 그 기본값을 유지한다. 라이브러리 resource 선택은 그 내부 저장 공간·`std::function` boxing·filesystem 경로·예외 저장 공간·사용자의 `std::make_shared` 객체·외부 라이브러리 내부 할당을 바꾸지 않는다. 객체의 표준 컨테이너 멤버가 상위 객체의 allocator를 자동 상속하지는 않는다. 소유권은 resource 객체에 적용하며 resource가 빌린 upstream이나 외부 arena도 표준 PMR 계약에 따라 유효해야 한다.

선택은 atomic이지만 allocator·resource 접근을 자동 직렬화하지는 않는다. native 파일 worker와 루프에서 동시에 할당할 수 있으므로 `synchronized_pool_resource` 같은 thread-safe resource를 사용하거나 적절한 동기화를 보장한다. `do_allocate`는 올바른 크기·정렬의 저장 공간을 반환하거나 `bad_alloc`을 던져야 하며 `do_deallocate`는 예외를 던지지 않아야 한다. no-op deleter로 만든 공유 포인터도 대상은 대여하므로 자동 수명이 필요하면 실제 소유권을 전달한다.
