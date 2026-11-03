# Native 서버와 클러스터 예제

[English](../en/examples.md) · [노드 API](node.md) · [libuv I/O](uv_io.md) · [TCP 전송](tcp_transport.md) · [Native Submit Benchmark](benchmark.md)

예제는 `node`, `uv_io`, `tcp_transport`와 소유권을 지원하는 counter 상태 머신을 조합한다. C++ 라이브러리를 사용한다. 각 서버는 별도 프로세스·이벤트 루프·영속 디렉터리를 갖는다. `RAFTPP_BUILD_EXAMPLES`의 기본값은 OFF이며 활성화하려면 Linux에서 `RAFTPP_WITH_UV=ON`이어야 한다. fixture·LZ4·테스트 활성화는 요구하지 않는다.

## 빌드와 실행

```sh
cmake -S . -B build -DRAFTPP_WITH_UV=ON -DRAFTPP_BUILD_EXAMPLES=ON
cmake --build build --target raftpp_example_cluster --parallel
./build/examples/raftpp-example-cluster /tmp/raftpp-demo
```

CMake 타깃은 `raftpp_example_server`, `raftpp_example_cluster`이며 실행 파일명은 하이픈을 사용한다. 클러스터 타깃을 빌드하면 서버도 빌드한다. 실행 파일은 빌드 트리에 둔다. 실행기는 현재 작업 디렉터리와 무관하게 자기 실행 파일 옆에서 서버를 찾으며 `--server PATH`로 지정할 수도 있다.

실행기는 `1`, `2`, `3` 디렉터리를 만들고 각 서버를 시작한다. 이후 무작위 서버 하나를 SIGINT로 종료하고 기다렸다가 기존 데이터로 재시작하는 동작을 반복한다. 기본 간격은 1~15초에서 각각 추출하며, 한 장애 주기에서는 종료 전 대기와 재시작 전 대기에 같은 간격을 사용한다. 새 데이터 디렉터리는 소유자 전용 권한으로 만든다. 종료해도 기존 데이터를 보존한다. 디렉터리를 재사용할 때는 같은 ID·포트 구성을 사용한다.

SIGINT 또는 SIGTERM은 실행기와 모든 자식을 종료한다. 자식 종료를 기다리고 프로세스 핸들을 닫는다. 5초 안에 종료하지 않는 자식은 강제로 종료하며 해당 실행은 실패를 반환한다. 예상하지 못한 자식 종료나 spawn/시작 실패가 발생하면 나머지 자식을 종료하고 실패를 반환한다.

## 서버의 API 조합

서로 다른 터미널에서 서버 세 개를 직접 실행할 수 있다.

```sh
./build/examples/raftpp-example-server /tmp/raftpp-demo/1 1
./build/examples/raftpp-example-server /tmp/raftpp-demo/2 2
./build/examples/raftpp-example-server /tmp/raftpp-demo/3 3
```

ID는 1~3이다. 새 디렉터리라면 각 서버가 같은 3-voter 설정으로 bootstrap한다. 기존 데이터의 `cannot_bootstrap`은 정상 시작·복구를 허용한다. 과거 진단 코드 자체가 종료 통지는 아니다. 애플리케이션은 예상하지 못한 종료를 판별할 때 노드 상태를 확인한다.

서버는 pre-vote, snapshot threshold 64, trailing retention 16을 설정한다. 리더인 동안 타이머가 125 ms마다 1 증가 명령을 제출하며 follower는 복제한 명령을 적용한다. counter는 기존 C 예제처럼 native `uint64_t`를 담은 정확히 8바이트의 명령을 받는다. 스냅샷은 해당 값의 독립적인 8바이트 복사본이며 restore는 상태를 변경하기 전에 크기를 확인한다. 이 샘플의 값 형식은 호스트 바이트 순서를 사용하며 RPC·디스크 프레이밍은 라이브러리의 자체 형식을 따른다.

FSM 결과는 counter의 공유 값에 기반한 `shared<void>`이며 보관하면 머신 파괴 뒤에도 살아 있다. 결과는 변경 가능한 counter 값을 참조하지만 스냅샷은 독립적인 바이트를 소유한다. 명령 버퍼와 콜백은 실제 완료까지 소유한다. 애플리케이션은 카운트 구간과 최종 count/restore/snapshot 합계를 출력한다.

SIGINT/SIGTERM 또는 `--run-for`가 발생하면 서버는 명령 제출을 중단하고 리더라면 리더십 이전을 요청한 뒤 노드를 닫는다. 인계는 최대 2초 동안 시도한다. 동료가 모두 종료되어 새 리더를 관찰할 수 없는 경우에도 이후 노드를 닫아 이전 요청을 정리한다. 이전 콜백은 최선 노력 인계이며 성공의 증거가 아니다. 네트워크·파일·요청 정리가 끝날 때까지 루프를 실행한 뒤 애플리케이션 상태를 파괴한다.

## 옵션

두 실행 파일이 공통으로 받는 옵션은 다음과 같다.

| 옵션 | 의미 |
| --- | --- |
| `--base-port N` | 1~65532의 base. 서버 ID 1~3은 loopback 포트 N+1~N+3을 사용한다. 기본 9000. |
| `--apply-interval MS` | 0보다 큰 명령 제출 간격, ms. 기본 125. |
| `--snapshot-threshold N` | 노드의 snapshot threshold. 기본 64. |
| `--snapshot-trailing N` | 노드의 trailing retention. 기본 16. |
| `--help` | 노드를 시작하지 않고 사용법을 출력한다. |

서버는 `<dir> <id>`가 필수이며 0보다 큰 실행 시간 `--run-for MS`를 선택적으로 받는다. 지정하지 않으면 신호를 기다린다. 클러스터의 디렉터리는 선택적이며 기본 `/tmp/raftpp`다. 추가 옵션은 다음과 같다.

| 옵션 | 의미 |
| --- | --- |
| `--cycles N` | 0보다 큰 종료/재시작 횟수. 생략하면 신호가 올 때까지 반복한다. |
| `--interval MS` | 무작위 초 간격 대신 사용할 양수 고정 간격. 종료 전·중단 시간·재시작 후 안정화에 적용한다. |
| `--server PATH` | 서버 실행 파일 경로. 셸 없이 인자를 직접 전달한다. |

지정한 마지막 재시작 후에도 실행기는 한 간격을 더 기다려 재시작한 노드를 실행한 뒤 모두 종료한다. 예를 들면 다음과 같다.

```sh
./build/examples/raftpp-example-cluster /tmp/raftpp-demo \
  --cycles 1 --interval 3000 --base-port 12000 \
  --apply-interval 20 --snapshot-threshold 4 --snapshot-trailing 2
```

새 디렉터리 또는 일치하는 영속 설정을 사용한다. 잘못된 ID·옵션명/값·값 누락, 시작 실패와 자식 실패는 0이 아닌 종료 상태를 반환한다. 네트워크 인증·암호화는 제공하지 않으며 예제는 loopback 주소에 bind한다.
