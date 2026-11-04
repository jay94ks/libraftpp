# 선택 어댑터 모듈

[English](../en/dependencies.md) · [모듈별 API](modules/index.md) · [I/O 조립](composition.md)

`raftpp::core`는 필수 모듈이다. 선택 SDK 모듈도 core에 의존한다. `deps/`는 상류 소스와 독립 SDK 준비 빌드를, `modules/<name>/`은 실제 어댑터·공개 헤더·CMake 빌드 규칙을 제공한다. SDK 준비 빌드 자체는 raftpp 모듈이 아니다.

## 타깃과 헤더

| 옵션 (기본 OFF) | 타깃 | 공개 헤더 | 연결 범위 |
| --- | --- | --- | --- |
| [`RAFTPP_MODULE_ASYNCPP`](modules/asyncpp.md) | `raftpp::asyncpp` | `<raftpp/asyncpp/asyncpp.hpp>` | `asyncpp_executor : executor` |
| [`RAFTPP_MODULE_ASIO`](modules/asio.md) | `raftpp::asio` | `<raftpp/asio/asio.hpp>` | `asio_stream : byte_stream`, `asio_executor : executor`, `asio_timer : timer` |
| [`RAFTPP_MODULE_MVFST`](modules/mvfst.md) | `raftpp::mvfst` | `<raftpp/mvfst/mvfst.hpp>` | `mvfst_stream : byte_stream` |
| [`RAFTPP_MODULE_MSQUIC`](modules/msquic.md) | `raftpp::msquic` | `<raftpp/msquic/msquic.hpp>` | `msquic_stream : byte_stream`, `open_msquic_api()` |
| [`RAFTPP_MODULE_LZ4`](modules/lz4.md) | `raftpp::lz4` | `<raftpp/lz4/lz4.hpp>` | `lz4_codec : compression_codec`, liblz4 ≥ 1.7.1 |
| [`RAFTPP_MODULE_ZSTD`](modules/zstd.md) | `raftpp::zstd` | `<raftpp/zstd/zstd.hpp>` | `zstd_codec : compression_codec` |
| [`RAFTPP_MODULE_ZLIB`](modules/zlib.md) | `raftpp::zlib` | `<raftpp/zlib/zlib.hpp>` | `zlib_codec : compression_codec` |

이 모듈들은 네이티브 SDK를 이용하는 core 추상 인터페이스의 실제 구현체를 포함한다. [스트림·framing·실행기·코덱](streams.md)에서 API 계약을 설명한다. `uv_stream`도 `byte_stream`을 구현하며, `framed_transport`는 호환되는 연결된 peer 스트림 위에서 core의 `transport`를 구현한다. 모듈 활성화만으로 노드의 백엔드가 자동 선택되지는 않는다. 디스크 영속화와 타이머는 계속 [UV](uv_io.md)가 제공하며 선택 스트림 어댑터가 별도의 영속 `io`를 제공하지는 않는다. QUIC 연결 수립·인증서·인증·재접속 정책은 애플리케이션 connector가 담당한다. 공개 비동기 API는 콜백을 사용한다.

`raftpp::raftpp`와 `<raftpp.hpp>`는 활성 모듈을 모은다. 특정 SDK만 쓰려면 그 모듈 타깃에 연결하고 해당 헤더를 포함한다. core는 항상 함께 연결된다. `raftpp::features::modules::<name>`과 생성 `RAFTPP_MODULE_<NAME>` 매크로는 SDK 모듈의 빌드 당시 활성 상태를 나타내며 소비자의 컴포넌트 부분 선택으로 바뀌지 않는다. 비활성 모듈의 헤더·타깃·의존성은 노출되지 않는다.

## SDK 준비와 선택

고정된 submodule 커밋은 Git gitlink와 `deps/revisions.json`에 기록한다. SDK를 빌드할 때만 해당 체크아웃이 필요하다. 기본 core/UV 빌드는 새 SDK를 다운로드하거나 구성하지 않는다.

```sh
git submodule update --init deps/asyncpp deps/asio deps/lz4 deps/zstd deps/zlib
cmake -S deps -B build/deps '-DRAFTPP_DEPENDENCIES=asyncpp;asio;lz4;zstd;zlib'
cmake --build build/deps --parallel 2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_DEPENDENCY_PREFIX="$PWD/build/deps/prefix" \
  -DRAFTPP_MODULE_ASIO=ON -DRAFTPP_MODULE_LZ4=ON \
  -DRAFTPP_MODULE_ZSTD=ON -DRAFTPP_MODULE_ZLIB=ON
cmake --build build --parallel 2
```

`RAFTPP_DEPENDENCIES`는 SDK 준비 목록이고 `RAFTPP_MODULE_<NAME>`은 raftpp 선택 목록이다. 필요한 SDK만 목록에 넣거나 `--target raftpp_dep_<name>`으로 빌드한다. 각 SDK는 별도 CMake 프로세스/빌드 디렉터리와 `<prefix>/<name>` 설치 경로를 사용한다. `RAFTPP_DEPS_PREFIX`, `RAFTPP_DEPS_JOBS`, `RAFTPP_DEPS_BUILD_TYPE`(기본 Release), `RAFTPP_DEPS_SHARED`로 준비 빌드를 설정한다. 동일 SDK를 이미 `<prefix>/<name>`에 준비했다면 `RAFTPP_DEPS_PREBUILT`에 해당 이름을 세미콜론 목록으로 지정해 재빌드를 생략할 수 있다. 이 명시적 재사용은 같은 커밋·빌드 옵션·도구와 설치 파일을 사용하는 경우에만 지정한다. 상류 테스트·예제·벤치마크·문서 빌드는 사용하지 않는다. zlib은 CMake가 원본 헤더를 변경하므로 빌드 디렉터리의 별도 소스 복사본을 사용한다.

`RAFTPP_DEPENDENCY_PREFIX`는 SDK 설치 루트다. 기존 설치 SDK를 쓸 때는 `CMAKE_PREFIX_PATH`, `PKG_CONFIG_PATH` 또는 각 package의 `_DIR` 설정을 사용할 수도 있다. Asio/asyncpp는 기본 `RAFTPP_DEPS_DIR` 체크아웃의 필요한 헤더를 사용한다. 체크아웃이 없으면 설치된 Asio 헤더 또는 asyncpp package를 찾는다. 바이너리 SDK는 해당 package를 구성 시 찾으며, 없으면 선택된 모듈의 configure가 실패한다. configure에서 자동으로 네트워크 다운로드나 SDK 빌드를 시작하지 않는다.

## QUIC SDK의 의존성

MsQuic 준비 빌드는 격리된 shared 라이브러리를 만든다. OpenSSL 백엔드의 필요한 하위 모듈만 준비한다.

```sh
git submodule update --init deps/msquic
git -C deps/msquic submodule update --init --depth 1 submodules/openssl
cmake -S deps -B build/quic-deps -DRAFTPP_DEPENDENCIES=msquic
cmake --build build/quic-deps --target raftpp_dep_msquic --parallel 2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_DEPENDENCY_PREFIX="$PWD/build/quic-deps/prefix" -DRAFTPP_MODULE_MSQUIC=ON
```

`RAFTPP_MSQUIC_TLS`는 `openssl`(기본)·`quictls`·`schannel` 중 선택한다. quictls에는 같은 이름의 하위 모듈이 필요하고 schannel은 Windows에서 사용한다. `RAFTPP_MSQUIC_EXTERNAL_OPENSSL=ON`이면 상류가 요구하는 설치 OpenSSL 3.5 이상을 사용한다. MsQuic의 테스트·도구·perf·로깅은 비활성화한다.

mvfst의 필수 의존 소스도 `deps/`의 고정 submodule로 관리한다. `RAFTPP_DEPENDENCIES=mvfst`는 아래 계층만 자동 선택해 빌드·설치하고 `raftpp_dep_mvfst`가 완료 순서를 보장한다. 상류 getdeps 전체 목록과 테스트 전용 의존성을 자동으로 가져오지 않는다. liboqs·aegis·haberdashery·선택 압축/프로파일링 계층은 비활성화한다.

| 의존성 | 사용 범위 |
| --- | --- |
| `folly`, `fizz` | mvfst transport와 TLS 기반 |
| `boost` | 필요한 헤더와 context/regex 바이너리만 빌드 |
| `openssl` | 공용 암호/TLS SDK. MsQuic의 내부 TLS 체크아웃과 독립 |
| `fmt`, `fast_float`, `gflags`, `glog` | 상류가 요구하는 포맷·수 변환·플래그·로깅 |
| `libevent`, `libsodium` | 이벤트 기반 및 Fizz 암호 지원 |
| `zstd`, `zlib` | Fizz가 요구하는 압축 SDK 재사용 |

```sh
git submodule update --init deps/mvfst deps/folly deps/fizz deps/boost deps/openssl \
  deps/fmt deps/fast_float deps/gflags deps/glog deps/libevent deps/libsodium deps/zstd deps/zlib
git -C deps/boost submodule update --init --recursive --depth 1 --jobs 4
cmake -S deps -B build/mvfst-deps -DRAFTPP_DEPENDENCIES=mvfst
cmake --build build/mvfst-deps --target raftpp_dep_mvfst --parallel 2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_DEPENDENCY_PREFIX="$PWD/build/mvfst-deps/prefix" -DRAFTPP_MODULE_MVFST=ON
```

준비하는 Folly SDK 프로필은 이 어댑터가 사용하지 않는 native Linux AIO와 SimpleAsyncIO를 제외한다. 호스트에서 감지된 libaio 링크 의존성도 피한다. 애플리케이션이 해당 API를 사용하면 별도의 전체 Folly 설치를 제공한다.

Boost는 헤더가 여러 하위 저장소에 나뉘어 있어 source 초기화가 필요하지만 테스트·추가 바이너리 라이브러리는 빌드하지 않는다. Boost/OpenSSL/libsodium 준비에는 POSIX shell·make·Perl·autoconf/automake/libtool이 필요하며 원본 submodule을 수정하지 않도록 별도 소스 복사본에서 빌드한다. Windows에서는 준비된 SDK 설치를 제공한다. 각 package는 `<prefix>/<name>`에 설치하고 `RAFTPP_DEPENDENCY_PREFIX`로 찾는다. 이미 준비된 SDK라면 `find_package(mvfst CONFIG)`가 찾을 수 있도록 설치 경로만 제공해도 된다.

## UV 압축과 검증 범위

`RAFTPP_MODULE_LZ4`는 UV 없이도 선택할 수 있다. 기존 `RAFTPP_WITH_LZ4`는 UV 스냅샷 LZ4 디코더를 켜는 별도 옵션이며 UV가 필요하다. 두 옵션이 모두 켜지면 UV가 `raftpp::lz4`를 사용하고, 모듈 옵션이 꺼져 있으면 기존 liblz4 package를 사용한다. Zstandard/zlib SDK 선택은 디스크·네트워크 형식이나 스냅샷 압축 방식을 변경하지 않는다.

`RAFTPP_BUILD_TESTS=ON`이면 선택된 어댑터 계약 검사를 `adapter` 라벨로 제공하며 Release에서만 빌드·실행한다. 상류 전체 suite·예제·벤치마크는 비활성화한다.

```cmake
find_package(raftpp CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE raftpp::asio)
```

설치 소비자도 같은 바이너리 SDK package를 찾을 수 있어야 한다. 별도 경로의 공유 SDK는 실행 시 동적 로더에서도 찾을 수 있어야 한다. Linux에서는 시스템 로더 설정 또는 `LD_LIBRARY_PATH`에 필요한 SDK의 `lib` 경로를 지정한다. `CMAKE_PREFIX_PATH`는 실행 시 라이브러리 검색 경로를 설정하지 않는다. Asio/asyncpp의 선택된 헤더는 raftpp 설치에 함께 복사하며 해당 라이선스를 보존한다. 프로젝트 라이선스는 [LICENSE.md](../../LICENSE.md)이며 외부 SDK에는 각자의 라이선스가 적용된다.

## 설치 컴포넌트 선택

`find_package(raftpp CONFIG REQUIRED COMPONENTS core asio)`처럼 필요한 컴포넌트를 선택하면 core와 선택된 모듈의 의존성만 찾는다. core는 목록에서 생략해도 필수다. 컴포넌트를 생략하면 설치 당시 활성화된 모든 모듈의 SDK를 찾는다. 설치 시 비활성화된 필수 컴포넌트를 요청하면 package 탐색이 실패한다.

부분 선택한 소비자는 해당 모듈 타깃·헤더를 사용한다. 통합 `raftpp::raftpp` 타깃과 `<raftpp.hpp>`는 설치된 활성 모듈 전체를 모으므로 전체 SDK가 필요하다. 정적 UV 컴포넌트의 LZ4 같은 실제 전이 링크 의존성은 함께 찾는다.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS asio)
target_link_libraries(my_app PRIVATE raftpp::asio)
```
