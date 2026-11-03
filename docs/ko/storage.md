# 버퍼, 엔트리와 스냅샷

[English](../en/storage.md) · [노드, I/O와 상태 머신](node.md) · [메모리 Resource](memory.md)

`<raftpp.hpp>`를 포함하고 CMake 타깃 `raftpp::raftpp`에 연결한다. 아래 API는 C++20을 사용하며 `RAFTPP_WITH_UV`, `RAFTPP_WITH_LZ4`, `RAFTPP_WITH_FIXTURE` 값과 무관하게 사용할 수 있다. 이름은 모두 `raftpp` 네임스페이스에 있다.

## 버퍼 소유권

| 타입 | 저장 공간과 복사 | 접근 |
| --- | --- | --- |
| `buffer` | `vector<byte, resource_allocator<byte>>` 기반 단독 소유. 복사 불가, 이동 가능. | 수정 가능한 바이트와 읽기 전용 바이트 뷰. |
| `aligned_buffer` | 지정 정렬의 단독 소유. 복사 불가, 이동 가능. | 수정 가능한 바이트와 읽기 전용 바이트 뷰. |
| `shared_buffer` | 표준 `shared_ptr`로 전체 할당을 공동 소유. 복사는 바이트를 복제하지 않는다. | 읽기 전용 범위와 할당을 공유하는 부분 범위. |

`buffer(size_t size, resource_allocator<byte> = {})`와 `aligned_buffer(size_t size, size_t alignment, resource_allocator<byte> = {})`는 바이트를 0으로 초기화한다. 두 타입은 할당 resource를 보관하고 `get_allocator()`를 제공한다. 기본 allocator는 라이브러리 선택을 보관하며 명시적 allocator로 resource를 직접 고를 수 있다. 선택·이동/swap 전파·수명은 [메모리 Resource](memory.md)를 참고한다. 정렬은 2의 거듭제곱이며 `alignof(max_align_t)` 이상이어야 한다. 유효하지 않으면 `invalid_argument`, 할당 실패 시 `bad_alloc`을 던진다. 일반 `buffer`의 생성·복사·크기 변경은 표준 컨테이너의 `bad_alloc` 또는 `length_error`를 던질 수 있다.

`buffer`는 `size()`, `empty()`, `data()`, `bytes()`, `resize(size_t)`, `clear()`, `swap(buffer&)`를 제공한다. `buffer::copy(view<const byte>)`는 독립적인 바이트 복사본을 만든다. 크기 변경과 소유 객체의 파괴는 기존 포인터·뷰를 무효화할 수 있다.

`aligned_buffer`는 `size()`, `empty()`, `alignment()`, `data()`, `bytes()`, `swap(aligned_buffer&)`를 제공한다. 크기는 생성 시 정하며 이동한 원본의 `size()`는 0이다. 이동·교환 시 정렬에 맞는 해제 방식도 함께 이전한다.

```cpp
explicit shared_buffer(buffer storage);
explicit shared_buffer(aligned_buffer storage);
static shared_buffer copy(view<const byte> input, resource_allocator<byte> allocator = {});
shared_buffer slice(size_t offset, size_t count = std::dynamic_extent) const;
```

소유 버퍼를 인자로 넘길 때 `std::move`를 사용한다. 생성자는 저장 공간의 소유권을 인수하며 공유 제어 블록 할당 시 `bad_alloc`을 던질 수 있다. 생성 실패 시에도 이동한 인자의 저장 공간은 자동 정리된다. `copy`는 별도 저장 공간을 할당하며 `bad_alloc` 또는 `length_error`를 던질 수 있다.

`slice`의 `offset`은 현재 범위의 시작으로부터의 바이트 수이며 `count`를 생략하면 나머지 전체를 선택한다. `offset > size()` 또는 `count > size() - offset`이면 `out_of_range`를 던진다. 빈 부분 범위도 허용한다. 부분 범위는 전체 할당의 수명을 연장하며 별도 바이트 할당을 하지 않는다.

`shared_buffer::bytes()`는 `view<const byte>`를 반환한다. 이 뷰 자체는 수명을 연장하지 않으므로, 사용하는 동안 원본이나 복사본의 소유권을 유지해야 한다. `storage()`는 `const shared<const byte>&`를 반환하며, 이를 값으로 복사하면 저장 공간을 유지한다. 반환 포인터는 부분 범위의 시작을 가리킬 수 있으므로 직접 해제해서는 안 된다. `shares_storage_with(const shared_buffer&)`는 두 범위가 같은 할당을 소유하는지 검사한다. 기본 생성한 두 빈 객체는 소유 할당이 없으므로 `false`다.

`shared_buffer`의 이동은 원본을 빈 상태로 만든다. 공유 소유권은 바이트에 대한 동시 접근을 동기화하지 않는다. 이동 전에 얻은 수정 가능한 별칭을 통해 공유 바이트를 변경해서는 안 된다.

```cpp
#include <raftpp.hpp>

raftpp::shared_buffer make_payload()
{
    raftpp::aligned_buffer storage{4096, 4096};
    storage.bytes()[0] = raftpp::byte{42};
    raftpp::shared_buffer batch{std::move(storage)};
    return batch.slice(0, 1); // 반환 범위가 전체 정렬 할당을 소유한다.
}
```

## 엔트리

```cpp
enum class entry_type : uint8_t { command = 1, barrier, change };

struct entry {
    raftpp::term term = 0;
    entry_type type = entry_type::command;
    shared_buffer payload;
};

error_code copy_entry(const entry& input, entry& output) noexcept;
error_code copy_entries(view<const entry> input, vector<entry>& output) noexcept;
```

일반 `entry` 값 복사는 메타데이터를 복사하고 payload의 소유권을 공유한다. `copy_entry`는 바이트까지 깊이 복사한다. `copy_entries`는 모든 바이트를 패딩 없이 입력 순서로 한 할당에 모으고, 각 출력 엔트리에 해당 부분 범위의 공유 소유권을 부여한다. 출력 엔트리 하나를 유지하면 전체 배치를 유지한다. 빈 입력은 출력 벡터를 비운다.

두 함수는 성공 시 빈 `error_code`, 할당 실패 시 `errc::out_of_memory`, 표현할 수 없는 크기나 컨테이너 길이 초과 시 `errc::too_big`을 반환한다. 오류 시 출력은 변경하지 않는다. 입력과 출력은 같은 객체이거나, 입력 뷰가 출력 벡터를 참조해도 된다. 이 복사 함수들은 엔트리의 term·type에 대한 프로토콜 유효성 검사를 수행하지 않는다.

## 스냅샷

```cpp
struct snapshot {
    log_index index = 0;
    raftpp::term term = 0;
    raftpp::configuration configuration;
    log_index configuration_index = 0;
    vector<shared_buffer> buffers;
};

error_code copy_snapshot(const snapshot& input, snapshot& output) noexcept;
```

`index`와 `term`은 포함된 마지막 엔트리를 나타내며, `configuration_index`는 설정 엔트리의 인덱스다. 일반 값 복사는 설정을 값으로 복사하고 각 payload의 소유권을 공유한다. `copy_snapshot`은 설정과 바이트를 깊이 복사하고 모든 입력 버퍼를 순서대로 한 출력 버퍼에 모은다. 빈 입력도 길이가 0인 출력 버퍼 하나를 만든다. 이 함수는 인덱스·term의 프로토콜 유효성을 검사하지 않는다.

성공 시 빈 `error_code`, 할당 실패 시 `errc::out_of_memory`, 크기·길이 한도 초과 시 `errc::too_big`을 반환한다. 오류 시 출력은 변경하지 않으며 입력과 출력은 같은 객체여도 된다. 데이터의 수명은 값과 공유 소유권으로 관리하므로 별도의 close나 free 호출은 필요하지 않다.
