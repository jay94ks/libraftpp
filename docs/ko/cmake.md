# CMake 빌드와 설치 패키지

[English](../en/cmake.md) · [이벤트 코어](core.md) · [libuv I/O](uv_io.md) · [추적](tracing.md)

`raftpp::raftpp`는 활성 모듈 라이브러리와 C++20 사용 조건을 연결하는 인터페이스 타깃이다. CMake 3.20 이상을 사용한다. 소스 빌드·테스트·예제·도구와 설치 패키지 소비자는 C++ 컴파일러만 사용한다. 모듈 라이브러리는 항상 활성인 `raftpp::core`와 `RAFTPP_WITH_UV=ON`의 `raftpp::uv`다. 전체 활성 모듈은 통합 타깃으로, 개별 API는 해당 모듈 타깃으로 연결한다. uv는 core를 전이적으로 연결한다.

`#include <raftpp.hpp>`는 전체 활성 모듈, `<raftpp/raft.hpp>`는 core, `<raftpp/uv/uv.hpp>`는 uv의 진입점이다. 구체적인 헤더는 `<raftpp/uv/uv_io.hpp>`처럼 `<raftpp/uv/...>`에 있다. 타입 이름은 `raftpp` 네임스페이스를 유지한다. UV 비활성 시 UV 헤더가 없고 fixture 비활성 시 `fixture.hpp`가 없다. 압축 보조 함수는 uv에 속하며 LZ4 비활성 시 `invalid`를 반환한다.

## 소스에서 사용

```cmake
cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES CXX)
add_subdirectory(path/to/raftpp raftpp-build)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE raftpp::raftpp)
```

`cxx_std_20`과 필요한 include/link 조건이 타깃에서 전달된다. 빌드별 생성 헤더 복사본은 `<build>/include/`에 있다. 소스 루트 `include/`는 마지막 configure에서 활성 모듈을 모은 gitignore 대상 미러다. `modules/core/include/raftpp/` 또는 `modules/uv/include/raftpp/uv/`의 원본을 수정하며 복사본을 편집하거나 공용 미러를 컴파일 입력으로 사용하지 않는다. `modules/<module>/privates/`의 비공개 헤더는 설치하지 않는다.

`RAFTPP_INSTALL`의 최초 기본값은 최상위 프로젝트에서 ON, `add_subdirectory`에서는 OFF다. 부모 프로젝트가 설치를 관리하려면 이 옵션을 명시적으로 선택한다. 캐시에 지정한 옵션은 이후 configure에서도 유지된다.

## 설치와 소비

```sh
cmake -S path/to/raftpp -B raftpp-build -DCMAKE_BUILD_TYPE=Release -DRAFTPP_INSTALL=ON
cmake --build raftpp-build --config Release --parallel
cmake --install raftpp-build --config Release --prefix /path/to/raftpp-prefix
cmake -S consumer -B consumer-build -DCMAKE_PREFIX_PATH=/path/to/raftpp-prefix
cmake --build consumer-build --config Release --parallel
```

소비자의 `CMakeLists.txt`는 다음과 같다.

```cmake
cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES CXX)
find_package(raftpp CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE raftpp::raftpp)
```

기본 설치 위치는 라이브러리 `lib/`, 엄브렐라 헤더 `include/raftpp.hpp`, core 헤더 `include/raftpp/`와 활성 UV 헤더 `include/raftpp/uv/`(생성 config/export 포함), package 설정 `lib/cmake/raftpp/`, 상류 라이선스 `share/raftpp/LICENSE.md`다. 실제 디렉터리는 `GNUInstallDirs`의 `CMAKE_INSTALL_LIBDIR`·`CMAKE_INSTALL_INCLUDEDIR`·`CMAKE_INSTALL_BINDIR`·`CMAKE_INSTALL_DATADIR`를 따른다. 상대 디렉터리를 사용하면 설치 prefix를 옮긴 후 새 위치를 `CMAKE_PREFIX_PATH` 또는 `raftpp_DIR`로 지정할 수 있다. 명시적인 절대 설치 디렉터리는 그 절대 위치를 사용한다.

설치 패키지는 `raftpp::raftpp`, `raftpp::core`, 선택적인 `raftpp::uv`와 활성 공개 헤더를 제공한다. 모듈 라이브러리 파일명은 플랫폼별 접두사/확장자를 붙인 `raftpp-core`, `raftpp-uv`다. 소비자가 수동으로 빌드 경로·라이브러리 파일을 추가할 필요가 없다. 생성 `config.hpp` guard와 설치 헤더 집합은 빌드 구성에 대응하며 비활성 기능은 노출하지 않는다. 기능 선택은 라이브러리를 빌드할 때 정하며 소비자의 매크로 재정의로 바꾸지 않는다.

패키지 검색은 버전을 지정하지 않는 `find_package(raftpp CONFIG)`를 사용한다. `reference_version`은 기준 C 구현의 버전으로 C++ ABI 또는 CMake package 버전 요구 조건을 뜻하지 않는다.

## 기능 옵션과 의존성

| 옵션 | 계약 |
| --- | --- |
| `RAFTPP_WITH_UV` | uv 모듈을 선택하며 최초 기본값은 libuv 감지 결과다. ON이면 Linux·libuv 1.18.0 이상과 필요한 Linux 헤더가 필요하다. 설치 소비자에게 libuv 사용 조건을 전달한다. |
| `RAFTPP_WITH_LZ4` | UV와 liblz4 1.7.1 이상을 감지하면 최초 기본 ON이다. UV 없이 활성화할 수 없다. |
| `RAFTPP_WITH_FIXTURE` | 기본 ON. OFF에서는 fixture API·구현·관련 테스트 의존성이 제외된다. |
| `RAFTPP_WITH_BACKTRACE` | 기본 OFF. 생산자 빌드에서 GNU libbacktrace 헤더·라이브러리와 `__assert_fail`이 필요하며 UV와 독립적이다. [추적](tracing.md)의 assertion 조건을 따른다. |
| `RAFTPP_BUILD_TESTS` | 기본 OFF. 선택한 기능에 맞는 CTest 타깃을 빌드한다. |
| `RAFTPP_BUILD_EXAMPLES` | 기본 OFF. UV가 필요하며 테스트·fixture·LZ4와 독립적이다. |
| `RAFTPP_BUILD_BENCHMARKS` | 기본 OFF. UV가 필요하며 예제·테스트·fixture·LZ4와 독립적이다. |
| `BUILD_SHARED_LIBS` | 별도 설정이 없으면 정적이다. ON이면 공유 라이브러리를 빌드한다. |
| `RAFTPP_INSTALL` | 최상위 기본 ON, 하위 프로젝트 기본 OFF. 설치 규칙과 CMake package를 생성한다. |

UV가 켜진 설치 패키지는 소비 환경에서 PkgConfig로 libuv를 찾는다. 정적 LZ4 구성은 liblz4도 다시 찾고, 정적 backtrace 구성은 소비 컴파일러의 검색 경로에서 GNU libbacktrace 라이브러리를 찾는다. 생산자의 의존성 절대 경로를 복사하지 않는다. 공유 라이브러리의 비공개 LZ4/backtrace 링크 조건은 소비자 개발 헤더·PkgConfig 요구로 노출하지 않는다. 외부 공유 라이브러리의 실행 시 검색은 플랫폼 로더 조건을 따른다.

UV/LZ4가 모두 꺼진 package는 PkgConfig를 찾지 않는다. backtrace도 꺼져 있으면 그 의존성도 요구하지 않는다. 정적 라이브러리의 필요한 비공개 링크 조건은 타깃에서 전달되므로 소비자가 `-llz4`·`-lbacktrace`를 수동 추가하지 않는다.

## 검색 오류

선택한 기능에 필요한 소비 의존성을 찾지 못하면 `raftpp_FOUND`가 거짓이고 `raftpp_NOT_FOUND_MESSAGE`에 원인이 기록된다. `find_package(raftpp CONFIG QUIET)`는 사용 가능 여부를 확인할 수 있고 `REQUIRED`는 구성 오류로 처리한다. `raftpp::raftpp`는 필요한 의존성을 찾은 뒤 정의한다. package가 지원하지 않는 필수 component도 검색 실패다.

설치본과 소스 빌드의 [이벤트 코어](core.md)·[콜백 노드](node.md) 계약은 같다. 공유 라이브러리의 공개 심볼과 기능 선택은 설치한 바이너리·생성 헤더의 조합을 따른다.

선택 SDK 타깃·개별 의존성 준비·모듈 옵션은 [선택 어댑터 모듈](dependencies.md)을 참고한다.
