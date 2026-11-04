# lz4 모듈 API

[English](../../en/modules/lz4.md) · [모듈 목록](index.md) · [I/O 조립](../composition.md)

`lz4_codec final : compression_codec`는 LZ4 프레임을 동기 압축·해제한다. `RAFTPP_MODULE_LZ4=ON`, `raftpp::lz4`, `<raftpp/lz4/lz4.hpp>`를 사용한다. core와 PkgConfig의 `liblz4 >= 1.7.1`에 의존한다. UV는 필요하지 않다. SDK 준비·검색은 [의존성](../dependencies.md)을 따른다.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS lz4)
target_link_libraries(app PRIVATE raftpp::lz4)
```

## 공개 API

아래 선언은 `raftpp` 네임스페이스에 속한다. 객체는 기본 생성할 수 있고 별도 설정이나 비동기 요청 상태가 없다.

```cpp
class lz4_codec final : public compression_codec {
public:
    error_code compress(view<const byte> input, buffer& output) const noexcept override;
    error_code decompress(view<const byte> input, buffer& output,
                          size_t max_size) const noexcept override;
};
```

| 인자·반환 | 계약 |
| --- | --- |
| `input` | 호출 동안 유효한 읽기 전용 바이트 뷰. 함수는 input을 수정하지 않는다. |
| `output` | 성공 시 결과 전체로 교체하는 이동 전용 소유 버퍼. 기존 내용 뒤에 붙이지 않는다. |
| `max_size` | 해제한 바이트의 최대 크기. 인코딩된 입력 크기/SDK 내부 메모리 상한이 아니다. |
| 빈 `error_code` | 한 프레임/스트림 연산 완료. 콜백은 없다. |

native `LZ4F_compressFrame`에 `contentSize=input.size()`와 content checksum 활성 설정을 전달해 프레임을 생성한다. 빈 입력의 content size 필드 표현은 SDK 규칙을 따른다. 고정 사전이나 압축 수준을 인자로 받지 않는다.

## 소유권과 입력 경계

`compress`는 빈 원문도 유효한 빈 payload 프레임으로 만든다. `decompress`는 정확히 하나의 완전한 프레임/스트림을 받으며 잘린 데이터·잘못된 형식·끝의 추가 바이트·연결된 두 프레임을 거부한다. 인코딩된 입력 자체가 비어 있으면 `corrupt`다. 유효한 빈 payload 프레임은 `max_size=0`으로 해제할 수 있다.

성공과 실패 모두 output의 allocator를 유지한다. 실패하면 기존 output 내용과 크기도 유지한다. input이 output의 기존 메모리를 가리켜도 이 계약을 적용하며, 결과는 임시 버퍼에서 완성한 뒤 교체한다. 성공 후 기존 output을 가리키던 뷰는 다시 사용하지 않는다.

데이터 저장소는 output의 allocator를 사용하며 native SDK context/내부 할당 전체가 그 allocator를 사용하는 것은 아니다. 각 호출은 독립 native 상태를 사용한다. 동시 호출 시 입력/출력과 allocator 자원의 동시 접근 안전성은 호출자가 보장한다.

## 오류

| 오류 | 의미 |
| --- | --- |
| `errc::corrupt` | 해제 입력이 잘못되었거나 잘림·추가 바이트 등 단일 프레임 계약을 위반한다. |
| `errc::too_big` | 복원 크기가 `max_size`를 초과하거나 native/버퍼 길이 범위를 벗어난다. |
| `errc::out_of_memory` | 작업 버퍼 할당 또는 native context 생성 등 명시적으로 보고한 할당 실패. |
| `errc::io_error` | native 압축/초기화 단계의 기타 실패. |

서명에 포함된 `noexcept` 연산은 오류 코드로 결과를 전달한다. 프레임의 크기 필드를 신뢰해 제한 없이 결과를 할당하지 않으며 실제 복원 바이트에도 `max_size`를 적용한다. 반환 코드가 우선이며 `error_details()`는 이 코덱 API에 없다.

## 사용 예제

```cpp
#include <raftpp/lz4/lz4.hpp>

raftpp::error_code round_trip(raftpp::view<const raftpp::byte> input,
                             raftpp::buffer& decoded)
{
    raftpp::lz4_codec codec;
    raftpp::buffer encoded;
    if (auto error = codec.compress(input, encoded))
        return error;
    return codec.decompress(encoded.bytes(), decoded, 8 * 1024 * 1024);
}
```

입력은 호출 동안 유지한다. 예제의 해제 한도는 8 MiB이며 더 큰 payload는 `too_big`다. 반환된 output의 수명은 codec 객체와 독립이다.

이 옵션은 `RAFTPP_WITH_LZ4`와 독립이다. 후자는 UV snapshot 디코더를 활성화하며 UV가 필요하다. `lz4_codec`은 UV 없이 사용하며, UV의 free 함수 `decompress`와 달리 뒤따르는 프레임/추가 바이트를 거부하고 출력 allocator를 보존한다. [UV 압축 계약](uv.md)을 참고한다.

모듈 활성화만으로 형식이나 snapshot 압축을 변경하지 않는다. 코덱 객체를 `io_components::snapshot_codec`에 명시적으로 지정하면 [I/O 조립](../composition.md)의 snapshot envelope로 저장/복원한다. 공통 소유권과 추상 인터페이스는 [스트림·실행기·코덱](../streams.md), [버퍼](../storage.md)를 따른다.
