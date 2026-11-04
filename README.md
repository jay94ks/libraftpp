# raftpp

[English](README.en.md)

raftpp는 cowsql/raft를 전면 재작성한 C++20 Raft 라이브러리다. 합의 코어에 저장소·타이머·transport·압축 모듈을 조립해 사용하며, RAII와 표준 스마트 포인터로 소유권을 표현한다.

- 리더 선출, 로그 복제, 멤버십 변경, 리더십 이전과 스냅샷을 제공한다.
- 동기 반환값과 요청·완료 콜백 API를 사용한다.
- 필수 core와 선택 어댑터를 독립적인 CMake 타깃으로 제공한다.
- 표준 컨테이너·문자열·뷰·오류 타입을 사용하고 C++20을 전이적으로 설정한다.

## 빠른 시작

CMake 3.20 이상과 C++20 컴파일러가 필요하다. 다음은 선택 SDK 없이 core를 빌드하는 구성이다.

```sh
git clone https://github.com/jay94ks/libraftpp.git
cd libraftpp

cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_WITH_UV=OFF -DRAFTPP_WITH_LZ4=OFF \
  -DRAFTPP_WITH_FIXTURE=OFF
cmake --build build/core --config Release --parallel
```

`raftpp::core`는 항상 활성화된다. `RAFTPP_MODULE_<NAME>` 옵션은 기본 OFF이며, 기본 구성에서 `RAFTPP_WITH_UV`는 설치된 libuv 감지 결과를 따른다. 라이브러리를 빌드할 때 필요한 기능을 선택한다.

## 모듈

| 모듈 | 활성화 옵션 | 역할 |
| --- | --- | --- |
| [core](docs/ko/modules/core.md) | 항상 활성 | 합의·노드·값 타입·추상 I/O·RPC framing |
| [uv](docs/ko/modules/uv.md) | `RAFTPP_WITH_UV` | Linux 영속 I/O·저장소·타이머, TCP·pipe·TTY |
| [asyncpp](docs/ko/modules/asyncpp.md) | `RAFTPP_MODULE_ASYNCPP` | 콜백 실행기 |
| [asio](docs/ko/modules/asio.md) | `RAFTPP_MODULE_ASIO` | 콜백 실행기·타이머·연결된 TCP 스트림 |
| [mvfst](docs/ko/modules/mvfst.md) | `RAFTPP_MODULE_MVFST` | 연결된 양방향 QUIC 스트림 |
| [msquic](docs/ko/modules/msquic.md) | `RAFTPP_MODULE_MSQUIC` | MsQuic API 소유권·연결된 QUIC 스트림 |
| [lz4](docs/ko/modules/lz4.md) | `RAFTPP_MODULE_LZ4` | LZ4 프레임 압축 |
| [zstd](docs/ko/modules/zstd.md) | `RAFTPP_MODULE_ZSTD` | Zstandard 프레임 압축 |
| [zlib](docs/ko/modules/zlib.md) | `RAFTPP_MODULE_ZLIB` | zlib 형식 DEFLATE 압축 |
| [rocksdb](docs/ko/modules/rocksdb.md) | `RAFTPP_MODULE_ROCKSDB` | RocksDB 영속 저장소 |
| [sqlite](docs/ko/modules/sqlite.md) | `RAFTPP_MODULE_SQLITE` | SQLite 영속 저장소 |

각 모듈은 `raftpp::<모듈명>` 타깃과 공개 헤더를 제공한다. `raftpp::raftpp`와 `<raftpp.hpp>`는 활성 모듈 전체를 모은다. 일부 컴포넌트만 소비하려면 `find_package(raftpp CONFIG REQUIRED COMPONENTS sqlite)`처럼 선택하고 해당 모듈 타깃·헤더를 사용한다.

`RAFTPP_MODULE_LZ4`는 독립 압축 코덱이다. UV의 기존 LZ4 스냅샷 읽기 기능을 선택하는 `RAFTPP_WITH_LZ4`와는 별도 옵션이다.

## 선택 SDK 준비

필요한 submodule만 초기화한다. SDK 준비와 raftpp 모듈 활성화는 별도 단계이며, 프로젝트 configure가 SDK를 자동 다운로드하거나 빌드하지 않는다. SQLite 예시는 다음과 같다.

```sh
git submodule update --init deps/sqlite deps/tcl
cmake -S deps -B build/deps -DRAFTPP_DEPENDENCIES=sqlite -DRAFTPP_DEPS_SHARED=OFF
cmake --build build/deps --parallel 2

cmake -S . -B build/sqlite -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_WITH_UV=OFF -DRAFTPP_WITH_LZ4=OFF \
  -DRAFTPP_MODULE_SQLITE=ON \
  -DRAFTPP_DEPENDENCY_PREFIX="$PWD/build/deps/prefix"
cmake --build build/sqlite --config Release --parallel
```

Tcl은 SQLite 소스 생성에 필요한 빌드 도구이며 런타임 의존성이 아니다. 이 준비 경로는 native POSIX 도구를 사용한다. 다른 환경이나 교차 컴파일에서는 설치된 SDK를 제공한다. 기존 SDK는 `CMAKE_PREFIX_PATH` 등으로 찾을 수도 있다. 다른 라이브러리와 QUIC 의존성은 [의존성 안내](docs/ko/dependencies.md)를 따른다.

## 저장소·타이머·네트워크 조립

사용자가 각 인터페이스의 구현체를 선택한다. 다음 함수는 SQLite 저장소에 타이머·transport와 선택 스냅샷 코덱을 연결하고, `node` 생성자의 backend에 전달할 `shared<io>`를 반환한다.

```cpp
#include <raftpp/composed_io.hpp>
#include <raftpp/sqlite/sqlite.hpp>

raftpp::shared<raftpp::io> make_backend(
    raftpp::fs::path database,
    raftpp::shared<raftpp::timer> timer,
    raftpp::shared<raftpp::transport> network,
    raftpp::shared<raftpp::compression_codec> codec = {})
{
    return raftpp::make_io({
        .storage = std::make_shared<raftpp::sqlite_storage>(std::move(database)),
        .timer = std::move(timer),
        .network = std::move(network),
        .snapshot_codec = std::move(codec),
    });
}
```

이 예제는 `raftpp::sqlite` 연결이 필요하다. DB 부모 디렉터리는 미리 생성하고, 유효한 timer와 network를 전달한다. 노드의 애플리케이션 상태는 사용자가 `state_machine`으로 구현한다.

모든 부품 호출과 노드/FSM·콜백은 같은 직렬 실행 영역에서 실행한다. 이벤트 루프는 종료 완료까지 유지하고 구동한다. RocksDB/SQLite의 DB 호출·커밋은 호출 스레드를 블로킹하며 완료는 인라인이다. 스트림 어댑터가 받는 연결과 QUIC의 TLS·인증은 애플리케이션 connector가 준비한다. 자세한 소유권·완료·종료 계약은 [I/O 조립](docs/ko/composition.md)과 [노드 API](docs/ko/node.md)를 참고한다.

## 설치와 CMake 소비

위 core 빌드를 설치할 수 있다.

```sh
cmake --install build/core --config Release --prefix "$PWD/build/install"
```

소비자 프로젝트의 `CMakeLists.txt`에서 다음처럼 연결한다. `main.cpp`는 소비자의 애플리케이션 코드다. 소비자 configure에 `-DCMAKE_PREFIX_PATH=/path/to/raftpp-prefix`를 지정한다.

```cmake
cmake_minimum_required(VERSION 3.20)
project(app LANGUAGES CXX)

find_package(raftpp CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE raftpp::raftpp)
```

선택 모듈을 빌드·설치한 경우 필요한 외부 SDK도 소비 환경에서 찾을 수 있어야 한다. 정적/공유 라이브러리는 `BUILD_SHARED_LIBS`로 선택한다. 설치·컴포넌트·기능 옵션의 상세 조건은 [CMake API](docs/ko/cmake.md)에 있다.

## 실행 예제와 테스트

Linux에 libuv 1.18.0 이상과 필요한 Linux 헤더가 있으면 단일 노드 카운터 예제를 실행할 수 있다. 아래 실행 파일 경로는 단일 구성 생성기 기준이다.

```sh
cmake -S . -B build/uv -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_WITH_UV=ON -DRAFTPP_WITH_LZ4=OFF \
  -DRAFTPP_BUILD_EXAMPLES=ON
cmake --build build/uv --config Release --parallel
./build/uv/examples/raftpp-example-composed ./build/uv/demo none uv
```

같은 명령을 다시 실행하면 저장된 스냅샷에서 카운터를 복원해 증가시킨다. 이 실행 예제의 저장소는 `uv_storage`다. 선택 SDK를 활성화하면 코덱·타이머 인자를 바꿀 수 있다. 서버·클러스터 예제의 사용법은 [예제 API](docs/ko/examples.md)를 참고한다.

앞서 구성한 core 빌드에 테스트를 추가하려면 다음을 사용한다. 어댑터 검증은 Release에서 수행한다.

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release -DRAFTPP_BUILD_TESTS=ON
cmake --build build/core --config Release --parallel
ctest --test-dir build/core -C Release --output-on-failure
```

개발 작업의 검증 대상 선택과 기존 결과 재사용 원칙은 [작업 지침](AGENTS.md)을 따른다.

## 문서와 소스 구조

- [모듈별 API](docs/ko/modules/index.md) · [I/O 조립](docs/ko/composition.md)
- [노드·상태 머신](docs/ko/node.md) · [빌드·설치](docs/ko/cmake.md)
- [선택 SDK와 의존성](docs/ko/dependencies.md) · [실행 예제](docs/ko/examples.md)
- [구현·검증 기록](REWRITTEN.md) · [기여 작업 지침](AGENTS.md)

공개 헤더 원본은 `modules/core/include/raftpp/`와 `modules/<name>/include/raftpp/<name>/`에 있다. 각 모듈은 `privates/`, `src/`, `CMakeLists.txt`를 갖는다. 루트 `include/`는 활성 공개 헤더의 생성 복사본이며 Git에서 제외된다. 헤더를 수정할 때는 모듈 원본을 사용한다. `docs/ko/`와 `docs/en/`은 한·영 API 설명서다.

## 라이선스

cowsql/raft의 직접적인 파생 저작물이며, `LGPL-3.0-only WITH LGPL-3.0-linking-exception`으로 제공한다. 상류 고지·링크 예외와 라이선스 원문은 [LICENSE.md](LICENSE.md)에 있다. `deps/`의 외부 라이브러리는 각 라이선스를 따른다.
