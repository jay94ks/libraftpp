# 메시지 인코딩과 체크섬

[English](../en/encoding.md) · [노드와 I/O](node.md) · [버퍼와 엔트리](storage.md)

`<raftpp.hpp>`를 포함하고 `raftpp::raftpp`에 연결한다. 모든 이름은 `raftpp` 네임스페이스에 있다. 아래 API는 libuv 타입을 사용하지 않으며 UV·LZ4·fixture 옵션과 무관하게 제공된다. 네트워크 형식과 디스크 형식의 숫자 필드는 little endian이다.

## 크기 제한과 오류

```cpp
struct encoding_limits {
    size_t max_header_size = 8 * 1024 * 1024;
    size_t max_payload_size = 8 * 1024 * 1024;
    size_t max_entries = 262144;
};
```

크기는 바이트 단위다. `max_header_size`는 RPC preamble을 제외한 헤더 또는 배치 헤더의 한도다. `max_payload_size`는 payload 전체의 한도이며, `max_entries`는 배치 엔트리 개수 한도다. 각 인코딩·헤더 디코딩 함수의 마지막 인자에 전달한다. `decode_message_payload`는 이미 해석한 헤더의 길이와 전달한 payload를 대조한다.

설정한 한도는 해당 크기를 포함한다. 한도를 크게 지정해도 형식·플랫폼의 표현 범위는 늘어나지 않는다. 배치 엔트리 개수는 `unsigned`로, 개별 엔트리 길이는 `uint32_t`로 표현할 수 있어야 하며 합계와 RPC 헤더·스냅샷 길이는 `size_t` 범위에 들어가야 한다. 넘치는 값은 자릿수를 잘라 해석하지 않고 `too_big`으로 거부한다. 디스크 레코드의 262144개 상한은 별도로 적용된다.

헤더 함수의 성공은 길이 선언을 해석했다는 뜻이며 payload 바이트가 존재하거나 payload 전체를 할당했다는 뜻은 아니다. 한도를 높여 매우 큰 선언을 허용해도 단계별 수신자는 필요한 바이트를 모아야 한다. `decode_message`는 길이·한도 검사를 통과해도 입력에 헤더·payload가 부족하면 `malformed`를 반환하고 출력과 `consumed`를 보존한다.

인코딩·디코딩 함수는 `noexcept`이며 `error_code`를 반환한다. 실패하면 모든 출력 인자와 `consumed`를 보존한다. 할당 실패는 `out_of_memory`, 표현할 수 없는 크기와 설정한 한도 초과는 `too_big`다. 잘린 RPC와 잘못된 엔트리 타입·길이는 `malformed`, 알 수 없는 RPC 코드는 `io_error`다. 레코드의 잘린 입력은 `io_error`, CRC 불일치·0개 엔트리·형식의 최대 개수 초과는 `corrupt`다. 디스크 레코드에서 정렬되지 않은 길이도 `corrupt`다. 디스크에서 CRC가 맞지만 엔트리 타입이 잘못된 경우 `malformed`다.

## RPC 송신

```cpp
struct encoded_message {
    shared_buffer header;
    vector<shared_buffer> payload;
};
error_code encode_message(const message& input, encoded_message& output,
                          encoding_limits limits = {}) noexcept;
```

`header`는 16바이트 preamble과 RPC 헤더를 소유한다. preamble의 바이트 0은 RPC 코드, 바이트 2는 version, 바이트 8~15는 RPC 헤더 길이다. 나머지 preamble 바이트는 0으로 기록한다. RPC 코드는 [메시지 타입](node.md)의 variant 순서에 1을 더한 값이다. 인코더는 기존의 전체 헤더 레이아웃을 기록하며, preamble의 version은 입력의 version을 사용한다.

`payload`를 순서대로 이어서 전송한다. AppendEntries에는 엔트리별 버퍼, InstallSnapshot에는 스냅샷 데이터 버퍼 하나가 있다. 각 버퍼 사이에는 패딩을 추가하지 않는다. 다른 메시지는 payload가 없다. 길이 0인 엔트리의 버퍼도 벡터에 포함될 수 있다.

헤더 바이트는 새로 소유하고, payload 바이트는 입력과 공동 소유한다. 원본 메시지가 파괴되어도 결과는 유효하다. 결과를 전송 완료까지 보관하며 공유 바이트를 다른 별칭으로 변경하지 않는다. null 엔트리 핸들은 `malformed`다. 스냅샷의 `needs_data`가 `true`이면 `invalid`이며, 먼저 안정된 스냅샷 데이터를 채워야 한다.

## RPC 수신

```cpp
struct message_preamble {
    uint8_t type = 0, version = 0;
    size_t header_size = 0;
};
struct message_header {
    message_payload payload;
    vector<entry_header> entries;
    size_t payload_size = 0;
};
error_code decode_preamble(view<const byte> input, message_preamble& output,
                           encoding_limits limits = {}) noexcept;
error_code decode_message_header(const message_preamble& preamble,
                                 view<const byte> input, message_header& output,
                                 encoding_limits limits = {}) noexcept;
error_code decode_message_payload(const message_header& header,
                                  shared_buffer payload, message& output) noexcept;
error_code decode_message(shared_buffer input, message& output, size_t& consumed,
                          encoding_limits limits = {}) noexcept;
```

단계별 수신은 다음 순서를 따른다.

1. 적어도 16바이트를 모아 `decode_preamble`로 해석한다. preamble의 예약 바이트는 무시한다. 0바이트 헤더는 허용하지 않는다.
2. 정확히 `header_size`바이트를 모아 `decode_message_header`에 전달한다. preamble 자체는 포함하지 않는다. 반환한 `entries`는 AppendEntries 엔트리의 메타데이터이며, `payload` 안의 AppendEntries 엔트리 벡터는 아직 비어 있다.
3. 정확히 `payload_size`바이트를 공동 소유하는 버퍼로 모아 `decode_message_payload`에 전달한다. 엔트리와 스냅샷 데이터는 이 버퍼의 부분 범위로 연결한다.

`decode_message`는 preamble부터 payload까지 모인 입력에서 메시지 하나를 해석하는 편의 함수다. 성공 시 `consumed`는 그 메시지의 전체 바이트 수다. 입력 끝의 다음 메시지는 소비하지 않는다. 짧은 입력은 실패이므로 단계별 API를 사용하는 backend가 필요한 바이트를 먼저 모아야 한다.

직접 만든 `message_header`를 payload 함수에 전달할 때도 일관성이 필요하다. AppendEntries의 `entries` 길이 합계는 `payload_size` 및 실제 버퍼 길이와 같아야 하며, `payload` 안의 엔트리 벡터는 비어 있어야 한다. 다른 RPC는 `entries` 메타데이터를 갖지 않는다. InstallSnapshot의 `needs_data`는 `false`여야 하며 그 외 비데이터 RPC의 payload는 비어 있어야 한다. 불일치는 `malformed`, 엔트리 길이의 표현 범위 초과는 `too_big`이며 기존 출력은 보존한다. 길이 0의 엔트리는 다음 엔트리의 바이트 위치를 진행시키지 않는다.

디코딩한 엔트리·스냅샷은 전체 입력 할당을 공동 소유한다. 원래 버퍼나 헤더 객체가 파괴되어도 보관한 엔트리는 유효하다. 결과의 `server_id`는 0, `server_address`는 빈 문자열이다. 이 두 값은 wire 데이터에 포함되지 않으므로 backend가 연결 정보에 따라 송신자 ID와 주소를 지정한다.

version 0의 길이에 따른 기존 버전 판별, 이전 투표·복제 응답의 생략된 기능 필드, 예약된 후행 바이트를 지원한다. 32바이트의 이전 RequestVote는 flags 없이 해석한다. 버전이 없는 투표 요청·응답은 헤더 길이에 따라 version 1 또는 2로 판별한다. 기능 필드가 없는 응답의 `features`·`capacity`와 이전 투표의 `pre_vote`는 0이다. TimeoutNow의 디코딩 version은 0이다.

## 배치와 디스크 레코드

```cpp
struct entry_header {
    raftpp::term term = 0;
    entry_type type = entry_type::command;
    size_t size = 0;
};
error_code encode_batch_header(view<const entry_header> entries, buffer& output,
                               encoding_limits limits = {}) noexcept;
error_code decode_batch_header(view<const byte> input, vector<entry_header>& output,
                               size_t& consumed, encoding_limits limits = {}) noexcept;
error_code encode_record(view<const entry> entries, buffer& output,
                         encoding_limits limits = {}) noexcept;
error_code decode_record(shared_buffer input, vector<entry>& output,
                         size_t& consumed, encoding_limits limits = {}) noexcept;
```

배치 헤더는 8바이트 엔트리 개수와 엔트리별 16바이트 헤더다. 엔트리 헤더는 term 8바이트, 타입 1바이트, 예약 3바이트, payload 길이 4바이트다. 길이는 `uint32_t`로 표현 가능해야 한다. 타입은 command·barrier·change이며, 배치 헤더 자체는 엔트리 0개도 허용한다. `decode_batch_header`는 앞의 헤더 하나만 소비하며 실제 payload를 읽지 않는다.

디스크 레코드는 헤더 CRC32 4바이트, 데이터 CRC32 4바이트, 배치 헤더, 엔트리 데이터 순서다. 각 CRC의 초기값은 0이다. 레코드에는 적어도 하나, 최대 262144개의 엔트리가 필요하다. 디스크 payload 길이는 각각 8바이트 배수여야 한다. 빈 배치 또는 정렬되지 않은 payload의 인코딩은 `invalid`, 엔트리 개수 상한 초과는 `too_big`를 반환한다. 디코딩에서 개수 0 또는 262144 초과는 `corrupt`다. `encoding_limits`가 허용하는 범위에서도 전체 헤더·payload가 필요하며, 잘린 입력은 `io_error`다. 길이를 늘리거나 명령 바이트를 바꾸는 자동 패딩은 하지 않는다. 네트워크 인코더에는 이 정렬 제한이 없다.

`disk_format_version`은 1이다. 레코드 함수는 segment 시작의 8바이트 버전 워드, I/O 블록 정렬 패딩, 파일 크기 정책을 포함하지 않는다. 상위 저장소가 이를 관리한다. `decode_record`는 CRC를 검증한 레코드 하나만 소비한다. 결과의 엔트리는 입력 저장 공간을 공동 소유한다.

## 영속 메타데이터 형식

```cpp
struct persistent_metadata {
    uint64_t version = 0;
    raftpp::term term = 0;
    server_id voted_for = 0;
};
error_code encode_metadata(const persistent_metadata& input, buffer& output) noexcept;
error_code decode_metadata(view<const byte> input, persistent_metadata& output) noexcept;
error_code encode_snapshot_metadata(const snapshot& input, buffer& output) noexcept;
error_code decode_snapshot_metadata(view<const byte> input, snapshot& output) noexcept;
```

`persistent_metadata`의 `version`은 슬롯 갱신 세대이며 디스크 형식 버전과 다르다. 세대는 1 이상이다. 인코딩은 형식 버전 1, 세대, 현재 term, 투표한 서버 ID를 각각 8바이트 little endian으로 기록한 32바이트다. 세대 0은 `corrupt`, 길이 부족은 `io_error`, 초과 길이는 `corrupt`, 형식 버전 불일치는 `malformed`다. 인코더도 세대 0을 `corrupt`로 거부한다. 새 저장소의 세대 0은 파일이 없는 상태를 표현하며 파일로 기록하지 않는다.

스냅샷 메타데이터는 형식 버전 8바이트, CRC 슬롯 8바이트, 설정 로그 인덱스 8바이트, 설정 길이 8바이트와 기존 설정 인코딩으로 구성된다. CRC32는 설정 로그 인덱스·길이와 설정 바이트에 대해 계산하고, CRC 슬롯의 하위 32비트를 사용한다. 설정 길이는 1 이상, 최대 1 MiB다. 인코딩 한도 초과는 `too_big`, 디코딩의 0 또는 과도한 설정 길이·CRC 불일치는 `corrupt`다. 잘린 입력은 `io_error`다. 설정 자체의 오류는 설정 디코더의 오류 코드로 반환한다. 설정 뒤의 추가 바이트는 기존 파일 판독 방식처럼 무시한다.

`encode_snapshot_metadata`는 입력의 `configuration`과 `configuration_index`만 사용한다. 스냅샷의 term·index는 파일명에 저장하며, 데이터 버퍼는 별도 파일에 둔다. `decode_snapshot_metadata`도 출력의 `configuration`·`configuration_index`만 변경하고 term·index·데이터 버퍼는 보존한다. 모든 함수는 실패 시 출력 인자를 보존한다.

## CRC32, SHA-1과 digest

```cpp
uint32_t crc32(view<const byte> data, uint32_t initial = 0) noexcept;

class sha1 {
public:
    sha1() noexcept = default;
    void reset() noexcept;
    void update(view<const byte> data) noexcept;
    array<byte, 20> digest() const noexcept;
};
uint64_t digest(string_view text, uint64_t n) noexcept;
```

`crc32`는 다항식 `0x04c11db7`, 비반사 방식이며 최종 XOR을 하지 않는다. 빈 입력은 `initial`을 그대로 반환한다. 이전 결과를 다음 호출의 `initial`로 전달해 연속 버퍼를 검사할 수 있다.

`sha1`은 동적 할당 없이 증분 SHA-1을 계산하는 값 타입이다. `update`는 입력을 보관하지 않는다. `digest()`는 20바이트 결과를 반환하며 기존 상태를 변경하지 않으므로 반복 조회하거나 이어서 업데이트할 수 있다. `reset`은 새 빈 상태로 되돌린다.

자유 함수 `digest(text, n)`는 text 바이트와 little-endian 8바이트 정수 n을 순서대로 SHA-1에 넣고 결과의 마지막 8바이트를 little endian으로 해석한다. 일반 C 문자열 입력에서는 기존 `raft_digest`와 같은 값이다. `string_view`의 지정 길이를 전부 사용하므로 내장 NUL도 해시에 포함한다.

```cpp
#include <raftpp.hpp>

raftpp::uint64_t server_digest()
{
    return raftpp::digest("node-1", 42);
}

raftpp::error_code make_heartbeat(raftpp::encoded_message& output)
{
    raftpp::message message;
    raftpp::append_entries heartbeat;
    heartbeat.term = 3;
    heartbeat.prev_log_index = 10;
    heartbeat.prev_log_term = 2;
    heartbeat.leader_commit = 10;
    message.payload = heartbeat;
    return raftpp::encode_message(message, output);
}
```
