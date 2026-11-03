# Native Benchmark

[English](../en/benchmark.md) · [노드 API](node.md) · [libuv I/O](uv_io.md) · [소유 진단 정보](errors.md)

`raftpp-benchmark submit`은 `node`, `uv_io`, native writer를 통한 순차 제출을 측정한다. 설정은 단일 voter와 no-op FSM이며 명령은 노드 자신의 영속 I/O를 거쳐 commit된다. 프로세스는 지정 주소로 실제 TCP transport도 초기화한다. 임시 상태는 선택한 디렉터리 아래에 격리하고 실제 종료 후 제거한다.

## 빌드와 실행

```sh
cmake -S . -B build -DRAFTPP_WITH_UV=ON -DRAFTPP_BUILD_BENCHMARKS=ON
cmake --build build --target raftpp_benchmark --parallel
./build/tools/benchmark/raftpp-benchmark submit --dir /tmp --buf 2048 --size 8M
```

`RAFTPP_BUILD_BENCHMARKS`는 기본 OFF이며 Linux의 UV가 필요하다. fixture·LZ4·예제·테스트는 독립적인 옵션이다. 도구는 C++ 라이브러리에 링크한다. `raftpp-benchmark --help` 또는 `raftpp-benchmark submit --help`로 사용법을 확인한다.

| 옵션 | 계약 |
| --- | --- |
| `--dir DIR`, `-d DIR` | 비공개 `bench-XXXXXX` 작업 공간을 만들 기존 디렉터리. 기본 `.`. 관련 없는 파일은 보존한다. |
| `--buf BYTES`, `-b BYTES` | 48~1048576바이트이며 8의 배수인 인코딩 엔트리 예산. 기본 2048. |
| `--size BYTES`, `-s BYTES` | UINT32_MAX 이하 양수 인코딩 총량. 4096과 엔트리 예산의 배수여야 한다. 기본 8 MiB. |
| `--address HOST:PORT` | 단일 voter의 TCP bind/광고 주소. 기본 `127.0.0.1:0`은 임시 로컬 포트를 사용한다. |
| `--timeout MS` | 시작 후 준비 pool 대기를 포함한 측정 구간의 양수 제한 시간. 기본 30000 ms. |
| `--help`, `-?` | 명령 사용법을 출력한다. |

크기는 부호 없는 십진수 바이트와 1024의 거듭제곱을 뜻하는 대문자 `K`, `M`, `G` 접미사를 받는다. 긴 옵션은 `--name=value`, 짧은 값 옵션은 `-b4096`처럼 붙인 값도 받는다. 시간은 양수 십진수 정수 ms다. 연산 횟수는 1건 실행을 포함해 정확히 `size / buf`다.

## Submit 측정 경계

초기화·bootstrap·디렉터리 준비·종료는 제출 지연 표본 밖에서 수행한다. 각 표본은 명령 버퍼/콜백을 만들기 전에 시작하고 해당 명령의 apply 완료가 전달될 때 끝난다. 버퍼 할당·인코딩·영속 I/O·no-op FSM dispatch를 포함한다. 두 번째 계열은 native writer의 짝을 이루는 submit/complete 이벤트와 native 이벤트 루프 dispatch를 측정한다. 계측 자체의 오버헤드도 존재한다.

첫 사용자 payload는 `buf - 40`바이트, 이후는 `buf - 32`바이트로 엔트리 프레이밍과 최초 segment 형식을 반영한다. native I/O는 backend 정렬을 따르므로 인코딩 엔트리 예산보다 큰 버퍼를 쓸 수 있다. 지원하는 작업량 범위에서는 스냅샷 생성을 끈다.

첫 제출도 측정한다. 후속 제출이 있으면 초기 추가 prepare-pool 파일이 나타날 때까지 기다린 뒤 순차 실행을 계속한다. 이 대기는 개별 지연 표본에 포함하지 않는다. 제한 시간 초과나 신호는 수락한 요청을 종료·정리하고 실행을 실패로 처리하며, 제한 시간 초과가 명령 수락을 철회하지는 않는다.

## JSON 보고서

submit 성공 출력에는 `submit:<buf>`, `submit:write:<buf>` 두 benchmark가 있고 각각 `latency` metric을 담는다.

```json
{
  "submit:2048": {
    "latency": {"value": 30000, "lower_bound": 20000, "upper_bound": 50000}
  },
  "submit:write:2048": {
    "latency": {"value": 25000, "lower_bound": 15000, "upper_bound": 40000}
  }
}
```

위 숫자는 스키마 예시다. 실제 값의 단위는 ns다. `value`는 nearest-rank 50백분위수, `lower_bound`는 관찰한 최솟값, `upper_bound`는 관찰한 최댓값이다. 표준 vector가 표본을 보관하고 표준 알고리즘으로 통계를 구하며 메모리는 표본 수에 비례한다. 고정 histogram bucket으로 반올림하거나 고정 시간 상한으로 자르지 않는다. JSON 숫자는 classic locale로 출력한다.

종료 상태가 0일 때만 stdout을 완성된 보고서로 취급한다. 입력·초기화·I/O·계측·시간 초과·중단·정리·보고서 출력 실패는 0이 아닌 상태를 반환한다. 오류는 stderr에 쓴다. 실패 실행은 부분 측정치를 의도적으로 출력하지 않지만 출력 장치 실패로 불완전한 출력이 남을 수 있다. native 연산과 라이브러리 소유 버퍼를 정리한 뒤 임시 데이터와 이벤트 루프 상태를 해제한다.

## Disk 명령

```sh
./build/tools/benchmark/raftpp-benchmark disk --dir /tmp --buf 4096 --size 8M --timeout 30000
```

`disk`는 Linux io_uring으로 queue depth 1의 순차 durable 쓰기를 측정한다. 대상은 `O_DIRECT`로 열고 고정 파일·고정 버퍼를 등록해 `IORING_OP_WRITE_FIXED`, `IOSQE_FIXED_FILE`, `RWF_DSYNC`로 제출한다. liburing은 필요하지 않다. 빌드에서 `linux/io_uring.h`를 사용할 수 없거나 커널·권한·파일시스템이 필요한 연산을 허용하지 않으면 오류와 0이 아닌 종료 코드를 반환하며 다른 I/O 방식으로 대체하지 않는다.

| 옵션 | 계약 |
| --- | --- |
| `--dir PATH`, `-d PATH` | 기존 디렉터리 또는 쓰기 대상 block 장치. 기본 `.`. 디렉터리에는 비공개 임시 파일을 만든다. block 장치를 지정하면 시작부터 `size`바이트를 덮어쓴다. |
| `--buf BYTES`, `-b BYTES` | 512~1048576바이트의 2의 거듭제곱. 기본 4096. 대상의 direct I/O 정렬 요구도 충족해야 한다. |
| `--size BYTES`, `-s BYTES` | UINT32_MAX 이하 양수이며 buf의 배수. 기본 8 MiB. block 장치의 용량을 초과할 수 없다. |
| `--timeout MS` | 파일 준비 후 링 설정·워밍업·측정 쓰기의 제한 시간. 기본 30000 ms. |
| `--help`, `-?` | 명령 사용법을 출력한다. |

숫자·접미사와 짧은/긴 옵션 값 형식은 submit과 같다. 일반 파일을 `--dir` 대상으로 직접 지정할 수 없다. 디렉터리의 임시 파일은 사전 할당하고 정렬된 0 데이터로 채운 뒤 파일과 포함 디렉터리를 fsync한다. 준비 시간은 처리량·지연 측정에서 제외한다. raw block 장치는 사전 할당하거나 truncate하지 않으며 장치 자체를 제거하지 않는다. 정렬된 실제 쓰기 버퍼는 반복 바이트 패턴 `i % 128`로 채운다.

첫 offset 0의 워밍업은 개별 지연 표본에서 제외한다. 이후 offset 0부터 정확히 `size / buf`번 측정하며 모든 완료에서 전체 buf 바이트 수를 확인한다. 지연은 SQE 제출 전부터 대응 CQE 관찰까지의 ns다. 처리량은 `size / 1048576` MiB를 링 설정·워밍업·측정 루프·링 정리를 포함한 전체 초로 나눈 MiB/s다. 부분 MiB도 보존한다. 이 구간의 계측과 완료 polling 오버헤드도 포함한다.

성공 출력은 단일 `disk:<buf>` 항목에 `latency`와 `throughput`을 담는다. 지연의 value/lower_bound/upper_bound는 submit과 같은 p50/최소/최대이고, 처리량은 value에 MiB/s를 담으며 두 bound는 0이다. 예를 들면 아래 숫자는 형식만 보여준다.

```json
{
  "disk:4096": {
    "latency": {"value": 30000, "lower_bound": 20000, "upper_bound": 50000},
    "throughput": {"value": 100.5, "lower_bound": 0, "upper_bound": 0}
  }
}
```

시간 초과·SIGINT·SIGTERM은 추가 쓰기를 중단하고 이미 수락된 I/O를 정리한 뒤 실패를 반환한다. 제한 시간은 장치가 이미 수락한 쓰기를 철회하거나 강제로 즉시 종료한다는 보장이 아니다. 링·등록된 버퍼·FD·임시 데이터는 실제 완료까지 유지되므로 장애가 난 장치의 정리 대기는 제한 시간을 넘을 수 있다. 완료의 오류·짧은 쓰기·대응하지 않는 토큰·정리 오류는 성공 보고서를 생성하지 않는다.

링의 공유 메모리·완료 모델과 등록 버퍼의 해제 계약은 Linux의 [io_uring(7)](https://man7.org/linux/man-pages/man7/io_uring.7.html), [io_uring_register(2)](https://man7.org/linux/man-pages/man2/io_uring_register.2.html)을 따른다.

## Kernel perf와 trace

```sh
./build/tools/benchmark/raftpp-benchmark disk --dir /mnt/nvme --buf 4096 --size 8M --perf
./build/tools/benchmark/raftpp-benchmark disk --dir /tmp --size 8M --trace block,nvme
```

| 옵션 | 계약 |
| --- | --- |
| `--perf`, `-p` | end-to-end disk 지연·처리량 대신 실제 block/NVMe 명령 지연을 출력한다. 식별된 NVMe 또는 null-block 장치와 해당 tracepoint·perf 권한이 필요하다. |
| `--trace SYSTEMS`, `-t SYSTEMS` | 쉼표로 구분한 tracefs subsystem을 측정 구간에 활성화한다. 반복 지정할 수 있으며 이름은 중복 없는 영문자·숫자·밑줄·하이픈 조합이어야 한다. |
| `--tracefs PATH` | 기존 tracefs mount 경로. 기본 `/sys/kernel/tracing`. 이벤트 메타데이터와 trace 활성화 파일을 이 경로에서 찾는다. |

`--perf`는 `linux/perf_event.h`가 있는 빌드에서 사용할 수 있다. UID만 검사해 접근을 보장하지 않으며 실제 커널의 perf/tracefs 접근 권한을 따른다. 필요한 헤더·메타데이터·권한이 없으면 오류와 0이 아닌 종료 코드를 반환한다. generic/식별되지 않은 파일시스템에서 비어 있는 kernel 보고서를 성공으로 반환하지 않는다.

block의 bio queue/request complete/bio complete와 NVMe의 setup/complete tracepoint를 CPU별 perf FD로 수집한다. 온라인 CPU 목록의 빈 번호를 지원하고 각 CPU의 이벤트 출력을 하나의 읽기·쓰기 mmap 링에 모은다. 각 이벤트의 실행 중인 커널 `format`에서 필드 위치와 크기를 읽으며 고정 C 구조체 배치를 가정하지 않는다. 측정은 워밍업 뒤 시작해 측정 쓰기가 끝나면 중지한다. 수집은 각 쓰기 뒤와 중지 시 링을 읽어 진행한다.

기록의 링 경계 순환·길이·raw payload를 검사하고 전역 monotonic 시간순으로 대응시킨다. kernel device 번호와 partition sector를 구분하며 NVMe LBA는 실제 logical block 크기로 512바이트 sector에 대응시킨다. NVMe 키는 controller/queue/command ID를 포함하며 완료 뒤 ID를 재사용할 수 있다. block과 NVMe 지연은 독립적인 기록에서 계산한다.

`--perf` 성공 출력은 `disk:block:<buf>`의 latency와, NVMe이면 추가로 `disk:nvme:<buf>`의 latency를 담는다. 통계 단위와 p50/최소/최대 계약은 앞선 latency와 같다. end-to-end throughput은 이 출력에 포함하지 않는다. 이벤트 유실·overflow·잘못된/불완전한 기록·multiplexing·완료 오류·누락·모호한 명령 대응은 보고서 생성 실패다. 측정 구간의 voluntary context switch도 허용하지 않는다. 호출자의 write가 분할·병합되지 않고 명령마다 하나의 추적 가능한 block 동작과 NVMe 동작을 생성해야 한다. 호출자와 다른 I/O worker가 제출하거나 장치가 명령을 분할·병합하여 이 대응을 확인할 수 없으면 측정을 성공으로 보고하지 않는다.

`--trace`는 지정 subsystem의 개별 event enable 상태와 `tracing_on`을 보존하고 필요한 항목을 활성화한다. 성공 종료·예외 정리에서 원래 상태로 복원하므로 혼합된 enable 상태도 보존한다. tracefs의 `trace` 내용은 지우지 않으며 기록은 선택한 mount의 `trace`에서 읽는다. 같은 mount의 설정을 다른 tracer가 동시에 변경하지 않는 상태에서 사용한다. 정상 종료 중 복원 실패는 오류로 보고하며, 예외 정리 중에도 복원을 시도한다. `--trace`만 사용하면 일반 disk 지연·처리량을 출력하며 계측 오버헤드가 포함된다. perf FD와 mmap은 측정 중지·기록 소비 뒤 닫는다.

기록과 접근 계약은 Linux [perf ring buffer](https://docs.kernel.org/userspace-api/perf_ring_buffer.html), [perf_event_open(2)](https://man7.org/linux/man-pages/man2/perf_event_open.2.html), [event tracing](https://docs.kernel.org/trace/events.html)을 따른다.
