# 클러스터 설정 값

[English](../en/configuration.md)

`<raftpp/configuration.hpp>`의 `configuration`은 순서가 있는 서버 목록과 기존 Raft 바이트 형식을 제공하는 동기 값 타입이다. UV·LZ4·fixture 없이 사용할 수 있다. 노드의 멤버십 변경은 [node](node.md)의 `add`, `assign`, `remove` 요청으로 수행한다.

## 값과 소유권

```cpp
struct server {
    server_id id;
    string address;
    raftpp::role role;
};
```

역할은 `role::voter`, `role::standby`, `role::spare` 중 하나다. 설정은 서버와 주소를 소유하며 기본 생성 시 비어 있다. 복사는 서버·주소를 독립적인 값으로 복사하고, 이동과 소멸은 표준 컨테이너의 의미를 따른다. 복사 생성·대입은 표준 할당 예외를 던질 수 있다. 명시적인 메서드는 아래의 `error_code` 또는 조회 결과를 반환하며 `noexcept`다.

서버·주소 저장소는 일반 `std::vector`·`std::string`의 allocator 의미를 따른다. 라이브러리 선택 resource의 적용 범위는 [메모리 할당](memory.md)을 참고한다.

`servers()`와 `get()`은 비소유 관찰이다. 설정을 보관해야 대상이 생존하며, 복사한 뷰나 포인터는 수명을 연장하지 않는다. 성공한 추가·삭제·디코딩과 대입 뒤에는 포인터·뷰·인덱스를 다시 조회한다. 설정이 소멸하면 기존 포인터·뷰를 사용하지 않는다.

## 변경

| 메서드 | 동작과 오류 |
| --- | --- |
| `error_code add(server_id id, string_view address, role role) noexcept` | 끝에 서버를 추가한다. ID 0은 `bad_id`, 잘못된 역할은 `bad_role`, 주소 내부 NUL은 `invalid`다. 빈 주소는 허용한다. 중복 ID·주소는 각각 `duplicate_id`·`duplicate_address`다. |
| `error_code remove(server_id id) noexcept` | 해당 서버를 삭제하고 나머지 순서를 유지한다. 없는 ID는 `bad_id`다. 마지막 서버를 지워 빈 값으로 만들 수 있다. 할당하지 않는다. |

`add`는 ID·역할·NUL을 먼저 검사한 뒤 저장 순서대로 중복을 검사한다. 주소를 복사하므로 호출 뒤 입력 문자열을 유지할 필요가 없다. 할당 실패는 `out_of_memory`, 표현할 수 없는 문자열·컨테이너 길이는 `too_big`다. 오류 시 이전 설정은 유지한다. 성공은 빈 `error_code`다.

## 조회

| 메서드 | 반환값 |
| --- | --- |
| `size_t size() const noexcept`, `bool empty() const noexcept` | 서버 수와 비어 있는지 여부. |
| `view<const server> servers() const noexcept` | 저장 순서의 읽기 전용 연속 뷰. |
| `borrowed<const server> get(server_id id) const noexcept` | 서버 포인터 또는 `nullptr`. |
| `size_t voter_count() const noexcept` | voter 수. |
| `size_t index_of(server_id id) const noexcept` | 전체 목록의 인덱스. 없으면 `size()`. |
| `size_t index_of_voter(server_id id) const noexcept` | voter만 센 인덱스. 없거나 voter가 아니면 `size()`. |
| `size_t actual_index_of_voter(size_t index) const noexcept` | voter 인덱스에 대응하는 전체 목록의 인덱스. 범위 밖이면 `size()`. |

## 인코딩과 디코딩

```cpp
size_t encoded_size() const noexcept;
error_code encode(view<byte> output) const noexcept;
error_code encode(buffer& output) const noexcept;
error_code decode(view<const byte> input) noexcept;
```

형식은 값이 1인 버전 바이트, little-endian `uint64_t` 서버 수, 각 서버의 little-endian ID·NUL 종료 주소·1바이트 역할 순서다. 인코딩은 전체 크기를 8바이트 배수로 올리고 패딩을 0으로 채운다. `encoded_size()`는 필요한 크기이며 크기를 표현할 수 없으면 0이다. 빈 설정의 계산 크기는 16이지만 `encode`는 `invalid`를 반환한다.

뷰 overload는 최소 `encoded_size()`의 출력이 필요하며 부족하면 `too_big`다. 성공하면 논리 데이터 뒤부터 전달한 뷰의 끝까지 0으로 채운다. 정확한 길이의 뷰를 전달해 쓸 범위를 지정할 수 있다. 버퍼 overload는 새 소유 버퍼를 준비해 성공 시 출력과 교체한다. 할당 실패는 `out_of_memory`, 길이 초과는 `too_big`다. 두 overload 모두 오류 시 출력은 유지한다.

`decode`는 서버·주소를 복사해 성공 시 설정 전체를 교체한다. 잘린 헤더·ID·NUL 주소·역할, 지원하지 않는 버전, 잘못된 ID·역할, 중복 ID·주소는 `malformed`다. 할당 실패는 `out_of_memory`이며 오류 시 이전 설정과 입력은 유지한다. 입력의 8바이트 패딩은 필수가 아니며 선언한 서버 뒤의 추가 바이트는 해석하지 않는다. 서버 수가 0인 유효 헤더는 빈 설정으로 읽을 수 있다. 성공 뒤 입력 저장소를 해제해도 설정은 생존한다.

## 예제

```cpp
#include <raftpp/configuration.hpp>

int main()
{
    raftpp::configuration members;
    if (auto error = members.add(1, "127.0.0.1:9001", raftpp::role::voter))
        return error.value();
    if (auto error = members.add(2, "127.0.0.1:9002", raftpp::role::standby))
        return error.value();

    raftpp::buffer bytes;
    if (auto error = members.encode(bytes)) return error.value();
    raftpp::configuration decoded;
    if (auto error = decoded.decode(bytes.bytes())) return error.value();
    return decoded.voter_count() == 1 && decoded.index_of_voter(2) == decoded.size()
        ? 0 : 1;
}
```
