# 모듈별 API

[English](../../en/modules/index.md) · [I/O 조립](../composition.md)

모든 타입은 `raftpp` 네임스페이스에 있다. core는 필수이며 선택 모듈은 core에 의존한다. 모듈 타깃을 연결하면 C++20, 공개 헤더와 필요한 링크 조건을 전달받는다.

| 모듈 | CMake 타깃 | 활성화 | 공개 진입 헤더 | 역할 |
| --- | --- | --- | --- | --- |
| [core](core.md) | `raftpp::core` | 항상 활성 | `<raftpp/raft.hpp>` | 합의, 노드, 값 타입, 추상 I/O·스트림·실행기·압축, RPC framing |
| [uv](uv.md) | `raftpp::uv` | `RAFTPP_WITH_UV` | `<raftpp/uv/uv.hpp>` | Linux 영속 I/O·타이머, TCP·pipe·TTY 스트림과 transport |
| [asyncpp](asyncpp.md) | `raftpp::asyncpp` | `RAFTPP_MODULE_ASYNCPP` | `<raftpp/asyncpp/asyncpp.hpp>` | dispatcher 기반 콜백 실행기 |
| [asio](asio.md) | `raftpp::asio` | `RAFTPP_MODULE_ASIO` | `<raftpp/asio/asio.hpp>` | standalone Asio 실행기·타이머와 TCP 스트림 |
| [mvfst](mvfst.md) | `raftpp::mvfst` | `RAFTPP_MODULE_MVFST` | `<raftpp/mvfst/mvfst.hpp>` | 연결된 mvfst 양방향 QUIC 스트림 |
| [msquic](msquic.md) | `raftpp::msquic` | `RAFTPP_MODULE_MSQUIC` | `<raftpp/msquic/msquic.hpp>` | MsQuic API 소유권과 연결된 QUIC 스트림 |
| [lz4](lz4.md) | `raftpp::lz4` | `RAFTPP_MODULE_LZ4` | `<raftpp/lz4/lz4.hpp>` | LZ4 프레임 코덱 |
| [zstd](zstd.md) | `raftpp::zstd` | `RAFTPP_MODULE_ZSTD` | `<raftpp/zstd/zstd.hpp>` | Zstandard 프레임 코덱 |
| [zlib](zlib.md) | `raftpp::zlib` | `RAFTPP_MODULE_ZLIB` | `<raftpp/zlib/zlib.hpp>` | zlib 형식의 DEFLATE 코덱 |
| [RocksDB](rocksdb.md) | `raftpp::rocksdb` | `RAFTPP_MODULE_ROCKSDB` | `<raftpp/rocksdb/rocksdb.hpp>` | `rocksdb_storage : storage` |
| [SQLite](sqlite.md) | `raftpp::sqlite` | `RAFTPP_MODULE_SQLITE` | `<raftpp/sqlite/sqlite.hpp>` | `sqlite_storage : storage` |

## 선택과 설치 소비

일곱 `RAFTPP_MODULE_<NAME>` 옵션의 기본값은 OFF다. UV의 최초 기본값은 libuv 감지 결과에 따른다. 옵션은 라이브러리를 빌드할 때 선택한다. 비활성 모듈의 헤더와 타깃은 제공하지 않으며 소비자에서 기능 매크로를 재정의해 활성화할 수 없다.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS asio zstd)
target_link_libraries(app PRIVATE raftpp::asio raftpp::zstd)
```

위 `app`은 소비자가 생성한 타깃이다. `COMPONENTS`는 설치된 활성 모듈 중 필요한 의존성만 찾으며 core는 생략해도 함께 제공한다. 컴포넌트를 생략하면 설치 시 활성화된 모든 모듈의 SDK를 찾는다. 부분 선택한 소비자는 각 모듈 헤더를 사용한다. `raftpp::raftpp`와 `<raftpp.hpp>`는 설치된 전체 활성 모듈을 모으므로 그 SDK 전체가 필요하다.

생성 `<raftpp/config.hpp>`의 `RAFTPP_MODULE_<NAME>` 및 `raftpp::features::modules::<name>`은 라이브러리 빌드 당시의 설정이다. UV·fixture·backtrace는 `raftpp::features`에서 조회한다. 소스 빌드, SDK 준비와 설치 경로는 [CMake](../cmake.md), [선택 어댑터와 의존성](../dependencies.md)을 따른다. 별도 경로의 공유 SDK는 실행 시 동적 로더에서도 찾을 수 있어야 한다.

## 조립과 실행 계약

`asio_stream`, `mvfst_stream`, `msquic_stream`, `uv_stream`은 core의 `byte_stream`을 구현한다. 이미 연결된 peer 스트림을 `framed_transport`에 등록해 Raft `transport`로 사용할 수 있다. UV의 `stream_transport`는 `uv_stream_transport` connector를 받아 연결·재시도를 관리하는 별도 API다. 두 타입의 입력과 수명 계약은 다르다.

하나의 노드·transport에 연결하는 스트림과 I/O 콜백은 같은 직렬 실행 영역을 사용해야 한다. 어댑터는 이벤트 루프 사이의 자동 전달이나 노드 동기화를 제공하지 않는다. QUIC 연결·TLS·peer 인증·재접속은 애플리케이션 connector가 담당한다. 디스크/타이머 `io`는 UV가 제공한다. 실행기는 콜백 제출 API이며 코루틴·future 기반 Raft API를 추가하지 않는다.

쓰기 성공은 각 SDK의 로컬 수락/완료이며 Raft commit이나 원격 애플리케이션 ACK가 아니다. 즉시 제출 실패 시 완료 콜백은 호출하지 않는다. 종료 후에도 수락된 작업과 native 정리가 끝날 때까지 실행 영역을 계속 구동한다. 세부 차이는 각 모듈 문서와 [공통 스트림 계약](../streams.md)을 따른다.

`make_io(io_components)`로 저장소·타이머·network와 선택 snapshot codec을 조립해 노드에 전달할 수 있다. UV는 `uv_storage`·`uv_timer`, Asio는 `asio_timer`를 제공한다. 구체 조합은 [I/O 조립](../composition.md)을 따른다.
