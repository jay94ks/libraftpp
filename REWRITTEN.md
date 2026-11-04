# C++20 재작성 현황

[English](REWRITTEN.en.md)

최종 갱신: 2026-11-04

전체 cowsql/raft 라이브러리를 C++20으로 이식하는 작업의 현황과 검증 기록이다. 작업 원칙은 [AGENTS.md](AGENTS.md)를 따른다. 이 문서와 영어 대응본은 함께 갱신한다. API 설명서는 루트의 `docs/ko/`와 `docs/en/`에 작성하며, 이 문서의 진행 기록을 API 설명서에 복사하지 않는다.

## 전체 상태

README 안내 문서(2026-11-04): 프로젝트 소개·빌드/설치·선택 모듈·I/O 조립·실행 예제·API 탐색을 담는 README.md와 영어 대응본을 추가했다. 문서 35쌍/링크 786개와 README 코드 7블록의 대응을 확인했다. 문서만 변경해 제품 빌드·테스트는 생략 — 기존 결과 재사용이다.

RocksDB/SQLite 저장소 모듈(2026-11-04): SDK와 필수 생성 도구 Tcl을 deps에 고정하고 `storage` 전체를 구현했다. UV 없이 `make_io`에 조립할 수 있다. 정적/공유 Release와 ASan/UBSan의 관련 8개 검사, 강제 종료 복구·SQLite rollback, 설치 소비자·선택 해제를 검증했다. 인라인 snapshot 완료로 드러난 기존 노드의 중복 생성도 수정했다. DB 작업은 블로킹/완료 인라인이며 상세 범위·최초 실패·제한은 아래 단위를 따른다.

이전 사용자 선택형 I/O 조립 단위(2026-11-04): `storage`·`timer`·`make_io`와 UV 저장소/타이머·Asio 타이머를 구현했다. 사용자가 transport와 선택 snapshot codec을 조합한다. core 계약 7개, UV 없는 Asio TCP 클러스터, 실제 codec별 snapshot 재시작, ASan/UBSan 13개 범위와 정적/공유 설치 소비자를 검증했다. 기존 `uv_io`는 유지하며 내장 영속 저장소는 UV만 제공한다. 상세 범위·최초 실패·재사용 조건은 아래 단위에 기록했다.

모듈별 API 설명서 단위(2026-11-04): core/uv와 일곱 선택 어댑터, 모듈 목록의 한·영 설명서 10쌍을 추가했다. 공개 API·소유권·콜백·오류·옵션·사용 예제와 기존 기능별 문서 탐색을 정리했다. 문서 전체 31쌍/상대 링크 638개와 새 코드·서명 블록 대응을 확인했다. 문서만 수정하여 제품 빌드·테스트는 생략 — 기존 결과 재사용으로 기록한다.

core 추상 인터페이스의 실제 어댑터 구현을 추가했다. 공통 `byte_stream`·`executor`·`compression_codec`와 `framed_transport`, 일곱 모듈의 스트림/실행기/코덱을 연결했다. 새 계약 검사 8개가 Release와 ASan/UBSan에서 각각 범위별 통과했고, 관련 UV/압축 18개와 정적/공유 설치 소비자를 확인했다. 최초 실패·중단과 선택 재개 결과는 아래 새 단위에 보존한다. QUIC 연결/TLS 수립과 별도 영속 `io` 전체를 제공하는 모듈은 아니며 해당 경계는 애플리케이션 connector와 기존 UV가 담당한다.

사용자 범위 정정(2026-11-03): SDK include/링크 타깃만으로는 모듈 구현이 아니다. 기존 UV처럼 core 추상 인터페이스의 실제 구현체를 제공하도록 공통 바이트 스트림·실행기·압축 경계와 네이티브 어댑터 작업에 착수했다. 이전 SDK smoke 검사는 의존성 준비/링크 검증 근거로만 유지하며 실제 어댑터 동작 통과로 간주하지 않는다.

라이선스 통합과 일곱 선택 의존성 모듈(`asyncpp`, `asio`, `mvfst`, `msquic`, `lz4`, `zstd`, `zlib`)의 저장소·빌드 연결에 착수했다. 기존 콜백 기반 API를 유지하며 외부 SDK 연결과 실제 Raft 백엔드 구현을 구분한다.

core/uv 모듈 재편·활성 공개 헤더 생성·분리된 라이브러리/설치 연결을 완료했다. 코어/공유 native/최소 구성과 모듈별 소비자 및 최종 정적 검사를 마쳤다.

한·영 API 문서와 루트 지침·기록을 독립 C++ 라이브러리 사용 설명으로 정리했다. 문서만 변경했으며 구현·빌드·테스트는 기존 결과를 재사용했다.

최상위 엄브렐라 헤더 `include/raftpp.hpp`를 추가하고 설치/두 언어 API 문서에 연결했다. 선택 기능 제외 헤더 검사와 정적·공유 설치 소비자 검사가 통과했다. 상세 실행/생략 범위는 아래 해당 단위를 따른다.

C 라이브러리 제거·C++ 자체 테스트 전환·포맷 통일을 완료했다. C++ 전용 정적/공유 및 선택 기능 빌드, 코어 92개/native 32개 선택 검사와 기본 개발 빌드가 통과했다. 프로젝트는 C++만 빌드하며 원본 라이선스를 별도로 보존한다. 실행/생략/과거 결과와 제한은 아래 정리 단위를 따른다.

이전 기능 이식 완료 감사(이번 정리 이전) — 2026-11-03 최종 요구사항 감사를 마쳤다. 원본 51개 소스/10개 기능군, 공개 함수 114개와 기능성 구조체·I/O·FSM·raw transport 계약을 독립 C++20 구현과 범위별 검증 근거에 대응시켰다. 합의·저장·네트워크·공개 callback API·fixture·선택 기능·설치·예제/도구에 남은 미이식 계층은 없다. 아래 최종 판정표는 구현 충족과 검증 조건을 구분한다.

해당 이전 감사에서 공유 라이브러리의 비-inline 선언 279개(별도 fixture OFF 구성 228개)의 export 누락이 없고, 정적/공유 C++ 산출물 세 개가 원본 C의 전역 심볼 371개에 의존하지 않음을 확인했다. 최종 소스/입력 hash 104개는 그대로여서 기존 runtime·옵션·소비자 결과를 재사용했다. 공통 소유권 별칭의 한·영 API 설명 누락만 보완했으며 제품·테스트·빌드 규칙은 변경하지 않았다.

검증 제한: 실제 kernel perf/trace 수집·raw 장치·모든 파일시스템/권한 조합·32비트 실행·LSan은 완료 주장에 포함하지 않는다. 원본 C의 정의되지 않은 동작/조기 해제 때문에 제한된 비교, 양쪽 모두 실패한 추가적인 전체 follower 제한 시간 수렴 조건, 원인 미확정인 최초 TCP timeout 기록을 보존한다. 206개는 제거 전 기본 구성의 CTest 등록 수이고 현재 C++ 전용 구성은 204개다. 어느 쪽도 이번 전체 suite 통과 수가 아니다. 과거 단위 요약의 남은 작업은 각 단위 당시 상태이며 현재 판정은 이 절과 최종 감사 표를 따른다.

이전 native 계약 감사 단위: 16파일/230정의를 구현·검증 근거에 대응시켰고 새 load/DNS/결합 barrier 3개 검사의 42개 C 대조 시나리오가 Debug·공유 Release·ASan/UBSan에서 각각 통과했다. C의 부분 listen 실패 두 경로는 조기 해제를 피하는 테스트 allocator 조건에서만 최종 비교했다. 제품은 유지하고 기존 203개는 재사용했다. 원본 유한 목록 43파일/494정의의 사례 대응은 정리했으며, 전체 요구사항·현재 산출물의 최종 완료 감사와 기존 참조/수렴/환경 제한은 남아 있다.

이전 core/node 계약 감사 단위: 원본 integration 15파일/118정의의 기능·오류·표준 대체 계약을 구현/검증 근거에 대응시켰다. 새 4개 검사의 30사례·123개 C 이벤트가 Debug·core-only 공유 Release·ASan/UBSan에서 각각 일치했다. 제품은 유지하고 기존 199개를 재사용했다. 한·영 core API에 늦은 완료/ACK·설치/재시도·catch-up 경계를 보완했다. 남은 사례 감사는 native 16파일/230정의이며 참조 구현·수렴·환경 제한과 전체 완료 증명은 남아 있다.

이전 공통·저장 값 계약 단위: 원본 core-unit 8파일/134정의의 정상·오류·표준 대체 대응을 정리했다. 새 foundation_failure가 Debug/core-only 공유 Release/ASan·UBSan에서 각각 통과해 26개 할당 실패 위치·재시도·소유/출력 보존과 원본 난수 벡터를 확인했다. 제품은 유지하고 기존 198개는 재사용했다. configuration 한·영 API/예제를 추가해 문서 경로는 16쌍이다. 남은 사례 감사는 31파일/348정의이며 기존 C/수렴/환경 제한은 유지한다.

이전 원본 계약·fuzzy/지연 pre-vote 단위: 원본 43파일/494정의의 유한 목록을 만들고 fuzzy 12시나리오를 171개 고정 시드 조합으로 이식했다. 늦은 pre-vote 제안 term의 불필요한 갱신을 독립 Release 회귀로 재현·수정했다. 새 5개/관련 기존 4개가 Debug·core-only 공유 Release·ASan/UBSan에서 통과했다. 구성별 391077개 C++ 이벤트 중 유효한 204687개만 C 일치로 기록하고, C 비활성 union 접근 직전 133개 사례의 비교를 중단했다. 기존 189개 재사용·한영 API/기록을 갱신했다. 나머지 39파일/482정의의 사례 감사와 C/수렴·기존 환경 제한은 남아 있다.

이전 전체 소스 대응·압축 snapshot 전송 단위: 원본 51개 소스를 10개 기능군에 대응시키고 오래된 기능 옵션 표를 현재 구현으로 정정했다. 새 compressed_transfer가 Debug/공유 Release/LZ4 OFF 공유 Release/ASan·UBSan에서 각각 통과해 실제 TCP 설치·수신 파일 바이트·전체 재시작과 원본 압축 파일 보존을 확인했다. 기존 192개는 재사용하고 한·영 API/기록을 갱신했다. 전체 기능 의미 대응 감사와 최초 TCP/환경/참조 제한은 남아 있다.

이전 native 클러스터 리더 선택 단위: 적용/snapshot 완료만으로 현재 리더를 보장할 수 없어 테스트가 sentinel 3을 배열에 사용할 수 있음을 소스에서 확인하고 리더 대기/범위 검사를 추가했다. 실제 TCP cluster/tracing/memory가 Debug/공유 Release/ASan·UBSan에서 각각 3개씩 통과했고 sandbox socket 실패는 환경 결과로 별도 보존했다. 제품은 유지하고 기존 189개는 재사용했다. 최초 TCP timeout과의 인과는 미확정이며 전체 계약 감사·기존 환경/참조 제한은 남아 있다.

이전 step 완료 알림 단위: 누락된 공개 legacy.step_cb를 node::set_step_callback/표준 소유 callable로 이식했다. 일반 97/강제 barrier 112건의 실제 C 알림과 시점/상태가 Debug·공유 Release·ASan/UBSan에서 일치했다. 이동 전용 캡처·자기 교체·OOM·해제·재진입·종료와 설치본 독립 소비자 두 구성이 통과했다. 기존 182개는 재사용하고 한·영 API/지침/기록을 갱신했다. TCP 최초 원인은 로그 부재로 미확정이며 전체 계약 감사와 기존 환경/참조 제한은 남아 있다.

이전 실행 중 I/O/FSM 종료 단위: 공유 Release에서 종료 후 append·applied 변경을 재현하고 사용자 호출 사이의 단계 중단을 수정했다. 새 I/O 28/FSM 8사례의 2개 검사가 Debug/공유 Release/ASan·UBSan/코어 전용에서 통과했고 기존 7/1/4개만 선택 재검증했다. 실제 C barrier/membership 대조와 완료→종료 순서·받아들인 작업의 정리를 확인했다. 기존 180개는 재사용하고 한·영 node API/기록을 갱신했다. 전체 경로 감사와 최초 TCP/환경/참조 제한은 남아 있다.

이전 노드 시작 재진입 단위: Release에서 중첩 load/restore와 종료 후 restore를 재현하고 starting admission·호출 이후 종료 확인·미뤄진 close pump를 수정했다. 새 재진입 4/종료 6사례의 2개 검사가 Debug/공유 Release/ASan·UBSan/코어 전용에서 통과했고 영향받는 기존 5/1/2개만 선택 재검증했다. 기존 180개는 재사용하고 한·영 node API/기록을 갱신했다. 다른 사용자 I/O 경계와 전체 완료 증명·최초 TCP/환경/참조 제한은 남아 있다.

이전 디스크 입력 경계 단위: 새 storage_limits가 Debug·엄격한 공유 Release·ASan/UBSan에서 통과했다. 원본 C reader 29회 중 27회 일치·파일명 expected_n의 32비트 절단 차이 2회를 구분하고 C++의 corrupt 거부를 유지했다. 설정 길이 1 MiB·큰 선언·CRC/추가 바이트와 payload 한도 2경우의 출력/파일/FD 보존을 확인했다. 제품 구현은 변경하지 않고 기존 184개는 재사용했으며 한·영 API/기록을 갱신했다. 전체 경로 완료 증명과 최초 TCP/환경/참조 제한은 남아 있다.

이전 설치·CMake package 단위: C++ install/export/find_package와 생성 헤더·원본 라이선스 설치를 구현했다. 이동한 설치본의 독립 C++ 소비자가 네 정적/공유·UV/LZ4/fixture/backtrace 구성에서 통과했고, 공백 prefix·공유 LZ4 개발 파일 제외·누락 의존성·하위 프로젝트 기본 OFF/ON→OFF도 확인했다. 기존 런타임 183개와 추가 matrix/sanitizer는 관련 구현 변경이 없어 재사용했다. 한·영 API/지침/기록을 함께 갱신했으며 전체 경로 완료 증명과 최초 TCP/환경/참조 제한은 남아 있다.

이전 TTY 메시지 adapter 단위: 실제 PTY connector로 6종 RPC 양방향 12개·4MiB native 대기 쓰기 취소/수명·잘린 frame EOF의 3사례를 추가했다. 새 adapter_tty가 Debug·엄격한 공유 Release·ASan/UBSan에서 통과했고 기존 182개는 생략했다. 실제 pipe framing/native 디스크 3노드는 원래 검증되어 있었음을 소스/과거 기록으로 확인해 최근 미검증 요약을 정정했다. 제품은 유지하고 한·영 API/기록을 갱신했으며 최초 TCP/환경/참조 제한과 전체 완료 증명은 남아 있다.

이전 native TTY stream 단위: 전용 PTY/libuv 독립 실험과 공개 uv_stream의 TTY 5사례를 추가했다. 바이너리 양방향 전송·chunk/write 수명·구독 중지/재개·실제 EOF 재진입 종료·활성 채택 거부·이동 대입 정리가 Debug·엄격한 공유 Release·ASan/UBSan에서 통과했다. 기존 터미널/전역 모드와 제품 구현은 유지하고 한·영 API/기록을 갱신했으며 기존 181개는 생략했다. 실제 connector framing·이전 TCP/환경/참조 제한과 전체 완료 증명은 남아 있다.

이전 native pipe stream 단위: 실제 익명 pipe의 송수신/공유 버퍼·쓰기 취소·EOF 재진입 종료·활성 채택 거부·단방향 오류의 5사례를 추가했다. 테스트의 최초 EBADF 가정은 독립 C/libuv 실험으로 EPIPE임을 확인해 고쳤고 최초 실패를 보존했다. 새 stream_pipe가 Debug·엄격한 공유 Release·ASan/UBSan에서 통과했으며 기존 180개는 재사용했다. 제품 구현은 유지하고 한·영 API/기록을 보완했다. 이전 TCP 실패 원인과 TTY/추가 adapter·환경/참조 제한 및 전체 완료 증명은 남아 있다.

이전 wire 길이·단계별 조립 경계 단위: 기존 제품 guard는 유지하고 새 encoding_limits 25사례를 추가했다. Debug·core-only 공유 Release·fixture OFF ASan/UBSan에서 새 1개가 통과했다. UV의 원본 C 정상 헤더 27건은 일치하며 2^32 개수를 0으로 절단하는 원본 차이 1건은 C++ 거부와 구분했다. 기존 179개는 재사용하고 한·영 encoding API/기록을 보완했다. 실제 대형 payload/32비트 실행·전체 완료 증명과 이전 제한은 남아 있다.

이전 선거 term/시계 경계 단위: Release에서 실제 term=0/next_timeout=1569 wrap을 재현하고, 다음 term·이벤트 시계·timeout setter admission을 수정했다. 새 term 8/clock 4/원본 C 7사례·34 events/callback node 6사례가 세 구성에서 통과했다. 구성별 새 4개·기존 영향 6개를 실행하고 기존 나머지 169개는 재사용했다. 최초 실패 로그는 보존하며 누수/환경/참조 제한과 전체 완료 증명은 남아 있다.

이전 전체 완료 근거/코어 인덱스 경계 단위: Release에서 마지막 index=18446744073709551615를 접수한 뒤 pipeline next_index=0이 되는 실제 오류를 재현했다. 코어 시작/제출/수신에서 이 terminal 값을 실제 로그·스냅샷 index로 거부하고, max-1의 정상 저장/ACK/heartbeat는 유지하도록 수정했다. 새 8사례가 Debug·core-only 공유 Release·fixture OFF ASan/UBSan에서 통과했다. 관련 기존 검사 4개의 구성별 결과와 변경 없는 170개 재사용을 기록했다. 현재 구현과 달랐던 구성요소 표 9행을 두 언어로 정리했으며 전체 완료는 아직 증명되지 않았다.

이전 네이티브 파일 예약 대체 경로 C 대조 단위: 기존 file_handle의 EOPNOTSUPP 대체 예약이 실제 C와 같은 것을 74사례로 확인했다. 54개 대체 예약 성공·native 정상 1개와 오류 19개에서 쓰기 위치/횟수·파일 크기/바이트·실패 파일/descriptor 정리·출력 핸들 보존·소유 진단 원인을 검증했다. 새 1개가 Debug·엄격한 공유 Release·fixture/LZ4 OFF ASan/UBSan에서 통과했다. 제품 구현은 수정하지 않았고 UV API 두 언어본에 대체 예약/오류 계약을 보완했다. 변경 없는 173개·추가 matrix/소비자는 재사용하며 전체 완료 증명·이전 제한은 유지한다.

이전 이벤트 코어 인계 C 대조 단위: 원본과 같은 자동 대상 선택·부분 ACK의 조기 TimeoutNow·늦은 힌트 선거·만료/재시도를 9사례/135개 이벤트로 대조했다. 제품 알고리즘은 바꾸지 않았으며 core API의 ID 0 설명을 실제 자동 선택 계약으로 바로잡았다. 새 검사 1개가 Debug·core-only 공유 Release·fixture OFF ASan/UBSan에서 통과했다. Release의 기존 cluster 테스트 임시 variant 이동 경고는 구체적 이벤트 전달로 정리하고 해당 기존 검사 1개만 세 구성에서 검증했다. 나머지 171개·추가 matrix/소비자는 재사용한다. C fixture counter 실패와 전체 완료 증명·이전 제한은 유지한다.

이전 fixture 스냅샷 주입·로드 단위: 원본은 모든 주입 엔트리를 스냅샷 접미사로 반환하지만 C++가 앞부분을 걸러 잃던 차이를 새 검사로 재현하고 수정했다. 주입 순서·빈/1/3개 접미사·스냅샷 위치·반복 load/노드 시작·trailing 0/1/4의 45사례에서 실제 C와 126회 로드를 대조했다. 24개 할당 위치의 출력 보존도 확인했다. 새 1개/기존 영향 4개가 Debug에서, 새 1개가 core-only 공유 Release에서, 새 1개/기존 영향 2개가 ASan/UBSan에서 통과했다. 나머지 167개·추가 matrix/소비자는 재사용한다. 전체 완료 증명과 이전 환경·실패 제한은 유지한다.

이전 로컬 스냅샷 할당 실패 단위: 준비 할당 실패가 노드를 종료하는 차이를 새 검사로 재현하고, 선택적인 생성 준비의 bad_alloc/length_error만 건너뛰도록 수정했다. 긴 구성 주소를 포함한 32개 할당 위치에서 13회 생략(고정 이후 6회 정리)/19회 제출과 재시도·명령/구성 보존을 확인했다. 실제 C 요청/구성 할당 실패 3개도 노드 생존·재시도가 일치한다. Debug 새 1개/기존 영향 3개, core-only 공유 Release 새 1개, ASan/UBSan 새 1개/기존 영향 3개, fixture OFF 공유 Release 새 1개가 통과했다. 나머지 167개·추가 matrix/소비자는 기존 기록을 재사용한다. 전체 완료 증명과 이전 환경·실패 제한은 유지한다.

이전 native probe 정리 오류 호환 단위: 원본 probeAsyncIO의 io_destroy 오류 검사가 C++ RAII 소멸자에서 누락된 것을 소스 대조로 확인하고, 성공/미지원 capability 공개 전에 정리를 확인하도록 수정했다. 실제 C probe의 정상/EIO/ENOMEM/EINVAL 결과와 C++가 일치했으며, C++ 미지원·앞선 제출 오류/출력 보존/진단/자원 정리도 검증했다. 새 1개와 기존 영향 2개가 Debug에서, 새 1개가 엄격한 공유 Release·ASan/UBSan에서 통과했다. 기존 나머지 167개·추가 matrix/소비자는 재사용했다. 전체 완료 증명과 이전 환경·실패 제한은 유지한다.

이전 fixture 확장·멤버십 C 대조 단위: 초기 1/3노드 클러스터를 실행 중 3/5노드로 확장하고 미복제 spare 승격·standby 전환·승격/강등·제거를 포함한 총 3920개 이벤트를 실제 C fixture와 비교했다. 이벤트/시간·구성 ID/역할/주소/순서·term/로그/commit/applied/vote·FSM과 구성 변경 콜백 7회씩이 일치했다. 새 검사 1개가 Debug·core-only 엄격한 공유 Release·ASan/UBSan에서 통과했고 제품 코드는 수정하지 않았다. 기존 168개·추가 matrix/소비자 결과는 재사용했다. 확장 API 두 언어본을 보완했으며 전체 실행 경로 완료 증명과 기존 환경·실패 제한은 남아 있다.

이전 native capacity 호환 단위: 실제 C backend 대조에서 준비 pool 2개·각 32 MiB 보고 크기의 값이 C=0/C++=65535인 차이를 재현했다. uv_io와 log_writer의 포화 계산을 원본 unsigned 합산·KiB/uint16_t 변환으로 맞췄다. 준비 pool/활성 segment·16비트 경계·큰 runtime 설정 24개와 실제 단일 노드의 no-space 거부/출력 보존/용량 회복 후 적용을 검증하는 새 2개가 Debug·엄격한 공유 Release·ASan/UBSan에서 통과했다. 관련 기존 3개만 Debug에서 재확인하고 나머지 기록을 재사용했다. 전체 원본 기능/실행 경로의 완료 증명과 기존 환경·실패 제한은 남아 있다.

이전 assertion backtrace 단위: 원본의 선택 기능 `--enable-backtrace`에 대응하는 기본 OFF 옵션 `RAFTPP_WITH_BACKTRACE`를 구현했다. 기존 6개 C++ 파일의 assertion 29곳을 독립 실패 출력 경로에 연결하고 표준 `source_location`과 기존 GNU libbacktrace를 사용한다. Debug OFF/ON·공유 Release·ASan/UBSan·외부 정적 소비자·같은 디렉터리 ON→OFF 및 의존성 거부/캐시 재사용을 검증했다. 새 검사 1개와 관련 기존 검사만 실행하고 나머지 기록을 재사용했다. 전체 원본 기능/실행 경로의 완료 증명과 기존 환경·실패 제한은 남아 있다.

이전 native recover 대조 단위: snapshot-only·남은 closed 로그·open 로그 prefix salvage·손상된 끝 파일/누락 구간과 자동 복구 ON/OFF·1/5명 구성·반복 복구의 36개 초기 조건/64회 호출을 실제 C recover와 비교했다. Debug·엄격한 공유 Release·ASan/UBSan에서 새 검사 1개가 통과했다. configuration 엔트리의 index/term·역할/주소·term/vote 메타데이터 보존·격리 파일을 포함한 모든 바이트가 일치했다. 해당 기존 C++ 구현의 대응을 확인하고 recover/auto-recovery의 실제 계약을 API 문서 두 언어본에 보완했다. 변경 없는 164개·추가 matrix/소비자는 재사용했다. 실제 kernel/raw 장치와 이전 TCP timeout·참조 C fixture sanitizer 제한은 별도로 유지하며 전체 재작성 완료는 아직 증명하지 않았다.

이전 fixture 호환 단위: 네트워크 제어의 C 이벤트 4600개(3·5노드), 직접 메모리 I/O의 제출·완료·truncate 실패와 영속 상태, snapshot 내부 truncate 카운터를 비교하는 새 3개를 추가했다. trailing=0 스냅샷이 fault 카운터를 건너뛰던 실제 차이(C=18/C++=0)를 재현하고 truncate(1)을 거치도록 고쳤다. 실패 시 이전 snapshot/log를 보존하고 재시도한다. Debug·core-only 엄격한 공유 Release·ASan/UBSan에서 새 3개와 기존 영향 3개를 범위별 검증했다. C bridge의 load 개수를 size_t로 맞춰 잘못된 초기 통과를 재사용하지 않았다. 변경 없는 158개·추가 matrix/소비자를 재사용하고 두 언어 fixture API를 갱신했다. 실제 kernel/raw 장치와 이전 TCP timeout·C fixture sanitizer 제한은 별도로 유지하며 전체 재작성 완료는 아직 증명하지 않았다.

이전 adapter 단위: 사용자 uv_stream_transport를 Raft 메시지 transport와 uv_io에 연결하는 stream_transport를 구현했다. 단계별 framing·최신 대기 3개 정책·재시도·주소 변경·취소·완료/종료 순서와 소유 진단을 연결했다. 첫 stream 종료 observer는 추가 할당 없이 저장한다. 새 7개와 기존 종료 영향 3개를 Debug·엄격한 공유 Release·ASan/UBSan에서 범위별로 확인했다. pipe의 6종 RPC·fragment·오류/할당 실패·8MiB 미완료 쓰기 취소, TCP 상호 운용과 실제 디스크 3노드 복제를 검증했다. 테스트 deleter의 통계 참조 수명 오류를 공유 소유로 고치고 영향 6개만 재검증했다. UV ON 공유 소비자의 새 API/문서 예제와 UV OFF 새 헤더 빌드도 확인했다. 변경 없는 151개·추가 matrix는 재사용했으며 실제 kernel/raw 장치와 이전 TCP timeout·참조 C fixture sanitizer 제한은 유지한다. 전체 재작성의 모든 경로 완료는 아직 증명하지 않았다.

이전 raw-stream 단위: 이동 전용 uv_stream과 uv_stream_transport/tcp_stream_transport를 구현했다. 기존 TCP 연결 엔진을 공유하고 native 주소·buffer·요청·deleter 수명을 완료까지 소유한다. connector 파괴 후 사용·취소·할당 실패·C 상호 운용을 포함한 새 6개를 Debug 범위별·엄격한 공유 Release·ASan/UBSan에서 확인했다. 기존 TCP 영향 15개는 Debug, 4개는 Release/sanitizer에서 통과했다. UV OFF/core-only 및 UV ON/fixture·LZ4 OFF 공유 외부 소비자가 빌드·실행되며 새 API export와 C 미링크를 확인했다. 변경 없는 Debug 133개·추가 matrix는 생략하고 이전 기록을 재사용했다. 메시지 adapter/UV backend 연결과 실제 kernel/raw 장치 검증, 이전 TCP timeout·참조 C fixture sanitizer 제한은 남아 있다.

이전 core-query 단위: C 공개 함수 114개와 I/O/FSM/transport 멤버의 기능 대응 목록을 만들고 이벤트 코어의 state/local_role/leader/match_index/catch_up/transferee 조회를 공개했다. callback 노드도 같은 조회를 사용하며 leader_info 주소는 독립 소유 값이다. 새 3개가 Debug·엄격한 공유 Release·ASan/UBSan·core-only에서, 기존 영향 2개가 Debug에서 통과했다. C 1152사례·출력 보존·수명·노드 이동/종료와 외부 core-only 공유 소비자의 새 6개 export/C 미링크를 확인했다. 기존 143개·추가 matrix는 재사용했다. 실제 남은 경계로 C UV transport의 raw-stream listen/connect 인계 API를 확인했다. 이 경계와 실제 kernel profiler/raw 장치 검증, 이전 TCP timeout·참조 C fixture sanitizer 제한은 남아 있으며 전체 재작성은 진행 중이다.

이전 profiler 단위: 독립 kernel perf/trace profiler를 submit/disk 도구에 연결했다. 동적 tracepoint 형식·writable perf ring·유실/순환 검사·시간순 block/NVMe 대응·CPU별 FD/mmap 소유·mixed tracefs 상태 복원과 --perf/--trace/--tracefs를 구현했다. Debug·엄격한 공유 Release·ASan/UBSan의 새 6개와 기존 영향 4개가 통과했고 마지막 하이픈 이름 보완은 옵션 검사만 재검증했다. native profiler는 sandbox 안팎 모두 tracepoint 메타데이터에 접근할 수 없어 생략했으며 실제 kernel metric 통과로 기록하지 않는다. perf 헤더 OFF 최소 도구의 비지원 오류·trace-only CLI도 통과했다. 변경 없는 134개·추가 matrix/소비자는 재사용했다. 실제 kernel perf/trace·raw 장치 실행, 전체 API/호환 대조와 이전 TCP timeout/참조 C fixture sanitizer 제한은 남아 있으며 전체 재작성은 진행 중이다.

이전 disk/io_uring 단위: 독립 disk/io_uring benchmark와 파일·장치 정보 계층을 추가했다. O_DIRECT·fixed file/buffer·RWF_DSYNC·queue depth 1·워밍업·정확한 지연/MiB/s를 연결하고 링·버퍼·FD·임시 데이터의 RAII 정리와 시간 초과 시 accepted I/O drain을 구현했다. Debug·엄격한 공유 Release·ASan/UBSan의 새 5개와 영향받는 CLI 1개가 통과했다. sandbox의 native 생략/CLI TCP 거부와 승인된 실제 커널 실행의 통과를 구분해 보존한다. io_uring 헤더 OFF·tests/fixture/LZ4/예제 OFF의 최소 도구 빌드와 비지원 오류 계약도 통과했다. 기존 132개·추가 matrix/소비자는 변경이 없어 재사용했다. raw 장치 실행·kernel perf/trace와 전체 API 범위 대조·이전 TCP timeout/참조 C fixture sanitizer 제한은 남아 있으며 전체 재작성은 진행 중이다.

이전 submit 기반 단위: 표준 컨테이너·chrono 기반 정확한 지연 통계/JSON 보고서와 독립 native submit benchmark를 추가했다. RAFTPP_BUILD_BENCHMARKS는 기본 OFF·UV 필수이며 예제/tests/fixture/LZ4를 요구하지 않는다. 실제 단일 투표자 apply·writer trace·임시 데이터·종료와 stdout 실패를 연결했다. Debug·엄격한 공유 Release·ASan/UBSan의 새 4개와 최소 구성·UV 거부/타깃 제외·C 미링크 검사가 통과했다. 입력 범위와 stdout 보완 후에는 영향받는 options 또는 CLI만 다시 확인했다. 기존 129개·추가 matrix/소비자는 변경이 없어 생략하고 기존 결과를 재사용했다. disk/io_uring/perf/trace와 전체 API 범위 대조, 이전 TCP timeout/참조 C fixture sanitizer 제한은 남아 있으며 전체 재작성은 진행 중이다.

이전 예제 단위: 독립 C++ counter FSM·native 서버와 세 프로세스 클러스터 예제를 추가했다. 주기적 apply·스냅샷/복구·signal·최대 2초 인계 후 종료·무작위 자식 종료/재시작과 프로세스 회수를 연결하고 RAFTPP_BUILD_EXAMPLES(UV 필수, 기본 OFF)로 선택한다. Debug·엄격한 공유 Release·ASan/UBSan의 새 3개와 tests/fixture/LZ4 OFF 예제 빌드·도움말, 옵션 거부/제외 및 C 참조 미링크 검사가 통과했다. 최초 실제 재시작의 진단 판단 오류와 동시 종료의 인계 대기 문제를 예제에서 수정한 뒤 영향받는 cluster 1개만 다시 확인했다. 기존 126개·추가 matrix/소비자는 재사용했다. 전체 이식·API 범위 대조·benchmark 도구와 이전 TCP timeout/참조 C fixture sanitizer 제한은 남아 있다.

이전 legacy 호환 단위: legacy callback 호환 드라이버를 일반/`unit-test` 실행 모드로 연결하고 C의 파일명 barrier 정책을 C++ force_barrier 입력으로 전달했다. add/assign/remove의 검증 우선순위를 C와 맞췄다. Debug·엄격한 공유 Release·core-only에서 C 비교 6개와 C++ 전용 transfer 2개, Debug 기존 영향 2개가 통과했다. ASan/UBSan은 barrier/membership C 비교 4개와 C++ transfer 2개가 통과했지만, C 참조 fixture의 TIMEOUT_NOW 통계 배열 범위 초과로 C transfer 비교 2개는 실패했다. 실패는 별도 보존하며 C++ 통과로 대체하지 않는다. 기존 116개·추가 matrix/소비자는 재사용했다. 전체 이식·남은 API 대조·예제/도구와 이전 TCP timeout 조사는 계속 진행 중이다.

이전 TCP 진단 단위: TCP listener/resolve/connect/handshake/read/write의 네이티브 원인·peer ID/주소를 소유 진단으로 보존하고 선택적 transport 조회→UV→node에 연결했다. pending eviction과 쓰기 완료는 요청별 정보를 공개하고 취소·독립 실패는 이전 원인을 재사용하지 않는다. Debug 새 3개/기존 영향 12개, 엄격한 공유 Release·ASan/UBSan 새 3개와 외부 core-only/공유 소비자가 통과했다. 예외 문맥 보완 뒤 Debug는 backend 진단 1개만 다시 확인했다. 기존 103개·추가 matrix를 재사용했다. 전체 이식은 진행 중이며 호환 드라이버·예제/도구와 이전 TCP timeout 조사가 남아 있다.

이전 디스크 진단 단위: disk_storage의 metadata·스냅샷·segment·capability probe 실패를 호출별 소유 진단 출력으로 기록하고 prepare/finalize/truncate worker·파일 coordinator·UV·노드까지 전달했다. 전역/TLS/디스크 공용 오류 저장소 없이 네이티브 원인·경로와 최초 실패를 보존한다. Debug 새 4개/기존 영향 17개, 엄격한 공유 Release·ASan/UBSan 새 4개가 통과했다. 기존 94개·추가 matrix/소비자는 재사용했다. 전체 이식은 진행 중이며 TCP 세부 진단·호환·예제/도구와 이전 TCP timeout 조사가 남아 있다.

이전 native writer 단위: 초기화·즉시 제출·KAIO/poll·threadpool·종료 원인을 요청별 소유 진단으로 캡처하고 로그/파일 coordinator·UV 조회에 연결했다. FD·offset·결과/예상 바이트·segment 이름을 보존하며 스냅샷 콜백은 자기 진단을 우선한다. Debug 새 3개/기존 영향 9개, 엄격한 공유 Release·ASan/UBSan 새 3개가 통과했다. 마지막 callback/cancel 보완은 file_diagnostics만 구성별 재확인했다. 당시 기존 99개·추가 matrix/소비자를 재사용했다.

이전 오류 기반 단위: 표준 error_code/array/string_view 기반 소유 진단과 할당 없는 C 메시지 감싸기를 구현했다. C 문자열 23개 중 달랐던 2개를 고쳤으며 native 파일 핸들·디렉터리 stat의 POSIX 원인과 사용자 I/O 진단을 노드까지 보존한다. 새 4개·기존 영향 2개가 Debug에서, 새 4개가 엄격한 공유 Release·ASan/UBSan에서 통과했다. core-only 정적/공유 외부 소비자가 통과했으며 기존 102개·추가 matrix는 재사용했다. 전체 이식은 진행 중이고 native worker/TCP의 세부 원인 전파·일부 호환·예제/도구와 기존 TCP timeout 조사가 남아 있다.

이전 native 설정 단위: UV/TCP 공개 설정 API와 runtime 전파를 연결했다. segment 크기·디스크/연결 재시도·자동 복구·bind 주소·C 압축 no-op을 제공하며 제출된 prepare 할당 크기는 고정한다. 새 설정 3개와 영향받는 기존 로그 3개는 Debug에서, 새 3개는 엄격한 공유 Release 및 ASan/UBSan에서 통과했다. 외부 core-only/공유 소비자도 통과했다. 관련 코드가 같은 기존 98개·추가 matrix는 재사용했다. 열린 writer의 block 정렬 변경은 busy로 제한한다. 전체 이식은 진행 중이며 native 오류 문맥·일부 호환 동작·예제/도구와 이전 TCP timeout 조사가 남아 있다.

이전 공개 API 단위: 공개 이벤트 코어·timeout 조회·상태/역할 이름·난수·참조 버전·timeout 설정 helper를 연결했다. callback 노드 setter의 C legacy 150/15→450/45 ms 보정을 보존했다. 새 테스트 4개는 Debug/공유 Release에서, 소유권 관련 2개는 ASan/UBSan에서 통과했다. core-only 정적/UV 공유 외부 소비자도 통과했다. 변경하지 않은 97개 테스트와 추가 옵션 matrix는 기존 기록을 재사용했다. 전체 이식은 진행 중이며 UV runtime 설정·문맥 오류·호환/예제/도구가 남아 있다.

이전 fixture 단위: 독립 C++ fixture와 메모리 I/O·가상 시간·선거/분할/장애·안전성 검사를 구현했다. C fixture와 이벤트 380개 및 명령 적용이 일치했고 새 검사 7개가 Debug·엄격한 Release 공유·ASan/UBSan·UV 없는 구성에서 통과했다. C와 같은 fixture 종료 flush 순서와 직접 노드 종료 취소를 구분했다. 한·영 API 예제의 두 외부 소비자와 ON→OFF 제외 검증도 통과했다. 변경 없는 기존 90개 runtime은 재사용했으며 전체 재작성은 진행 중이다.

검증 제한: ASan/UBSan TCP resource 검사는 처음 대기 시간 초과가 있었고 원인은 미확정이다. 조건과 제한을 유지한 채 호출 위치·노드 상태 진단을 추가했으며, 이후 단독 1회·원인 조사 3회는 통과했다. 처음 실패와 후속 통과를 아래에 구분해 보존한다.

이전 native 컨테이너 단위: node·writer·log_writer·file_backend·uv_io·TCP의 native 요청 큐·배열·내부 주소를 resource 표준 컨테이너에 연결했다. splice allocator 동등성과 세 resource 교체 후 barrier 완료/취소를 포함한 Debug 영향 검사 41개·Release 공유 7개가 통과했다. ASan/UBSan은 14개 통과와 TCP의 최초 시간 초과·진단 후 별도 통과를 구분했다. 변경 없는 Debug 49개·전체 matrix·소비자는 재사용했으며 전체 재작성은 진행 중이다.

이전 코어 컨테이너 단위: 공통 allocator·표준 컨테이너 별칭을 `common.hpp`에 두고 로그·trail·코어 상태에 전파했다. resource 교체·확장·실패 출력 보존을 포함한 Debug 영향 검사 29개, Release 공유 6개·ASan/UBSan 6개와 core-only 외부 소비자가 통과했다. 변경 없는 Debug 60개·추가 구성·전체 옵션 matrix는 기존 결과를 재사용했다. native 요청 큐의 allocator 전파와 fixture 등 전체 재작성은 계속 진행 중이다.

이전 메모리 단위: 표준 PMR 기반 resource 선택·소유 allocator와 버퍼·객체 할당 연결을 구현했다. resource 변경 뒤에도 원래 allocator로 해제하고 마지막 weak 제어 블록까지 수명을 유지한다. 새 메모리 검사 6개, 영향받는 Debug 74개·Release 공유 9개·ASan/UBSan 16개가 통과했다. 앞선 같은 작업의 일치하는 통과 8개와 변경 없는 helper 6개는 재사용했다. 일반 컨테이너 backing·콜백 boxing은 기존 표준 allocator 경계로 남아 있으며 전체 이식은 진행 중이다.

이전 추적 단위: 독립 추적 API와 합의·노드·파일·TCP의 기본 진단을 구현했다. 표준 source location·공유 콜백 수명·수준·no-op/stderr·환경 변수 동작과 writer metric을 연결했다. 영향받는 Debug 50개, 새 기능 Release 공유 6개와 ASan/UBSan 17개가 통과했다. 추적 단위 3개는 앞선 같은 작업의 통과 결과를 재사용했다. 변경 없는 형식·저장 값 검사와 전체 옵션 matrix는 생략했다. API·검증 범위는 아래 추적 단위에 기록하며 전체 이식은 계속 진행 중이다.

이전 TCP 단위: 구체적인 `tcp_transport`를 독립 구현하고 공개 `uv_io`에 연결했다. C handshake·IPv4 주소 처리·6종 RPC 스트림·송신 큐·재시도·비동기 종료를 이식했다. 새 TCP 검사 10개가 Debug·엄격한 Release 공유·ASan/UBSan에서 범위별로 통과했다. 실제 디스크·TCP 3노드에서 선거·명령·리더십 이전·스냅샷 따라잡기·전체 재시작을 검증했다. 기존 66개 테스트는 관련 실행 경로가 바뀌지 않아 기존 결과를 재사용하고 생략했다. 전체 라이브러리 이식은 계속 진행 중이다.

이전 공개 I/O 단위: 스냅샷 준비 재시도·barrier 이후 공개·보존·조회와 종료를 native 파일 coordinator에 구현했다. 공개 `uv_io`와 `transport` 경계를 추가했다. C native 파일 비교, 수명·실패·할당 검사, 실제 디스크 노드 재시작과 실제 디스크·테스트 메시지 전송을 쓰는 3노드 분할·스냅샷 따라잡기를 포함한 새 테스트 10개가 통과했다. 기본 및 ASan/UBSan 전체 테스트 66개와 외부 소비자 3개 구성이 통과했다. 환경 제한으로 누수 검사는 제외했다. 옵션별 실행·재사용 범위는 아래 기준 표에 기록했으며 중복 전체 재실행은 사용자 요청에 따라 중단했다. TCP 소켓은 남은 범위다.

이전 로그 쓰기 단위: capability probe·정렬 배치 버퍼·두 파일 prepare pool·append/finalize·blocking/nonblocking barrier와 비동기 truncate coordinator를 구현·검증했다. C의 실제 native backend와 닫힌 segment 이름·바이트 및 capability 결과가 일치했다. 기본 및 ASan/UBSan 테스트 56개, 기능 옵션 6개 조합(UV 활성 56개·비활성 34개), 외부 소비자 3개 구성이 통과했다. 환경 제한으로 누수 검사는 제외했다. 상위 스냅샷 조정·전체 공개 I/O backend·네트워크·tracing·fixture 등은 남아 있다.

이전 writer 단위: direct I/O·KAIO·threadpool 비동기 writer와 요청·버퍼 수명 관리를 구현·검증했다. 기본·ASan/UBSan 테스트 48개, 기능 옵션 6개 조합(UV 활성 48개·비활성 34개), core 전용 공유·UV/LZ4 공유·정적 외부 소비자가 통과했다. 환경 제한으로 누수 검사는 제외했다. 마지막 누적 길이·offset 경계 보완 후 기본 48개와 Release/ASan/UBSan writer 5개를 다시 검증했다. 당시 prepare pool·append·barrier·파일시스템 capability probe·네트워크 연결은 남아 있었다.

이전 segment 단위: 동기 segment 파일 생성·읽기·open 복구·잘라내기·보존과 전체 저장 상태 로드를 구현·검증했다. 기본 및 ASan/UBSan 테스트 43개, 기능 옵션 6개 조합(UV 활성 43개·비활성 34개), 외부 소비자의 core 전용 공유·UV/LZ4 공유·UV/LZ4 정적 구성이 통과했다. 환경 제한으로 누수 검사는 제외했다. 비동기 append·prepare pool·barrier·direct I/O는 다음 범위다.

할당 실패 주입으로 libstdc++ `directory_iterator`의 내부 예외 종료 경로를 확인하여 디렉터리 스캔을 기존 의존성인 libuv의 RAII 요청으로 변경했다. 경로·문자열·컨테이너·정렬은 표준 구현을 유지한다.

기반 구성요소와 메모리 로그·스냅샷을 내부 이벤트 구동 합의 코어에 연결했다. 메모리상 클러스터에서 선거·복제·commit·멤버십·리더십 이전·분할·복구를 C와 비교한다. 공개 callback 노드 API와 I/O·FSM 인터페이스를 구현했으며 모의 backend로 검증한다. RPC·디스크 배치 직렬화와 체크섬을 독립 구현하고 형식 비교·검증을 마쳤다. 메타데이터·스냅샷·segment의 동기 파일 계층과 저장 상태 복구, direct I/O·KAIO writer와 로그 쓰기 coordinator도 구현했다. 테스트용 I/O 어댑터로 실제 디스크를 사용하는 단일 노드의 종료·재시작을 검사했다. 스냅샷·파일 coordinator와 공개 `uv_io`는 실제 저장·타이머를 추상 메시지 전송에 연결한다. 구체적인 `tcp_transport`를 `uv_io`와 노드에 연결했다. 실제 디스크·TCP 노드를 구성할 수 있다. 기본 추적과 메모리 resource의 버퍼·객체·코어/native 컨테이너 연결을 구현했다. 독립 공개 C++ 가상 시간 fixture로 메모리 I/O와 클러스터 제어를 제공한다. native 예제와 submit/disk benchmark·kernel perf/trace를 구현했다. 전체 API·호환 범위의 현재 판정은 아래 최종 감사 표를 따르며 실제 커널 측정/장치 실행의 제한은 별도로 유지한다.

기존 C 구현은 `raft/`에 비교 기준으로 보존한다. `raftpp::raft`는 기존 C 라이브러리, `raftpp::raftpp`는 독립적으로 구현한 C++ 구성요소를 제공한다. 기존 C 타깃이 빌드되거나 기능 플래그가 활성화되었다는 사실은 C++ 계층 전체의 이식 완료를 의미하지 않는다.

### README 진입 안내 (2026-11-04)

루트에 `README.md`/`README.en.md`를 추가했다. 프로젝트 소개·필수 core/선택 모듈 11개·core 최소 빌드·SQLite/Tcl SDK 준비·사용자 선택형 `make_io` 예제·설치/CMake 소비·UV 카운터 실행·Release 테스트 명령·소스 구조·API/작업 지침 링크·기존 라이선스 식별자를 같은 내용으로 제공한다. DB 호출의 블로킹/인라인 완료, 직렬 실행 영역, 종료까지 loop 유지, QUIC connector 책임도 설명했다. 성공/진행률/테스트 이력을 README/API 문서에 중복 기록하지 않았다.

참조 범위는 루트/deps CMake 옵션·설치/소비 규칙, 공개 composed_io/storage/SQLite API와 소유·콜백 계약, 기존 composed 예제 CLI/타깃과 관련 한·영 API 설명서 및 LICENSE다. 실제 수정은 README 2개, 루트 지침의 대응 문서 목록 2개, API 모듈 목록의 오래된 선택 모듈 수/DB 조립 설명 2개, 진행 기록 2개뿐이다. 제품·테스트·빌드 규칙·SDK·생성 헤더는 변경하지 않았다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 문서 대응·상대 링크 | 실행 | 총 35쌍·상대 링크 786개 오류 없음. `/tmp/raftpp-readme-validation.json` | README 쌍/상호 링크와 한·영 API/루트 문서의 링크 대상 존재를 확인 |
| README 코드/옵션·API 대응 | 실행 | 7개 한·영 코드 블록 일치, 각 언어에 모듈 링크 11개. 같은 JSON 보고서 | 선언·옵션명·CLI·타깃·경로와 기존 구현을 대조. 신규 문서 예제의 컴파일/실행 결과는 아님 |
| 문서 공백 | 실행 | `git diff --check` 통과 | 새 README와 수정한 문서/지침/기록만 검사 |
| 제품 빌드·Release 어댑터·ASan/UBSan·설치 소비자 | 생략 — 기존 결과 재사용 | 앞선 `RocksDB/SQLite 영속 저장소 모듈`과 `사용자 선택형 I/O 조립`의 해당 검사·제한 | 관련 구현/테스트/공유 헤더/생성 코드/빌드 옵션/SDK/도구·실행 환경에 변경 없음. 과거 통과·최초 실패 기록을 유지하고 이번 통과 수에 합산하지 않음 |

`python3 /tmp/raftpp-check-readme.py`와 `git diff --check`를 실행했다. 문서만 변경했으므로 새 예제 실행이나 추가 옵션 matrix 검증은 생략했다. 기존 네이티브 SDK/하드웨어/LSan 제한은 앞선 단위의 기록을 따른다.

### RocksDB/SQLite 영속 저장소 모듈 (2026-11-04)

`deps/rocksdb`, `deps/sqlite`, `deps/tcl`을 고정된 submodule로 추가했다. RocksDB v11.13.2는 `17b624b5c0d41bd42db5cf88aa867f3b26eaf1fa`, SQLite 3.54.1은 `57520227c95396ce39ba6ecaac8d475b78dd3964`, Tcl 8.6.17은 `433df5b20a8bd6957c3bda981c83d65202eb7de7`이며 gitlink/`deps/revisions.json`/checkout이 일치하고 작업 트리가 깨끗하다. 기존 SDK pin은 유지했다. SQLite canonical 소스의 amalgamation 생성에 필요한 Tcl은 빌드 도구일 뿐 런타임 의존성이 아니다. 두 DB의 외부 필수 런타임 경계는 플랫폼 thread 지원이다.

`RAFTPP_MODULE_ROCKSDB`/`RAFTPP_MODULE_SQLITE`(기본 OFF), `raftpp::rocksdb`/`raftpp::sqlite`, 공개 헤더·export·features·엄브렐라·선택 설치/소비자를 연결했다. `rocksdb_storage`/`sqlite_storage`는 선언/링크 smoke가 아니라 core `storage` 전체를 실제 구현한다. 사용자는 `make_io`에 타이머·transport·선택 snapshot codec과 함께 전달한다. UV 의존성은 없다. core에는 `storage_limits`와 SDK를 포함하지 않는 공용 비공개 KV 저장 알고리즘을 두고 각 모듈에서 독립적으로 사용한다. 기존 합의 알고리즘/RPC/UV 파일 형식은 유지하며 새 DB 전용 v1 키 공간·메타데이터/로그/snapshot 직렬화는 API 문서에 명시했다. 기존 UV 파일 디렉터리의 자동 이관은 제공하지 않는다.

term/vote·bootstrap/recover·로그 append/truncate·snapshot 게시/설치·trailing 보존과 재시작 load/get을 구현했다. RocksDB는 WAL을 활성화한 동기 `WriteBatch`; SQLite는 FULL 동기화·DELETE journal·EXCLUSIVE 연결/트랜잭션과 rollback을 사용한다. 같은 snapshot index/term을 재게시해도 이후 로그를 보존하며 같은 index의 term 충돌은 거부한다. 원자적 갱신 뒤에만 cache를 반영하고 크기/개수/인덱스·형식/길이·로그 연속성과 출력 보존을 검사한다. DB 호출·커밋은 호출 스레드를 블로킹하며 성공 완료는 인라인이다. 단일 직렬 실행 영역, 콜백/요청 데이터 수명, 콜백 안 facade 해제·close와 native 자원 해제를 보장한다. 물리 쓰기/동기화 실패로 영속 결과가 불확실하면 후속 연산 전 close/reopen해야 한다.

새 DB 노드 검사에서 시작 설정의 snapshot 인라인 완료가 코어 이벤트 처리보다 앞서 같은 인덱스의 snapshot을 중복 제출하는 기존 드라이버 문제를 발견했다. 두 DB 모두 `node leader` 실패와 core `invalid` 종료를 보였고, DB 없는 메모리 저장소의 기존 `composed_io_node`에 threshold 1/trailing 0을 지정해 독립적으로 재현했다. `node.cpp`에 성공적으로 저장된 로컬 snapshot index를 기록해 대기 중인 완료 이벤트보다 먼저 중복 제출하지 않도록 했다. 즉시 제출 실패는 이 index를 갱신하지 않아 재시도할 수 있다. 합의 상태 전이 자체는 변경하지 않았다.

참조 범위는 storage/io/composed_io/node의 입력·출력·메타데이터/로그/snapshot 순서·실패/콜백/정리, 기존 configuration/encoding/byte/buffer/error·메모리 모델, SDK의 실제 사용 DB/iterator/write-batch/statement/transaction API와 원본 라이선스, 의존성 준비/모듈/생성 헤더/설치/소비자 규칙이다. 실제 수정은 새 SDK/저장 모듈·공용 비공개 알고리즘·core limits/인라인 snapshot guard·관련 테스트/빌드와 한·영 API/기록이다. `docs/{ko,en}/modules/{rocksdb,sqlite}.md` 및 composition/dependencies/node/CMake/목록에는 사용 계약만 기록했다.

환경: Linux x86-64, GCC 13.3.0, CMake 3.28.3, Ninja. SDK 준비는 `/tmp/raftpp-database-sdk`, prefix는 그 아래 `prefix`다. RocksDB/SQLite는 정적 PIC SDK로 준비했고 Tcl은 source-generation 도구만 사용한다. RocksDB의 tests/examples/tools/benchmarks·gflags/liburing/선택 압축, SQLite의 CLI/Tcl 바인딩/readline/확장 로드·선택 확장/수학 함수를 제외했다. SQLite configure의 zlib 감지는 상류 shell용이며 빌드한 DB archive에 zlib/Tcl 참조가 없음을 확인했다. 상류 전체 suite는 실행하지 않았다. 모든 어댑터/설치 소비자 검증은 Release다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 필수 SDK 준비·pin·라이선스 | 실행 | `/tmp/raftpp-database-sdk-{configure,build-first}.log`에서 RocksDB/SQLite/Tcl 준비 완료. Tcl `info patchlevel`=8.6.17. 세 gitlink/JSON/checkout 일치·소스 무변경. SDK 및 설치본의 RocksDB LICENSE.Apache/LICENSE.leveldb/COPYING·SQLite LICENSE.md, Tcl 도구 license.terms 원문 보존 | SDK를 독립 빌드하며 필요한 라이브러리/생성 도구만 준비. SQLite prebuilt 재사용 시 Tcl을 새로 요구하지 않는 선택 경로도 구현; 그 prebuilt 경로의 별도 실행은 미검증 |
| 인라인 snapshot 기존 문제 재현 | 실행 — 수정 전 실패 | `/tmp/raftpp-database-static-tests-first.log`, `*-diagnostic-tests.log`, `*-trace-tests.log`; DB 없는 독립 재현 `/tmp/raftpp-database-inline-core-repro-{build,test}.log`에서 `composed node leader` 실패 | 최초 DB 실패를 테스트 조작으로 숨기지 않고 실제 core 중복 snapshot 이벤트를 재현·수정. 최초 실패 로그는 통과로 재사용하지 않음 |
| 정적 Release DB/노드 회귀 | 실행 | `/tmp/raftpp-database-static-inline-tests.log`: 8개 통과. `module_rocksdb`, `module_sqlite`, 수정 `composed_io_node`, 기존 `node_{snapshot,snapshot_allocation,restore,io_close,fsm_close}` | 두 DB ON, UV/LZ4/fixture/backtrace 및 나머지 SDK OFF, `-Wall -Wextra -Wpedantic -Werror`. 새 DB의 bootstrap/recover·term/vote·원자적 실패·불일치 snapshot 설치·trailing/재게시·크기/개수 한도·손상 metadata·배타 소유·재진입 파괴·실제 node snapshot/FSM 재시작을 검사 |
| 공유 Release DB/노드 회귀 | 실행 | `/tmp/raftpp-database-shared-tests.log`: 같은 범위 8개 통과 | 같은 옵션/SDK와 `BUILD_SHARED_LIBS=ON`. 정적 PIC SDK를 공유 어댑터에서 소비 |
| DB 강제 종료 복구·SQLite rollback | 실행 | 위 두 DB 검사 안에서 각각 통과. 자식이 term/vote/append/snapshot 커밋을 알린 직후 SIGKILL하고 다시 열어 확인. SQLite는 배치 두 번째 INSERT에서 trigger로 실패시켜 첫 INSERT도 rollback됨을 확인 | 정상 close가 없는 소프트웨어 프로세스 종료와 실제 SQLite 트랜잭션 실패를 검사. 하드웨어 전원 차단 검사는 아님. 손상 주입은 SDK의 최소 DB API만 사용 |
| Release ASan/UBSan | 실행 | `/tmp/raftpp-database-sanitizer-tests.log`: 위 8개 통과. `/tmp/raftpp-database-sanitizer-build.log` | 두 DB만 ON/공유, UV/LZ4/fixture/backtrace OFF. `-fsanitize=address,undefined -fno-omit-frame-pointer`, `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`. 외부 SDK는 비계측, LSan 제외 |
| 이동 설치 소비자·헤더/export | 실행 | `package_install` 정적/공유 각각 통과, 각 내부 소비자 7개(aggregate/core/두 DB, 격리 core/두 DB). `/tmp/raftpp-database-{static,shared}-package.log`. 비공개 `database_storage.hpp` 미설치, 활성 헤더/export와 원본 라이선스 일치도 확인 | 새 공개 API/SDK 링크와 독립 `find_package` 소비. 최초 정적 소비자 기록은 OFF 재구성 전 `.../tests/package-install/Release-db/`로 보존했고 공유는 `Release/` 유지 |
| 같은 빌드에서 두 DB OFF | 실행 | `/tmp/raftpp-database-off-{configure,build,tests}.log`: core-only `composed_io_node`/`package_install` 2개와 내부 소비자 3개 통과. `*-off-headers.log`: 두 SDK 헤더/타깃 제거, 매크로 0 확인 | RocksDB/SQLite3/Threads/PkgConfig 감지를 강제로 비활성화해 선택 의존성 없는 core 빌드·소비를 확인. UV/LZ4/모든 SDK OFF. 원래 두 DB ON 옵션은 configure로 복구했으며 복구만을 이유로 재실행하지 않음 |
| 기존 UV 비동기 snapshot 조합 | 실행 | `/tmp/raftpp-database-uv-regression-tests.log`: `composition_uv`, `composition_zstd` 2개 통과 | 기존 `/tmp/raftpp-composition-native`의 공유 Release·UV/LZ4·Asio/3 codec ON(두 DB OFF). node snapshot guard 변경이 기존 비동기 저장/UV timer 및 Asio timer+Zstd의 실제 재시작을 유지하는지 확인 |
| 최초 준비/빌드 실패·수정 | 실행 기록 보존 | 정적 `*-configure.log`, `*-build-first.log`, `*-build-fixed.log`, `*-build-initialized.log`, `*-build-ready.log`; 성공 `*-build-final.log`, `*-inline-build.log` | 최초 git submodule의 tag/branch 선택은 tag fetch/detach로 정정. CMake imported target의 디렉터리 가시성, 고정 extent span 생성, RocksDB 11.13.2의 unique_ptr Open API, 테스트 helper 이름 충돌을 수정했다. GCC optional 수명 경고는 경고를 끄지 않고 concrete snapshot+콜백용 optional 구성으로 정리. 그 과정의 초기 치환 컴파일 오류도 보존 |
| API 언어/링크·C++ 포맷 | 실행 | 문서 34쌍·상대 링크 732개 오류 없음·모듈 코드/서명 39블록 대응. `/tmp/raftpp-database-docs-validation.json`; 변경/추가 C++ 11파일 clang-format 검사와 `git diff --check` 통과 | 두 언어의 API/한도/오류/소유·블로킹/인라인 계약, 옵션·예제를 소스와 대조. 문서의 모든 코드를 실행한 결과는 아님 |
| 변경 없는 검사·추가 matrix | 생략 — 기존 결과 재사용 | 앞선 `사용자 선택형 I/O 조립`/`core 추상 인터페이스의 실제 어댑터`와 기능별 기록의 관련 결과 | 합의/직렬화 알고리즘·기존 SDK 구현/checkout은 그대로다. node snapshot 영향은 위 회귀로 한정해 재검증했다. 전체 suite/상류 suite/무관한 SDK 검사와 추가 fixture/backtrace/UV matrix는 반복하지 않음 |

SDK 명령: `cmake -S deps -B /tmp/raftpp-database-sdk -G Ninja '-DRAFTPP_DEPENDENCIES=rocksdb;sqlite' -DRAFTPP_DEPS_SHARED=OFF -DRAFTPP_DEPS_JOBS=3`, `cmake --build /tmp/raftpp-database-sdk --parallel 2`. 제품은 각 `/tmp/raftpp-database-{static,shared,sanitizer}`에서 `cmake -S . -B <profile> -G Ninja -DCMAKE_BUILD_TYPE=Release -DRAFTPP_BUILD_TESTS=ON -DRAFTPP_MODULE_ROCKSDB=ON -DRAFTPP_MODULE_SQLITE=ON -DRAFTPP_WITH_UV=OFF -DRAFTPP_WITH_LZ4=OFF -DRAFTPP_WITH_FIXTURE=OFF -DRAFTPP_WITH_BACKTRACE=OFF -DRAFTPP_DEPENDENCY_PREFIX=/tmp/raftpp-database-sdk/prefix`에 위 링크/컴파일 옵션을 지정했다. 빌드는 `cmake --build <profile> --parallel 3 --target raftpp_module_rocksdb_check raftpp_module_sqlite_check raftpp_composed_io_tests raftpp_node_tests`; 선택 CTest regex는 `^raftpp\.(module_(rocksdb|sqlite)|composed_io_node|node_(snapshot|snapshot_allocation|restore|io_close|fsm_close))$`다. 설치는 `^raftpp\.package_install$`, 기존 UV는 `^raftpp\.composition_(uv|zstd)$`로 `ctest --test-dir <profile> -C Release -R '<regex>' --output-on-failure`를 사용했다. 문서 검사 스크립트의 모듈 목록/보고서 경로만 확장해 과거 최초 JSON은 유지했다.

현재 구현·위 범위 검증을 완료했다. 남은 제한은 DB 작업의 호출 스레드 블로킹, 자동 UV 파일 이관/백그라운드 실행기 미제공, Windows/교차 컴파일·공유 SDK 준비 조합·장치 전원 차단/모든 파일시스템·LSan 미검증이다. 이전 단위의 “내장 영속 저장소는 UV뿐”이라는 상태는 당시 기록이며 현재는 UV/RocksDB/SQLite를 선택할 수 있다.

### 사용자 선택형 I/O 조립 (2026-11-04)

core에 `storage`, `timer`, `io_components`와 `make_io()`를 추가했다. 사용자가 필수 저장소·타이머·transport와 선택 `compression_codec`을 직접 조합하면 기존 `io`/`node`의 요청·완료 콜백 API로 사용한다. 특정 SDK 조합이나 코루틴/future API를 강제하지 않는다. core는 선택 SDK 없이 빌드된다. `executor`는 기존 콜백 제출 경계이며 저장소/타이머로 자동 변환하지 않는다.

UV에는 기존 디스크 조정 계층을 재사용하는 `uv_storage`와 독립 `uv_timer`, Asio에는 `asio_timer`를 추가했다. `uv_storage`의 비공개 storage-only 경로는 network와 노드 tick 타이머를 만들지 않으며 기존 파일 동기화·retry·barrier·정리 정책을 유지한다. 공개 `uv_io` 생성자/동작은 유지했다. 내장 영속 저장소는 여전히 UV뿐이다. UV 없는 영속화에는 사용자 `storage` 구현이 필요하며, Asio/QUIC 어댑터에 파일 백엔드를 구현했다고 기록하지 않는다.

조립기는 부품의 init/start/close 수명을 독점 관리하고 수락한 요청·배치·메시지·원본/인코딩 snapshot을 완료까지 유지한다. 동기/인라인 완료, 제출 실패, 예외/OOM, 콜백 재진입, facade 해제와 부분 초기화 실패를 처리한다. 종료는 모든 부품·요청·활성 콜백 정리와 보유 payload 해제 후 알린다. 모든 부품과 노드/FSM 호출은 같은 직렬 실행 영역에서 실행해야 하며 자동 스레드/loop bridge를 제공하지 않는다.

선택 codec은 snapshot payload만 v1 envelope(magic 8바이트, little-endian encoding/원문 길이 각 8바이트)로 저장하고 읽을 때 해제한다. codec 없는 일반 원문은 그대로 유지하며 예약 magic과 겹치는 새 원문은 escape한다. 원문/인코딩 상한, 잘린 입력·미지원 encoding·길이 불일치·코덱 누락·실패 시 출력 보존을 검사한다. RPC/로그/메타데이터 형식과 기존 `uv_io`의 원문/LZ4 경로는 변경하지 않았다. 새 envelope는 opt-in 저장 경로이며 codec 식별자/사전을 저장하지 않는다. 같은 codec 설정으로 `make_io`를 사용해 다시 열어야 하고 legacy `uv_io`는 이를 해제하지 않는다. 예약 magic으로 시작하는 이전 원문은 별도 변환이 필요하다.

참조 범위는 core I/O·node/FSM·transport/framing·snapshot/codec·buffer/error/random의 입력/출력·소유/정리 경로, UV 파일 조정·load/close·네이티브 timer, Asio executor/stream/timer와 사용 SDK API, 모듈/생성 헤더/설치/예제/테스트 규칙이다. 실제 수정은 조립 API·구현, UV 저장소 facade/두 timer, 관련 빌드·테스트/소비자·예제와 한·영 API/기록이다. 합의 알고리즘과 기존 디스크 계층을 재작성하지 않았다. `docs/{ko,en}/composition.md` 및 관련 모듈 문서는 사용 계약만 설명한다. 새 `raftpp-example-composed DIRECTORY [none|lz4|zstd|zlib] [uv|asio]`는 사용자가 고른 codec/timer로 snapshot 게시/종료와 재시작 복원을 보여준다.

환경과 재사용 조건: Linux x86-64, GCC 13.3.0, CMake 3.28.3, libuv 1.48.0, 기존 SDK prefix `/tmp/raftpp-sdk-build/prefix`와 상류 checkout을 사용했다. 모든 어댑터·설치 소비자·예제 검증은 Release다. 일반 구성은 `-Wall -Wextra -Wpedantic -Werror`; fixture/backtrace는 OFF다. 다음 프로필별 실행/생략을 구분하며 아래 개수는 전체 suite 통과 수가 아니다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| core-only 조립 계약 | 실행 | `/tmp/raftpp-composition-core`: 서로 다른 7개 `composed_io_{contracts,failures,reentrancy,snapshots,limits,allocations,node}` 통과. 최초 6개 `/tmp/raftpp-composition-core-tests-first.log`, 추가 limits/소비자 `/tmp/raftpp-composition-core-limits-package.log`, 0바이트 codec frame 추가 후 limits `/tmp/raftpp-composition-core-empty-frame-tests.log` | UV/LZ4/모든 SDK OFF, 정적 Release. 요청/콜백/close 순서·수명·상태/출력 보존·할당 실패·singleton node. 마지막 빈 frame 복사 guard의 추가 영향은 limits로 재검증하고 해당 경로를 사용하지 않는 최초 6개 결과는 재사용 |
| UV 없는 Asio 조립 | 실행 | `/tmp/raftpp-composition-asio`: `composition_timer`, `composition_cluster` 2개 통과. `/tmp/raftpp-composition-asio-tests.log` | Asio만 ON, UV/LZ4 OFF, 정적 Release. 실제 TCP stream+framed_transport+Asio timer+독립 메모리 저장소 모델의 3노드 선출·복제·종료. 내구성 파일 검사가 아님 |
| 공유 native 조합·기존 UV 회귀 | 실행 | `/tmp/raftpp-composition-native`: 새 `composition_{timer,cluster,uv,lz4,zstd,zlib}` 6개, 수정 `module_asio` 1개, 기존 UV 9개가 각각 통과. 최초 12개 통과 기록 `/tmp/raftpp-composition-native-tests-first.log`, 수정 후 영속 조합 4개 `/tmp/raftpp-composition-native-aligned-tests.log` | UV/LZ4 및 Asio/LZ4/Zstd/zlib 모듈 ON, 나머지 SDK OFF, 공유 Release. UV 저장소+UV timer/무압축 및 UV 저장소+Asio timer/각 codec의 실제 snapshot 재시작. 기존 9개는 `uv_io_{lifetime,node,limits,allocation,cluster,settings}`, `snapshot_files`, `compressed_snapshot`, `storage_load_contracts` |
| ASan/UBSan 조립 수명 | 실행 | `/tmp/raftpp-adapters-sanitizer`: 서로 다른 13개(core 7+조합 6) 범위별 통과. 최초 core 7/timer/TCP 2 통과는 `/tmp/raftpp-composition-sanitizer-tests.log`; 영속 조합 4개와 변경된 limits는 `/tmp/raftpp-composition-sanitizer-aligned-tests.log` | 공유 Release, 모든 SDK/UV/LZ4 ON이지만 해당 타깃은 core/Asio/UV/3 codec만 연결. `-fsanitize=address,undefined -fno-omit-frame-pointer`, `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`. LSan 제외, 외부 SDK 바이너리는 비계측. 최초 실패 4개를 통과로 재사용하지 않음 |
| 이동한 설치본 소비자 | 실행 | `package_install`이 core-only 정적/Asio-only 정적/native 공유의 세 프로필에서 각각 통과. 내부 소비자 각 3/5/9개 통과. `/tmp/raftpp-composition-core-limits-package.log`, `/tmp/raftpp-composition-asio-package.log`, `/tmp/raftpp-composition-native-package.log` | aggregate/core/UV/Asio/활성 codec 및 격리 core/Asio 소비. 새 factory·timer/storage 헤더/export와 SDK 없는 선택 소비 검증. 각 `/tmp/raftpp-composition-*/tests/package-install/Release/*/Testing/Temporary/LastTest.log` 보존 |
| 새 예제와 선택 기능 OFF | 실행 | Zstd+Asio fresh/restart에서 `value=1`, `value=2`: `/tmp/raftpp-composition-example-roundtrip.log`. 같은 native 빌드에서 SDK 7개/UV LZ4 OFF, UV ON으로 예제 재빌드 후 none+UV fresh/restart 1→2; 비활성 Zstd/Asio 선택은 exit 1. `/tmp/raftpp-composition-minimal-example.log` | 새 예제의 실제 callback·snapshot 완료와 재시작, 선택 헤더/링크 분기. 원래 native 옵션은 configure로 복구했으며 복구 자체로 검사를 반복하지 않음 |
| 최초 실패·수정 경위 | 실행 기록 보존 | `/tmp/raftpp-composition-core-tests-build*.log`, native `*-build-first.log`, `*-build-fixed.log`, `*-tests-first.log`, `*-persistence-tests.log`, `*-ready-capacity-tests.log` 및 sanitizer 대응 로그 | 테스트의 entry 타입/enum 오류, SDK의 없는 `cancel(error_code&)` 사용, 누락 config include에 따른 unknown suite를 수정. 영속 테스트 최초 15바이트 명령은 UV 로그의 8바이트 배수 조건을 어겨 invalid; readiness만으로 해결되지 않음을 확인하고 16바이트로 수정. 최초 실패 시 테스트 정리의 abort/SegFault도 보존하며 RAII loop 정리/공유 callback 상태로 수정. 이를 제품 회귀/통과로 재분류하지 않음 |
| 한·영 API/링크/포맷 | 실행 | 문서 32쌍, 상대 링크 694개 오류 없음; 모듈 코드/서명 35블록과 조립 문서 코드 3블록 일치(번역 주석 제외). `/tmp/raftpp-composition-docs-validation.json`. 수정/추가 C++ 22파일의 clang-format 검사와 `git diff --check` 통과 | 새 composition 문서/모듈 계약/탐색 및 실제 코드와 비교. 새 예제 실행을 모든 문서 코드 블록 실행으로 보고하지 않음 |
| 변경 없는 SDK/전체 suite/추가 matrix | 생략 — 기존 결과 재사용 | 앞선 `core 추상 인터페이스의 실제 어댑터 (2026-11-03–04)`의 framed_transport·asyncpp/mvfst/msquic/codec 고유 계약·소비 결과와 이전 기능별 기록 | 해당 구현/SDK checkout/직접 검증 입력은 변경 없음. core에 추가한 조립 경로는 위 새 검사로 검증; 기존 직렬화/상태 전이 전체를 다시 실행하지 않음. 일반 codec 자체/QUIC TLS·연결 수립/상류 전체 suite도 범위를 넓히지 않음 |

실행 명령은 각 프로필의 `cmake -S . -B <profile> -DCMAKE_BUILD_TYPE=Release -DRAFTPP_BUILD_TESTS=ON`과 위 옵션, `cmake --build <profile> --parallel 4 --target raftpp_composed_io_tests raftpp_composition_module_tests`(존재하는 타깃만), `ctest --test-dir <profile> -C Release -R '<해당 검사 regex>' --output-on-failure`다. 영속 조합 최종 regex는 `^raftpp\.composition_(uv|lz4|zstd|zlib)$`, 설치는 `^raftpp\.package_install$`이다. sanitizer는 같은 CTest에 위 환경 변수를 지정했다. 예제는 `cmake --build /tmp/raftpp-composition-native --parallel 4 --target raftpp_example_composed` 후 임시 디렉터리에서 두 번 실행했다. 문서 검사 스크립트는 기존 `/tmp/raftpp-check-module-docs.py`의 보고서 경로를 이번 JSON으로 분리해 실행해 과거 최초 결과를 보존했다.

남은 경계: UV 외 내장 파일 저장소·자동 실행 영역 bridge·QUIC 연결/TLS 수립은 추가하지 않았다. 실제 커널/장치·전체 파일시스템·32비트·LSan 검증은 기존 제한을 유지한다. 사용자 선택형 조립과 위 부품은 구현·해당 범위 검증을 마쳤으며, 이 사실을 모든 SDK의 독립 영속 백엔드 제공으로 확대하지 않는다.

### 모듈별 API 설명서 (2026-11-04)

`docs/ko/modules/`와 `docs/en/modules/`에 같은 이름의 `index.md` 및 core/uv/asyncpp/asio/mvfst/msquic/lz4/zstd/zlib 문서를 추가했다. 모듈별 활성화 옵션·설치 컴포넌트·타깃·헤더, 공개 메서드와 인자/반환, 생성 예외·오류 코드·진단, 소유권 이전·실행 영역·완료/종료 콜백 계약과 사용 예제를 설명한다. core/uv 페이지는 기존 기능별 API 문서를 연결하며 새 경계와 UV 압축 보조 함수도 설명한다. API 설명서에 작업 상태·실행 이력은 추가하지 않았다.

참조 범위는 일곱 어댑터의 공개 헤더·구현·CMake, core 스트림/transport/실행기/코덱·framed_transport·buffer/error/common/random/version과 호출 관계, UV 공개 헤더·압축 구현/빌드, 관련 기존 API 문서와 asyncpp dispatcher 계약 및 기존 계약 검사 예제다. 실제 수정은 한·영 API 문서와 루트 진행 기록뿐이다. 공통 스트림 설명의 close observer 정책을 UV의 복수 observer와 다른 어댑터의 단일 observer로 구분하고, Asio의 실패 시 소켓 소비와 UV/QUIC의 성공 시 native 인계를 명확히 했다. UV 첫 LZ4 프레임 decoder와 엄격한 단일 프레임 코덱의 후행 바이트/allocator/오류 차이를 원본 구현에 맞춰 설명했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 모듈별 한·영 대응·상대 링크 | 실행 | 문서 전체 31쌍, 상대 링크 638개 오류 없음. 새 모듈 9개와 목록 각 언어 10페이지. `/tmp/raftpp-module-api-docs-validation.json` | 새 페이지와 기존 CMake/의존성/기능별 문서의 탐색 링크 |
| 새 문서 공개 API·코드 블록 대응 | 실행 | SDK 헤더의 공개 클래스/함수 이름 누락 없음, 새 한·영 코드/서명 블록 32개 동일. 같은 JSON에 기록 | native 소유/콜백·종료 경로와 기능 옵션을 실제 소스에서 대조. 문서의 코드 블록을 빌드·실행한 결과는 아님 |
| 문서 공백 검사 | 실행 | `git diff --check` 통과 | 문서와 진행 기록의 whitespace |
| 빌드·Release 어댑터·ASan/UBSan·UV 회귀·설치 소비자 | 생략 — 기존 결과 재사용 | 바로 앞 `core 추상 인터페이스의 실제 어댑터 구현 (2026-11-03~04)`의 각 검사/제한 | 제품·테스트·생성 헤더·빌드 규칙·의존성/옵션을 변경하지 않음. 최초 실패·중단 기록도 그대로 보존 |
| 새 문서 예제 컴파일·실행 | 생략 | 헤더/서명 대조와 한·영 코드 동일성만 확인 | 문서만 변경하여 빌드/실행은 생략. 기존 runtime 통과를 새 예제 실행 결과로 간주하지 않음 |

검사는 `/tmp/raftpp-check-module-docs.py`의 `python3` 실행과 `git diff --check`로 수행했다. API 진입점은 `docs/ko/modules/index.md`와 영어 대응본이다. 원래 구현/QUIC 연결·TLS·LSan 등 검증 제한은 앞 단위와 동일하다.

### core 추상 인터페이스의 실제 어댑터 구현 (2026-11-03~04)

이 단위는 앞 절의 SDK 준비·include/링크 단계에 이어 실제 구현을 추가한 기록이다. core에 `byte_stream`, `executor`, `compression_codec`, `framed_transport`를 추가했다. UV 스트림은 공통 바이트 스트림을 구현한다. `asyncpp_executor`는 dispatcher의 콜백 실행/수명 유지를, `asio_executor`와 `asio_stream`은 native post와 연결된 TCP 소켓을 구현한다. `mvfst_stream`과 `msquic_stream`은 이미 연결된 QUIC 양방향 스트림을 받아 공통 스트림을 구현한다. LZ4/Zstandard/zlib 모듈은 상류 프레임 API를 사용하는 실제 코덱이다. 일곱 모듈 모두 core에 의존하는 소스 라이브러리이며 생성 export 헤더·공개 헤더·엄브렐라·설치 타깃을 제공한다.

`framed_transport`는 등록된 peer 스트림 위에서 기존 여섯 RPC 형식을 사용하며 분할·합쳐진 입력과 요청/메시지 수명을 처리한다. 스트림 어댑터는 쓰기·읽기·종료 완료까지 상태를 유지하고 콜백 실행 중 종료 완료를 지연한다. Asio 읽기 중지는 쓰기를 취소하지 않으며 이미 소비한 읽기 바이트를 재개까지 유지한다. MsQuic은 SEND_COMPLETE까지 버퍼를 유지하고 shutdown 이벤트 뒤 native 스트림을 해제한다. OOM 종료 제출은 native worker의 inline abort 경로를 사용한다. 코덱은 제한 크기, 단일 프레임, 잘림/추가 바이트, 입출력 alias, 출력 allocator와 실패 시 출력 보존을 검사한다.

참조·수정 범위는 새 core 경계와 직렬화/transport 의존, 각 어댑터의 실제 호출 API·소유/콜백/정리 경로, 해당 모듈 CMake와 기능/설치 생성, 관련 테스트·소비자·API 문서다. 합의·영속화 알고리즘과 디스크/네트워크 형식은 변경하지 않았다. Folly/Fizz/mvfst를 포함한 필수 SDK 준비를 마쳤고, 별도 `FindSodium.cmake`로 설치 mvfst 의존 탐색을 지원한다. Folly 준비 프로필에서는 미사용 Linux AIO/SimpleAsyncIO 오브젝트를 제외해 불필요한 호스트 libaio 미해결 심볼을 제거했다. glog의 미사용 unwind를 `WITH_UNWIND=none`으로 비활성화했다. 상류 소스·전체 suite를 수정/검증하지 않았다.

검증 환경은 Linux x86-64, GCC 13.3.0, CMake 3.28.3, SDK prefix `/tmp/raftpp-sdk-build/prefix`다. 모두 Release이며 공유 기본 구성은 `-Wall -Wextra -Wpedantic -Werror`, 일곱 SDK ON, UV/LZ4 ON, fixture/backtrace/examples/benchmarks OFF다. 코어 전용은 모든 SDK/UV/LZ4/fixture/backtrace OFF, 정적, PkgConfig 탐색 OFF다. 전체 SDK 정적 구성은 UV/LZ4/fixture/backtrace OFF다. ASan/UBSan은 공유·일곱 SDK ON·UV/LZ4/fixture/backtrace OFF와 `-fsanitize=address,undefined -fno-omit-frame-pointer`, `ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`을 사용했다. 외부 SDK 자체는 sanitizer 계측 대상이 아니다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 새 Release 계약 검사 | 실행 | `framed_transport`와 일곱 `module_*` 검사, 서로 다른 8개 모두 범위별 통과. 최초 7개 중 Asio 실패는 `/tmp/raftpp-adapters-tests-six-initial.log`에 보존. 최종 Asio `/tmp/raftpp-asio-clean-test.log`, mvfst `/tmp/raftpp-mvfst-native-test.log` | core 여섯 RPC/분할/오류/요청 수명; asyncpp 콜백 순서/dispatcher 유지; Asio 실제 TCP/중지·재개/종료·재진입; QUIC native 경계; 코덱 경계/할당 실패. 전체 suite 통과 수가 아님 |
| 새 Release ASan/UBSan 계약 검사 | 실행 | 서로 다른 8개 모두 범위별 통과. `/tmp/raftpp-adapters-sanitizer-tests-six.log`, `/tmp/raftpp-asio-sanitizer-test.log`, `/tmp/raftpp-mvfst-sanitizer-test.log` | 새 상태/버퍼/콜백 수명. LSan 제외이며 외부 SDK 비계측 |
| UV 공통 스트림 회귀·압축 | 실행 | stream 8개, adapter 8개, compression/compressed_snapshot 2개, 총 18개 통과. `/tmp/raftpp-adapters-uv-install-first.log` | 새 core/UV 압축 헤더의 가드 충돌을 `RAFTPP_UV_COMPRESSION_HPP`로 수정한 뒤 빌드/검증. 최초 컴파일 실패 `/tmp/raftpp-adapters-native-final-build.log`, 수정 빌드 `/tmp/raftpp-adapters-uv-guard-build.log` 보존 |
| 코어 전용 정적/설치 | 실행 | `framed_transport`, `package_install` 2개 통과. `/tmp/raftpp-adapters-core-tests.log` | 선택 SDK/UV/PkgConfig 없이 core와 이동된 설치본 소비 |
| 일곱 모듈 정적 설치 소비자 | 실행·중단 후 선택 재개 | 최초 base 소비자 9개와 isolated core 1개 통과 후 외부 package_install 실행 중단. 남은 isolated Asio/mvfst만 재개해 각각 통과, 개별 소비자 총 12개 통과 | `/tmp/raftpp-adapters-static-package.log`와 `/tmp/raftpp-adapters-static/tests/package-install/Release/{consumer,component-core,component-asio,component-mvfst}/Testing/Temporary/LastTest.log` 보존. `/tmp/raftpp-static-{asio,mvfst}-resume-build.log`. 외부 package_install 자체의 통과로 집계하지 않음 |
| 일곱 모듈+UV 공유 설치 소비자 | 실행·실패 후 선택 재개 | 최초 base 소비자 10개 중 8개 통과, mvfst/MsQuic은 별도 SDK DSO의 runtime loader 경로 부재로 실패. 경로 지정 후 해당 2개만 통과하고 최초 실행 isolated core/Asio/mvfst 3개도 통과, 개별 소비자 총 13개 통과 | 최초 `/tmp/raftpp-adapters-shared-consumers-first.log`, 재개 `/tmp/raftpp-adapters-shared-loader-resume.log`, `/tmp/raftpp-shared-{core,mvfst}-resume-test.log`, 각 component `LastTest.log`. 테스트 프로세스의 `LD_LIBRARY_PATH`로 SDK 경로를 제공하고 package export에 절대 SDK 경로를 넣지 않음. 최초 외부 package_install 실패는 보존 |
| Release 전용 등록·모듈 해제 | 실행 | Debug configure에서 SDK 검사 제외 확인만 수행, 어댑터 빌드/실행 없음. Release SDK/UV 해제 시 헤더 제거·코어 빌드 통과 | `/tmp/raftpp-adapters-debug-{policy,registration}.log`, `/tmp/raftpp-adapters-core-{disable,build}.log` |
| 소스 포맷·문서 링크 | 실행 | 자체 C++ 191개 clang-format 검사 통과 `/tmp/raftpp-adapters-format.json`; 당시 문서 21쌍/링크 468개 오류 없음 `/tmp/raftpp-adapters-docs.json` | 이후 UV 헤더 가드 이름 변경은 관련 헤더만, 진행/SDK 사용 문서 변경은 최종 링크 검사만 별도 확인 |
| 최종 관련 헤더 포맷·문서 대응/상대 링크 | 실행 | UV 압축 헤더 clang-format dry-run 통과, 21쌍/468개 링크 오류 없음. `/tmp/raftpp-adapters-docs-final.json` | 가드 변경 헤더와 이번 진행 기록·SDK 런타임 사용 설명만 재확인. 이전 191개 포맷 결과 보존 |
| 기존 합의/저장/fixture·전체 suite·추가 옵션 matrix | 생략 — 기존 결과 재사용 | 앞선 core/uv 재편 및 C++ 자체 검사 기록 | 해당 구현·검증 입력은 변경하지 않음. 위 영향받는 UV/압축/설치만 실행 |
| 실제 QUIC 연결 수립·TLS 종단 통신·LSan | 미검증 | mvfst는 필요한 QuicSocketLite API만 구현한 제어 대상, MsQuic은 실제 API open/close와 제어된 callback table을 사용 | QUIC handshake·인증·실제 UDP 클러스터 통과나 누수 검사 완료를 주장하지 않음 |

구성 디렉터리는 공유 `/tmp/raftpp-adapters`, sanitizer `/tmp/raftpp-adapters-sanitizer`, 전체 SDK 정적 `/tmp/raftpp-adapters-static`, 코어 전용 `/tmp/raftpp-adapters-core`다.

실행은 `cmake --build <위 구성 디렉터리> --config Release --parallel 2`로 영향 타깃을 빌드하고 `ctest --test-dir <같은 디렉터리> -C Release -R '<위 검사명 선택>' --output-on-failure`로 수행했다. 설치 소비자는 `tests/package` 및 `tests/package/component`를 이동된 `raftpp_DIR`로 별도 configure/build 후 CTest했다. shared SDK runtime 경로는 테스트 환경에만 설정한다. 테스트를 모아 다시 통과 수를 만들기 위해 전체를 재실행하지 않았고, 최초 실패·중단 기록과 변경 없는 성공 결과를 보존했다.

제한과 다음 범위: 이 어댑터 단위의 구현·선택 계약 검증은 완료했지만 UV처럼 별도 디스크/타이머 `io` 전체를 제공하는 대체 백엔드는 아니다. 영속 `io`는 계속 UV가 담당한다. QUIC listener/연결·TLS 인증·재접속과 실행 영역 사이 callback 전달은 애플리케이션 connector 책임이다. 연결된 스트림을 같은 직렬 실행 영역에서 core transport와 조립해야 한다. 자동 connector/별도 영속 backend는 이 단위에서 구현 완료로 기록하지 않는다. 사용 계약은 `docs/ko/streams.md`, `docs/ko/dependencies.md`와 영어 대응본을 따른다.

### 라이선스 통합과 선택 SDK 모듈 (2026-11-03)

독립 C++20 전체 구현의 직접적인 cowsql/raft 파생 관계를 `LICENSE.md`와 영어 대응본에 명시했다. 상류 저작권·기여 고지·LGPL 3.0 링크 예외·LGPL 본문을 원문 그대로 포함하고 GPL 3.0 본문도 함께 제공한다. 루트 `licenses/`를 제거하고 출처 주석·설치·현재 링크는 새 라이선스 파일을 참조한다. 외부 SDK에는 각자의 라이선스가 적용되며 원문 고지를 유지한다. 기존 기록의 옛 라이선스 경로는 당시 이력으로 보존한다.

루트 Git 저장소(main)를 초기화하고 요청된 일곱 SDK를 `deps/`의 고정 커밋 submodule로 추가했다. `modules/{asyncpp,asio,mvfst,msquic,lz4,zstd,zlib}`에 공개 헤더·privates/src·CMake 경계를 추가했다. 이 모듈은 필요한 상류 API를 노출하는 SDK 연결 인터페이스이며 실제 Raft `io`/transport 구현이나 코루틴 메서드를 구현한 것으로 판정하지 않는다. 기존 core의 io/message_transport 추상 계약이 SDK와 독립적이므로 합의·메모리·종료 계약은 재작성하지 않았다.

core는 항상 필수이며 모든 선택 모듈이 `raftpp::core`에 의존한다. SDK 준비 빌드만 별도의 CMake 프로젝트다. SDK 선택은 기본 OFF인 `RAFTPP_MODULE_<NAME>`으로, SDK 준비는 `RAFTPP_DEPENDENCIES`로 구분한다. 활성 헤더 생성·상위 엄브렐라·설치·package 의존성에 연결했고 비활성 SDK 헤더 디렉터리도 제거한다. `RAFTPP_MODULE_LZ4`는 UV와 독립이며 기존 UV 디코더 옵션 `RAFTPP_WITH_LZ4`와 구분한다. 둘 다 활성화하면 UV가 LZ4 모듈을 사용한다. zstd/zlib 선택은 기존 파일·네트워크 형식을 바꾸지 않는다.

사용자 후속 지시에 따라 어댑터 테스트·설치 소비자는 Release에서만 검증한다. 상류 테스트·예제·벤치마크·문서는 끄고 필요한 선언·링크·API 경계만 검사한다. mvfst를 위한 Folly/Fizz/Boost/OpenSSL/fmt/fast_float/gflags/glog/libevent/libsodium도 `deps/`에 추가 중이며, 기존 Zstandard/zlib을 재사용한다. 상류 getdeps의 테스트 전용·선택 의존성 전체를 가져오는 대신 명시적 필수 계층을 연결한다. Boost 바이너리는 context/regex만 빌드한다.

현재 환경은 Linux x86-64/GCC 13.3.0/CMake 3.28.3이다. 실제 실행한 검사와 제한은 다음 표를 따른다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| SDK 준비: asyncpp/asio/LZ4/Zstandard/zlib | 실행 | Release 준비 빌드 성공. `/tmp/raftpp-sdk-{configure,build-initial}.log` | 헤더 복사·독립 CMake/설치·zlib의 원본 변경을 피한 소스 복사 |
| MsQuic SDK 준비 | 실행 | 고정 OpenSSL 하위 모듈을 이용한 Release shared 준비 성공. `/tmp/raftpp-msquic-sdk-{configure,build}.log` | 상류 테스트·도구·perf·로깅 OFF |
| core-only Release static 설치 소비자 | 실행 | package_install 1개 성공, 내부 통합/core 소비자 2개. `/tmp/raftpp-deps-core-package.log` | 새 라이선스 설치·선택 SDK/UV/PkgConfig 없는 구성 |
| 여섯 SDK/UV Release shared 연결·압축·설치 | 실행 | 선택 CTest 9개 성공: 여섯 SDK 연결·compression/compressed_snapshot/package_install. 설치 내부 소비자 9개 포함. `/tmp/raftpp-deps-native-{configure,build-initial,tests-initial}.log` | 엄격한 C++20, UV/LZ4 ON, fixture/backtrace/도구 OFF, SDK asyncpp/asio/msquic/lz4/zstd/zlib ON. SDK 준비 경로는 `/tmp/raftpp-sdk-build/prefix` |
| LZ4 SDK 독립 선택/같은 디렉터리 비활성화 | 실행 | UV OFF에서 SDK LZ4 ON configure 성공. OFF 재구성/PkgConfig 비활성화에서 SDK 헤더 디렉터리 제거·core 유지·매크로 0 확인 (`/tmp/raftpp-deps-lz4-{independent.log,disable.log,disable.json}`) | 독립 SDK 선택과 기존 UV 디코더 옵션 분리 |
| mvfst SDK 누락 진단/명세 | 실행 | 패키지 미설치 상태에서 선택 ON은 명시적 configure 실패. 준비 타깃 명세 configure 성공 (`/tmp/raftpp-deps-mvfst-{missing.log,plan.log}`) | 실제 mvfst 컴파일 성공으로 간주하지 않는다 |
| mvfst 의존 소스·준비/선택 소비자 | 진행 중 | 고정 소스·최소 의존 계층을 연결하고 준비 빌드 확인 중 | 최초 gflags install의 사용자 CMake 등록 경로 쓰기 실패와 네트워크 복제 중단은 별도 보존하며 수정 중 |
| 그 외 core/네트워크/fixture/전체 suite·sanitizer | 생략 | 생략 — 기존 결과 재사용. 앞선 모듈 재편/기능 검증 기록 | 합의·저장·callback 동작의 제품 토큰은 그대로다. 선택된 링크·헤더·압축·설치 범위만 새 검사에 포함 |

API 사용법은 `docs/ko/dependencies.md`와 영어 대응본에만 설명하며 작업·검증 기록은 이 절에 유지한다. SDK API 연결은 Raft 백엔드의 I/O·수명·네트워크 계약 검증을 대신하지 않는다.

### core/uv 모듈 재편과 활성 헤더 생성 (2026-11-03)

루트 `src/`를 제거하고 라이브러리를 `modules/core`와 `modules/uv`로 재편했다. 각 모듈은 `include/`, `privates/`, `src/`, `CMakeLists.txt`를 소유한다. core의 공개 헤더 28개는 `modules/core/include/raftpp/`, uv의 공개 헤더 7개는 `modules/uv/include/raftpp/uv/`에 있다. 구현 소스는 core 28개/uv 13개(조건부 소스 포함), 비공개 헤더는 core 7개/uv 9개다. 공개 값 타입 정의의 의존 폐쇄를 확인해 필요한 core 지원 헤더 7개만 공개 `detail/`에 유지했고 나머지는 설치에서 제외했다.

`core`는 합의·값/코덱·오류/메모리/추적·추상 I/O/transport·선택 fixture/backtrace를 제공한다. `uv`는 내장 파일 I/O·TCP·stream·LZ4 프레임 판별/해제를 제공한다. 타입/함수의 `raftpp` 네임스페이스와 callback·소유권·파일/네트워크 형식은 유지했다. 기존 `<raftpp/uv_io.hpp>` 등의 평면 경로는 `<raftpp/uv/uv_io.hpp>`로 바뀌었다. 압축 보조 API도 uv에 속한다. UV OFF에서는 해당 헤더/함수와 compression 테스트 등록을 제외하며, UV ON/LZ4 OFF에서는 해제의 `invalid` 동작을 유지한다.

빌드는 `raftpp::core`, 선택적인 `raftpp::uv`와 통합 인터페이스 `raftpp::raftpp`를 제공한다. 각 모듈은 자체 CMake에서 소스/옵션/링크/export를 설정한다. 공유 심볼은 core의 `RAFTPP_API`, uv의 `RAFTPP_UV_API`를 사용하고 UV의 비공개 assertion 호출에 필요한 core 심볼도 내보낸다. 통합 `raftpp` 빌드 목표도 활성 모듈을 빌드한다. 설치 파일은 `libraftpp-core`/`libraftpp-uv`에 플랫폼 확장자를 붙인다.

루트 `include/`는 `.gitignore`에 추가한 생성 복사본이다. 모듈 원본과 생성 config/export·`cmake/raftpp.hpp.in`의 통합 진입점을 configure 시 모은다. 컴파일과 설치는 빌드별 `<build>/include/`만 사용하므로 다른 구성의 공용 미러를 읽지 않는다. 파일 잠금으로 미러 갱신을 직렬화하고 비활성 헤더는 제거한다. 내용이 같은 헤더는 다시 쓰지 않아 무관한 재컴파일을 피한다. VS Code·예제·도구·테스트·설치 경로와 API 설명서 두 언어본을 함께 갱신했다. 누락되어 있던 상류 라이선스는 기존 백업에서 SHA-256 `79d77bb9421aea2a322bd8b7e096a7efddec595fadfbcc484f951bee8063328f` 원문으로 복원했다.

참조 범위는 모든 공개 include의 재귀 의존성·내부 호출/정리·기능 분기·테스트/도구의 private 의존성과 설치 소비자까지다. 실제 소스 수정은 이동/include·export 연결이며, 41개 구현 파일의 include/주석/공백을 제외한 토큰이 이전과 동일함을 확인했다(`/tmp/raftpp-modules-token-check.json`). 알고리즘이나 비동기 수명 계약을 재작성하지 않았다.

검증 환경은 Linux x86-64/GCC 13.3.0/CMake 3.28.3/libuv 1.48.0/LZ4 1.9.4이며 새 구성은 `-Wall -Wextra -Wpedantic -Werror`를 사용한다. 로그 접두사는 `/tmp/raftpp-modules-`다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적 core: UV/LZ4 OFF, fixture/backtrace ON | 실행 | 전체 빌드와 선택 8개 통과. `core-{configure,build-fixed,build-final,tests}.log`. 최종 package 재검증에서 독립 통합/core 소비자 2개 통과(`core-package-final.log`) | 코어/private/public 지원 헤더·선택 fixture·backtrace·정적 export/링크 |
| Debug 공유 native: UV/LZ4/fixture/backtrace 및 예제/benchmark ON | 실행 | 전체 빌드·선택 17개 통과. `native-{configure,build-fixed,build-final,tests}.log`. 수정 후 독립 통합/core/uv 소비자 3개 통과(`native-package-final.log`) | 모듈 사이 소유권·writer·TCP·스냅샷·LZ4·공유 심볼·이동 설치 |
| Release 정적 최소 uv: UV ON, LZ4/fixture/backtrace OFF | 실행 | 라이브러리/foundation/persistence 타깃 빌드와 compression/compressed_snapshot/package의 3개 통과. `minimal-uv-{configure,build,tests}.log`; 소비자 3개 포함 | 해제 대체 경로·fixture 제외·정적 모듈 전이 링크 |
| Release 공유 최소 core: UV/LZ4/fixture/backtrace OFF, PkgConfig 탐색 비활성 | 실행 | core 빌드 및 package 1개 통과. `minimal-core-{configure,build,tests}.log`; 독립 소비자 2개 포함 | 외부 선택 의존성 없는 core·비활성 헤더/타깃 제외 |
| 헤더/소스 선택 6조합·같은 디렉터리 ON→OFF·잘못된 UV/LZ4·상위 프로젝트 | 실행 | `gates.json`, `gates.log`: 6개 유효 조합의 헤더/소스/미러 일치, 잔여 private·평면 UV 헤더 부재, UV OFF+LZ4 ON configure 거부, 공백을 포함한 부모 binary 디렉터리·기본 install OFF·PkgConfig 비활성 통과 | 새 생성/삭제 규칙과 모듈 활성 조건. 이 행은 전체 런타임 matrix 통과가 아님 |
| 공개 헤더 단독 include | 실행 | `header-check.json`: core 31/native 39, 총 70건 C++20 엄격 syntax 통과 | 통합 헤더의 include 순서에 의존하지 않는 모듈 export/자기 완결성 |
| 기본 IDE 빌드·통합 빌드 목표 | 실행 | 기본 Debug 정적 UV/LZ4/fixture·예제/benchmark 전체 빌드 통과(`ide-build.log`). 명시 configure 후 기본/공유 `--target raftpp` 통과(`aggregate-build-final.log`, `native-aggregate-build-final.log`). compile_commands 115개에 루트 공용 include 검색 경로 없음 | 실제 개발 경로 갱신·기존 통합 빌드 목표 유지. 통합 목표 추가 후 export 파일 2개 hash가 같아 설치 소비자 결과는 재사용(`export-comparison.json`) |
| 문서·포맷·모듈 경계 정적 확인 | 실행 | C++ 162파일, 기본 공개 헤더 39개, 컴파일 항목 115개, 문서 18쌍/링크 452개 통과 (`/tmp/raftpp-modules-static-check.json`) | API 문서는 사용 계약만, 현재 작업/검증은 이 기록에만 유지 |
| 그 밖의 런타임·sanitizer·실제 장치/커널 측정 | 생략 | 생략 — 기존 결과 재사용. 앞선 정리/기능 단위의 관련 구현·입력이 같은 결과만 사용하며 이번 통과 수에 합산하지 않음 | 구현 토큰/시나리오는 유지되며 바뀐 모듈 링크·헤더·옵션 경계는 위에서 선택 검증했다. 새 전체 suite/ASan·UBSan/장치 측정은 실행하지 않음 |

명령은 `cmake -S . -B <profile> <위 옵션>`, `cmake --build <profile> --parallel 2` 또는 해당 최소 타깃, `ctest --test-dir <profile> --output-on-failure --output-log <위 로그> -R <선택 범위>`다. 프로필은 `/tmp/raftpp-modules-{core,native,minimal-uv,minimal-core}`다. core의 선택 범위는 `assertions|configuration|wire|core_replication|node_lifetime|fixture_lifetime|memory_containers|package_install`, native는 `assertions|compression|metadata_files|snapshot_files|compressed_snapshot|disk_probe|writer_basic|writer_async|log_append|log_memory|file_snapshots|uv_io_node|stream_interop|tcp_interop|tcp_memory|compressed_transfer|package_install`이다. native의 실제 loopback 검사는 승인된 로컬 실행 권한을 사용했다. package 입력을 모듈별 소비자로 확장한 뒤에는 package만 다시 실행했다.

최초 실패도 보존한다. `*-build-initial.log`의 UV export include 누락/core-only 테스트의 private include 조건을 고쳤다. 공유 UV만 직접 연결한 설치 실행의 형제 core 검색 실패(exit 127)는 `uv-only-before.log`에 보존했고, UV 설치 RUNPATH `$ORIGIN`으로 수정했다. 동일 실행 파일과 `LD_BIND_NOW=1` 실행이 성공하며 새 모듈별 소비자 회귀로 확인했다. 통합 빌드 목표 최초 두 요청은 이전 Makefile에 목표가 없어 실패했으며 명시 재구성 후 통과를 별도로 기록했다. 이미 통과한 기능 검사는 반복하지 않았다.

### 독립 C++ 사용 설명 정리 (2026-11-03)

한·영 예제/benchmark/CMake/core/TCP/오류/추적 API 설명과 루트 지침·기록의 호출·링크·ABI 비교 문구를 정리했다. 문서는 공개 C++ 타입·함수·기능 옵션과 실제 사용 조건을 설명한다. 원본 소스 출처·라이선스·동작/형식 대조의 이력과 이전 실행 결과는 보존했다. 공개 시그니처·오류 값·예제 코드·옵션·동작은 바꾸지 않았다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 문서 표현·한/영 대응·링크 정적 확인 | 실행 | 프로젝트 문서 36개 전체에서 대상 표현 잔여 0개, 대응 문서의 제목/표/예제 일치. 상대 링크 452개 중 450개 정상·기존 파일 부재 2개 구분 | 문서 9쌍(루트 2쌍·API 7쌍) 수정. API 설명에는 진행/실행 기록을 추가하지 않음 |
| 빌드·runtime·헤더/링크·sanitizer | 생략 | 생략 — 기존 결과 재사용. 엄브렐라 단위의 헤더/설치 소비자 결과와 정리 단위의 기존 선택 검증 참조. 새 통과 없음 | 구현·공개 헤더·빌드 규칙·테스트 입력 SHA-256이 수정 전과 동일한 문서 전용 작업 |

정적 확인과 입력 hash 비교 결과는 `/tmp/raftpp-doc-wording-check.json`에 보존한다. 기존 두 언어 기록의 `licenses/cowsql-raft.LICENSE` 링크 대상은 현재 작업 공간에 없으며, 이 작업에서 해당 링크나 라이선스 파일을 변경하지 않았다. 기존 검증의 시점·숫자·성공/실패/환경 제한을 새 결과로 바꾸지 않았다.

### 최상위 엄브렐라 헤더 (2026-11-03)

[include/raftpp.hpp](cmake/raftpp.hpp.in)를 추가했다. `#include <raftpp.hpp>`가 `<raftpp/raft.hpp>`를 포함하므로 공개 헤더 목록과 UV/fixture 기능 guard는 기존 한 곳에서 유지된다. `cmake/install.cmake`에 `${CMAKE_INSTALL_INCLUDEDIR}/raftpp.hpp` 설치를 연결하고 기존 설치 소비자의 include를 새 경로로 바꿨다. CMake API 설명서 두 언어본에 포함 경로·선택 기능 조건·설치 위치를 설명했다. 라이브러리 구현/공개 타입·함수/링크 의존성은 바꾸지 않았다.

참조 범위는 기존 umbrella→공개 node/io/FSM·선택 기능 헤더 guard, 공개 include 사용 조건과 설치/재배치 소비자 경로다. 실제 수정은 새 헤더·설치 규칙·기존 소비자 include·두 언어 API/기록에 한정했다. 검증 환경은 기존 Linux/GCC 13.3.0/CMake 3.28.3과 같다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 소스 헤더 포함 — UV/LZ4/fixture OFF, backtrace ON | 실행 | C++20 엄격 syntax 검사 통과. 새 헤더 중복 include와 `<raftpp/raft.hpp>` 병용, `features`/`buffer`/`io_state` 확인. `/tmp/raftpp-umbrella-source-header.log` | 새 진입점의 guard·선택 기능 제외·직접 포함 사용 확인 |
| 이동한 설치본의 독립 소비자 — Debug 정적 UV/LZ4 OFF·fixture ON·backtrace OFF | 실행 | `/tmp/raftpp-cxx-tests-core` 재구성 후 package_install 1개 통과(9.65초). `/tmp/raftpp-umbrella-core-{configure,package}.log` | 설치된 `<raftpp.hpp>`만으로 코어/fixture·압축 API를 컴파일·링크·실행 |
| 이동한 설치본의 독립 소비자 — Release 공유 UV/LZ4/fixture ON·backtrace OFF | 실행 | `/tmp/raftpp-cxx-tests-native` 재구성 후 package_install 1개 통과(9.18초). `/tmp/raftpp-umbrella-native-{configure,package}.log` | 설치된 새 헤더와 활성 선택 기능·공유 링크 검증 |
| 기본 IDE 구성·포맷·문서 | 실행 | 기본 `build/` 재구성, 새 헤더/소비자의 clang-format 검사와 수정한 문서 쌍 구조·예제·상대 링크 검사 통과 | 새 설치 규칙 반영과 두 언어 API/기록 일치 확인 |
| 라이브러리 재빌드·그 밖의 runtime/옵션 matrix/sanitizer | 생략 | 생략 — 기존 결과 재사용. C 원본 제거 단위의 라이브러리 빌드·코어 92/native 32개 선택 결과 및 해당 과거 검사 기록 참조. 이번 통과 수에 합산하지 않음 | 새 forwarding 헤더/설치 진입점 외 구현·사용 조건은 동일하다. 기존 라이브러리를 재사용하고 영향받은 설치 소비자만 실행 |

명령: `cmake -S . -B <profile>` 후 `ctest --test-dir <profile> --output-on-failure --output-log /tmp/raftpp-umbrella-<core-or-native>-package.log -R '^raftpp\.package_install$'`. `<profile>`은 위 두 디렉터리다. 소스 헤더는 `c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I include -I /tmp/raftpp-cxx-only-core/include -fsyntax-only -x c++ -`에 새/기존 include와 타입·기능 static_assert를 입력해 확인했다. 이전 외부/내부 소비자 LastTest 로그는 실행 전 `/tmp/raftpp-umbrella-previous-<core-or-native>-<ctest-or-consumer>.log`에 보존했다. 새 링크 심볼이나 추가 의존성은 없다.

### C 원본 제거·C++ 자체 검사·포맷 통일 (2026-11-03)

사용자 요청과 “C 비교 코드도 제거하고 C++ 자체 테스트로 전환” 선택에 따라 `raft/` 전체와 C 기준 타깃/생성 헤더/비교 bridge를 제거했다. 외부 C oracle을 새로 요구하지 않는다. 삭제 전 원본은 commit `148951f79a1ed529d6f112661a3067494f1a0917`이었으며, 과거 대응표의 원본 링크는 해당 upstream commit을 가리킨다. 아래의 C 대조·206개 등록·이전 hash 기록은 제거 전 이력이다. 새 구현 완료 판정이나 현재 실행 결과로 확대하지 않는다.

실제 변경 범위:

- `CMakeLists.txt`는 `LANGUAGES CXX`만 사용한다. `raftpp::raftpp`만 제공하며 C 소스/타깃/`raft.h` 생성·검색 경로를 제거했다. Linux 헤더·GNU libbacktrace probe도 C++ 검사로 옮겼다. 기존 libuv/LZ4/fixture/backtrace/정적·공유/예제/benchmark/install 선택 규칙은 유지한다.
- `tests/`의 C 소스·헤더 16개를 제거했다. 필요한 ENOSPC/예약 syscall 주입만 `fallocate.cpp`, `file_allocation_hooks.cpp`, `file_allocation.hpp`로 유지하며 독립적인 테스트 보조 코드로 유지한다. 비교 전용 등록명을 `contracts`로 바꾸고 중복된 `legacy_transfer_cpp_{normal,forced}` 두 개를 제거했다. 기본 전체 구성은 204개, 코어·fixture 구성은 99개를 등록한다. 등록 수는 이번 통과 수가 아니다.
- 구성·trail·로그는 독립 `vector`/`map` 모델과 비교한다. CRC/SHA-1/난수/구성·RPC 형식은 고정 벡터와 round-trip/증분/경계 검사를 사용한다. 코어·노드·fixture는 상태/영속 인덱스/멤버십/콜백/실패 출력·소유권 조건을 검사한다. fuzzy 171개 고정 시드 조합은 유지하되 C의 timer 내부 조회 대신 각 tick에 유효한 선거 timeout을 설정한다.
- 파일/복구는 제출한 엔트리·스냅샷·메타데이터와 명시한 오류·범위·반복 로드 안정성을, writer·예약 fallback은 파일 바이트/쓰기 위치·횟수/정리를 확인한다. TCP/stream 상호 운용은 POSIX 상대편을 사용한다. TCP는 6종 RPC 양방향과 handshake/frame 바이트를 확인하며 resolver의 9개 다중 주소 사례를 유지한다. 과거 모든 이벤트의 C 일치 검사를 현재의 자체 검사와 같은 검증 방식으로 주장하지 않는다.
- `.clang-format`을 추가하고 clang-format 18.1.8로 C++ 파일 159개를 정리했다. LLVM 기반 4칸/100열이며 include·using 순서, 주석 재배치와 문자열 분할을 끈다. 제품 헤더/소스·예제·도구 112개는 주석·공백을 제외한 토큰이 삭제 전과 동일하다. 알고리즘·공개 API·파일/네트워크 형식은 변경하지 않았다.
- 원본 라이선스를 [licenses/cowsql-raft.LICENSE](LICENSE.md)에 바이트 그대로 보존했다(SHA-256 `79d77bb9421aea2a322bd8b7e096a7efddec595fadfbcc484f951bee8063328f`). 출처 주석·설치 경로·한/영 지침·CMake/API 문서를 함께 갱신했다. VS Code에 C++ formatter/저장 시 포맷을 설정하고 기본 `build/compile_commands.json`을 재생성했다(115개 항목, C 소스 0개). 이전 `libraft`/생성 C 헤더와 C 객체 산출물을 정리했다.

참고 범위는 C 타깃·생성 헤더에서 모든 테스트 bridge/호출자·네이티브 오류 주입·기능 분기·설치 소비자까지 추적했다. 실제 제품 수정은 포맷/출처 주석뿐이며 빌드·테스트·지침·기록·관련 API 문서를 따로 수정했다. 원본 전체를 재번역하거나 API 문서에 진행 기록을 넣지 않았다.

검증 환경: Linux x86-64, GCC 13.3.0, CMake 3.28.3, libuv 1.48.0, LZ4 1.9.4. 새 `/tmp` 구성은 `-Wall -Wextra -Wpedantic -Werror`를 사용했다. 아래 결과는 이 작업에서 실행한 범위다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적 코어·fixture: UV/LZ4 OFF, fixture ON, backtrace OFF | 실행 | `/tmp/raftpp-cxx-tests-core` configure/전체 빌드 통과. 선택 92개 최종 통과. 최초 90 통과/2 실패는 `...core-tests-initial.log`, 로그 모델 수정 후 `...core-model-tests.log`의 log 통과, 진행 상태 시간 전제 수정 후 `...core-progress-tests.log`의 progress 통과로 구분 | C oracle을 제거한 코어/값/fixture/노드/fuzzy/설치 검사와 빌드 규칙 변경 |
| Release 공유 native: UV/LZ4/fixture ON, backtrace OFF, 예제·benchmark ON | 실행 | `/tmp/raftpp-cxx-tests-native` configure/전체 빌드 통과. native 파일·저장·writer·설치 29개 통과(`...native-tests.log`), socket 3개 최종 통과(`...native-sockets.log`, `...tcp-tests.log`) | 새 C++ syscall 주입/파일·복구 조건·POSIX 상대편/공유 링크·설치 경로 확인. socket은 로컬 루프백 권한으로 실행 |
| Release 공유 코어: UV/LZ4/fixture OFF, backtrace ON | 실행 | `/tmp/raftpp-cxx-only-core` configure와 `cmake --build ... --target raftpp --parallel 2` 통과(`...core-configure.log`, `...core-backtrace-build.log`) | C++ 전용 backtrace 의존성 probe와 선택 계층 제외 빌드 확인. 이 구성의 runtime은 새로 실행하지 않음 |
| VS Code 기본 Debug 정적 native·예제·benchmark 구성 | 실행 | `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 통과; 전체 기본 빌드 및 마지막 이름/문구 정리의 관련 네 타깃 빌드 통과(`...ide-configure.log`, `...ide-build.log`, `...ide-name-build.log`) | C 경로 제거와 compile_commands 갱신. runtime은 위 변경 범위별 결과를 사용 |
| 포맷/제품 토큰/문서·C 의존성 정적 확인 | 실행 | C++ 159개 clang-format dry-run 통과, 제품 112개 토큰 동등 확인. 최종 문서 18쌍(API 16쌍)의 제목/표/예제·상대 링크 450개와 C 소스/링크/컴파일 항목 부재 확인 통과(`...static-evidence.json`). 내부 이름 정리 후 네 파일과 TCP 후속 포맷 검사는 따로 통과 | 형식 변경이 제품 로직을 바꾸지 않았는지와 한/영 대응·삭제 경로 확인 |
| 그 밖의 변경 없는 런타임·옵션 matrix·sanitizer | 생략 | 생략 — 기존 결과 재사용. 아래 최종 감사 및 각 해당 단위의 과거 결과 참조. 새 통과 개수에 합산하지 않음 | 제품 토큰/개별 검사 입력과 해당 기능 조건이 유지되는 범위만 재사용. 새 C++ 테스트 전환에 대한 sanitizer 실행을 주장하지 않음 |

실행 명령과 결과 보존: 로그 접두사는 `/tmp/raftpp-c-removal-`이다. 코어는 `cmake --build /tmp/raftpp-cxx-tests-core --parallel 2` 후 `ctest --test-dir /tmp/raftpp-cxx-tests-core --output-on-failure --output-log /tmp/raftpp-c-removal-core-tests-initial.log -E '^raftpp\.(assertions|foundation_failure|core_failure|core_stale_vote|core_index_limits|core_term_limits|core_clock_limits)$'`로 실행했다. 바뀌지 않은 7개는 제외하고 실패한 log/progress만 수정에 따라 재검증했다. native는 `cmake --build /tmp/raftpp-cxx-tests-native --parallel 2` 후 file allocation/probe, snapshot/capacity, segment buffer/log append, writer, metadata/snapshot/segment/recovery/limits, native errors/package의 29개와 `ctest --test-dir /tmp/raftpp-cxx-tests-native -R '^raftpp\.(stream_interop|tcp_interop|tcp_resolver_contracts)$' --output-on-failure`를 선택 실행했다. native와 코어의 package_install은 각각 이동한 설치본의 독립 C++ 소비자를 빌드·실행했다.

최초 실패도 보존한다. core 초기 컴파일의 lambda 치환 잔여 코드를 고쳤고 native 엄격 빌드에서 C 비교용으로만 쓰던 두 보조 함수를 제거했다(`...core-build-initial.log`, `...native-build-initial.log`, `...native-build-cleanup.log`). 코어 runtime 실패 두 개는 새 로그 모델의 snapshot/last 범위와 progress의 never-sent/시간 전제 오류였다. 새 TCP 상대편의 accept predicate가 재호출될 때 이미 얻은 FD를 덮어쓰던 문제를 고친 뒤 tcp_interop 한 개만 다시 통과했다. 제품 버그 수정으로 기록하지 않는다. 통과한 다른 검사는 반복하지 않았다.

마지막 정리는 테스트 내부의 `reference`/`compare_c` 함수명을 `contracts`/`check_descriptors`로 바꾸고 진단 문구를 정리한 것이다. 조건·입력·CLI 등록은 바꾸지 않았으므로 해당 타깃만 다시 컴파일하고 통과한 runtime은 재사용한다.

현재 C 라이브러리/비교 의존성은 없다. 과거의 C 비교와 전체 기능 이식 감사는 당시의 근거로 보존한다. 실제 perf/trace/raw 장치·모든 파일시스템/권한·32비트 실행·LSan과 과거 최초 TCP timeout/추가 수렴 조건의 한계는 해소한 것으로 보고하지 않는다. 이번 정리에는 새 전체 suite/전체 옵션 matrix/ASan·UBSan 실행을 포함하지 않는다.

## 구성요소별 현황

| 구성요소 | 상태 | C 기준 및 C++ 구현 | 실제 범위와 남은 작업 |
| --- | --- | --- | --- |
| 공통·시멘틱 타입 | 표준 별칭·도메인 기반 연결 | [common.hpp](modules/core/include/raftpp/common.hpp), [types.hpp](modules/core/include/raftpp/types.hpp) | 표준 타입의 전체 템플릿 인터페이스를 유지하는 별칭과 소유·공유·대여·뷰, ID·term·index·역할, PMR 소유 allocator·표준 컨테이너 별칭을 정의했다. 구성·메시지·스냅샷·노드 도메인 타입은 각 공개 헤더에 연결했다. 비소유 수명·동기화 조건은 API 계약으로 유지한다. |
| 버퍼 | 공개/코어/native 연결·범위별 검증 | [buffer.hpp](modules/core/include/raftpp/buffer.hpp), [buffer.cpp](modules/core/src/buffer.cpp) | 일반·정렬 버퍼와 표준 shared_ptr aliasing 기반 읽기 전용 부분 범위를 구현했다. 부분 범위는 전체 할당의 수명을 유지하고 allocator/정렬/바이트 수를 함께 이전한다. 공개 요청·디스크 writer·네트워크와 PMR resource에 연결했으며 관련 할당·수명·해제 범위의 검증을 기록했다. |
| 메모리 관리 | resource·버퍼·객체·코어/native 컨테이너 연결 | [heap.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/heap.c), [common.hpp](modules/core/include/raftpp/common.hpp), [memory.hpp](modules/core/include/raftpp/memory.hpp), [memory.cpp](modules/core/src/memory.cpp), [buffer.hpp](modules/core/include/raftpp/buffer.hpp), [buffer.cpp](modules/core/src/buffer.cpp) | 표준 PMR 소유 allocator와 get/set/reset, 버퍼·공유 제어 블록·비동기 객체·entry/tracer, 로그·trail·코어 및 native 요청 컨테이너를 연결했다. splice 큐는 동일 allocator를 유지한다. 공개 값 컨테이너·std 함수 boxing·filesystem·외부 의존성은 별도 경계다. |
| 추적 | API·기본 연결 구현·검증 | [tracing.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/tracing.c), [tracing.h](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/tracing.h), [tracing.hpp](modules/core/include/raftpp/tracing.hpp), [tracing.cpp](modules/core/src/tracing.cpp), [내부 형식화](modules/core/privates/tracing.hpp) | 표준 `source_location`, 공유 콜백·동기 대여 이벤트·수준·1023바이트 메시지 한도·no-op/stderr·환경 변수, core/node/UV/TCP 연결과 writer 제출·완료 metric을 구현했다. 세부 진단 문구·소스 위치의 일대일 대응은 보장하지 않는다. |
| Assertion | 선택 기능 구현·범위별 검증 | [assert.h](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/assert.h), [assert.hpp](modules/core/privates/assert.hpp), [assert.cpp](modules/core/src/assert.cpp) | 기본 OFF인 GNU libbacktrace 옵션과 기존 assertion 29곳을 연결했다. NDEBUG와 표준 OFF 동작을 보존하고 실패 시 stderr 위치/프레임 출력 뒤 abort한다. Debug/Release·정적/공유·sanitizer·옵션 해제·의존성/소비자를 선택 검증했다. 심볼 해석은 바이너리/디버그 정보에 의존한다. |
| 오류 | 코드·소유 진단·native 전달 구현 | [C 오류 정의](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/err.h), [error.hpp](modules/core/include/raftpp/error.hpp), [error.cpp](modules/core/src/error.cpp), [node.cpp](modules/core/src/node.cpp), [file.cpp](modules/uv/src/file.cpp) · [disk.cpp](modules/uv/src/disk.cpp) · [tcp_transport.cpp](modules/uv/src/tcp_transport.cpp) | C 숫자/문구·고정 메시지 wrapping을 표준 error_code/array/string_view에 연결했다. 파일/디스크 syscall 경로·요청별 KAIO/threadpool/poll/종료와 TCP 연결/송수신 원인을 log/file/UV/node로 전달한다. 기존 단위별 진단 검증과 아래 최종 계약 감사를 근거로 판정하며 이전 환경/실패 제한은 구분한다. API: [한국어](docs/ko/errors.md)·[영어](docs/en/errors.md). |
| 클러스터 설정 | 구성요소 이식 | [configuration.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/configuration.c), [configuration.hpp](modules/core/include/raftpp/configuration.hpp), [configuration.cpp](modules/core/src/configuration.cpp) | 추가·삭제·조회·투표자 인덱스·값 복사와 기존 형식의 인코딩·디코딩을 구현했다. 멤버십 변경 프로토콜은 아래 합의 코어와 공개 노드 항목에서 관리한다. |
| 로그 term 추적기 | 구성요소 이식 | [trail.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/trail.c), [trail.hpp](modules/core/include/raftpp/detail/trail.hpp), [trail.cpp](modules/core/src/trail.cpp) | run-length 표현, 추가·잘라내기·스냅샷·복구를 표준 `deque`로 구현했다. 로그 엔트리 payload와 배치 관리는 포함하지 않는다. |
| 압축 보조 기능 | 구성요소 이식 | [compress.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/compress.c), [compression.hpp](modules/uv/include/raftpp/uv/compression.hpp), [compression.cpp](modules/uv/src/compression.cpp), [compression_lz4.cpp](modules/uv/src/compression_lz4.cpp) | LZ4 magic 판별과 크기 제한을 지원하는 해제를 구현했다. LZ4 비활성 시 기존 오류를 반환한다. 스냅샷 파일 읽기에 압축 판별·해제를 연결했다. 기준 C 구현은 해제만 제공하므로 별도 압축기 생성을 이식 요건으로 추가하지 않는다. |
| 공개 이벤트 API·조회 | 공개 연결·선택 검증 완료 | [core.hpp](modules/core/include/raftpp/core.hpp), [core_api.cpp](modules/core/src/core_api.cpp), [types.hpp](modules/core/include/raftpp/types.hpp), [random.hpp](modules/core/include/raftpp/random.hpp), [version.hpp](modules/core/include/raftpp/version.hpp) | 기존 독립 C++ 엔진을 공개 별칭·step/update로 연결하고 timeout 설정/조회·이름·난수·참조 버전을 제공한다. callback setter의 150/15 ms 보정을 보존한다. API: [한국어](docs/ko/core.md)·[영어](docs/en/core.md). |
| 공개 노드·I/O·FSM | 드라이버·native/호환 연결·범위별 검증 | [legacy.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/legacy.c), [node.hpp](modules/core/include/raftpp/node.hpp), [io.hpp](modules/core/include/raftpp/io.hpp), [state_machine.hpp](modules/core/include/raftpp/state_machine.hpp), [node.cpp](modules/core/src/node.cpp), [raft.hpp](modules/core/include/raftpp/raft.hpp) | 시작·복구·명령/barrier·멤버십/승격·인계·비동기 종료와 요청 소유권을 구현했다. libuv 파일/스냅샷·TCP·추적·소유 진단과 일반/unit-test 호환 동작을 연결했다. 준비 할당 실패·스냅샷 설치 대기/정리·콜백 재진입/이동/해제를 검증한 범위를 기록하며 C fixture sanitizer 등 기존 제한은 유지한다. step_callback 알림의 C 시점/소유·재진입/설치 계약도 연결했다. |
| 빌드·기능 선택 | 기능 조건·설치 package 연결·선택 검증 | [CMakeLists.txt](CMakeLists.txt), [config.hpp.in](cmake/config.hpp.in), [install.cmake](cmake/install.cmake) | C++20 정적/공유·0/1 config/export 헤더와 UV/LZ4/fixture/backtrace·예제/benchmark/tests를 연결한다. RAFTPP_INSTALL은 C++ 타깃·헤더·원본 라이선스와 재배치 가능한 find_package 설정을 제공하며 선택 의존성은 소비 환경에서 다시 찾는다. 과거 설치 소비 네 구성과 이번 C++ 전용 코어/native 두 구성의 설치·독립 소비를 확인했다. 변경 없는 runtime/matrix는 재사용한다. API: [한국어](docs/ko/cmake.md)·[영어](docs/en/cmake.md). |
| 로그·엔트리·공유 배치 | 코어 연결 | [log.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/log.c), [entry.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/entry.c), [log.hpp](modules/core/include/raftpp/detail/log.hpp), [log.cpp](modules/core/src/log.cpp), [core.cpp](modules/core/src/core.cpp) | 캐시·term 추적기를 이벤트 코어에 연결했다. 전송·저장 결과물이 공유 엔트리 핸들을 보관하며 잘라낸 배치도 완료까지 유지된다. 공개 드라이버가 I/O 요청과 완료 콜백의 소유권을 유지한다. 로그 쓰기 coordinator를 추가했다. 공개 파일 backend와 구체적인 TCP 전송을 연결했다. |
| 스냅샷 값 | 코어 연결 | [snapshot.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/snapshot.c), [snapshot.hpp](modules/core/include/raftpp/snapshot.hpp), [snapshot.cpp](modules/core/src/snapshot.cpp), [replication.cpp](modules/core/src/replication.cpp), [membership.cpp](modules/core/src/membership.cpp) | 값 복사·병합·정리와 이벤트 코어의 로컬 스냅샷 알림·compaction·설치·완료 상태를 구현했다. 완료에 필요한 설정 값은 I/O 제출 전에 준비한다. FSM 준비·두 번째 단계·정리, 송신용 스냅샷 조회와 설치를 드라이버에 연결했다. 스냅샷 파일의 준비·공개·읽기·정리도 추가했다. 비동기 barrier·segment 정리·조회와 공개 I/O에 연결했다. TCP 스냅샷 송수신·설치와 따라잡기도 검증했다. |
| 합의 코어·멤버십 | 이벤트/콜백/native 연결·범위별 C 대조 | [raft.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/raft.c), [replication.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/replication.c), [core.hpp](modules/core/include/raftpp/detail/core.hpp), [core.cpp](modules/core/src/core.cpp), [convert.cpp](modules/core/src/convert.cpp), [client.cpp](modules/core/src/client.cpp), [membership.cpp](modules/core/src/membership.cpp), [replication.cpp](modules/core/src/replication.cpp), [receive_core.cpp](modules/core/src/receive_core.cpp), [timeout.cpp](modules/core/src/timeout.cpp) | 시작·선거·복제·충돌·영속화 완료·commit·멤버십/catch-up·인계·스냅샷을 독립 코어에 구현하고 공개 callback 노드·파일/TCP·추적/진단에 연결했다. 135개 인계 이벤트를 포함한 C 대조와 할당 실패·인덱스 경계를 검증한 범위를 기록했다. 전체 계약의 구현 판정은 아래 최종 감사 표를 따르며 원본 C의 비교 제한을 구분한다. |
| 메시지·바이트 형식 | 코덱 구현·검증 | [byte.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/byte.c), [uv_encoding.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_encoding.c), [uv_segment.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_segment.c), [encoding.hpp](modules/core/include/raftpp/encoding.hpp), [checksum.hpp](modules/core/include/raftpp/checksum.hpp), [encoding.cpp](modules/core/src/encoding.cpp), [checksum.cpp](modules/core/src/checksum.cpp), [byte.hpp](modules/core/privates/byte.hpp) | 6종 RPC와 단계별 수신, batch 헤더, CRC 레코드, CRC32·증분 SHA-1·digest를 독립 구현했다. 메타데이터·스냅샷 메타데이터 코덱도 구현했다. 동기 segment 파일 복구를 추가했다. 소켓 handshake와 비동기 backend를 연결했다. |
| 네트워크·영속화·복구 | 파일·coordinator·UV/TCP 연결·범위별 검증 | [uv.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv.c), [uv_metadata.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_metadata.c), [uv_snapshot.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_snapshot.c), [uv_fs.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_fs.c), [uv_os.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_os.c), [uv_segment.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_segment.c), [uv_list.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_list.c), [uv_truncate.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_truncate.c), [uv_finalize.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_finalize.c), [disk.hpp](modules/uv/privates/disk.hpp), [disk.cpp](modules/uv/src/disk.cpp), [metadata.cpp](modules/core/src/metadata.cpp) | 두 슬롯 메타데이터·스냅샷·segment 생성/복구/잘라내기/보존과 bootstrap/recover를 독립 구현했다. native 핸들/요청의 RAII 정리·capability probe·direct/KAIO/NOWAIT/threadpool writer·로그/스냅샷 coordinator·공개 UV/TCP를 연결했다. 파일/C recover/예약 fallback의 실제 대조 근거와 원본 수치 절단/손상 입력 차이·환경 제한을 구분한다. |
| 비동기 파일 writer | 내부 구현·검증 | [uv_writer.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_writer.c), [uv_writer.h](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_writer.h), [uv_os.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_os.c), [syscall.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/syscall.c), [writer.hpp](modules/uv/privates/writer.hpp), [writer.cpp](modules/uv/src/writer.cpp), [file.hpp](modules/uv/privates/file.hpp), [file.cpp](modules/uv/src/file.cpp) | RWF_DSYNC·NOWAIT·eventfd·poll 완료, EAGAIN threadpool 재시도와 다중 요청을 구현했다. 요청이 iovec·버퍼를 소유하며 종료 시 커널 정리와 FD 닫기를 worker에서 수행한다. capability probe·prepare pool·append·barrier는 아래 coordinator에 연결했다. 공개 파일 backend를 연결했다. writer 제출·완료 metric을 추적에 연결했다. |
| 로그 쓰기 coordinator | 내부 구현·검증 | [uv_prepare.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_prepare.c), [uv_append.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_append.c), [uv_finalize.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_finalize.c), [uv_truncate.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_truncate.c), [probe.cpp](modules/uv/src/probe.cpp), [segment_buffer.cpp](modules/uv/src/segment_buffer.cpp), [log_writer.cpp](modules/uv/src/log_writer.cpp) | capability probe·정렬 staging·두 파일 준비·append/finalize·blocking/nonblocking barrier·비동기 truncate와 안전한 종료를 구현했다. 실제 C 파일 비교와 디스크 단일 노드 재시작을 검사했다. 스냅샷·공개 파일 I/O에 연결했다. 구체적인 TCP 전송을 연결했다. |
| Native 파일 coordinator·공개 UV backend | 기록 범위의 구현·검증 | [uv.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv.c), [uv_snapshot.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_snapshot.c), [file_backend.hpp](modules/uv/privates/file_backend.hpp), [file_backend.cpp](modules/uv/src/file_backend.cpp), [transport.hpp](modules/core/include/raftpp/transport.hpp), [uv_io.hpp](modules/uv/include/raftpp/uv/uv_io.hpp), [uv_io.cpp](modules/uv/src/uv_io.cpp) | 스냅샷 준비·재시도·barrier·공개·보존·조회·종료와 실제 파일 연산·타이머를 공개 `io`에 연결했다. 추상 전송에 구체적인 `tcp_transport`를 연결했다. API 계약: [한국어](docs/ko/uv_io.md)·[영어](docs/en/uv_io.md). |
| TCP 전송 | 기록 범위의 구현·검증 | [uv_tcp.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_tcp.c), [uv_tcp_connect.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_tcp_connect.c), [uv_tcp_listen.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_tcp_listen.c), [uv_ip.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_ip.c), [uv_send.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_send.c), [uv_recv.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/uv_recv.c), [tcp_transport.hpp](modules/uv/include/raftpp/uv/tcp_transport.hpp), [tcp_transport.cpp](modules/uv/src/tcp_transport.cpp), [tcp_transport.cpp](tests/tcp_transport.cpp) | 주소 해석·다중 listener 중복 제거·handshake·단계별 수신·큐·재연결·주소 변경·종료와 요청 수명을 구현했다. C TCP handshake와 상호 통신하며 실제 디스크·TCP 노드에 연결했다. API 계약: [한국어](docs/ko/tcp_transport.md)·[영어](docs/en/tcp_transport.md). |
| Raw stream 소유·메시지 adapter | 기록 범위의 구현·검증 | [uv_stream.hpp](modules/uv/include/raftpp/uv/uv_stream.hpp), [uv_stream_transport.hpp](modules/uv/include/raftpp/uv/uv_stream_transport.hpp), [stream_transport.hpp](modules/uv/include/raftpp/uv/stream_transport.hpp), [uv_stream.cpp](modules/uv/src/uv_stream.cpp), [stream_transport.cpp](modules/uv/src/stream_transport.cpp), [stream_transport.cpp](tests/stream_transport.cpp), [한국어](docs/ko/stream_transport.md), [영어](docs/en/stream_transport.md) | native stream 인계와 완료까지의 소유권, 사용자 connector의 메시지 framing·재시도·수신/송신·종료를 uv_io에 연결한다. UV 필요; fixture/LZ4와 독립적이다. |
| fixture | 독립 구현·자체 계약 검사 | [fixture.c](https://github.com/cowsql/raft/blob/148951f79a1ed529d6f112661a3067494f1a0917/src/fixture.c), [fixture.hpp](modules/core/include/raftpp/fixture.hpp), [fixture.cpp](modules/core/src/fixture.cpp), [테스트](tests/fixture.cpp) | 가상 시간·요청 순서·메모리 저장/전송·선거/분할/kill/revive·장애·6종 counter·안전성 관찰을 공개 node/io/FSM에 연결했다. 현재 C++ 자체 검사는 확장/멤버십/네트워크·디스크/스냅샷 주입·반복 load/소유권을 확인한다. C 대조·참조 제한은 과거 근거로 보존하며 C bridge는 남아 있지 않다. |
| 테스트·예제·도구 | C++ 자체 검사·도구·범위별 검증 | [foundation.cpp](tests/foundation.cpp), [storage.cpp](tests/storage.cpp), [consensus.cpp](tests/consensus.cpp), [core.cpp](tests/core.cpp), [node.cpp](tests/node.cpp), [encoding.cpp](tests/encoding.cpp), [persistence.cpp](tests/persistence.cpp), [segments.cpp](tests/segments.cpp), [fallocate.cpp](tests/fallocate.cpp), [writer.cpp](tests/writer.cpp), [log_writer.cpp](tests/log_writer.cpp), [uv_io.cpp](tests/uv_io.cpp), [테스트 CMake](tests/CMakeLists.txt) | 독립 모델/고정 벡터/계약으로 잘못된 입력·할당 실패·콜백 수명과 모의/실제 디스크·TCP 동작을 검사한다. native 서버/프로세스 클러스터·submit/disk benchmark·kernel perf/trace 도구는 공개 C++ API를 사용한다. 과거 C 대조는 이력으로 보존한다. 실제 kernel profiler/raw 장치·LSan은 미검증이고 최초 TCP timeout 원인은 미확정이다. |

이전 저장 값 이식 단위에서는 로그·엔트리·공유 배치와 스냅샷 값 복사, 정렬 버퍼의 구현 및 기본·옵션별·ASan/UBSan 검증을 마쳤다. 외부 소비자의 정적·공유 링크 검증도 마쳤다. 참조 범위는 `log.c`, `entry.c`, `snapshot.c` 및 `legacy.c`, `uv_encoding.c`, `uv_send.c`, `uv_append.c`의 소유권·해제 경로다. 이 저장 값 단위의 과거 검증을 이후 합의 코어·비동기 I/O 계층의 검증으로 확대 해석하지 않는다. API 계약은 [한국어](docs/ko/storage.md)·[영어](docs/en/storage.md) 설명서에 기록했다.

이 표의 C 소스 링크는 각 영역의 시작점이다. 관련 헤더·호출 함수·메모리 해제·콜백·조건부 컴파일 경로를 추적해야 하며, 링크한 파일 하나를 확인한 것만으로 영역 전체를 검토했다고 기록하지 않는다. 위에 명시한 구현·검증 범위를 구분해 관리한다.

합의 코어의 선거·복제 진행 상태와 RequestVote 수신·follower 전환의 구현 및 아래 범위의 검증을 마쳤다. `progress.c`, `election.c`, `random.c`, `message.c`와 `convert.c`, `timeout.c`, `recv_request_vote*.c`, `replication.c`의 호출·상태·오류 경로를 참조한다. 해당 선거 단위는 내부 상태·RPC 값 타입, 선거 타이머·시작·투표·집계, 복제 진행 상태와 RequestVote 수신 시 term 갱신·follower 전환의 알고리즘이다. 노드 이벤트 루프·candidate 및 leader 전환·실제 전송은 포함하지 않는다.

candidate·leader 전환, 영속화·복제·멤버십·스냅샷과 이벤트 구동 코어를 구현하고 기본·옵션별·ASan/UBSan 검증을 마쳤다. 외부 소비자의 정적·공유 링크 검증도 마쳤다. `convert.c`, `replication.c`, `client.c`, `membership.c`, `restore.c`, `timeout.c`, `recv_*.c`, `raft.c`와 `legacy.c`의 메시지·저장 완료·로그 소유 경로를 추적한다. 외부 libuv 백엔드와 공개 callback 노드 API는 해당 코어 단위와 구분한다.

공개 node·io·state_machine 콜백 API와 이벤트 드라이버를 구현하고 기본 테스트 28개, 기능 옵션 6개 조합, ASan/UBSan 및 외부 정적·공유 소비자 검증을 마쳤다. 누수 검사는 제외했다. API 계약은 [한국어](docs/ko/node.md)·[영어](docs/en/node.md)에 기록한다. `legacy.c`, `raft.c`, 공개 I/O·FSM·request 선언의 시작·저장 완료·FSM 적용·승격·스냅샷·종료 경로를 추적한다. libuv·파일 백엔드는 별도 구현 범위로 남긴다.

메시지·로그 배치 인코딩과 CRC32·SHA-1·digest를 구현하고 기본 테스트 33개, 기능 옵션 6개 조합, ASan/UBSan과 외부 정적·공유 소비자를 검증했다. 누수 검사는 제외했다. `byte.c`, `uv_encoding.c`, `uv_recv.c`, `uv_send.c`, `uv_append.c`, `uv_segment.c`와 `raft_digest`의 바이트·패딩·체크섬·소유권 경로를 참조한다. 실제 소켓·파일 backend와 전체 segment 복구 정책은 별도 범위다.

영속 메타데이터·스냅샷 파일 계층을 구현하고 기본 테스트 39개, 기능 옵션 6개 조합, ASan/UBSan과 외부 정적·공유 소비자를 검증했다. 누수 검사는 제외했다. `uv_metadata.c`, `uv_snapshot.c`, `uv_fs.c`, `uv_os.c`, `uv.c`의 두 슬롯 복구·fsync/fdatasync·익명 임시 파일·공개·정리·압축 읽기 경로를 추적한다. segment 파일 복구·비동기 barrier·실제 socket 전송은 별도 범위다.

## 기능 옵션과 적용 범위

| 옵션 | 현재 적용 범위 | 검증 조건과 제한 |
| --- | --- | --- |
| `RAFTPP_WITH_UV` | 파일·KAIO writer·probe·로그/스냅샷 coordinator·공개 uv_io·TCP/raw stream adapter와 요청별 소유 진단을 연결한다. | Linux·libuv 및 필요한 헤더가 필요하다. OFF에서는 해당 소스·헤더 내용·외부 의존성·테스트를 제외한다. |
| `RAFTPP_WITH_LZ4` | 압축 판별·해제와 기존 압축 snapshot 읽기를 연결한다. native TCP catch-up·수신 저장·재시작의 해제 payload 경로도 검증했다. | UV가 필요하다. OFF에서 압축 파일 읽기는 invalid다. snapshot compression setter는 원본과 같은 no-op이며 압축기를 추가하지 않는다. |
| `RAFTPP_WITH_FIXTURE` | 독립 가상 시간 fixture와 네트워크·디스크·멤버십·snapshot 주입/실패 및 실제 C 비교를 연결한다. | OFF에서는 fixture 소스·공개 헤더 내용·관련 테스트를 제외한다. 예제/benchmark는 fixture를 요구하지 않는다. 참조 C의 TIMEOUT_NOW 통계 배열 오류는 별도 제한이다. |
| `RAFTPP_WITH_BACKTRACE` | 기본 OFF·다른 기능과 독립. GNU libbacktrace·__assert_fail 검사, 생성 플래그, 독립 C++ assertion 출력을 연결한다. | NDEBUG가 없는 라이브러리의 assertion 실패 경로에 적용한다. 심볼/소스 해석은 바이너리·디버그 정보에 따른다. |
| `RAFTPP_BUILD_TESTS` | 기능별 CTest 등록과 C 비교 bridge·실제 파일/TCP·수명/실패/입력 경계 검사를 선택한다. | 최신 등록 수와 실행/재사용은 아래 해당 단위 기록을 따른다. 등록 수는 전체 suite 통과 수가 아니다. |
| `RAFTPP_BUILD_EXAMPLES` | 기본 OFF·UV 필수. 공개 C++ counter/server/프로세스 클러스터의 종료·재시작을 연결한다. | tests·fixture·LZ4 없이도 빌드된다. 기존 최소 구성/옵션 거부 검증을 재사용한다. |
| `RAFTPP_BUILD_BENCHMARKS` | 기본 OFF·UV 필수. submit/disk·표준 통계/JSON·io_uring·장치 조회·kernel perf/trace 도구를 연결한다. | 예제·tests·fixture·LZ4와 독립이다. 실제 raw 장치/NVMe/nullb kernel perf/trace는 환경 제한으로 미검증이다. |
| `RAFTPP_INSTALL` | 최상위 기본 ON/add_subdirectory 기본 OFF. C++ 타깃·헤더·원본 라이선스와 재배치 가능한 find_package 설정을 설치한다. | 독립 이동 소비자 네 구성·의존성 부재·공백 경로·ON→OFF 기록을 재사용한다. C 기준 라이브러리는 export에 포함하지 않는다. |
| `BUILD_SHARED_LIBS` | C++와 C 기준 타깃의 정적·공유 빌드 및 C++ 공개 심볼/외부 소비자를 지원한다. | 새 공개 API나 링크 요구사항을 바꾸면 해당 소비자만 재검증한다. 기존 결과의 적용 범위는 단위별 기록을 따른다. |

`RAFTPP_WITH_LZ4=ON`은 `RAFTPP_WITH_UV=ON`을 필요로 한다. 옵션을 끄는 재구성에서는 기존에 생성된 C 선택 헤더도 제거한다. 생성 헤더는 선택 기능의 빌드 구성을 표현하며 이식 완료 목록으로 사용하지 않는다.

## 검증 기록

기반 기능·로그·저장 값·선거·이벤트 코어·공개 노드 드라이버 검증은 이전 작업의 기록이다. 바이트 형식·체크섬·메타데이터·스냅샷·segment 파일과 낮은 계층의 비동기 writer 검증도 이전 기록이며, 현재 로그 쓰기 coordinator 검증은 아래 별도 항목에 기록한다.

- Debug 기본 구성에서 `raftpp.buffer`, `raftpp.configuration`, `raftpp.trail`, `raftpp.compression` 테스트가 통과했다.
- 설정은 600회 변경 시퀀스의 결과·오류를 C 구현과 비교하고, 인코딩 바이트·패딩 및 양방향 디코딩을 확인했다. 잘린 입력·과도한 개수·출력 크기 부족 시 상태 보존도 검사했다.
- term 추적기는 10,000회 추가·잘라내기·스냅샷·복구 시퀀스를 C 구현과 비교했다. 이 결과를 합의 알고리즘 전체의 검증으로 확대 해석하지 않는다.
- LZ4 활성 구성에서는 알려진 크기·알려지지 않은 크기의 프레임, 출력 제한, 잘린 프레임·체크섬 오류와 빈 프레임을 검사했다. 비활성 구성에서는 오류 반환과 출력 보존을 확인했다.
- 다음 6개 조합에서 빌드·테스트 및 선택 헤더·링크 심볼을 확인했다. 정적·공유 모드를 모두 포함했으나, 6개 조합 각각에 두 모드를 모두 적용한 12개 조합 검증은 아니다.

| UV | LZ4 | Fixture | 검증한 링크 모드 |
| --- | --- | --- | --- |
| OFF | OFF | OFF | 공유 |
| OFF | OFF | ON | 정적 |
| ON | OFF | OFF | 정적 |
| ON | OFF | ON | 공유 |
| ON | ON | OFF | 공유 |
| ON | ON | ON | 정적 |

- 선택 기능을 켰다가 끄는 재구성과 `UV=OFF, LZ4=ON`의 configure 거부를 확인했다. 코어 전용 구성은 PkgConfig 검색을 비활성화하고 검사했다.
- 외부 CMake 프로젝트에서 `raftpp::raftpp`만 연결해 설정 인코딩·디코딩과 해제 API 호출을 빌드·실행했다. 해당 C++ 구현은 기존 C 라이브러리에 링크를 위임하지 않는다.
- ASan/UBSan을 적용한 기반 테스트가 통과했다. LeakSanitizer는 실행 환경의 ptrace 관련 제한으로 실패하여 누수 검사를 제외했다. 누수 검사 통과 결과는 없다.

### 로그·저장 값 이식 검증 (2026-11-02)

- 기본 Debug 구성의 테스트 9개가 통과했다. 추가한 테스트는 `raftpp.shared_buffer`, `raftpp.entries`, `raftpp.log`, `raftpp.log_lifetime`, `raftpp.snapshot`이다.
- 메모리 로그는 6,000회의 추가·잘라내기·스냅샷·복구·획득·해제 연산을 C 구현과 비교했다. 작업 중 보관한 엔트리의 메타데이터와 바이트도 계속 비교하고, 같은 `(term,index)`의 busy·해제 후 재시도와 다른 term의 대체를 별도로 검사했다.
- 엔트리 깊은 복사·패딩 없는 배치 레이아웃과 스냅샷 단일 버퍼 병합을 C 구현과 비교했다. 자기 자신을 대상으로 한 복사, 빈 payload, 빈 입력, 공유 배치의 마지막 참조 해제, 정렬·이동·부분 범위 경계를 확인했다.
- 원래 C 수동 해제 모델의 외부 계약을 C++ RAII로 표현한다. 획득한 핸들은 메모리 로그가 파괴된 뒤에도 엔트리·배치를 유지한다. 새 로그의 `discard`는 공유 참조를 놓으며, 호출자가 가진 공유 payload는 유지된다. 실제 비동기 요청과의 수명 연결은 아직 검증 대상이 아니다.
- 위 6개 기능 옵션·링크 모드 조합에서 각각 테스트 9개와 선택 헤더·링크 심볼을 다시 확인했다. Release의 C++ 코드는 `-Wall -Wextra -Wpedantic -Werror`로 빌드했다. 선택 기능을 끄는 재구성, PkgConfig 없는 코어 구성, 잘못된 UV/LZ4 조합 거부도 확인했다.
- ASan/UBSan의 테스트 9개가 통과했다. 앞서 확인한 환경 제한으로 `detect_leaks=0`을 사용했으며 누수 검사는 수행하지 않았다.
- 외부 CMake 프로젝트에서 `raftpp::raftpp`만 연결해 새 버퍼·엔트리·스냅샷 API를 빌드·실행했다. 공유 코어 전용 구성과 LZ4 활성 정적 구성을 확인했으며 링크 명령에는 기존 C 라이브러리가 포함되지 않았다.
- 한국어·영어 API 문서의 코드 예제와 상대 링크를 확인했다. 기준 구현인 `raft/`는 수정하지 않았다.

### 선거·복제 진행 상태 이식 검증 (2026-11-02)

- 기본 Debug 구성에서 테스트 15개가 통과했다. 추가한 테스트는 `raftpp.random`, `raftpp.progress`, `raftpp.election`, `raftpp.vote`, `raftpp.vote_request`, `raftpp.messages`다.
- 난수는 4개 seed의 유효 범위·전체 32비트 범위에서 출력과 후속 상태를 C와 비교했다. 같은 하한·상한에서는 C 기준 구현이 0으로 나누는 정의되지 않은 동작을 가지므로, C++에서는 상태를 진행시키지 않고 유일한 값을 반환한다. 일반 선거 시퀀스는 변경하지 않는다.
- 복제 진행 상태는 12,000단계의 probe·pipeline·snapshot 전환, 응답·거절, 타임스탬프·capability·catch-up과 스냅샷 제한 시간을 C와 비교했다. 구성 재배열·추가·삭제 시 ID별 상태 보존과 인덱스 오버플로 거부도 검사했다.
- pre-vote 유무별 200회 선거 시작과 중복 투표를 포함한 집계, follower 타이머 1,000회, voter·standby·spare의 투표 판단 15,000회를 C와 비교했다. 디스크에 저장된 prefix만 광고하는지 확인했다.
- RequestVote 3,000건을 C의 공개 `raft_step()` 경로와 비교했다. follower·candidate·leader, 높은·낮은 term, 기존 leader 존재, pre-vote·disrupt_leader·스냅샷 설치 상태에서 역할·term·vote·응답·타이머·업데이트 플래그를 확인했다.
- 메시지가 설정 객체의 수명에 의존하지 않고 주소·설정·payload를 유지하는지 확인했다. 표준 variant가 역할별 vector·문자열을 정리한다. 실제 전송 완료 콜백과의 연결 검증은 남아 있다.
- 위 6개 기능 옵션·링크 모드 조합에서 각각 테스트 15개, 선택 헤더와 링크 심볼을 다시 확인했다. Release C++은 `-Wall -Wextra -Wpedantic -Werror`로 빌드했다. 코어 전용 구성은 PkgConfig 없이 빌드하고, 옵션 비활성 재구성과 잘못된 UV/LZ4 조합 거부도 확인했다.
- ASan/UBSan에서 테스트 15개가 통과했다. 앞서 확인한 환경 제한으로 누수 검사는 제외했다. C 비교 실행 파일만 기준 C 라이브러리에 연결한다.
- 공개 API 변경이 없어 API 설명서는 변경하지 않았다. 진행 기록의 한국어·영어 대응과 상대 링크를 확인했다. `raft/` 원본은 변경하지 않았다. 할당 실패 주입, 추적 연결과 전체 노드 통합 검증은 남아 있다.

### 이벤트 구동 코어 이식 검증 (2026-11-02)

- 기본 Debug 구성의 테스트 22개가 통과했다. 새 테스트는 `raftpp.core_election`, `raftpp.core_replication`, `raftpp.core_conflict`, `raftpp.core_membership`, `raftpp.core_snapshot`, `raftpp.core_cluster`, `raftpp.core_failure`다.
- 내부 `step()`에 시작·수신·저장 완료·설정 적용·스냅샷·타임아웃·제출·catch-up·리더십 이전을 연결했다. 각 이벤트를 C의 공개 `raft_step()`에도 전달해 역할·term·vote·로그 term·commit·설정·진행 상태·타이머·업데이트 플래그·RPC 메타데이터·개수와 저장할 엔트리·스냅샷 바이트를 비교했다.
- 초기 barrier, 단일 voter 자동 선출, 로컬·원격 영속화와 quorum, 중복 요청·응답, 충돌 잘라내기, committed 충돌의 shutdown, 용량 제한, 설정 rollback·commit, spare catch-up과 TimeoutNow 이전을 확인했다.
- 메모리상 3노드 테스트는 pre-vote 유무별로 400개 명령과 손실·중복·재정렬을 실행했다. 리더십 이전, 소수파 리더의 미확정 쓰기, 다수파 재선거, 분할 복구 후 commit·payload 일치와 기존 committed prefix 보존을 C와 대조했다. 모의 저장 완료 이벤트를 사용하는 테스트이며 실제 디스크·네트워크 검증은 남아 있다.
- 명령 제출·설정 제출·설정 수신·스냅샷 접수의 4개 경로에서 각각 0~79번째 표준 C++ 할당에 실패를 주입했다. 미완료 추가의 로그·설정·commit·저장 결과를 복구하고 재시도가 성공하는지 확인했다. 스냅샷 완료에 필요한 설정은 제출 전에 준비하므로 완료 알림 메시지 할당이 실패해도 durable 상태 설치를 마친다.
- 전송·저장 결과물은 공유 엔트리 핸들을 소유한다. 잘라낸 엔트리와 교체된 엔트리가 공존하고, 코어 파괴 후에도 보관한 요청 데이터가 유효하며 마지막 핸들 해제 시 저장 공간이 정리됨을 확인했다. 실제 백엔드 요청·완료 콜백 연결은 별도 작업이다.
- 위 6개 옵션·링크 모드 조합에서 각각 테스트 22개, 선택 헤더·링크 심볼을 확인했다. Release C++은 `-Wall -Wextra -Wpedantic -Werror`로 빌드했다. PkgConfig 없는 코어 빌드, 선택 기능 비활성 재구성 및 잘못된 UV/LZ4 조합 거부도 검사했다. 상태 타입 수정과 빌드가 겹친 중간 객체 파일은 제거하고 안정된 최종 소스로 전체 조합을 다시 검증했다.
- ASan/UBSan에서 테스트 22개가 통과했다. 환경 제한으로 `detect_leaks=0`을 사용했으며 누수 검사 통과를 의미하지 않는다.
- 외부 CMake 프로젝트에서 raftpp::raftpp만 연결해 코어 시작·제출·영속화 완료를 실행했다. LZ4 활성 정적 구성과 선택 의존성이 없는 공유 구성을 확인했으며, 코어 파괴 후 데이터 보관과 최종 해제도 검사했다. 링크 명령에 기준 C 라이브러리는 포함되지 않았다.
- 공개 umbrella API는 변경하지 않았다. 내부 헤더와 이벤트 코어를 대상으로 작업했으며, API 설명서는 유지하고 진행 기록의 한국어·영어 대응본을 함께 갱신했다. 기준 C 소스는 변경하지 않았다.

### 공개 callback 노드 드라이버 검증 (2026-11-02)

- 기본 Debug 구성에서 테스트 28개가 통과했다. 새 공개 노드 테스트는 `raftpp.node_lifetime`, `raftpp.node_failure`, `raftpp.node_snapshot`, `raftpp.node_cluster`, `raftpp.node_restore`, `raftpp.node_allocation`이다.
- `legacy.c`의 시작·저장 완료·FSM 적용·멤버십 승격·스냅샷·종료 경로를 `node.cpp`에 연결했다. 공개 `io`·`state_machine` 인터페이스와 이동 전용 `node` 핸들을 추가했다.
- 명령·barrier·추가·승격·제거·리더십 이전, term·vote 저장 후 응답, 인라인·지연 완료, 즉시 실패 시 콜백 미호출과 받아들인 요청의 정확히 한 번 완료를 검사했다. term 기록 실패 시 투표·응답을 차단한다. 시작 전 상태는 기준 `raft_init`과 같은 `follower`로 보존한다. `nullptr`·null 함수 포인터·일반 함수 참조 콜백의 오버로드도 검사한다.
- 콜백에서 재진입·노드 파괴, 이동 전용 콜백·버퍼, 진행 중 I/O와 FSM 수명, 비동기 종료 완료 후 최종 해제를 확인했다. 표준 할당의 0~79번째 실패를 주입하고 즉시 실패·완료·종료 등록 실패를 검사했다. 단일 명령의 완료 bookkeeping이 추가 할당 없이 진행하는 경로도 검사했으며 모든 완료 경로가 무할당이라고 일반화하지 않는다.
- 3노드 공개 API와 모의 저장·전송으로 선거·명령·spare 추가·스냅샷 조회·송신·설치·catch-up 승격·제거·이전을 확인했다. 시작 시 다중 버퍼 스냅샷 복원과 미확정 로그 접미사 보존·후속 commit, 로컬 스냅샷 중 설치 대기, 두 번째 FSM 단계 실패와 저장 제출·완료 실패·취소의 정확히 한 번 정리를 확인했다. 실제 디스크·소켓 backend와 C++ fixture 검증은 남아 있다.
- 최종 콜백 오버로드 수정 후 ASan/UBSan에서 테스트 28개가 통과했다. 환경 제한으로 `detect_leaks=0`을 사용했으며 누수 검사는 수행하지 않았다.
- 외부 CMake 프로젝트가 `raftpp::raftpp`만 연결해 공개 노드 시작·버퍼 이동·이동 전용 완료·FSM 결과·종료를 실행했다. 선택 의존성 없는 공유 구성과 LZ4 활성 정적 구성을 확인했다.
- 최종 소스로 위 기능 옵션·링크 모드 6개 조합에서 각각 테스트 28개, 선택 헤더와 링크 심볼을 검증했다. Release C++은 `-Wall -Wextra -Wpedantic -Werror`로 빌드했다. 선택 의존성 없는 코어 구성, 켰다가 끄는 재구성 및 잘못된 UV/LZ4 조합 거부도 확인했다.
- API 계약을 `docs/ko/node.md`와 `docs/en/node.md`에 함께 작성했다. 문서 경로·상호 링크와 C++20 예제 컴파일을 확인했다. `raft/` 원본은 변경하지 않았다.

### 바이트 형식·체크섬 이식 검증 (2026-11-02)

- 기본 Debug 구성의 테스트 33개가 통과했다. 새 테스트는 `raftpp.checksums`, `raftpp.wire`, `raftpp.records`, `raftpp.encoding_malformed`, `raftpp.encoding_failure`다.
- CRC32와 여러 조각으로 업데이트한 SHA-1을 C와 600회 비교했다. `raft_digest`도 같은 text·64비트 n으로 비교했다. SHA-1의 `abc`와 100만 개 `a` 표준 벡터, 반복 digest·후속 업데이트·reset을 확인했다. 증분 상태는 표준 배열·정수로 보관하며 동적 할당하지 않는다.
- 6종 RPC의 600개 입력에서 C 인코더의 preamble·헤더·payload 바이트와 버퍼 개수, C 헤더 디코더의 엔트리 메타데이터·payload 길이를 비교했다. C++의 완전한 인코딩·디코딩 왕복, 수신 엔트리의 공유 저장 공간과 최종 참조 해제를 확인했다. 공개 노드 모의 전송도 이 코덱을 거쳐 선거·명령·스냅샷·승격·이전을 실행한다.
- 디스크 레코드 400개를 변경하지 않은 C segment-buffer 인코더의 실제 출력과 비교했다. 이 비교는 메모리상 레코드 형식의 검증이며 파일 I/O·복구 정책의 검증이 아니다. UV 비활성 구성에서는 libuv 없이 C++ 왕복·오류 검사를 실행하고, UV 활성 구성에서 C wire·segment 비교 소스를 추가한다. 비교 타깃은 함수별 section과 linker GC로 해당 레코드 버퍼 함수만 사용한다.
- 각 RPC의 모든 잘린 길이, 레코드의 모든 잘린 길이·바이트 손상, 과도한 개수·설정 제한·잘못된 타입·이전 버전과 비정상 입력 5,000건을 검사했다. 8개 인코딩·디코딩 경로에서 각각 0~79번째 표준 할당 실패를 주입해 실패 시 출력·consumed 보존과 할당에 성공하는 호출을 확인했다.
- C 저장 경로의 엔트리 길이 8바이트 배수 제한과 기존 CRC·little-endian 형식을 유지한다. 네트워크는 C 송신 바이트와 같이 payload를 패딩 없이 연결한다. 기존 C 수신 helper의 패딩 포인터 계산을 정렬되지 않은 payload에 적용하지 않으며, 32바이트 이전 RequestVote의 flags 범위 밖 읽기도 재현하지 않는다. C++의 SHA-1 digest 조회는 상태를 소비하지 않고, string_view digest는 내장 NUL을 포함한 지정 길이를 해시한다. 일반 C 문자열의 자유 함수 digest 결과는 C와 일치한다.
- ASan/UBSan에서 테스트 33개가 통과했다. 기존 환경 제한으로 `detect_leaks=0`을 사용했으며 누수 검사는 제외했다.
- 외부 CMake 소비자가 C++ 타깃만 연결해 체크섬·digest, RPC 인코딩·디코딩, 레코드 인코딩·공유 payload 디코딩을 실행했다. 선택 의존성 없는 공유 구성과 LZ4 활성 정적 구성 모두 통과했다.
- 최종 소스로 위 기능 옵션·링크 모드 6개 조합에서 각각 테스트 33개, 선택 헤더·링크 심볼을 확인했다. Release C++은 `-Wall -Wextra -Wpedantic -Werror`로 빌드했다. PkgConfig 없는 코어 빌드, 선택 기능 비활성 재구성과 잘못된 UV/LZ4 조합 거부도 통과했다. 해당 단위의 검증에는 실제 segment 파일·스냅샷 메타데이터·압축 파일 읽기·socket handshake·backend 통합이 포함되지 않았다.
- API 계약과 코드 예제를 `docs/ko/encoding.md`·`docs/en/encoding.md`에 작성하고 두 언어 예제의 C++20 컴파일과 문서 링크를 확인했다. 원본 `raft/`는 수정하지 않는다.

### 메타데이터·스냅샷 파일 이식 검증 (2026-11-02)

- 기본 Debug 구성의 테스트 39개가 통과했다. `raftpp.metadata_formats`는 옵션과 무관하게 실행한다. UV 활성 시 `raftpp.metadata_files`, `raftpp.snapshot_files`, `raftpp.disk_failures`, `raftpp.compressed_snapshot`, `raftpp.disk_no_space`를 추가한다. UV 비활성 구성의 전체 테스트는 34개다.
- `uv_metadata.c`·`uv_snapshot.c`·`uv_fs.c`·`uv_os.c`와 `uv.c`의 초기화·term/vote 갱신 경로를 참조해 동기 파일 계층을 독립 구현했다. `metadata1`·`metadata2`의 세대 비교, 짧은 파일 제거·이전 슬롯 복구, 초과 길이·동일 세대 거부, 새 파일의 fsync→close→디렉터리 fsync와 기존 파일의 fdatasync→close 순서를 확인했다. 성공한 기록만 캐시에 반영하고, 파일 변경 후 동기화가 불확실해지면 같은 인스턴스의 추가 메타데이터 쓰기를 중단한다.
- 스냅샷 메타데이터 CRC·설정 형식과 파일명(term,index,timestamp), 익명 `O_TMPFILE` 준비·내용 fsync, metadata→data 공개 후 디렉터리 fsync, orphan·tmp 파일 정리와 최신 두 스냅샷 보관을 구현했다. 준비 단계와 공개 단계 사이의 barrier 조정은 아직 포함하지 않는다. 준비 객체의 이동·폐기·저장소 객체보다 긴 수명과 실제 파일 descriptor 개수를 검사했다.
- C가 C++의 메타데이터와 스냅샷을 읽고 C++가 C의 메타데이터·스냅샷 fixture를 읽는 검사가 통과했다. C 스냅샷 fixture는 C 설정 인코더·byte helper·동기 파일 writer를 사용하며 C 비동기 스냅샷 드라이버를 실행한 것은 아니다. 손상된 스냅샷 메타데이터는 양쪽에서 거부했다. 파일명 충돌은 기존 pair를 변경하지 않는다.
- 메타데이터·스냅샷 형식 500회 왕복, 잘린 길이·CRC 손상·세대 0·잘못된 형식·할당 실패의 출력 보존을 확인했다. 실제 파일 연산의 open·write·allocate·fsync·fdatasync·close·공개·디렉터리 동기화 오류를 주입하고, 스냅샷 준비의 0~79번째 표준 할당 실패에서 핸들이 정리됨을 확인했다.
- 네이티브 `posix_fallocate`의 ENOSPC 주입으로 기존 C와 같은 `no_space`를 반환하는 회귀 검사를 추가했다. 초기 C++ 구현이 `io_error`로 반환하던 매핑을 수정했다. 테스트 전용 interposer는 일반 호출을 실제 libc로 전달하고 이 검사에서만 ENOSPC를 반환한다. LZ4 활성 압축 스냅샷 읽기와 비활성 시 오류·출력 보존도 검사한다.
- 최종 ASan/UBSan에서 테스트 39개가 통과했다. 기존 환경 제한으로 `detect_leaks=0`을 사용했으며 누수 검사는 제외했다. 외부 소비자는 선택 의존성 없는 공유 구성에서 메타데이터 코덱을, LZ4 활성 정적 구성에서 실제 파일 쓰기·스냅샷 공개·다시 열기를 실행했다. 최종 기능 옵션·링크 모드 6개 조합도 통과했다. UV 활성 조합은 각각 테스트 39개, 비활성 조합은 각각 34개다. Release C++은 `-Wall -Wextra -Wpedantic -Werror`로 빌드하고, 비활성 시 C++ libuv 심볼·의존성 제외와 잘못된 UV/LZ4 조합 거부도 확인했다. 실제 전원 차단·모든 파일시스템의 내구성 검증은 수행하지 않았으며, 확인한 범위는 파일 형식·명시적 동기화 순서·부분 파일과 실패 정리다. segment 파일·복구·비동기 I/O coordinator·네트워크 전송은 남아 있다.
- 새 공개 메타데이터 코덱을 한국어·영어 API 설명서에 함께 기록하고 링크·예제 컴파일을 확인했다. `disk_storage`와 `prepared_snapshot`은 내부 파일 계층이며 완성된 공개 I/O backend가 아니다. 원본 C 소스는 변경하지 않는다.

### Segment 파일·저장 상태 복구 검증 (2026-11-02)

- 기본 Debug 테스트 43개가 통과했다. UV 활성 구성에 `raftpp.segment_files`, `raftpp.segment_recovery`, `raftpp.segment_snapshot`, `raftpp.segment_failures`를 추가했다. UV 비활성 구성은 기존 34개 테스트를 유지한다.
- `uv_segment.c`, `uv_list.c`, `uv_truncate.c`, `uv_finalize.c`, `uv.c` 및 파일·배치·설정·해제 경로를 추적했다. `detail/disk.hpp`와 `disk.cpp`에 닫힌 segment의 임시 파일 작성→파일 fsync→rename→디렉터리 fsync, 읽기·배치 수명 공유, open 파일의 정상 prefix 복구·빈 파일 제거·닫힌 이름 변경, trailing 보존과 suffix 잘라내기를 독립 구현했다.
- 전체 저장 상태 로드에 최신부터 이전 스냅샷 fallback, 최신 연속 segment 구간 선택, 스냅샷 이후 gap 검증, open counter 정렬, 마지막 손상 segment와 이후 open 파일 격리, corruption 시 한 번 재시도를 연결했다. bootstrap은 term을 먼저 기록하고 설정 엔트리를 작성하며, recover는 마지막 로그 term으로 새 설정을 추가한다. 비동기 제출·완료·barrier·direct I/O는 포함하지 않는다.
- `segments.cpp`는 80개 연속 segment의 엔트리·payload를 C와 비교한다. C의 실제 동기 load/bootstrap/recover 진입점을 사용해 open 파일·CRC 손상·0 채움·잘린 입력·malformed 타입·잘못된 count·중간/마지막 손상·gap·스냅샷 fallback·경계 선택의 결과와 복구 후 파일 바이트를 비교했다. C 비교 bridge는 transport·timer를 초기화하지 않으므로 실제 비동기 backend 검증이 아니다. 잘라내기는 C prefix 재작성과 worker의 후속 파일 제거 순서를 별도로 비교한다.
- scan·open·read·write·fsync·close·공개·truncate·rename·remove·디렉터리 sync 실패를 주입했다. 로드 할당 실패 위치 0–99, 파일 생성 0–69에서 출력 보존·FD 정리·재시도를 검사했다. 삭제 실패 시 원본과 먼저 영속화한 잘린 파일이 함께 남는 C의 한계도 검사한다. open 복구의 파일 제거·rename 후 디렉터리도 동기화하며 이 추가 동기화의 실패는 I/O 오류로 반환한다. 이는 원자적인 다중 파일 트랜잭션이나 전원 차단 복구를 보장하지 않는다.
- 할당 실패 주입에서 libstdc++ `directory_iterator` 내부의 예외 종료를 backtrace로 확인했다. 오류 코드 계약을 지키기 위해 디렉터리 스캔만 기존 libuv의 RAII 요청으로 바꾸고, 경로·컨테이너·문자열·정렬·배치 소유권은 표준 구현을 사용한다. 닫힌 파일 인덱스는 C의 16자리 형식 범위를 검사하고, overflow·중복 counter·잘못된 범위와 스냅샷 없는 로드의 overlap을 안전하게 거부한다.
- 공개 사용자 API는 변경하지 않았으며 이 단위는 `detail` 파일 계층이다. API 전용 `docs/`는 변경하지 않았다. ASan/UBSan에서도 테스트 43개가 통과했으며 환경 제한으로 누수 검사는 제외했다. 외부 소비자의 core 전용 공유·UV/LZ4 공유·UV/LZ4 정적 구성도 통과했으며 새 파일 계층을 실행했다. 기능 옵션 6개 조합에서 UV 활성 43개·비활성 34개 테스트와 선택 헤더·심볼 검사가 통과했다. Release C++은 `-Wall -Wextra -Wpedantic -Werror`를 사용했다. 선택 기능 해제 재구성·PkgConfig 없는 core 빌드·UV 없는 LZ4 활성 거부도 확인했다. 기존 C 소스는 보존했다.

### 비동기 파일 writer 검증 (2026-11-02)

- 기본 Debug 테스트 48개가 통과했다. UV 활성 구성에 `raftpp.writer_basic`, `raftpp.writer_async`, `raftpp.writer_close`, `raftpp.writer_failure`, `raftpp.writer_allocation`을 추가했다. UV 비활성 구성은 34개 테스트를 유지한다. ASan/UBSan 테스트 48개와 core 전용 공유·UV/LZ4 공유·정적 외부 소비자가 통과했다. 환경 제한으로 누수 검사는 제외했다. 기능 옵션 6개 조합에서 UV 활성 48개·비활성 34개 테스트와 선택 헤더·심볼 검사가 통과했다. Release C++은 `-Wall -Wextra -Wpedantic -Werror`를 사용했고, 선택 기능 해제 재구성·PkgConfig 없는 core 빌드·UV 없는 LZ4 활성 거부도 확인했다. 마지막 누적 길이·offset 보완 후 기본 48개와 Release/ASan/UBSan writer 5개를 다시 검증했다.
- `uv_writer.c`, `uv_writer.h`, `uv_os.c`, `syscall.c` 및 `uv_append.c`의 호출·종료 경로를 추적했다. `file_writer`가 RWF_DSYNC scatter write, direct I/O, RWF_NOWAIT·eventfd·UV poll, 제출/완료 EAGAIN의 threadpool 재시도와 병렬 요청을 구현한다. 기존 파일 RAII를 `file_handle`로 분리해 동기 저장 계층에서도 재사용한다. 코루틴 API는 추가하지 않았다.
- 요청·iovec·공유 버퍼를 표준 소유 타입과 list로 유지한다. 요청 제거 후 콜백을 호출하여 재진입 제출을 지원하고, 콜백 동안 payload가 생존한다. 종료·소유자 파괴 후에도 accepted 요청을 유지하며 kernel io_destroy·FD 닫기를 worker에서 수행한다. polled 요청의 취소 콜백은 커널의 버퍼 사용 종료 후, close 콜백은 요청과 라이브러리의 버퍼 참조·native 핸들 해제 후에 실행한다. worker 요청은 정상 완료를 전달한다.
- 원본 C writer와 buffered/direct 파일 바이트를 비교했다. C capability probe에서 block size 4096과 NOWAIT 지원을 확인했으며 실제 polled 완료, 다중 요청, 제출 및 완료 EAGAIN fallback을 실행했다. 원본 poll 콜백의 EAGAIN 후 조기 return이 나머지 완료를 건너뛰는 경로는 C++에서 전체 이벤트 순회로 처리한다. C의 kernel 정리 전 취소 메모리 해제 순서는 재현하지 않는다. 합의 알고리즘·디스크 형식은 변경하지 않았다.
- 초기화·queue·제출·eventfd 읽기·완료 결과·정리 queue 실패, 음수 I/O 결과와 short-write 상태, busy/offset·누적 길이 검증, 콜백 예외와 정리 재시도를 검사했다. 누적 쓰기 길이는 `size_t` 폭과 무관한 `uint64_t`로 관리한다. 표준 할당 실패 위치 0–19(초기화)·0–24(제출), close 무할당, 이동·소유자 파괴·진행 중 worker/poll 종료와 정확히 한 번 완료를 검사했다. FD 비교는 libuv의 최초 프로세스 전역 signal pipe 생성 후 수행한다.
- `RAFTPP_WITH_UV=ON`이면 `raftpp::raftpp`가 libuv 헤더·링크 사용 조건을 전달한다. 한국어·영어 node API 문서에 이를 설명했다. writer 자체는 `detail` 구성요소다. C++ 파일시스템 capability probe·prepare pool·append/barrier·상위 I/O backend·tracing 연결은 남아 있으며 기존 C 소스는 보존했다.

기본 검증 명령:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DRAFTPP_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### 로그 쓰기·barrier·truncate coordinator 검증 (2026-11-02)

- 기본 Debug와 ASan/UBSan의 전체 테스트 56개가 통과했다. UV 활성 구성에 `raftpp.segment_buffer`, `raftpp.disk_probe`, `raftpp.log_append`, `raftpp.log_barrier`, `raftpp.log_close`, `raftpp.log_failure`, `raftpp.log_allocation`, `raftpp.native_node`를 추가했다. Release의 `-Wall -Wextra -Wpedantic -Werror` 빌드와 새 테스트 8개도 통과했다. LeakSanitizer는 기존 환경 제한으로 `detect_leaks=0`을 사용하여 제외했다.
- 위 표의 기능 옵션·링크 모드 6개 조합에서 UV 활성 56개·비활성 34개 테스트, 선택 헤더·링크 심볼 검사가 통과했다. Release C++은 `-Wall -Wextra -Wpedantic -Werror`로 빌드했다. 선택 기능 해제 재구성·PkgConfig 검색 없는 core 전용 구성과 UV 없는 LZ4 활성 거부도 확인했다. 이 결과는 각 옵션 조합에 정적·공유를 모두 적용한 12개 조합 검증을 뜻하지 않는다.
- 참조 범위는 `uv_fs.c`·`uv_os.c`·`uv_segment.c`·`uv_prepare.c`·`uv_append.c`·`uv_finalize.c`·`uv_truncate.c`와 `uv.c`·`uv_snapshot.c`·`legacy.c`의 호출·요청·취소·종료 경로다. 이 중 실제 수정 범위는 내부 probe·배치 staging·prepare pool·append/finalize·barrier·truncate와 파일 할당·writer 초기화 정리다. 스냅샷 호출 경로를 참고한 사실은 상위 스냅샷 조정 구현을 뜻하지 않는다.
- [probe.hpp](modules/uv/privates/probe.hpp)·[probe.cpp](modules/uv/src/probe.cpp), [segment_buffer.hpp](modules/uv/privates/segment_buffer.hpp)·[segment_buffer.cpp](modules/uv/src/segment_buffer.cpp), [log_writer.hpp](modules/uv/privates/log_writer.hpp)·[log_writer.cpp](modules/uv/src/log_writer.cpp)를 추가했다. probe는 direct I/O 정렬과 NOWAIT 지원을 판별한다. 배치 staging은 마지막 부분 블록을 유지하고, 제출용 정렬 복사본은 완료·재시도 동안 변하지 않는다. 복사를 제거하거나 성능이 개선되었다고 주장하지 않는다.
- 두 개의 idle 파일 준비, 한 번에 하나의 준비 worker, 순서대로 기록하는 append·finalize, 연산별 재시도 시점, 선행 segment의 완료를 기다리는 barrier를 구현했다. blocking barrier는 후속 쓰기를 중지하며 nonblocking barrier는 허용한다. suffix truncate는 blocking barrier와 worker로 연결한다. 다음 append 인덱스 이후 truncate는 즉시 성공하여 기존 노드 드라이버의 매 append 전 truncate 호출도 처리한다.
- [log_writer.cpp](tests/log_writer.cpp)와 `tests/log_writer_reference.c` (제거됨)는 실제 C native backend의 닫힌 segment 파일명·전체 바이트와 C 배치 버퍼·capability 결과를 비교한다. buffered 512바이트와 자동 probe의 direct/NOWAIT 경로, 반복 flush·부분 블록·큰 배치·segment 교체와 다시 읽기를 검사했다. C 비교 transport는 소켓을 사용하지 않으므로 네트워크 검증은 아니다.
- 요청 순서·재진입·소유자 파괴·정확히 한 번 완료, 진행 중 준비/쓰기/truncate의 종료, 시작 전 truncate 취소와 진행 중 truncate 완료 후 close를 검사했다. append 수락 후 오류는 완료 콜백으로만 전달한다. prepare의 no-space 재시도·쓰기 완료 오류 재시도·finalize rename 오류와 durable prefix 복구, 초기화 0–34·append 0–49번째 표준 할당 실패 및 무할당 close를 검사했다. 실패한 낮은 writer 초기화의 native 정리도 상위 소유자가 완료까지 기다린다.
- 테스트용 `io` 어댑터와 공개 `node`를 연결하여 단일 voter가 명령 12개를 실제 디스크에 기록하고, 콜백 안에서 종료한 뒤 새 FSM으로 재시작하여 다음 명령과 함께 13개를 적용함을 확인했다. 이 어댑터는 전송·스냅샷 I/O를 제공하지 않으며 완성된 공개 native backend가 아니다.
- `raftpp::raftpp`만 연결하는 외부 소비자의 core 전용 공유·UV/LZ4 공유·UV/LZ4 정적 구성이 통과했다. UV 구성은 실제 로그 append→truncate→close를 콜백에서 이어 호출한다. 새 코드는 UV 옵션으로 제외한다. API 전용 `docs/`에 작업 기록을 추가하지 않았다. 파일 오류의 다중 파일 원자성·전원 차단 복구와 네트워크·fixture는 이번 검증 범위가 아니다.
- 한국어·영어 문서 5쌍의 절·표 구조와 번역된 주석을 제외한 코드 예제, 상대 링크 258개를 확인했다. 기준 C 소스는 변경하지 않았다.

### 스냅샷 coordinator·공개 libuv I/O 검증 (2026-11-02)

- 옵션 검증의 마지막 정적 구성에서 고정한 리더로 명령을 제출하는 3노드 검사가 한 번 실패했다. 진단을 추가한 뒤 20회 반복에서는 재현되지 않았다. 검사를 매 명령 전 현재 리더 조회, 1초 선거 시간, 명시적인 리더십 이전과 전체 노드 오류 확인으로 보완했다. 보완 뒤 Release 3노드 검사가 5회 연속 통과했고 기본 및 ASan/UBSan 전체 66개도 다시 통과했다. 사용자 요청에 따라 중복 전체 옵션 재실행은 중단했다. 수정된 공유/LZ4 3노드 검사만 이후 단독 통과했으며 생산 코드는 변경하지 않았다.

- 새 테스트 10개가 통과했다. `raftpp.file_snapshots`, `raftpp.file_snapshot_order`, `raftpp.file_snapshot_failure`, `raftpp.file_snapshot_close`, `raftpp.file_snapshot_allocation`, `raftpp.uv_io_lifetime`, `raftpp.uv_io_node`, `raftpp.uv_io_limits`, `raftpp.uv_io_allocation`, `raftpp.uv_io_cluster`다. `-Wall -Wextra -Wpedantic -Werror` Release 빌드와 같은 10개 검사도 통과했다. 기본 및 ASan/UBSan 전체 테스트 66개와 core 전용 공유·UV/LZ4 공유·UV/LZ4 정적 외부 소비자가 통과했다. 환경 제한으로 `detect_leaks=0`을 사용하여 누수 검사는 제외했다. 옵션별 실행·재사용·중단 범위는 아래 기준 표에 구분했다.
- 재귀 참조 범위는 `uv.c`·`uv_snapshot.c`·`uv_append.c`·`uv_finalize.c`·`uv_truncate.c`와 기존 C 공개 I/O·transport 선언, `legacy.c`·`node.cpp`의 저장·설치·콜백·종료 경로다. 실제 구현은 `file_backend`의 스냅샷 prepare 재시도→barrier→공개·보존, 비동기 get·close와 공개 `uv_io`의 파일·타이머·transport 연결이다. TCP connect/listen·handshake·stream 수신/송신을 구현했다고 해석하지 않는다.
- 준비 실패는 기존 disk-retry 간격으로 재시도한다. 로컬 스냅샷의 barrier는 후속 쓰기를 허용하고 설치용 barrier는 다음 인덱스를 변경한 뒤 공개·정리까지 후속 쓰기를 막는다. 최신 두 스냅샷과 trailing segment 정책을 유지한다. 공개 실패는 완료 오류이며 rollback을 보장하지 않는다. 실제 C native backend의 trailing 0·3 각각 3회 put 경로와 파일명(타임스탬프 정규화)·닫힌 segment·metadata·data 바이트 및 보존 결과를 비교했다.
- `file_backend`는 요청과 공유 스냅샷·버퍼를 native 작업이 끝날 때까지 유지한다. 준비 중·재시도·barrier 대기는 종료 시 취소하고, 이미 실행 중인 공개·조회는 완료까지 기다린다. 실패한 로그 writer 초기화의 비동기 정리가 끝났음을 상위 coordinator에 알리는 콜백도 연결했다. 요청 데이터는 close 콜백 전에 놓으며, 사용자가 보유한 참조의 해제까지 기다리지는 않는다.
- 공개 `uv_io`는 probe·메타데이터·bootstrap/recover/load·로그 쓰기·truncate·스냅샷·시간·난수·타이머를 `io`에 연결하고, `transport`는 메시지 수신·송신·종료의 경계를 정의한다. 콜백에서 종료·소유자 파괴·예외가 발생해도 native 경계를 넘지 않고 수명을 유지한다. 활성 tick·receive 콜백은 표준 공동 소유로 보관한다. C의 `std::rand` 기반 maximum 제외 범위를 유지하고, 용량 보고는 16비트 wrap 대신 최댓값에서 포화한다.
- 준비 no-space 재시도, 공개 및 준비/공개/조회 queue 오류, 준비/공개/조회 중 종료와 긴 재시도 중 취소를 검사했다. 내부 open/put/get의 0–44번째와 공개 init/load/start/send의 0–54번째 표준 할당 실패, 즉시 실패의 콜백 미호출, 무할당 close와 FD 정리를 검사했다. 시작 시 스냅샷 크기 제한 실패는 출력을 유지하며 종료한다.
- 공개 노드는 실제 디스크 스냅샷으로 재시작하여 새 FSM에 복원하고 로그 접미사를 적용한다. 3노드 테스트는 실제 파일 저장과 테스트 메시지 전송/코덱으로 선거, 분할 중 명령 16개 commit·다수파 내 리더십 이전, 디스크에서 조회한 스냅샷을 통한 뒤처진 노드의 설치·따라잡기, 복구 후 17번째 명령과 전체 종료·저장 상태 재로드를 확인했다. 소켓 또는 전원 차단 복구 시험은 아니다.
- API 계약은 `docs/ko/uv_io.md`와 `docs/en/uv_io.md`에 작성하고 node 문서에서 연결했다. `transport`는 UV 없이도 사용할 수 있는 추상 인터페이스이며 `uv_io`·내부 파일 coordinator·관련 테스트·libuv 의존성은 UV 옵션으로 제외한다. 기준 C 소스는 변경하지 않았다. TCP 전송·tracing·사용자 heap/PMR·fixture·예제/도구와 남은 호환 설정은 별도 남은 범위다.

## 검증 기록 재사용 기준과 현재 기준 결과

현재 기록 방식: `검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유`. 관련 구현·테스트·공유 의존성과 검증 조건이 그대로인 검사는 `생략 — 기존 결과 재사용`으로 남긴다. 새 검사나 영향받는 검사만 실행하며 CTest 등록은 유지한다.

2026-11-03 재확인: 최초 실행 기록은 보존하고, 후속 작업에는 해당 기록의 절·검사명과 생략 이유를 참조한다. 기록 갱신만을 위한 재실행은 하지 않는다. 이 문서·지침 보완에서는 빌드·테스트를 실행하지 않았고 새 통과 결과는 없다. 별도로 진행 중인 코드 변경의 영향까지 검증된 것으로 간주하지 않는다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 2026-11-03 테스트 기록 보존 지침 보완의 빌드·테스트 | 생략 | 아래 기존 재사용 기준 및 각 검사의 원래 실행 기록 참조; 신규 통과 없음 | 이번 보완은 AGENTS·REWRITTEN 두 언어본만 수정한다. 별도 코드 변경의 검증 상태는 유지하며 이 기록으로 통과를 주장하지 않는다. |

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 이번 테스트 기록 형식 명시 작업의 빌드·런타임 테스트 | 생략 | 기존 결과 재사용 — 아래 및 후속 단위의 해당 검증 기록 | 한·영 지침과 진행 기록만 수정했다. 구현·테스트·빌드 규칙은 변경하지 않았으며 새 통과 결과를 주장하지 않는다. |

2026-11-02 사용자 지시에 따라 관련 코드가 바뀌지 않은 검사는 생략하고 기존 결과를 재사용한다. 전체 재검증 프로세스는 중단했으며 중단된 전체 실행을 통과로 기록하지 않는다. 이번 규칙·문서 변경에 대해서는 빌드·런타임 테스트를 추가하지 않았다. 아래 결과는 앞선 구현 변경의 검증 기록이다.

검증 환경은 Linux, C++20, GCC 13.3.0, libuv 1.48.0, LZ4 1.9.4다. Release는 `-Wall -Wextra -Wpedantic -Werror`, sanitizer는 ASan/UBSan 및 `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`을 사용했다. 환경·도구·옵션이 결과에 영향을 줄 정도로 바뀌면 해당 범위의 기록을 무효화한다.

| 검사 범위 | 관련 구현·공유 의존 범위 | 기준 기록과 재사용 조건 |
| --- | --- | --- |
| 기본 전체 66개·ASan/UBSan 66개 | 앞 절의 전체 이식 구현과 테스트, 공통 헤더·생성 설정 | 보완한 3노드 검사까지 전체 통과했다. 관련 구현·검사·구성이 같으면 생략한다. 이 행 전체가 아니라 실제 변경의 영향 범위만 무효화한다. |
| native 파일·공개 UV 검사 10개 | `file_backend.cpp`, `uv_io.cpp`, `disk.cpp`, `log_writer.cpp`, 대응 헤더와 버퍼·코덱·노드·코어 호출 경로; `tests/uv_io.cpp`, C 비교 bridge | Debug·Release·ASan/UBSan의 위 기록을 재사용한다. 테스트 함수 하나만 바뀌면 공유 코드에 미치는 영향을 확인하고 필요한 함수만 재검증한다. |
| `raftpp.uv_io_cluster` | 실제 파일 I/O·노드·코어·메시지 코덱, 해당 테스트의 bus/FSM/리더 선택·이전 코드 | 보완 뒤 기본·ASan/UBSan, Release 정적 5회, UV 활성·LZ4 비활성 공유와 UV/LZ4 활성 공유에서 통과했다. 마지막 공유/LZ4 구성은 기존 기록이 없던 이 검사 1개만 `ctest --test-dir /tmp/raftpp-options-build -R '^raftpp.uv_io_cluster$' --output-on-failure`로 확인했다. |
| 외부 소비자 3개 구성 | 공개 헤더·export·타깃 사용 조건과 실행한 API | core 전용 공유·UV/LZ4 공유·UV/LZ4 정적의 빌드·실행 통과를 재사용한다. 공개 `uv_io` put→get→close를 포함한다. 이후 변경은 검사 코드뿐이라 소비자를 재실행하지 않았다. |
| 잘못된 옵션 거부·선택 헤더·의존성 검사 | 해당 CMake 조건·소스 선택·헤더 생성/제거·링크 규칙 | 관련 규칙이 그대로인 항목은 앞선 옵션 검증 결과를 재사용한다. 규칙이 바뀐 항목만 다시 확인한다. |

옵션별 결과는 전체 suite의 한 번 실행과 개별 검사 결과 재사용을 구분한다.

| UV | LZ4 | Fixture | 링크 | 사용할 기록 |
| --- | --- | --- | --- | --- |
| OFF | OFF | OFF | 공유 | 코어 34개·선택 헤더/심볼 통과. UV 전용 검사 보완은 영향 없음. |
| OFF | OFF | ON | 정적 | 코어 34개·선택 헤더/심볼 통과. UV 전용 검사 보완은 영향 없음. |
| ON | OFF | OFF | 정적 | 보완 뒤 66개·선택 헤더/심볼 통과. |
| ON | OFF | ON | 공유 | 보완 뒤 66개·선택 헤더/심볼 통과. |
| ON | ON | OFF | 공유 | 보완 전 전체 66개·헤더/심볼 통과 중 바뀌지 않은 65개는 재사용한다. 수정된 3노드 검사는 보완 뒤 단독 통과했다. |
| ON | ON | ON | 정적 | 초기 실행의 개별 통과 65개를 재사용한다. 당시 실패한 3노드 결과는 재사용하지 않고, 보완 뒤 Release 5회 및 native 검사 10개 통과로 대체한다. |

생략 결과는 `생략 — 기존 결과 재사용`으로 보고한다. 이 기록은 CTest 등록을 비활성화하지 않으며 사용자가 명시적으로 요청한 재실행은 해당 범위에서 수행한다. 아직 구현·검증하지 않은 범위에 이 기록을 확대 적용하지 않는다. 새 TCP 검증 범위는 다음 절에 별도로 기록한다.

### TCP 전송 단위: 구현·검증과 재사용 기록

- 2026-11-02: `tcp_transport.hpp`·`tcp_transport.cpp`를 추가했다. 재귀 참조는 `uv_tcp*.c`·`uv_ip.c`·`uv_send.c`·`uv_recv.c`의 handshake·해석·큐·콜백·정리와 공개 C transport, 기존 C++ 코덱·`transport`·`uv_io`·노드 경계까지 추적했다. C 구현을 호출하지 않고 기존 libuv와 표준 소유 타입·컨테이너로 구현했다. 원본 `raft/`는 변경하지 않았다.
- protocol 1의 little-endian handshake·8바이트 주소 패딩, IPv4·기본 포트 8080·bind 재정의, 중복 listener 제거, 비동기 DNS와 IPv4 결과 순차 시도, 단계별 6종 RPC 수신과 소유 payload를 구현했다. 목적지 ID별 client·주소 변경·재연결과 연결 실패 뒤 최신 대기 3개 보존, 즉시 제출 오류/수락된 요청 완료를 구분한다. DNS·연결·쓰기·타이머·소켓을 정리한 뒤 종료 콜백을 호출한다. C의 listener 오류 정리 시 조기 메모리 해제와 다중 주소 순회 버그는 재현하지 않는다.
- 새 검사: `raftpp.tcp_exchange`, `tcp_fragments`, `tcp_malformed`, `tcp_retry`, `tcp_lifetime`, `tcp_allocation`, `tcp_interop`, `tcp_cluster`, `tcp_failure`, `tcp_shutdown`. 실제 C TCP의 양방향 handshake·RPC 스트림 바이트, 6종 메시지·공유 payload 수명, 조각·연속 수신, 길이·ID·protocol 오류, 연결 실패 큐·주소 변경, 콜백 중 파괴·예외, 4개 경로의 표준 할당 실패 0..39, 네이티브 제출 실패와 실제 부분 쓰기의 종료 취소를 검사했다.
- 실제 디스크·TCP 3노드는 8개 명령 뒤 follower를 종료하고 다수에서 16개를 더 commit한다. 가능한 두 리더 모두 명령 20개 이상을 담은 스냅샷 공개·정리를 마친 뒤 리더십을 이전하고 follower를 재시작한다. TCP로 받은 스냅샷의 실제 FSM restore와 24개 따라잡기, 25번째 명령, 전체 종료·재시작 후 26번째 명령을 검사했다. C 노드를 포함한 혼합 클러스터나 실제 전원 차단 검증은 아니다.
- 최초 소켓 검사는 샌드박스 `EPERM` 때문에 실행 경로에 진입하지 못했다. 루프백 소켓이 가능한 샌드박스 밖에서 검사했다. 이후 끊어진 상대 쓰기의 `SIGPIPE`는 기존 C 예제·전송 테스트와 같은 신호 처리 조건을 테스트·API 문서에 명시했다. 스냅샷 완료 횟수만 기다려 로그만으로 따라잡던 검사 조건은 실제 최신 snapshot compaction 대기로 보완했다. 해당 실패 기록은 통과로 재사용하지 않았다.

| 검사·명령 | 새 TCP 범위의 결과·재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture ON: `cmake --build build --target raftpp_tcp_tests --parallel 3` | 빌드 통과. TCP 10개 범위별 통과. 처음 7개와 추가 native 실패·종료 검사의 통과는 재사용했고, 마지막 클러스터 보완 뒤 `ctest --test-dir build -R '^raftpp.tcp_cluster$' --output-on-failure`만 통과를 확인했다. 전체 76개를 새로 실행한 기록은 아니다. |
| 엄격한 Release 공유, UV/LZ4 ON·fixture OFF: `/tmp/raftpp-options-build`, 같은 타깃 빌드와 `ctest ... -R '^raftpp.tcp_' --output-on-failure` | TCP 9개 개별 통과를 재사용하고, 보완한 `tcp_cluster` 1개만 재검증해 통과했다. C 기준 헤더만 SYSTEM 검색 경로로 격리했으며 C++는 `-Wall -Wextra -Wpedantic -Werror`를 통과했다. |
| ASan/UBSan 정적, UV/LZ4 ON·fixture OFF: `/tmp/raftpp-cmake-debug`의 같은 타깃·선택 검사 | TCP 10개 범위별 통과. 보완한 클러스터만 마지막으로 실행했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. 기존 환경 제한에 따라 누수 검사는 제외했다. |
| 외부 `add_subdirectory` 소비자 `/tmp/raftpp-tcp-consumer`: core 정적·UV/LZ4 공유, 둘 다 fixture/tests OFF | `consumer`만 빌드·실행해 통과했다. `raftpp::raftpp`만 링크한다. 새 API 예제 컴파일, TCP 공개 심볼, init/start/send 오류/close와 core-only umbrella 헤더를 확인했다. core는 PkgConfig 감지를 꺼도 빌드되며 TCP·libuv 심볼이 없다. |
| 기존 66개 runtime·파일·코어 검사, 기존 외부 소비자와 전체 옵션 matrix | **생략 — 기존 결과 재사용.** 앞 절의 스냅샷/공개 UV 기준 결과를 사용한다. 이번 변경은 새 TCP 소스·헤더·테스트, umbrella의 UV-gated 선언과 해당 CMake 등록이다. 기존 실행 경로·테스트·코덱·노드·파일·의존성 규칙은 바뀌지 않았다. 새 헤더·소스 gate와 export는 위의 새 소비자로 검증했다. 모든 옵션 조합의 새 TCP 검사 통과로 확대 보고하지 않는다. |

한국어·영어 API 설명서는 `docs/ko/tcp_transport.md`·`docs/en/tcp_transport.md`에 있으며 기존 UV 설명서에서 연결한다. `SIGPIPE`의 애플리케이션 처리 조건과 종료·오류·소유권·큐 계약을 포함한다. 문서 7개 대응 쌍의 제목·표·예제 대응과 상대 링크 320개, 새 TCP export·소비자 링크·core-only 심볼 제외, CTest 76개 등록 및 원본 C 무변경을 확인했다. CTest는 UV 비활성 시 기존 34개를 등록한다. 추적·상세 오류·사용자 지정 할당·fixture·예제·도구와 일부 호환 설정은 계속 남아 있다.

### 추적 단위: 구현·검증과 재사용 기록

- 2026-11-02: `tracing.hpp`·`tracing.cpp`와 내부 `detail/tracing.hpp`를 추가했다. 재귀 참조는 C `tracing.c/.h`·공개 tracer 선언, `raft.c`의 이벤트 요약, `legacy.c`의 오류·종료, 선거·역할 전환·수신·timeout, `uv_writer.c`의 submit/complete와 UV 파일·TCP의 호출·수명 경계다. 제품 구현은 표준 `source_location`·`shared_ptr`·`function`·atomic·chrono·stdio를 사용한다.
- C와 같은 diagnostic 코드 1·수준 1..5·1023바이트 메시지 한도, writer 제출 256·완료 512와 null 데이터, no-op/stderr 및 `LIBRAFT_TRACE` 존재 여부에 따른 전역 활성화를 구현했다. 기본 stderr는 비활성이며 노드 생성에서 C `raft_init`처럼 환경 변수 활성화를 수행한다. C의 정적 형식 버퍼는 독립 스택 버퍼로, 전역 스위치는 표준 atomic으로 바꿨다.
- 콜백을 공유 소유하며 전달 중 교체해도 실행 callable을 유지한다. 이벤트·diagnostic 데이터는 동기 대여 값이고 보관할 메시지는 호출자가 표준 문자열로 복사한다. 비활성 경로와 복사·형식화·전달은 라이브러리 할당이 없으며 관찰자 예외·중첩 추적은 연산 상태를 바꾸지 않는다. 관찰 중 객체 연산 재진입은 허용하지 않으며 tracer 교체는 허용한다.
- `node_options`·`uv_io_options`·`tcp_transport_options`의 `tracing`과 각 객체의 `set_tracer`를 추가했다. 코어 이벤트·역할/term·투표·timeout·결과 상태, 노드 오류 문맥, UV/TCP 수명·연결·재시도·수신과 스냅샷·segment 완료 진단에 연결했다. UV setter는 coordinator와 기존/미래 native writer로 전파된다. Node·UV·TCP 채널은 독립적이다. C 세부 진단 문구 전체의 일대일 재현이나 로그 문구의 파싱 호환성을 주장하지 않는다.
- 새 검사 6개는 `raftpp.tracing_callback`, `tracing_lifetime`, `tracing_stderr`, `core_tracing`, `writer_tracing`, `tcp_tracing`이다. 형식 경계·모든 수준·소스 위치·환경 변수·custom 이벤트·콜백 교체/예외/중첩·할당 실패를 검사한다. 실제 C와 형식·선택한 핵심 이벤트 요약 및 buffered/direct native writer 이벤트 순서를 비교했다. 추적을 켠 실제 디스크·TCP 3노드에서 26개 명령, 종료 follower의 스냅샷 설치, 이전·전체 재시작, node/UV/TCP tracer 교체와 writer metric 대응을 검사했다.
- 첫 stderr 검사의 실패는 redirect된 FD와 `FILE*`의 파일 위치 동기화가 빠진 테스트 capture 문제였으며 `fseek` 동기화 뒤 해당 검사만 다시 통과했다. 엄격한 빌드에서 새 옵션 멤버의 생략 초기화 경고를 발견해 `tracer tracing{}`를 명시했다. 실패 결과는 통과로 재사용하지 않았다. C 원본은 변경하지 않았다.

| 검사 범위 | 명령·결과와 재사용 조건 |
| --- | --- |
| Debug 정적·UV/LZ4/fixture ON | 영향받는 합의·선거/투표·노드·writer·log/file coordinator·UV·TCP 실행 파일만 빌드했다. `ctest --test-dir build -R '^raftpp.(core_.*\|election\|vote\|vote_request\|node_.*\|writer_.*\|log_(append\|barrier\|close\|failure\|allocation)\|native_node\|file_snapshot.*\|uv_io_.*\|tcp_.*)$' --output-on-failure`의 50개가 통과했다. 명령의 `\|`는 Markdown 표 안의 이스케이프이며 셸에서는 `\` 없이 쓴다. 새 추적 단위 3개는 앞선 같은 작업의 통과 기록을 재사용하여 중복 실행하지 않았다. 현재 단위의 검증 범위는 합계 53개이며 전체 82개 실행이 아니다. 로그: `/tmp/raftpp-tracing-debug.log`. |
| 엄격한 Release 공유·UV/LZ4 ON·fixture OFF | `/tmp/raftpp-options-build`에서 새 추적 6개만 `-R '^raftpp.(tracing_.*\|core_tracing\|writer_tracing\|tcp_tracing)$'`로 실행해 통과했다. C++에 `-Wall -Wextra -Wpedantic -Werror`를 적용했다. 로그: `/tmp/raftpp-tracing-release.log`. |
| ASan/UBSan 정적·UV/LZ4 ON·fixture OFF | `/tmp/raftpp-cmake-debug`에서 새 6개와 관련 실패·할당·종료·수명 경로 11개, 합계 17개가 통과했다. 선택 항목은 `core_failure`, `node_allocation`, `writer_allocation`, `uv_io_allocation`, `tcp_allocation`, `writer_async`, `writer_close`, `uv_io_lifetime`, `tcp_lifetime`, `tcp_shutdown`, `file_snapshot_close`다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`·`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했으며 기존 환경 제한에 따라 누수 검사는 제외했다. 로그: `/tmp/raftpp-tracing-asan.log`. |
| 공개 헤더·새 API 외부 소비자 | `/tmp/raftpp-tcp-consumer`의 core 정적·UV/LZ4 공유 소비자를 새 추적 API·예제로 갱신하고 `consumer`만 빌드·실행해 둘 다 통과했다. `raftpp::raftpp`만 링크하며 core는 PkgConfig 감지를 끈 구성이다. 새 표준 별칭·observer·emit·stderr 제어·TCP setter와 한·영 예제 컴파일을 확인했다. |
| 변경 없는 기존 29개 runtime, 추가 sanitizer/소비자 구성, 전체 옵션 matrix·잘못된 옵션 검사 | **생략 — 기존 결과 재사용.** 기존 TCP/공개 UV·파일 기준 중 이번 추적 호출이나 설정 수명과 무관한 버퍼·설정·trail·압축·체크섬·코덱·동기 파일/복구·순수 progress/random/메시지 검사 결과를 사용한다. 공통 헤더의 표준 타입 별칭 추가만으로 모든 기존 동작을 다시 실행하지 않았다. 새 core-only 헤더/링크와 공유 export는 새 소비자로 확인했으며, 옵션 의존성 규칙은 바뀌지 않았다. 이번 결과를 모든 옵션의 전체 suite 검증으로 확대하지 않는다. |

API 계약은 `docs/ko/tracing.md`·`docs/en/tracing.md`에 기록하고 node·UV·TCP 설명서의 옵션도 함께 갱신했다. 8개 문서 대응 쌍의 제목·표·예제와 상대 링크 340개, 새 추적 export·core-only 심볼 제외·C 없는 소비자 링크를 확인했다. 추적 예제를 전용 헤더만 포함한 C++20·엄격한 경고 조건에서도 컴파일했다. UV 활성 CTest 등록 82개를 확인했으며 UV 비활성의 등록 수는 기존 34개에 공통 새 검사 4개를 더한 38개다. 사용자 지정 할당·PMR·fixture·예제·도구·추가 오류 문맥과 일부 호환 설정은 남아 있다.

### 메모리 resource 단위: 구현·검증과 재사용 기록

- 2026-11-02: `memory.hpp`·`memory.cpp`에 library resource get/set/reset과 표준 Allocator adapter를 구현했다. C `heap.c/.h`·공개 `raft_heap`의 malloc/free/calloc/realloc/aligned/get/set/reset, 버퍼·entry·snapshot·코덱·log, callback/node, native disk/writer/log/file/TCP의 할당·실패·해제 범위를 추적했다. C raw heap ABI 대신 표준 PMR, zero-initialized buffer/resize, 표준 `allocate_shared`와 PMR object construction으로 연결했다.
- `resource_allocator<T>`는 표준 polymorphic allocator에 resource 공유 소유권과 전파 traits를 보완한다. 제어 블록·컨테이너를 직접 구현하지 않는다. `resource_owned<T>`는 표준 `unique_ptr`의 custom deleter 별칭이며 `allocate_owned`는 표준 PMR `new_object/delete_object`를 사용한다. 라이브러리의 기존 `make_shared/make_unique` 객체 생성 경로를 이 allocator에 연결했다. libuv의 `addrinfo` 등 native 자원은 원래 native deleter를 유지한다.
- 바이트 버퍼는 표준 vector에 resource allocator를 명시하고, 정렬 버퍼 deleter는 resource·요청 byte 수·정렬을 함께 보관한다. 이동·swap은 서로 다른 resource에서도 원래 allocator를 전달하고 이동 원본을 재사용할 수 있다. 공유 payload의 표준 제어 블록 allocator도 resource를 소유해 payload 파괴 뒤 마지막 weak 참조까지 유효하다. 기본 정적 resource에는 동적 제어 블록이 없으며 그 alias handle의 weak/use_count 의미는 API 설명서에 명시했다.
- 라이브러리 선택은 표준 atomic shared pointer로 교체하며 다른 표준/외부 라이브러리 allocator를 바꾸지 않는다. 일반 vector/string/list backing, `std::function` boxing, filesystem·exception 저장과 사용자/외부 라이브러리 할당은 별도 경계다. 이 단위가 전체 내부 컨테이너 allocator 전파를 완료한 것은 아니다. node의 미래 요청은 생성 시점 선택을 사용하며 node 생성 시 선택을 고정하지 않는다.
- 새 검사 6개는 `raftpp.memory_default`, `memory_alignment`, `memory_lifetime`, `memory_objects`, `memory_failure`, `tcp_memory`다. C calloc/realloc과 zero/prefix 비교, 기본 선택 교체·복원, 정렬·서로 다른 resource의 move/swap, 재할당/생성/제어 블록 실패, std container rebinding, 마지막 weak 해제와 정확한 resource·byte·alignment 해제를 확인했다. thread-safe 추적 resource로 실제 디스크·TCP 3노드를 실행하고 8개 명령 후 선택을 복원했다. 스냅샷 따라잡기·이전·26개 명령·전체 재시작·종료 뒤 추적한 할당/해제 개수가 같고 resource가 파괴됨을 확인했다. 이 집계는 별도 allocator 경계까지의 전역 누수 증명이 아니다.
- 첫 `tracing_lifetime` 실패는 표준 PMR의 aligned new를 기존 할당 실패 주입이 가로채지 못한 문제였다. 제품 코드의 실패를 숨기지 않고 테스트 주입에 표준 aligned new/delete도 추가했다. 해당 실패 후 관련 검사만 다시 통과했다. 기존 표준 new 주입을 포함해 failure counter는 한 번 실패하는 같은 규칙을 유지한다.

- 마지막 표준 계약 검토에서 const entry/batch/tracer 객체의 `allocate_shared<const T>`에는 cv 한정이 없는 `resource_allocator<T>`를 전달하도록 보완했다. const 객체 생성을 memory 검사에 추가하고 Debug의 `memory_objects/entries/log/log_lifetime/tracing_lifetime/tcp_memory` 6개, Release·ASan/UBSan의 `memory_objects/tracing_lifetime/tcp_memory` 각 3개만 다시 통과했다. 나머지는 변경 없는 입력·행동 범위의 기존 결과를 재사용했다. 현재 라이브러리로 다시 링크한 두 소비자도 통과했다. 로그는 `/tmp/raftpp-memory-cv-debug.log`, `/tmp/raftpp-memory-cv-release.log`, `/tmp/raftpp-memory-cv-asan.log`다.

| 검사 범위 | 실행·재사용 결과와 조건 |
| --- | --- |
| Debug 정적·UV/LZ4/fixture ON | 새 memory 단위 5개·buffer/shared_buffer를 먼저 실행해 통과했다. 보완한 `tracing_lifetime`도 단독 통과했다. 이 8개와 변경 없는 `trail/random/progress/election/vote/vote_request` 6개를 제외한 `ctest --test-dir build -E '^raftpp.(memory_.*\|buffer\|shared_buffer\|tracing_lifetime\|trail\|random\|progress\|election\|vote\|vote_request)$' --output-on-failure`의 74개가 통과했다. 표의 `\|`는 Markdown escape이며 셸 명령에는 `\`를 넣지 않는다. 현재 단위의 통과 범위는 82개이며 전체 88개를 한 번에 새로 실행하지 않았다. 로그: `/tmp/raftpp-memory-debug.log`. |
| 엄격한 Release 공유·UV/LZ4 ON·fixture OFF | `/tmp/raftpp-options-build`에서 새 6개와 `node_allocation/writer_allocation/core_failure`, 합계 9개가 통과했다. C++는 `-Wall -Wextra -Wpedantic -Werror`다. 로그: `/tmp/raftpp-memory-release.log`. |
| ASan/UBSan 정적·UV/LZ4 ON·fixture OFF | `/tmp/raftpp-cmake-debug`에서 새 6개와 `node_allocation/core_failure/writer_allocation/uv_io_allocation/tcp_allocation/tracing_lifetime/writer_close/uv_io_lifetime/tcp_shutdown/file_snapshot_close`, 합계 16개가 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`·`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했다. 환경 제한으로 누수 검사는 제외했다. 로그: `/tmp/raftpp-memory-asan.log`. |
| 외부 소비자·예제 | `/tmp/raftpp-tcp-consumer`의 core 정적·UV/LZ4 공유, fixture/tests OFF 소비자를 새 PMR get/set/reset·pool 버퍼/객체 예제로 갱신했다. `raftpp::raftpp`만 연결한 `consumer` 빌드·실행이 둘 다 통과했다. core는 PkgConfig 감지를 끈 상태다. 전용 헤더를 사용하는 두 API 예제를 C++20·엄격한 경고로 별도 컴파일했다. |
| 나머지 검사·전체 matrix | **생략 — 기존 결과 재사용.** 같은 작업의 8개 통과는 구현/입력이 같아서 재사용했다. 과거 helper 6개는 바뀐 buffer/object/resource 경로를 호출하지 않으며 관련 알고리즘·컨테이너는 변경하지 않았다. 추가 sanitizer/소비자 구성과 전체 옵션 matrix·잘못된 옵션 검사는 관련 옵션 규칙이 같아 재사용한다. 새 API·핵심 수명은 위의 core/shared 소비자와 선택 검증으로 확인했으며 모든 옵션의 전체 검사로 확대하지 않는다. |

API는 `docs/ko/memory.md`·`docs/en/memory.md`에 기록하고 storage 설명서의 실제 allocator·시그니처도 함께 갱신했다. UV 활성 등록은 88개, UV 비활성은 이전 38개에 공통 memory 5개를 더한 43개다. 원본 C 구현은 보존했다. 일반 컨테이너 allocator 전파와 fixture·예제/도구·추가 오류 문맥·일부 호환 설정은 남아 있다.

최종 문서·연결 검사에서는 문서 9개 대응 쌍의 제목·표·예제와 상대 링크 356개, 전용 헤더 예제 컴파일, memory export·core-only 선택 심볼 제외·C 없는 소비자 링크, CTest 88개 등록과 제품 allocator의 cv 올바름을 확인했다. 원본 `raft/`는 변경하지 않았다.

### 코어 컨테이너 단위: 구현·검증과 재사용 기록

- 2026-11-02: `common.hpp`에 기존 resource 선택 함수·소유 allocator 정의를 모으고 `resource_vector/deque/list/map/basic_string/string`을 표준 컨테이너 별칭으로 추가했다. 기존 `vector/string/list` 등 표준 기본 allocator 별칭은 유지한다. `memory.hpp`는 이를 포함하고 소유 deleter·객체 생성 API를 제공한다.
- 로그 캐시의 deque·참조 map, trail의 deque, 코어 주소·후보 votes·리더 progress·rollback 백업과 송신 메시지 배열에 적용했다. `log::acquire`·progress build/rebuild는 기존 기본 allocator 출력 API를 유지하면서 resource vector 출력도 받는다. 임시 출력은 호출자가 보유한 allocator를 사용하고 성공 시에만 swap한다. 공개 entry batch·메시지 주소·configuration/snapshot 값·native 요청 큐는 표준 기본 allocator를 유지한다.
- 참조 범위는 C `heap/trail/log/progress/election/convert/message/replication/membership`와 C++ 대응 계층, 노드의 생성·leader 반환·I/O 완료·종료, native 요청 큐의 splice 관계 및 대응 테스트다. native 큐는 참조만 했고 이번 단위에서는 수정하지 않았다. 객체 allocator가 멤버 컨테이너에 자동 전파된다고 가정하지 않는다.
- 새 `raftpp.memory_containers`는 resource A로 만든 저장 공간을 선택 B 이후 확장하고, A의 실패를 주입해 trail·로그 참조 인덱스·acquire·progress build/rebuild의 상태/출력 보존과 원래 resource·byte·alignment 정리를 확인한다. 실제 TCP resource 검사도 resource 선택을 실행 중 복원한 뒤 스냅샷·재시작·종료를 수행했다.

| 검사 범위 | 명령·결과·재사용 조건 |
| --- | --- |
| Debug 정적·UV/LZ4/fixture ON | `cmake --build build --parallel 3`와 CMake 등록 변경 후 memory 타깃 빌드가 통과했다. `ctest --test-dir build -R '^raftpp.(memory_containers\|trail\|log\|log_lifetime\|progress\|election\|vote\|vote_request\|messages\|core_.*\|node_.*\|native_node\|uv_io_(node\|cluster))$' --output-on-failure` 26개와 `tcp_(cluster\|tracing\|memory)` 3개, 합계 29개가 통과했다. 표의 `\|`는 Markdown escape이며 셸에서는 `\`를 넣지 않는다. 전체 89개를 실행한 결과가 아니다. |
| 엄격한 Release 공유·UV/LZ4 ON·fixture OFF | `/tmp/raftpp-options-build`의 memory/core/core_failure/node/TCP 타깃 빌드가 통과했다. `ctest --test-dir /tmp/raftpp-options-build -R '^raftpp.(memory_containers\|core_failure\|core_membership\|core_snapshot\|node_allocation\|tcp_memory)$' --output-on-failure` 6개가 통과했다. 로그: `/tmp/raftpp-containers-release.log`. |
| ASan/UBSan 정적·UV/LZ4 ON·fixture OFF | `/tmp/raftpp-cmake-debug`에서 같은 타깃과 같은 6개 선택 검사가 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`·`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 적용했으며 환경 제약에 따라 누수 검사는 제외했다. 로그: `/tmp/raftpp-containers-asan.log`. |
| 공개 헤더·외부 소비자 | 수정한 memory 예제만 전용 헤더·C++20·엄격한 경고로 다시 컴파일했다. `/tmp/raftpp-resource-common.cpp`는 common 헤더만으로 컨테이너 생성·다른 allocator 인자·동일 allocator splice·resource 선택을 컴파일·링크·실행했다. `/tmp/raftpp-tcp-consumer/core`의 `consumer`도 다시 빌드·실행하여 통과했다. UV/LZ4/fixture/tests OFF·PkgConfig 감지 OFF이며 `raftpp::raftpp`만 링크한다. 공유 export는 위 memory 검사에서 새 log/progress overload를 직접 호출해 확인했다. |
| 변경 없는 60개 Debug 검사·추가 구성·전체 옵션 matrix | **생략 — 기존 결과 재사용.** 앞선 메모리·추적·TCP 단위에서 통과한 버퍼/객체 primitive·설정·압축·체크섬·코덱·동기 파일/복구·저수준 writer/native coordinator·직접 TCP 전송·random 중 수정한 코어 저장 공간을 사용하지 않는 범위를 재사용한다. 이전에 재사용했던 trail/progress/선거/투표는 이번 변경의 영향 범위여서 다시 실행했다. 옵션 의존성·선택 소스·링크 규칙과 나머지 실행 환경은 그대로다. 추가 sanitizer/소비자 구성과 전체 matrix도 반복하지 않는다. |

현재 UV 활성 CTest 등록은 89개다. UV 비활성 등록은 기존 43개에 새 공통 검사 1개를 더한 44개로 유도되며 이번에는 해당 전체 suite를 실행하지 않았다. API는 한·영 memory 설명서에 갱신했고, 수정한 문서의 시그니처·예제·상대 링크 대응을 확인했다. 전체 재작성은 진행 중이며 native 큐 전파·fixture·오류/설정 호환·예제/도구는 남아 있다.

### Native 컨테이너 단위: 구현·검증과 재사용 기록

- 2026-11-02: `node.cpp`의 요청·operation·이벤트·종료 콜백 큐, `writer.cpp`의 요청 리스트·버퍼/iovec/io_event 배열, `log_writer.cpp`의 segment·prepare·truncate·barrier 큐와 임시 entry 배열, `file_backend.cpp`의 snapshot get, `uv_io.cpp`의 send, `tcp_transport.cpp`의 listener·peer·incoming·send 큐·버퍼 descriptor·내부 주소에 resource 표준 컨테이너를 적용했다. 공개 값 컨테이너·file-list 결과·native addrinfo deleter는 유지한다.
- `splice`의 allocator 동등성 조건을 보존한다. TCP와 segment의 pending/writing, log writer의 segments/retiring은 생성 시 allocator를 공유한다. 모든 barrier 요청 그룹은 부모 barrier 큐 allocator를 사용하며 완료·취소 임시 리스트는 원본 큐 allocator를 받는다. resource 선택을 바꾸거나 종료 콜백이 재진입해도 allocator가 다른 노드를 splice하지 않으며 취소 리스트를 만드는 과정은 할당하지 않는다.
- 참조 범위는 C `uv_writer.c`, `uv_append.c`의 barrier/종료, `uv_send.c`, `uv_tcp.c`, `uv_snapshot.c`의 요청 소유·queue 정리와 앞선 단위에서 추적한 callback/node/native 완료 경로다. 이번 수정은 위 C++ 6개 소스와 해당 테스트·한/영 memory API·현황이다. 합의 알고리즘·파일/네트워크 형식·공개 API 시그니처·옵션 의존 규칙과 C 원본은 바꾸지 않았다.
- 새 UV 검사 `raftpp.log_memory`는 A로 coordinator를 만들고 B와 C에서 append/barrier를 제출한 뒤 선택을 복원한다. 두 blocking 그룹의 정상 완료·unblock·영속 순서를 확인하고, 별도 경로에서 부모 resource의 다음 할당을 실패하도록 설정한 상태로 두 그룹을 취소한다. 취소는 그 할당을 시도하지 않고 모든 callback을 정확히 한 번 완료한다. 마지막 객체 파괴 뒤 세 resource의 weak 만료·할당/해제 개수·byte/alignment 일치를 검사한다.
- ASan/UBSan의 첫 선택 실행은 14개 통과·`tcp_memory` 1개 시간 초과였다. 원래 출력은 대기 helper 위치만 제공하여 실패 단계를 특정할 수 없었다. 조건·12초 제한을 바꾸지 않고 호출 위치와 노드 state/term/log/commit/applied/FSM/snapshot/error 진단을 추가했다. 이후 해당 검사만 단독 8.03초 통과, 원인 조사용 `--repeat until-fail:3`에서도 9.36·5.65·4.53초로 통과했다. 처음 실패는 그대로 보존하며 원인은 미확정이다. 이를 타임아웃 문제가 해결됐다는 증거로 확대하지 않는다. 진단 추가 후 Debug·Release TCP 타깃도 다시 빌드했다. 성공 조건·제품 실행 경로가 같아 기존 통과 runtime은 재사용했다.

| 검사 범위 | 실행·결과·재사용 조건 |
| --- | --- |
| Debug 정적·UV/LZ4/fixture ON | `cmake --build build --parallel 3`와 새 검사 등록 후 log-writer 타깃 빌드가 통과했다. `ctest --test-dir build -R '^raftpp.(node_.*\|writer_.*\|log_(append\|barrier\|close\|failure\|allocation\|memory)\|native_node\|file_snapshot.*\|uv_io_.*)$' --output-on-failure` 29개와 `ctest --test-dir build -R '^raftpp.tcp_.*$' --output-on-failure` 12개, 합계 41개가 통과했다. 표의 `\|`는 Markdown escape다. 로그: `/tmp/raftpp-native-memory-debug.log`, `/tmp/raftpp-native-memory-tcp-debug.log`. |
| 엄격한 Release 공유·UV/LZ4 ON·fixture OFF | `/tmp/raftpp-options-build`에서 node/writer/log_writer/UV/TCP 타깃을 빌드했다. `ctest --test-dir /tmp/raftpp-options-build -R '^raftpp.(log_memory\|log_allocation\|writer_allocation\|node_allocation\|uv_io_allocation\|tcp_memory\|tcp_allocation)$' --output-on-failure` 7개가 통과했다. 로그: `/tmp/raftpp-native-memory-release.log`. |
| ASan/UBSan 정적·UV/LZ4 ON·fixture OFF | `/tmp/raftpp-cmake-debug`의 같은 타깃을 빌드하고 `log_(memory\|barrier\|close\|allocation)`, `writer_(close\|allocation)`, `node_(lifetime\|allocation)`, `file_snapshot_(close\|allocation)`, `uv_io_(lifetime\|allocation)`, `tcp_(allocation\|shutdown\|memory)` 15개를 선택했다. 14개 통과는 재사용했고 TCP의 최초 실패·후속 1회와 조사용 3회 통과는 위처럼 구분했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용하며 누수 검사는 제외했다. 로그: `/tmp/raftpp-native-memory-asan.log`, `/tmp/raftpp-native-memory-asan-tcp-diagnostic.log`, `/tmp/raftpp-native-memory-asan-tcp-investigation.log`. |
| 나머지 49개 Debug 검사·전체 matrix·소비자 | **생략 — 기존 결과 재사용.** 코어 컨테이너 단위의 29개 중 native/node 경로를 사용하는 항목은 위에서 다시 검사했고, 순수 core/trail/progress/선거/투표·로그 값·버퍼/설정·코덱·동기 디스크 등 이번 구현 변경과 무관한 검사는 앞선 코어/메모리 단위의 기록을 재사용한다. 공개 헤더·심볼·링크 사용 조건·feature gate·옵션 의존성은 바꾸지 않아 외부 소비자·예제·추가 sanitizer 구성·전체 옵션 matrix를 반복하지 않았다. CMake 변경은 기존 UV 조건 안의 검사 등록 1개다. |

현재 UV 활성 등록은 90개, UV 비활성은 44개다. 해당 전체 suite를 새로 실행한 결과가 아니다. 한·영 memory API에 실제 할당 경계와 큐 확장의 원래 resource 사용, 닫힌 핸들의 resource 보관을 설명했다. fixture·추가 오류/설정 호환·예제/도구와 TCP 시간 초과의 원인 조사는 남아 있다.

### C++ Fixture 단위: 구현·검증과 재사용 기록

- 2026-11-02: C `fixture.c`와 공개 `fixture.h` 전체, `test_fixture.c`, 요청의 복사·완료·정리, `LegacyFireCompletedRequests`, 노드·FSM·로그·난수·스냅샷 연결을 확인했다. `fixture.hpp`·`fixture.cpp`에 독립 메모리 I/O와 가상 시간 scheduler를 구현했다. 공개 노드 API를 사용한다. 최대 8개 서버·ID/주소·설정/시작·관찰·step/wait/hook·선거/depose·방향별 연결/포화·kill/revive·지연·저장 값·장애·6종 메시지 counter를 제공한다.
- 이벤트는 예정 시각·서버 인덱스·같은 서버의 tick 우선·요청 FIFO 순서로 선택한다. send 완료와 transmit 전달을 분리하고 배치·스냅샷을 깊은 복사한다. election safety·leader append-only와 commit 관찰을 Release에서도 검사한다. 공개 노드의 상태 관찰과 현재 선거 timer 제어만 fixture 전용 내부 접근자로 연결했으며 일반 노드의 실행 경로·객체 배치는 바꾸지 않았다.
- C fixture 종료처럼 fixture `close()`는 가상 시간을 전진시키지 않고 서버/FIFO 순서로 대기 디스크·send 요청을 완료하고 transmit을 버린 뒤 노드·FSM을 해제한다. 앞 서버에 새로 생긴 작업도 다시 확인해 남기지 않는다. 노드·backend를 직접 닫을 때는 공개 I/O 계약대로 취소한다. 재진입 close와 이벤트 hook 안의 close를 지원하며 현재 요청/backend는 반환까지 유지한다. 일반 자원·pending 큐·leader-log 관찰은 표준 PMR·스마트 포인터·컨테이너를 사용한다.
- C stub의 unsupported recover는 `io_error`를 유지한다. C의 null snapshot get·메시지 counter 인덱스·snapshot load prefix 처리와 잘못된 인자에서의 assert/UB는 재현하지 않는다. 빈 snapshot optional·검사한 0기반 counter·올바른 snapshot 이후 suffix·오류 반환으로 공개 I/O 계약을 지킨다. 즉시 할당 실패는 콜백 없이 오류를 반환하고 완료 중 실패는 출력을 보존하며 한 번 완료한다. valueless 메시지 variant도 거부한다. 사용자 지정 resource 교체 뒤 기존 큐 확장 실패와 마지막 외부 backend 핸들까지의 수명을 검사했다.
- 새 검사 7개는 `fixture_reference`, `fixture_ordering`, `fixture_election`, `fixture_network`, `fixture_storage`, `fixture_lifetime`, `fixture_failure`다. C fixture와 동일 bootstrap·명령의 이벤트 380개, 서버 인덱스·종류·시각·state·term·안정 리더·FSM 결과가 일치했다. 동시각 tick/디스크 순서·지연 0·송신/전달 분리·방향별 실패/포화·독립 배치 복사·모든 voter의 강제 선거·depose·분할 중 16개 명령·스냅샷 설치·리더 정지/복귀·persisted 값·반복/영구 fault·OOM·제한 인자·move·정확한 resource 해제를 확인했다.
- 첫 Debug 네트워크 검사는 kill 직후 아직 갱신하지 않은 leader 관찰에 단순 `has_leader`를 적용해 즉시 반환했다. C와 같은 캐시 관찰 계약에 맞춰 새 리더를 명시적으로 기다리도록 테스트를 고쳤다. 실패를 통과로 재사용하지 않았다. 종료 순서와 직접 backend 제출의 OOM 경계를 보완한 뒤 아래 7개를 다시 실행했다. 마지막 snapshot-get 완료 예외 기록 보완 뒤에는 `fixture_failure`만 구성별로 재검증했고 나머지 6개는 변경 없는 성공 경로 결과를 재사용했다.

| 검사 범위 | 실행·결과·재사용 조건 |
| --- | --- |
| Debug 정적·UV/LZ4/fixture ON | `cmake --build build --target raftpp_fixture_tests --parallel 2`와 `ctest --test-dir build -R '^raftpp.fixture_' --output-on-failure` 7개가 통과했다. 마지막 예외 경계 보완 뒤 `-R '^raftpp.fixture_failure$'` 1개만 통과를 확인했다. 로그: `/tmp/raftpp-fixture-debug-final.log`, `/tmp/raftpp-fixture-debug-callback.log`. |
| 엄격한 Release 공유·UV/LZ4/fixture ON | `/tmp/raftpp-options-build`에서 fixture 옵션을 ON으로 구성하고 같은 타깃·7개 검사와 마지막 failure 1개가 통과했다. C++는 `-Wall -Wextra -Wpedantic -Werror`, 비교 bridge는 C11 이상이다. 로그: `/tmp/raftpp-fixture-release-final.log`, `/tmp/raftpp-fixture-release-callback.log`. 앞선 fixture OFF 검증과 구성을 구분한다. |
| ASan/UBSan 정적·UV/LZ4/fixture ON | `/tmp/raftpp-cmake-debug`에서 같은 타깃·7개 검사와 마지막 failure 1개가 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`·`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했고 누수 검사는 환경 제약으로 제외했다. 로그: `/tmp/raftpp-fixture-asan-final.log`, `/tmp/raftpp-fixture-asan-callback.log`. |
| Core 전용 엄격한 Release 정적·fixture ON | `/tmp/raftpp-fixture-core`를 UV/LZ4 OFF·fixture/tests ON·PkgConfig 감지 OFF로 구성해 동일 타깃·7개 검사와 마지막 failure 1개가 통과했다. 로그: `/tmp/raftpp-fixture-core-final.log`, `/tmp/raftpp-fixture-core-callback.log`. 외부 네트워크/압축 의존성 없이 실행한다. |
| 외부 소비자·ON→OFF gate | `/tmp/raftpp-tcp-consumer`의 전용 `fixture_example`은 한·영 API 예제와 같은 코드이고 `raftpp::raftpp`만 링크한다. core 정적·UV/LZ4 공유에서 빌드·실행했다. 동일 core 디렉터리의 ON→OFF 재구성에서도 빌드·실행하고 공개 클래스의 예상 컴파일 실패를 확인했다. `/tmp/raftpp-fixture-core`의 OFF 구성은 C/C++ archive에서 fixture/접근자 심볼이 없고 C 선택 헤더와 fixture 테스트 등록도 없으며 기존 core 44개 등록은 유지했다. 최종 구현 뒤 두 소비자를 다시 링크·실행하여 통과했다. OFF 경로와 gate는 이후 fixture 내부 보완의 영향을 받지 않아 해당 제외 결과를 재사용한다. |
| 변경 없는 기존 90개 runtime·추가 matrix | **생략 — 기존 결과 재사용.** 앞선 native/core/container·메모리/추적 단위에서 통과한 기존 코어·노드·파일·네트워크·값/코덱의 실행 경로는 바꾸지 않았다. 노드에는 옵션으로 포함되는 내부 관찰/타이머 접근자와 friend 선언만 추가했고 public 객체 배치·일반 동작은 유지한다. 새 옵션 gate·헤더·링크·source 선택은 위 fixture/core/공유 소비자와 ON→OFF로 검증했다. 전체 suite·옵션 matrix·기존 TCP 타임아웃을 이유 없이 다시 실행하지 않았다. |

fixture ON의 등록은 UV 활성 97개·비활성 51개이며, OFF는 각각 기존 90개·44개다. 해당 전체 runtime을 새로 통과한 기록이 아니다. 한·영 `docs/ko/fixture.md`·`docs/en/fixture.md`에 API·오류·소유·종료·시간 계약과 예제를 작성하고 node/memory 설명서에서 연결했다. C 원본은 보존했다. 추가 오류/설정 호환·예제/도구와 앞선 실제 TCP 시간 초과 원인 조사는 남아 있다.

최종 문서·산출물 확인에서는 10개 한·영 대응 쌍의 제목/표 구조, fixture 예제의 동일 코드와 상대 링크 378개를 확인했다. 현재 shared fixture export와 두 소비자의 링크, fixture ON 등록 97/51개도 확인했다. API 설명서에는 작업/테스트 이력을 넣지 않았다.

### 공개 이벤트 API·조회 단위: 구현·검증·재사용

- 2026-11-02: C 공개 선언과 `raft.c` 설정·이름·난수·timeout, 선거 추출 및 C++ 코어·노드·드라이버 경계를 대조했다. 공개 `core.hpp`, `random.hpp`, `version.hpp`, `core_api.cpp`의 timeout helper, `node::timeout()`, 표준 `string_view` 상태/역할 이름을 추가했다. 기존 독립 엔진을 타입/함수 별칭으로 노출하며 알고리즘을 중복 구현하거나 C에 위임하지 않는다. 원본 C 소스는 보존했다.
- 이벤트 값과 이동된 update는 이후 step/코어 파괴 후에도 표준 소유자를 유지한다. timeout helper는 타이머 시작과 기존 update 플래그를 보존하고 잘못된 설정은 값/시드를 보존한다. callback election setter의 150/15 ms는 C legacy처럼 450/45 ms로 바뀌며 순수 이벤트 setter와 명시적 생성자 옵션은 그대로 적용한다. 이름·참조 0.22.1 정보는 C 값을 유지하고 단일 값 난수 범위는 기존 C++의 정의된 동작을 유지한다.
- 새 검사 4개: `public_types`는 C 이름·버전 매크로와 시드 난수 500회를 비교한다. `public_settings`는 C follower/candidate/leader 설정·만료 시각과 잘못된 인자의 상태 보존을 검사한다. `public_core`는 공개 헤더만으로 시작·제출·영속화·commit과 코어 파괴 후 update 수명을 검사한다. `node_settings`는 backend tick 설정·절대 만료 시각·legacy 보정·생성자 값·이동/종료·잘못된 설정을 검사한다.

| 검사 범위 | 실행·결과와 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture ON | 기존 `build`를 configure하고 `raftpp_public_core_tests`/`raftpp_node_tests`만 빌드한 뒤 `ctest --test-dir build -R '^raftpp.(public_types\|public_settings\|public_core\|node_settings)$' --output-on-failure`를 실행해 4개 모두 통과했다. `\|`는 Markdown 표 이스케이프이며 셸에서는 역슬래시를 뺀다. 로그: `/tmp/raftpp-public-api-debug.log`. 최초 빌드는 재생성 전에 새 타깃을 요청해 규칙을 찾지 못했다. configure로 등록한 뒤 빌드는 성공했으며 런타임 테스트 실패가 아니다. |
| 엄격한 Release 공유, UV/LZ4/fixture ON | `/tmp/raftpp-options-build`에서 같은 타깃 빌드와 선택 4개가 `-Wall -Wextra -Wpedantic -Werror`로 통과했다. 로그: `/tmp/raftpp-public-api-release.log`. helper 3개와 `node::timeout` export를 확인했다. |
| ASan/UBSan 정적, UV/LZ4/fixture ON | `/tmp/raftpp-cmake-debug`에서 같은 영향 타깃을 빌드하고 `-R '^raftpp.(public_core\|node_settings)$'`만 실행해 2개 모두 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했다. 누수 검사는 계속 제외한다. 로그: `/tmp/raftpp-public-api-asan.log`. |
| 외부 헤더·링크·예제 | `/tmp/raftpp-tcp-consumer`에 `raftpp::raftpp`에만 연결하는 전용 `core_example`을 추가했다. UV/LZ4/fixture/tests OFF, PkgConfig 감지 OFF의 core-only 정적 Release로 configure·빌드·실행에 성공했다. 현재 엄격한 공유 라이브러리에도 같은 예제/조회 번역 단위를 C++20·엄격한 경고로 컴파일·실행했다. 독립 공개 core 헤더·새 helper·난수/버전 헤더·timeout 심볼을 확인한다. |
| 기존 runtime 97개·추가 matrix | **생략 — 기존 결과 재사용.** 이전 fixture/native/core/container/memory/tracing 단위에서 구현·테스트 경로가 일치하는 기록을 재사용한다. 기존 테스트는 변경한 timeout setter를 호출하지 않는다. 모의 backend의 tick 간격 기록은 큐·할당·콜백 동작을 바꾸지 않는다. 공유 헤더 추가는 별칭/인라인 조회이며 기존 알고리즘 변경이 아니다. 옵션 gate/의존성 규칙도 재사용하고 전체 suite/matrix/TCP 조사를 반복하지 않았다. 이전 ASan TCP timeout은 원인 미확정 실패로 보존하며 통과로 재사용하지 않는다. |

API 전용 `docs/ko/core.md`/`docs/en/core.md`와 노드 조회·설정 설명을 함께 추가했다. 현재 UV/fixture ON 등록 수는 101개다. 위 다른 등록 수는 이전에 확인한 97/51/90/44개에 무조건 등록 4개를 더한 값이다. 등록과 실행은 구분한다. UV runtime setter·native 상세 문맥 오류·일부 호환 동작·예제/도구와 TCP timeout 조사는 남아 있다.

최종 문서·산출물 확인: API 문서 11개 대응 쌍의 제목/표 구조, 새 API 시그니처·예제의 양쪽 동일성, 상대 링크 406개와 검증 기록 대응을 확인했다. 새 공유 심볼 export와 core-only 소비자의 선택 의존성 제외를 확인했다. 이 문서 변경 후에는 빌드·런타임 테스트를 추가 실행하지 않았다.

### UV/TCP 설정 단위: 구현·검증·재사용

- 2026-11-02: C `uv.c` 설정 함수, `uv_tcp.c` bind 설정, `uv_prepare.c`의 작업별 할당 크기, `uv_append.c` 크기/용량 계산, `uv_send.c` 재시도 예약과 C++ UV/file/log/TCP 옵션·worker·콜백·종료 경계를 추적했다. `uv_io`에 block/segment 크기·disk retry·connect retry·auto recovery·snapshot compression 설정을, `tcp_transport`에 bind 주소·connect retry 설정을 추가했다. 파일 coordinator/log writer까지 새 설정을 전파하며 C 구현을 호출하지 않는다. 원본 C 소스는 보존했다.
- `transport::set_connect_retry_delay`는 기존 사용자 구현을 유지하는 기본 no-op 가상 메서드다. TCP는 이후 타이머 예약에 새 간격을 사용하고 이미 예약한 만료 시각은 바꾸지 않는다. 디스크 재시도도 같은 예약 경계를 따른다. compression setter는 C처럼 LZ4와 무관한 성공 no-op이다. bind override는 표준 소유 문자열로 교체하고 실패 시 기존 값을 보존하며 init 후 busy다. 이미 열린 writer의 block 정렬 변경은 기존 버퍼/offset 수명을 보존하도록 busy다. 위험한 중간 정렬 교체나 C의 반복 bind 할당 누수는 재현하지 않는다.
- segment 크기 변경은 기존 append-fit/capacity 정책과 이후 준비에 적용한다. 기존 pool·진행 중인 쓰기/준비는 보존한다. worker가 변경 가능한 전체 옵션을 읽지 않도록 각 prepare에 처음 제출한 할당 크기를 저장해 재시도까지 유지한다. 이 할당 캡처 경로만 기존 동작에 영향을 주므로 로그 append·실패·종료 검사를 선택해 다시 검증했다.
- 새 `log_settings`는 단일 libuv worker를 점유해 준비를 큐에 둔 뒤 segment 크기를 변경한다. 제출 전 크기 4096과 이후 크기 1536, 쓰기 실패 후 runtime retry, 닫기/재로드를 확인한다. `uv_io_settings`는 probe block 복원·busy/invalid/shutdown·실제 손상 파일의 자동 복구 토글·writer capacity 전파·snapshot 준비 실패 후 재시도를 확인한다. `tcp_settings`는 bind 실제 연결·기존 주소 보존·반복 교체/OOM·초기화 후 동결·advertised identity 보존과 60000→1 ms 변경 후 실제 재접속을 확인한다.

| 검사 범위 | 실행·결과와 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture ON | `cmake -S . -B build`, `cmake --build build --target raftpp_log_writer_tests raftpp_uv_io_tests raftpp_tcp_tests --parallel 3`가 통과했다. `ctest --test-dir build -R '^raftpp.(log_settings\|uv_io_settings\|log_append\|log_failure\|log_close)$' --output-on-failure` 5개와 `-R '^raftpp.tcp_settings$'` 1개가 통과했다. `\|`는 표 이스케이프이며 셸에서는 역슬래시를 뺀다. 로그: `/tmp/raftpp-native-settings-debug.log`, `/tmp/raftpp-native-settings-tcp-debug.log`. |
| 엄격한 Release 공유, UV/LZ4/fixture ON | `/tmp/raftpp-options-build`에서 같은 타깃을 `-Wall -Wextra -Wpedantic -Werror`로 빌드했다. 새 `uv_io_settings/log_settings` 2개와 `tcp_settings`가 각각 통과했다. 로그: `/tmp/raftpp-native-settings-release.log`, `/tmp/raftpp-native-settings-tcp-release.log`. 새 UV/TCP 공개 설정 심볼 8개의 export를 확인했다. |
| ASan/UBSan 정적, UV/LZ4/fixture ON | `/tmp/raftpp-cmake-debug`에서 같은 영향 타깃을 빌드하고 `-R '^raftpp.(uv_io_settings\|log_settings\|tcp_settings)$'` 3개 모두 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했다. 누수 검사는 환경 제한으로 제외한다. 로그: `/tmp/raftpp-native-settings-asan.log`. |
| 외부 소비자 | `/tmp/raftpp-tcp-consumer`의 전용 `native_settings_example`은 새 virtual 메서드를 override하지 않는 사용자 transport를 포함한다. UV/LZ4/fixture/tests OFF·PkgConfig 감지 OFF·core-only 정적 Release에서 `raftpp::raftpp`만 연결해 빌드/실행했다. 같은 소스를 현재 엄격한 UV/LZ4 공유 라이브러리에 C++20 경고 옵션과 libuv로 직접 연결해 실행했다. 새 설정 호출과 기본 가상 메서드·선택 헤더 제외를 확인한다. |
| 기존 98개·추가 구성/소비자/matrix | **생략 — 기존 결과 재사용.** 공개 API/fixture/native-container/core/memory/tracing 단위의 일치하는 기록을 재사용한다. 합의·노드·코덱·동기 디스크·FSM·fixture 및 기존 TCP 실행 경로는 변하지 않았다. 기본 가상 메서드는 기존 사용자 구현에 강제 override를 요구하지 않는다. 새 setter를 호출하지 않는 경로는 이전 옵션을 그대로 사용한다. prepare 입력 캡처의 기존 영향 3개만 위에서 다시 확인했다. 기존 ASan TCP timeout은 원인 미확정 실패로 남기고 재실행/통과 재사용하지 않았다. |

최종 수명 경계 보완: 사용자 transport의 정책 메서드가 마지막 facade를 파괴해도 가상 호출이 끝날 때까지 backend/transport를 소유하도록 `uv_io::set_connect_retry_delay`에 내부 상태 소유자를 유지했다. 초기화 전 재진입 파괴를 `uv_io_settings`에 추가하고 Debug/공유 Release/ASan·UBSan 각각 해당 1개만 다시 빌드·실행해 통과했다. 로그: `/tmp/raftpp-native-settings-debug-lifetime.log`, `/tmp/raftpp-native-settings-release-lifetime.log`, `/tmp/raftpp-native-settings-asan-lifetime.log`. 변경 없는 로그/TCP와 앞선 기존 영향 검사는 재사용했다. 공유 소비자는 현재 라이브러리를 동적으로 사용해 다시 실행했고 통과했으며 core-only 결과는 해당 UV 제외 경로가 같아 재사용했다.

`log_settings`에만 `UV_THREADPOOL_SIZE=1`을 지정해 worker 입력 캡처를 결정적으로 검사한다. 다른 테스트 환경은 변경하지 않는다. TCP 검사는 앞서 확인한 소켓 sandbox 제한 때문에 loopback 권한이 있는 환경에서 실행했다. 등록 수는 UV/fixture ON 104개이며 fixture OFF는 기존 94개에 새 UV 검사 3개를 더한 97개다. core-only의 55/48개 등록은 유지한다. 전체 suite나 전체 옵션 matrix를 다시 실행한 결과가 아니다. 두 언어 UV/TCP API 문서에 설정 시점·오류·소유권·예약 경계를 추가했다. 최종 문서 검사는 API 11개 대응 쌍의 제목·표 구조와 새 선언의 동일성, 상대 링크 406개를 확인했다.

### 소유 오류 진단 단위: 구현·검증·재사용

- 2026-11-02: `err.h`/`err.c`의 문구·감싸기, C legacy의 I/O 진단 전달, C++ node의 제출/완료/상태·종료 경로와 native file/disk 초기화를 참조했다. `error_info`는 표준 error_code 2개와 array<char,256>를 소유하며 `text()`는 이 배열의 view다. `error_message(errc)`, `make_error_info`, `wrap_error_info`는 할당 없이 구성하고 C 256바이트 경계와 일치한다. `name_too_long`/`busy`의 기존 다른 문구를 C와 맞췄다. 성공은 C++의 success 문구를 유지한다.
- 선택적 `io::error_details()` 기본 구현은 빈 값이어서 기존 backend를 유지한다. node는 완료 콜백 진입 시 요청별 진단을 소유하고, 즉시 load/metadata/truncate/submission 실패도 캡처한다. 종료를 시작한 오류는 cleanup·바깥 wrapper가 덮어쓰지 않는다. node의 고정 진단 조회는 할당하지 않으며 기존 소유 string 조회는 할당할 수 있다. bootstrap/recover backend 실패도 종료 없이 기록한다. init 예외에는 일치하는 backend 문맥을 포함한다.
- file_handle의 open/allocate/close에 선택적 진단 출력을 추가하고 Linux POSIX 원인을 표준 generic_category로 보존했다. disk 초기 stat의 경로/원인을 UV I/O와 node 생성자까지 연결했다. worker·file coordinator·TCP 전체의 상세 원인 연결은 이 단위의 완료 범위가 아니며 다음 단위로 남긴다. callback 상태 코드·저장/응답 순서·원본 C 소스는 보존했다.
- 새 `error_messages`는 C 오류 23개와 category 문구를 비교한다. `error_context`는 접두사 0/1/252..256/1000바이트, C wrapping, 강제 OOM 중 구성/복사·foreign category·잘못된 비종단 공개 배열의 경계 읽기를 검사한다. `native_errors`는 실제 파일 open/할당 실패와 디렉터리 초기화 원인·소유 경로·node 예외·핸들 정리를 검사한다. `node_errors`는 인라인/지연 완료 후 backend 진단 교체·OOM 중 조회·이동·load/metadata 실패와 첫 오류 보존을 검사한다.
- 첫 Debug `node_errors`의 마지막 항목은 단일 투표자 시작에서 term 쓰기가 발생한다고 잘못 가정해 실패했다. C++ 코어와 C 기준의 단일 투표자 전환은 해당 쓰기 없이 leader가 된다. 테스트를 실제 load 실패와 3노드 follower의 투표 수신/term 쓰기 경로로 나누고 그 항목만 다시 실행해 통과했다. 합의 알고리즘은 이 실패 때문에 바꾸지 않았고 실패 기록을 통과로 재사용하지 않았다.

| 검사 범위 | 실행·결과와 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture ON | `cmake -S . -B build`, `cmake --build build --target raftpp_error_tests raftpp_node_tests --parallel 3` 후 `ctest --test-dir build -R '^raftpp.(error_messages\|error_context\|native_errors\|node_errors\|node_failure\|node_allocation)$' --output-on-failure`에서 5개 통과/위 새 항목 1개 실패. 수정 후 `-R '^raftpp.node_errors$'`만 다시 실행해 통과했다. 6개 범위의 결과이며 전체 재실행이 아니다. 로그: `/tmp/raftpp-errors-debug.log`, `/tmp/raftpp-errors-debug-node.log`. `\|`는 표 이스케이프이며 셸에서는 역슬래시를 뺀다. |
| 엄격한 Release 공유, UV/LZ4/fixture ON | `/tmp/raftpp-options-build`에서 같은 타깃을 `-Wall -Wextra -Wpedantic -Werror`로 빌드하고 새 `error_messages/error_context/native_errors/node_errors` 4개만 실행해 모두 통과했다. 로그: `/tmp/raftpp-errors-release.log`. 새 helper/조회 6개의 공유 export를 확인했다. |
| ASan/UBSan 정적, UV/LZ4/fixture ON | `/tmp/raftpp-cmake-debug`에서 같은 영향 타깃을 빌드하고 새 4개만 실행해 모두 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했다. 환경 제한으로 누수 검사는 제외한다. 로그: `/tmp/raftpp-errors-asan.log`. |
| 외부 소비자/기능 제외 | `/tmp/raftpp-tcp-consumer`의 전용 `errors_example`이 두 언어 API 예제와 새 문맥 helper를 사용한다. core-only 정적 Release, UV/LZ4/fixture/tests OFF·PkgConfig 감지 OFF에서 `raftpp::raftpp`만 연결해 빌드/실행했다. 같은 소스를 현재 엄격한 공유 라이브러리에도 C++20 경고 옵션으로 컴파일/실행했다. |
| 기존 102개·추가 matrix/소비자 | **생략 — 기존 결과 재사용.** 이전 설정·공개 API·fixture·컨테이너·메모리·추적 단위에서 관련 실행 경로가 같은 기록을 사용한다. 합의 알고리즘·네트워크·파일 형식·worker/일반 파일 동작의 상태 반환은 유지한다. 새 고정 진단은 표준 값이며 할당 수를 바꾸지 않는다. 공유 헤더의 선언 추가만으로 전체를 반복하지 않았으며 기존 오류/할당 영향 2개만 위에서 다시 확인했다. 원인 미확정의 이전 ASan TCP timeout은 실패로 보존하고 재실행/통과 재사용하지 않았다. |

기존 C 오류 유틸리티는 테스트 타깃에서 C11로만 컴파일하며 product는 독립 C++다. 새 API 전용 오류 설명서와 node/UV 링크를 두 언어로 추가했다. UV/fixture ON은 108개 등록이며 다른 구성은 이전 104/55/97/48에 UV 전용 1개와 무조건 3개를 더한 108/58/101/51이다. 등록과 실제 실행 결과를 구분한다. native worker/TCP 상세 원인, 나머지 호환·예제/도구 및 처음 TCP timeout 조사는 남아 있다.

최종 문서/산출물 검사: API 12개 대응 쌍의 제목·표 구조와 새 오류 선언/예제의 양쪽 동일성, 상대 링크 428개를 확인했다. 새 helper/조회 6개 export와 외부 공유 소비자의 링크를 확인했다. 문서만의 변경 후 빌드·runtime을 추가 실행하지 않았다.

### Native Writer 요청별 진단 단위: 구현·검증·재사용

- 2026-11-02: C `uv_writer.c`의 요청 메시지·결과 매핑·전달, C++ KAIO/threadpool/poll·eventfd·초기화·종료, log_writer 재시도/완료와 file_backend 스냅샷 작업·UV 보호 콜백을 추적했다. writer 초기화의 선택적 진단 출력과 내부 writer/log/file `error_details()`를 추가했다. product는 C에 위임하지 않고 기존 표준 error_info 값을 사용한다. 원본 C 소스는 보존했다.
- worker는 자기 요청의 상태·진단만 작성하고 이벤트 루프가 완료 전에 공개한다. 네이티브 errno와 operation·FD·offset, 쓰기 결과/예상 바이트를 유지한다. poll 실패는 대기 요청에 복사하고 종료 작업은 별도 소유 진단을 가져, 파일 핸들이 해제된 뒤에도 조회/복사할 수 있다. 초기화/queue/submit·부분 쓰기·EAGAIN fallback·후속 정리의 기존 코드와 완료/버퍼 수명 순서는 유지한다. 음수 ENOSPC 결과는 기존 io_error와 원인 ENOSPC를 함께 보존한다.
- 로그는 `open-N` 이름과 원인을 덧붙인다. 성공 재시도 후에도 관찰한 진단을 남길 수 있으며 기존 반환/완료와 last_error 상태는 바꾸지 않는다. 스냅샷 작업도 요청별 준비/공개/조회 문맥을 보관한다. 완료 중 선택적 표준 값으로 현재 진단을 우선해, 이전 writer의 치명적 오류와 새 조회 오류/취소가 섞이지 않게 했다. UV의 보호 콜백은 사용자 콜백 전에 해당 문맥을 소유한다. 디스크 syscall 전체의 개별 errno/경로와 TCP 상세 원인은 아직 남은 범위다.
- 새 `writer_diagnostics`는 초기화 5곳·즉시 제출 2곳, 서로 다른 길이/offset/원인을 가진 동시 worker 4개, short write와 범위를 넘는 합성 결과(INT64_MIN), 종료 queue EAGAIN 재시도 및 실제 NOWAIT/poll 결과를 검사한다. `log_diagnostics`는 첫 ENOSPC 쓰기 실패 후 성공 재시도·원인/segment 수명·기존 성공 상태를 확인한다. `file_diagnostics`는 동시 스냅샷 조회 오류 4개, 부분 writer 초기화 원인, writer 실패 뒤 다른 snapshot 오류, 성공 준비 재시도 기록 및 실제 준비 worker 대기 중 종료/취소와 자원 정리를 검사한다.

| 검사 범위 | 실행·결과와 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture ON | `cmake -S . -B build`와 `cmake --build build --target raftpp_writer_tests raftpp_log_writer_tests raftpp_uv_io_tests --parallel 3` 후 새 3개 및 `writer_async/writer_close/writer_failure/writer_allocation/log_failure/log_close/file_snapshot_failure/file_snapshot_close/uv_io_node` 9개를 `ctest --test-dir build -R '^raftpp.(writer_diagnostics\|log_diagnostics\|file_diagnostics\|writer_async\|writer_close\|writer_failure\|writer_allocation\|log_failure\|log_close\|file_snapshot_failure\|file_snapshot_close\|uv_io_node)$' --output-on-failure`로 실행해 12개 모두 통과했다. 로그: `/tmp/raftpp-worker-diagnostics-debug.log`. `\|`는 표 이스케이프이며 셸에서는 역슬래시를 뺀다. |
| 엄격한 Release 공유, UV/LZ4/fixture ON | `/tmp/raftpp-options-build`에서 같은 타깃을 `-Wall -Wextra -Wpedantic -Werror`로 빌드하고 `-R '^raftpp.(writer_diagnostics\|log_diagnostics\|file_diagnostics)$'` 새 3개만 실행해 모두 통과했다. 로그: `/tmp/raftpp-worker-diagnostics-release.log`. |
| ASan/UBSan 정적, UV/LZ4/fixture ON | `/tmp/raftpp-cmake-debug`에서 같은 영향 타깃/새 3개가 모두 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했다. 환경 제한으로 누수 검사는 제외했다. 로그: `/tmp/raftpp-worker-diagnostics-asan.log`. |
| 최종 callback/cancel 보완 | callback 중 현재 스냅샷 진단 우선과 취소의 code 일치를 보완하고 세 구성에서 `raftpp_uv_io_tests`만 다시 빌드했다. 각 구성에서 `-R '^raftpp.file_diagnostics$'` 1개만 다시 실행해 모두 통과했다. 중간 보완 로그는 `/tmp/raftpp-worker-diagnostics-{debug,release,asan}-final.log`, 취소 보완 최종 로그는 `/tmp/raftpp-worker-diagnostics-{debug,release,asan}-cancel.log`다. writer/log 및 기존 9개 통과는 관련 실행 경로가 같아 재사용했다. |
| 기존 99개·추가 matrix/외부 소비자 | **생략 — 기존 결과 재사용.** 이전 오류/설정/공개 API/fixture/메모리 단위에서 일치하는 기록을 사용한다. 합의·노드·네트워크·코덱·파일 형식·코어 전용 경로 및 외부 공개 헤더/링크 요구사항은 변하지 않았다. 선언 추가는 UV 전용 내부 detail 클래스이며 공유 테스트가 새 내부 export를 사용한다. 표준 진단 필드는 추가 할당이나 소유권 순환을 만들지 않는다. 처음 ASan TCP timeout은 실패로 유지하고 이 범위의 통과로 대체하거나 재실행하지 않았다. |

최초 Debug 실행 뒤 `build/Testing/Temporary/LastTest.log`에서 native direct block 4096/NOWAIT=1을 확인했으므로 그 실행은 조건부 poll 진단도 포함한다. 해당 임시 로그는 후속 단일 file_diagnostics 실행으로 갱신되며, 관찰한 값은 이 기록에 보존한다. UV/fixture ON 등록은 111개이며 fixture OFF는 104개다. 코어 전용은 기존 58/51개를 유지한다. 전체 suite/matrix 재실행 결과가 아니다. API 오류 설명서 두 언어본에 진단 원인·완료 시점·재시도 뒤 관찰 정보와 취소 우선순위를 설명했다. UV 정상 node 회귀는 실행했지만 이 단위의 native fault 검증은 writer/로그/파일 coordinator에서 수행했으며 모든 디스크 fault→UV→node 조합을 통합 검증한 것으로 확대 해석하지 않는다.

최종 문서/산출물 확인: API 12개 대응 쌍의 제목·표 구조와 상대 링크 428개, UV/fixture ON 등록 111개 및 새 내부 writer/log/file 진단 getter 3개의 공유 export를 확인했다. 문서 변경만으로 runtime·추가 옵션 검사를 반복하지 않았다.

### 디스크 호출별 진단 단위: 구현·검증·재사용

- 2026-11-02: C `uv_fs.c`·`uv_metadata.c`·`uv_snapshot.c`·`uv_segment.c`의 파일 연산/오류 전달과 C++ disk/probe·log_writer·file_backend·UV·node의 요청/정리 범위를 참고했다. `raft/`는 수정하지 않았다. disk_storage 오류 반환 메서드와 probe에 선택적 `error_info*` 출력을 추가했으며 성공하면 출력을 비운다. 공용/TLS 마지막 오류를 추가하지 않고 각 호출·worker가 진단 값을 소유한다. 파일 경로·POSIX 원인·기존 Raft 상태를 구분하고 best-effort 정리가 원래 오류를 덮어쓰지 않게 했다.
- metadata의 open/write/sync/close, 파일 stat/read/scan/rename/unlink/allocate/truncate, O_TMPFILE·linkat 스냅샷 공개와 probe의 네이티브 원인을 캡처한다. 정상 metadata 슬롯 부재·미지원 capability와 스냅샷 fallback 성공은 오류 진단을 남기지 않는다. 논리적 invalid/corrupt·할당 실패 등은 원인 없이 연산/디렉터리 문맥만 가질 수 있다. 모든 C 세부 문구를 그대로 복제하거나 모든 syscall fault 조합을 검증한 것은 아니다.
- prepare/finalize/truncate 요청이 자기 디스크 출력을 소유하고 이벤트 루프가 완료 전에 공개한다. 로그 완료 중 현재 요청 진단을 우선해 재진입 중 다른 실패가 섞이지 않게 했다. 스냅샷 worker는 원인을 유지하며 phase 문구를 감싼다. UV metadata 호출은 먼저 디스크 출력을 갱신한 뒤 전달해 인자 평가 순서에 의존하지 않는다. 기존 callback·재시도·자원 정리·영속화 순서는 유지한다.
- 새 `disk_metadata_diagnostics`는 실제 metadata1 EISDIR, write/close 주입, filesystem_error의 EROFS와 실패 후 독립 호출의 원인 초기화를 검사한다. `disk_snapshot_diagnostics`는 손상된 최신 스냅샷의 성공 fallback·linkat EEXIST·4개 thread의 각 16회 독립 출력과 실제 snapshot worker stat ENOENT를 검사한다. `disk_segment_diagnostics`는 O_EXCL EEXIST·finalize ENOENT·정리 실패보다 최초 write 오류 우선·OOM·probe EACCES를 검사한다. `disk_node_diagnostics`는 실행 중 metadata2 경로를 디렉터리로 바꾼 뒤 RequestVote의 실제 open EISDIR가 UV→node와 종료 뒤 소유 진단까지 유지되는지 검사한다. 소켓을 사용하지 않는다.

| 검사 범위 | 실행·결과와 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture ON | `cmake -S . -B build` 및 영향 타깃 빌드 후 `ctest --test-dir build -R '^raftpp.disk_(metadata\|snapshot\|segment\|node)_diagnostics$' --output-on-failure` 새 4개가 통과했다. 로그: `/tmp/raftpp-disk-diagnostics-debug-new.log`. `\|`는 표 이스케이프이며 셸에서는 역슬래시를 뺀다. |
| 기존 영향 범위, 같은 Debug 구성 | `raftpp_persistence_tests/raftpp_log_writer_tests/raftpp_uv_io_tests/raftpp_error_tests`를 빌드했다. `ctest --test-dir build -R '^raftpp.(metadata_files\|snapshot_files\|disk_failures\|compressed_snapshot\|disk_no_space\|segment_files\|segment_recovery\|segment_snapshot\|segment_failures\|log_failure\|log_close\|file_snapshots\|file_snapshot_failure\|file_snapshot_close\|file_diagnostics\|uv_io_node\|native_errors)$' --output-on-failure` 17개 모두 통과했다. C 파일/복구 비교와 실패·정리 경로를 포함한다. 로그: `/tmp/raftpp-disk-diagnostics-debug-regression.log`. |
| 엄격한 Release 공유, UV/LZ4/fixture ON | `/tmp/raftpp-options-build`에서 `cmake --build /tmp/raftpp-options-build --target raftpp_disk_diagnostics_tests --parallel 2`가 `-Wall -Wextra -Wpedantic -Werror`로 통과했다. 위 새 4개 정규식으로 해당 build의 CTest만 실행해 통과했다. 빌드/검사 로그: `/tmp/raftpp-disk-diagnostics-release-build.log`, `/tmp/raftpp-disk-diagnostics-release.log`. |
| ASan/UBSan 정적, UV/LZ4/fixture ON | `cmake -S . -B /tmp/raftpp-cmake-debug` 후 같은 새 타깃을 빌드하고 새 4개만 실행해 모두 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했다. 누수 검사는 환경 제한으로 제외했다. 로그: `/tmp/raftpp-disk-diagnostics-asan-{configure,build}.log`, `/tmp/raftpp-disk-diagnostics-asan.log`. |
| 기존 94개·추가 matrix/외부 소비자 | **생략 — 기존 결과 재사용.** 앞선 writer/오류/설정/공개 API/fixture/메모리 기록에서 일치하는 기준을 사용한다. 이번에는 기존 111개 중 위 17개만 재검증했다. 나머지 합의·네트워크·코덱·공개 API와 정상 파일 형식/상태 의미는 바뀌지 않았으며 새 진단은 할당하지 않는 표준 값이다. UV 전용 detail 선언과 UV 조건 안의 새 테스트 등록 외에 옵션·공개 헤더·외부 링크 요구사항은 바뀌지 않았다. core-only·추가 matrix와 외부 소비자를 다시 실행하지 않았다. 기존 ASan TCP timeout은 원인 미확정 실패로 보존하며 이번 통과로 대체하지 않았다. |

최초 Debug 빌드에서는 일괄 시그니처 수정 중 생긴 prune_snapshots의 불필요한 쉼표와 중첩 호출의 잘못된 진단 인자 삽입을 고쳤다. 이후 라이브러리/영향 타깃 빌드가 통과했다. 새 테스트의 close 반환값 무시 경고도 고친 뒤 실행했다. ASan 새 타깃의 첫 빌드는 기존 구성에 등록되지 않아 `No rule to make target`으로 종료했으며 configure 후 빌드·검사했다. 이 빌드 실패들을 runtime 통과로 재사용하지 않는다. 새 4개와 기존 17개는 각 Debug 실행에서 한 번씩만 검사했으며 변경 없는 검사를 반복하지 않았다. 파일 준비의 문구 검사만 정확한 snapshot 경로에 맞게 조정했다.

UV/fixture ON은 115개로 등록되며 fixture OFF의 예상 등록은 108개다. core-only 58/51개는 이전 기록을 유지한다. 등록 수는 전체 suite/matrix 실행 결과가 아니다. API 오류 설명서 두 언어본에 호출별 출력·경로/원인·fallback·정리 계약을 추가했으며 진행·검증 기록은 이 문서 쌍에만 남겼다.

최종 정적 확인: API 10개 대응 쌍(루트 지침/진행 기록 2개 쌍을 합하면 12개 쌍)의 제목·표 구조와 루트 포함 상대 링크 428개를 확인했다. CTest는 목록 조회로만 115개 등록을 확인했고 새 진단 출력이 포함된 내부 disk/probe/log_writer 공유 심볼을 확인했다. 문서 검사에서 코드 블록의 C++ 람다를 링크로 오인한 검증 스크립트는 코드 블록을 제외하도록 고쳤다. 문서만의 후속 변경으로 빌드·runtime을 반복하지 않았다.

### TCP 요청별 진단 단위: 구현·검증·재사용

- 2026-11-02: C `uv_tcp.c`·`uv_tcp_connect.c`·`uv_tcp_listen.c`·`uv_send.c`·`uv_recv.c`의 원인·재시도·eviction·쓰기/수신/종료와 C++ TCP·transport·UV·node의 요청/오류 전달을 참고했다. 원본 C 소스는 변경하지 않았다. `transport::error_details()` 선택적 가상 메서드는 빈 값을 기본 제공하고 `tcp_transport`는 실제 소유 진단을 반환한다. 사용자 구현의 override는 필수가 아니며 표준 error_code/error_category/array/string_view만 사용한다.
- TCP의 libuv 원인은 `libuv` 카테고리와 원래 음수 값을 보존한다. Linux errno에 해당하면 표준 error_condition에 연결하고 EAI/EOF/unknown은 libuv 값으로 유지한다. 동기 listener resolver는 `getaddrinfo` 카테고리 또는 EAI_SYSTEM의 당시 errno를 보존한다. 카테고리의 문자열 조회는 표준 owning string 경계이며 진단 구성/조회/복사는 할당하지 않는다. 경로 대신 연산과 peer ID/설정 주소를 담고 기존 256바이트 제한을 유지한다.
- 연결 시도와 전송 요청이 자기 정보를 보관한다. 실패 연결의 pending eviction은 처음 캡처한 값을 각 완료 요청에 복사해 다른 peer/재진입 실패에 오염되지 않게 한다. 실제 연결·handshake·RPC 제출/완료와 accept/read/decode·callback 예외 문맥을 기록한다. 취소는 이전 오류 원인 없이 자기 peer 문맥을 만든다. EOF·즉시 send 거절의 진단은 기존 last_error 의미를 바꾸지 않는다. 기존 최신 3개 보존·재시도·wire/handshake·완료/종료 순서를 유지했다.
- UV는 transport init/start/즉시·비동기 send 실패를 외부 콜백 전에 복사한다. 반환된 상태와 진단이 일치하지 않으면 Raft 상태로 설명하며 사용자 transport가 예외를 던진 경우 과거 진단을 재사용하지 않는다. 노드의 기존 실패 정책은 유지한다. 전송 오류만으로 노드가 반드시 종료된다는 의미가 아니다.
- 새 `tcp_diagnostics`는 listener ENOMEM·무할당 조회/복사·즉시 쓰기 ENOBUFS·두 peer의 독립 EIO/EPIPE 완료와 콜백 재진입/복사 수명을 검사한다. `tcp_peer_diagnostics`는 EAI_NONAME 카테고리·취소의 원인 초기화, 실제 두 peer의 ECONNREFUSED/4개 중 1개씩 eviction/나머지 종료와 실제 EOF의 기존 정상 상태를 검사한다. `tcp_backend_diagnostics`는 실제 포트 충돌이 UV→node 및 종료 뒤까지 유지되는지, UV 즉시/비동기 쓰기 원인, 동기 resolver 카테고리와 예외를 던지는 사용자 transport의 stale 원인 차단을 검사한다. 쓰기 완료 fault는 실제 native write가 끝난 뒤 테스트 전용 고정 슬롯에서 결과만 주입하며 product에는 주입 코드를 추가하지 않았다.

| 검사 범위 | 실행·결과와 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture ON | `cmake -S . -B build`와 `cmake --build build --target raftpp_tcp_tests --parallel 2` 후 `ctest --test-dir build -R '^raftpp.tcp_(diagnostics\|peer_diagnostics\|backend_diagnostics)$' --output-on-failure` 새 3개가 통과했다. 로그: `/tmp/raftpp-tcp-diagnostics-debug-new.log`. `\|`는 표 이스케이프이며 셸에서는 역슬래시를 뺀다. |
| 예외 문맥 보완 및 기존 영향 범위 | `raftpp_tcp_tests/raftpp_uv_io_tests`만 다시 빌드하고 `ctest --test-dir build -R '^raftpp.(tcp_backend_diagnostics\|tcp_exchange\|tcp_fragments\|tcp_malformed\|tcp_retry\|tcp_lifetime\|tcp_allocation\|tcp_interop\|tcp_failure\|tcp_shutdown\|tcp_settings\|uv_io_lifetime\|uv_io_allocation)$' --output-on-failure` 13개가 통과했다. 새 backend 1개 재검증+기존 영향 12개이며 다른 새 2개는 TCP 경로가 같아 앞선 통과를 재사용했다. C handshake/RPC 상호 운용·실제 native cancel·실패 주입·할당/종료를 포함한다. 로그: `/tmp/raftpp-tcp-diagnostics-debug-affected.log`. |
| 엄격한 Release 공유, UV/LZ4/fixture ON | `/tmp/raftpp-options-build`에서 `--target raftpp_tcp_tests --parallel 2`를 `-Wall -Wextra -Wpedantic -Werror`로 빌드하고 해당 build의 CTest에서 위 새 3개 정규식만 실행해 통과했다. 로그: `/tmp/raftpp-tcp-diagnostics-release-build.log`, `/tmp/raftpp-tcp-diagnostics-release.log`. |
| ASan/UBSan 정적, UV/LZ4/fixture ON | `/tmp/raftpp-cmake-debug`를 configure 후 같은 타깃을 빌드하고 새 3개만 실행해 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했으며 누수 검사는 환경 제한으로 제외했다. 로그: `/tmp/raftpp-tcp-diagnostics-asan-{configure,build}.log`, `/tmp/raftpp-tcp-diagnostics-asan.log`. |
| 외부 소비자 | `/tmp/raftpp-tcp-consumer/transport_errors.cpp`를 `g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror`로 현재 헤더와 이전 core-only 정적 라이브러리에 연결해 기본 가상 메서드의 소스 호환성을 확인했다. 같은 소스를 현재 공유 Release/libuv에 연결해 TCP 새 getter의 공개 export·가상 호출·초기화/resolver 진단을 확인했다. `/tmp/raftpp-tcp-consumer/transport-errors-{core,shared}`가 모두 통과했다. C++ virtual 인터페이스가 추가되므로 기존 바이너리 혼용의 ABI 호환성을 뜻하지 않으며 사용자 transport는 일치하는 헤더로 다시 빌드해야 한다. |
| 기존 103개·추가 matrix | **생략 — 기존 결과 재사용.** 앞선 디스크/writer/오류/설정/fixture/메모리 기록의 일치하는 범위를 사용한다. 기존 115개 중 이번에는 위 12개만 재검증했다. 합의·형식/코덱·파일 syscall·공개 core·fixture·메모리 allocator의 동작과 옵션/의존성 gate는 바뀌지 않았다. 정상 TCP cluster/tracing/resource 시나리오의 기존 상태·소유권·관찰자/allocator 의미도 유지하며 새 상세 실패와 콜백 경계는 위 검사로 검증했다. 추가 matrix/소비자 전체를 반복하지 않았다. 처음 ASan tcp_memory timeout의 원인은 여전히 미확정이며 해당 실패를 이번 통과로 대체하거나 재실행하지 않았다. |

TCP 검사는 로컬 루프백 소켓 때문에 기존 승인된 `ctest --test-dir` sandbox 예외로 실행했다. 첫 새 테스트 빌드에서 node 생성자의 인자 순서를 잘못 쓴 컴파일 오류를 고친 뒤 실행했으며 runtime 실패는 없었다. 최초 새 3개 통과 후 사용자 transport 예외의 stale 진단 재사용을 검토해 보완했고 해당 backend 범위만 다시 실행했다. 빌드/테스트를 중복 전체 실행하지 않았다.

UV/fixture ON은 118개 등록이며 fixture OFF의 예상 등록은 111개다. core-only 58/51개는 이전 결과를 유지한다. 등록 수는 전체 suite/matrix 실행 결과가 아니다. TCP/UV/오류 API 설명서 세 쌍을 갱신했고 진행 내역은 이 문서 쌍에만 남겼다.

최종 정적 확인: API 10개 대응 쌍의 제목·표 구조와 루트 문서를 포함한 상대 링크 432개를 확인했다. CTest 목록 조회로 118개 등록을, 공유 심볼 표와 외부 소비자로 새 `tcp_transport::error_details()` export를 확인했다. 문서만의 후속 변경으로 빌드·runtime을 반복하지 않았다.

### Legacy Callback 호환 드라이버 단위: 구현·검증·재사용

- 2026-11-02: 공개 `raft.h.in`·UV/fixture 선언과 C `legacy.c`·`convert.c`·`membership.c`·fixture/log 및 legacy 통합 검사, C++ node/core/fixture와 기존 비교 bridge를 대조했다. 전체 API 범위 대조를 착수했으며 이 단위의 실제 수정·검증은 callback 멤버십 검증 순서와 barrier/명령·설정 변경·리더십 이전 드라이버다. 모든 API 조합의 대조가 끝났다는 의미는 아니다. 제품 C++은 기존 C에 위임하지 않고 원본 `raft/`를 보존했다.
- `add`/`remove`는 공통 수명 진입 검사 후 설정 복사본의 ID·주소 검증을 리더/변경 가능 여부보다 먼저 수행한다. `assign`은 리더/이전 여부→역할 유효성→변경 가능 여부→대상 존재/기존 역할 순서로 수행한다. 기존에는 pending 변경 중 bad_role/duplicate_id/duplicate_address/bad_id 대신 cannot_change가 먼저 반환될 수 있었다. 성공·영속화·응답·콜백 계약은 유지했다.
- 새 `legacy_reference.c`는 원래 C fixture·callback API와 소유 요청을 구동하고 `legacy_compatibility.cpp`는 독립 C++ fixture를 구동한다. 일반 executable과 basename `unit-test` 두 모드에서 드라이버가 force_barrier를 명시한다. 제품 라이브러리는 실행 파일명을 읽지 않는다. 이벤트마다 상태/term/투표/last·commit·applied/timeout/리더, 설정 순서/역할, 저장 경계·정의된 엔트리 바이트, FSM 적용과 callback 상태·횟수·발생 시점을 비교한다.
- `legacy_barrier_{normal,forced}`는 선출 시 barrier 정책 및 command+barrier의 순서와 FSM 비적용을 비교한다. `legacy_membership_{normal,forced}`는 standby 승격, pending 중 여러 검증 오류 우선순위, spare 추가/제거와 voter 강등을 비교한다. `legacy_transfer_{normal,forced}`는 명령 적용→리더십 이전→새 리더의 명령과 모든 FSM 적용을 비교한다. C의 사용자 raft_barrier 버퍼 8바이트는 초기화하지 않으므로 내용을 비교하지 않고 길이/type/term·동작을 비교한다. command/configuration의 정의된 바이트는 비교한다.
- 공유 C 빌드는 내부 logGet을 내보내지 않아 첫 링크가 실패했다. 테스트 타깃에 변경하지 않은 C `log.c`를 함수별 section으로 컴파일하고 미사용 함수를 제거해 조회만 참조 측에 연결했다. C++ 제품의 의존/링크 규칙은 바뀌지 않았다. 첫 product 빌드의 assign 지역 변수 이름 충돌도 고친 뒤 실행했다.
- **참조 C sanitizer 실패:** `raft/src/fixture.c:30`의 N_MESSAGE_TYPES=6인 배열에 TYPE=6(TIMEOUT_NOW)을 인덱스로 써 `fixture.c:416`에서 `n_send[6]` 범위 초과가 발생한다. 두 C transfer 비교가 ASan/UBSan에서 이 지점으로 실패했다. 이 실패와 stack은 `/tmp/raftpp-legacy-asan.log`에 보존했다. C 원본·sanitizer 옵션을 완화하지 않았으며 해당 두 검사를 다시 실행하거나 통과로 재사용하지 않았다.
- 별도 `legacy_transfer_cpp_{normal,forced}`는 C fixture를 생성/호출하지 않고 같은 C++ 이전 시나리오의 명령·barrier 정책, 콜백 정확히 한 번, 새 리더와 세 FSM의 두 명령 적용을 검증한다. ASan/UBSan에서 두 범위가 통과했다. 이는 C++ 요청/수명 검사이며 sanitizer를 통과한 C 비교를 뜻하지 않는다. C transfer 비교의 Debug/Release/core-only 결과는 별도로 유지한다.

| 검사 범위 | 실행·결과와 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture ON | configure 후 `cmake --build build --target raftpp_legacy_normal_tests raftpp_legacy_forced_tests --parallel 2`, `ctest --test-dir build -R '^raftpp.legacy_(barrier\|membership\|transfer)_(normal\|forced)$' --output-on-failure`의 최초 C 비교 6개가 통과했다. 공유 조회 연결·C++ 전용 검사 및 최종 모든 FSM 확인 뒤 `-R '^raftpp.legacy_transfer(_cpp)?_(normal\|forced)$'` 4개만 실행해 통과했다. 바뀌지 않은 barrier/membership 4개는 앞선 결과를 재사용했다. 로그: `/tmp/raftpp-legacy-debug-new.log`, `/tmp/raftpp-legacy-debug-transfer.log`. `\|`는 표 이스케이프이며 셸에서는 역슬래시를 뺀다. |
| 기존 영향 2개 | `cmake --build build --target raftpp_node_tests --parallel 2` 후 `ctest --test-dir build -R '^raftpp.node_(cluster\|allocation)$' --output-on-failure`가 통과했다. 실제 add/promote/remove와 기존 할당/요청 수명 회귀를 포함한다. 로그: `/tmp/raftpp-legacy-debug-regression.log`. |
| 엄격한 Release 공유, UV/LZ4/fixture ON | `/tmp/raftpp-options-build`에서 위 두 타깃을 `-Wall -Wextra -Wpedantic -Werror`로 빌드하고 `-R '^raftpp.legacy_(barrier\|membership\|transfer\|transfer_cpp)_(normal\|forced)$'` 8개가 통과했다. 첫 logGet 링크 실패: `/tmp/raftpp-legacy-release-build.log`; 수정 뒤 빌드/검사: `/tmp/raftpp-legacy-release-final-build.log`, `/tmp/raftpp-legacy-release.log`. |
| ASan/UBSan 정적, UV/LZ4/fixture ON | `/tmp/raftpp-cmake-debug`에서 최초 C 비교 6개 중 barrier/membership 4개 통과, transfer 2개는 위 참조 C 배열 오류로 실패했다. 이후 해당 두 실패는 재실행하지 않고 `-R '^raftpp.legacy_transfer_cpp_(normal\|forced)$'`의 새 C++ 전용 2개만 실행해 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 누수 검사는 제외했다. 최초 실패 포함 로그: `/tmp/raftpp-legacy-asan.log`; C++ 보완: `/tmp/raftpp-legacy-asan-cpp.log`. |
| core-only Release 정적, UV/LZ4 OFF·fixture ON | `/tmp/raftpp-fixture-core`를 configure하고 두 타깃을 빌드했다. 최초 C 비교 6개와 최종 transfer/C++ transfer 4개가 각각 통과했다. 바뀌지 않은 앞선 4개는 재사용해 최종 8개 범위를 확인했다. 선택 의존성 없이 C callback/fixture 드라이버가 동작한다. 로그: `/tmp/raftpp-legacy-core.log`, `/tmp/raftpp-legacy-core-transfer.log`. |
| 기존 116개·추가 matrix/외부 소비자 | **생략 — 기존 결과 재사용.** 앞선 TCP/디스크/writer/설정/core/fixture/메모리 기록의 일치하는 범위를 사용한다. 기존 118개 중 위 node 2개만 다시 검사했다. callback 멤버십 검증 우선순위 외의 알고리즘·파일/전송/직렬화/allocator와 공개 헤더·export·외부 링크 계약은 유지한다. 테스트 등록은 기존 fixture 조건 안에만 추가했다. 추가 옵션 matrix와 외부 소비자는 반복하지 않았다. ASan C fixture의 새 실패 2개 및 이전 TCP timeout을 통과 결과로 대체하지 않는다. |

CTest 목록 조회는 UV/fixture ON 126개, core-only/fixture ON 66개를 확인했다. fixture OFF는 각각 기존 111/51개를 유지하는 조건이다. 전체 suite/matrix 실행 결과가 아니다. API 10개 대응 쌍의 제목·표 구조와 루트 포함 상대 링크 432개를 확인했다. node API 두 언어본에 오류 우선순위·remove의 bad_id·multi-voter force_barrier와 명시적 드라이버 정책을 설명했다. 진행/실패 기록은 이 문서 쌍에만 유지하며 문서만의 후속 변경으로 runtime을 반복하지 않았다.

### Native 서버·프로세스 클러스터 예제 단위: 구현·검증·재사용

- 2026-11-02: 원래 `raft/example/server.c`·`cluster.c`의 FSM/주기적 apply/스냅샷/restore·리더십 이전/신호·별도 자식 프로세스의 무작위 종료/재시작을 참고하고 현재 node/UV/TCP·오류·정리 계약까지 추적했다. `examples/server.cpp`, `cluster.cpp`, `counter.hpp`, `runtime.hpp`와 CMake 타깃을 독립 C++ 공개 API로 구현했다. 원본 C는 보존하며 제품 라이브러리 소스·헤더는 변경하지 않았다.
- counter는 native uint64_t 8바이트 명령과 스냅샷을 유지하되 memcpy/표준 shared_ptr·컨테이너와 RAII 버퍼를 사용한다. 결과는 공유 counter 값, 스냅샷은 독립 바이트를 소유한다. 서버는 3-voter 설정, loopback base+ID, 125ms apply·pre-vote·64/16 스냅샷 설정을 유지한다. 새 디렉터리는 생성하고 기존 데이터는 bootstrap 거절을 정상 처리해 복구한다. 신호/실행 시간 종료 후 실제 callback·네트워크/파일 핸들이 정리될 때까지 app/FSM/loop를 유지한다.
- 실행기는 libuv spawn/process/timer/signal과 표준 filesystem·문자열·난수로 세 프로세스를 관리한다. 무작위 1~15초 대기→SIGINT→exit/close→같은 데이터로 재시작을 유지하며 sibling 실행 파일을 찾아 작업 디렉터리에 의존하지 않는다. 제한 cycles/interval/base-port·FSM 설정을 제공하고 마지막 재시작 뒤 안정화 간격을 둔다. 예기치 않은 자식 실패는 나머지를 종료·회수하고 5초 강제 종료 watchdog은 실패 상태를 반환한다. 기본 데이터 위치는 `/tmp/raftpp`이며 기존 데이터를 자동 삭제하지 않는다.
- 초기 실제 재시작 검사는 예제의 `node.error()` 오용으로 실패했다. cannot_bootstrap은 과거 비치명 진단인데 실행 중 종료로 판단했다. 노드의 unavailable 상태로 판단하도록 고친 뒤 cluster 1개만 재실행했다. 그 실행은 동시 종료에서 리더의 인계 대기가 끝나지 않아 supervisor watchdog 실패를 확인했다. C legacy도 follower로 내려간 뒤 새 리더를 기다리는 동일 흐름이다. 라이브러리/합의 동작을 변경하지 않고 애플리케이션 인계만 최대 2초로 제한한 후 node.close로 실제 요청을 정리하게 고쳤다. 두 실패는 `/tmp/raftpp-example-debug.log`, `/tmp/raftpp-example-debug-restart.log`에 보존했고 최종 cluster는 통과했다.
- CMake `RAFTPP_BUILD_EXAMPLES`는 기본 OFF이며 UV 없이 ON이면 configure를 거부한다. `raftpp_example_server`/`raftpp_example_cluster`는 C++ 타깃에만 링크하고 실행 파일명은 `raftpp-example-server`/`raftpp-example-cluster`다. tests·fixture·LZ4는 필요하지 않으며 새 테스트는 TESTS+EXAMPLES 조건 안에서만 등록한다. AGENTS 두 언어본에 옵션/디렉터리를 기록했다.
- 새 `example_counter`는 명령/restore 크기 검사와 값 보존, 독립 snapshot 바이트 및 FSM 파괴 뒤 결과 소유권을 검사한다. `example_cluster`는 실제 자식 3개·종료/재시작 1회·스냅샷 restore·네 번의 종료 출력·모든 자식 회수를 검사한다. `example_failure`는 도움말·잘못된 ID·제한 시간 standalone follower 종료, 실행 파일 부재의 부분 process 핸들 정리와 두 번째 서버 port 충돌 시 다른 자식 종료를 검사한다. 테스트는 임시 데이터/사용 가능한 포트를 쓰고 timeout 시 자기 프로세스 그룹만 정리한다.

| 검사 범위 | 실행·결과와 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture ON·EXAMPLES ON | `cmake -S . -B build -DRAFTPP_BUILD_EXAMPLES=ON`, `cmake --build build --target raftpp_example_tests --parallel 2` 후 `ctest --test-dir build -R '^raftpp.example_(counter\|cluster\|failure)$' --output-on-failure` 실행. counter/failure 2개는 최초 통과했고 cluster는 위 두 예제 문제를 각각 수정한 뒤 해당 1개만 `-R '^raftpp.example_cluster$'`로 재검증해 통과했다. 최종 로그 `/tmp/raftpp-example-debug-handoff.log`; 바뀌지 않은 counter/실패 정리 경로의 앞선 통과를 재사용했다. `\|`는 표 이스케이프이며 셸에서는 역슬래시를 뺀다. |
| 엄격한 Release 공유, UV/LZ4/fixture ON·EXAMPLES ON | `/tmp/raftpp-options-build`를 `-DRAFTPP_BUILD_EXAMPLES=ON`으로 configure하고 같은 새 타깃을 `-Wall -Wextra -Wpedantic -Werror`로 빌드했다. 위 새 3개 정규식만 해당 build의 CTest에서 실행해 모두 통과했다. 로그 `/tmp/raftpp-example-release-{configure,build}.log`, `/tmp/raftpp-example-release.log`. |
| ASan/UBSan 정적, UV/LZ4/fixture ON·EXAMPLES ON | `/tmp/raftpp-cmake-debug`에서 같은 configure/타깃/새 3개가 모두 통과했다. 환경은 `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`이며 자식도 이를 상속한다. 누수 검사는 제외했다. 로그 `/tmp/raftpp-example-asan-{configure,build}.log`, `/tmp/raftpp-example-asan.log`. |
| 최소 예제 구성 | `/tmp/raftpp-example-minimal`: 엄격한 Release 정적, UV ON·LZ4/fixture/tests OFF·EXAMPLES ON으로 configure 후 `--target raftpp_example_cluster --parallel 2` 빌드와 두 실행 파일의 `--help`가 통과했다. 원래 C archive/object가 빌드되지 않았음을 확인했다. 공유 서버 ldd에도 libraft.so가 없고 libraftpp.so를 사용한다. 로그 `/tmp/raftpp-example-minimal-{configure,build}.log`, `/tmp/raftpp-example-link.log`. |
| 옵션 거부/제외 | `/tmp/raftpp-example-no-uv`에서 UV/LZ4/fixture/tests OFF·PkgConfig 비활성 상태로 EXAMPLES ON의 configure 거부 문구를 확인한 뒤 같은 디렉터리를 EXAMPLES OFF로 바꿔 configure 성공과 예제 타깃 제외를 확인했다. 런타임 suite는 실행하지 않았다. 로그 `/tmp/raftpp-example-gates.log`. |
| 기존 126개·추가 matrix/외부 소비자 | **생략 — 기존 결과 재사용.** 앞선 legacy/TCP/디스크/writer/core/fixture/메모리의 일치하는 기준을 사용한다. 이번에는 라이브러리 구현·공개 헤더/ABI·기능 macro·기존 검사가 바뀌지 않았다. 예제와 예제 옵션/조건부 등록만 추가했다. 전체 suite·matrix·추가 소비자를 반복하지 않았다. 원인 미확정 TCP timeout과 참조 C fixture transfer sanitizer 실패는 이번 C++ 예제 통과로 대체하거나 재실행하지 않았다. |

초기 빌드의 fclose 함수 포인터 attribute 경고는 표준 unique_ptr의 native FILE deleter로 고친 뒤 검증했다. 실제 소켓·프로세스 검사는 기존 승인된 `ctest --test-dir` sandbox 예외를 사용했다. C reference를 실행하지 않는 앱 검사이므로 앞선 C fixture bounds 제한과 구분한다. 실행한 옵션/테스트 범위를 넘겨 전체 이식·전체 sanitizer 완료로 해석하지 않는다.

예제 API 문서는 `docs/ko/examples.md`/`docs/en/examples.md`로 함께 추가하고 node API에서 연결했다. 11개 API 대응 쌍의 제목/표 구조와 루트 포함 상대 링크 442개를 확인했다. 예제 ON/UV/fixture ON 등록은 129개이며 OFF일 때는 기존 126개다. fixture OFF의 예상 등록은 예제 ON/OFF 각각 114/111개이고 core-only는 예제가 꺼져야 하므로 기존 66/51개다. 등록은 전체 실행 결과가 아니며 진행·실패 기록은 이 문서 쌍에만 유지한다.

최종 정적 확인: API 11개 대응 쌍·상대 링크 442개와 CTest 목록의 129개 등록을 재확인했고 실행 중인 예제 자식 프로세스가 남지 않았음을 확인했다. 예제 문서/진행 기록만의 후속 변경으로 빌드·runtime을 추가 실행하지 않았다.

### Native benchmark submit·통계 기반 검증 (2026-11-02)

- 참조 범위: `raft/tools/benchmark`의 main/report/timer/options/parse/submit/FS와 disk/disk_uring/profiler 전체 구현·헤더를 확인했다. 실제 수정 범위는 `tools/benchmark/{main.cpp,options.hpp,report.hpp,runtime.hpp,submit.hpp,submit.cpp,CMakeLists.txt}`, 루트/테스트 CMake와 `tests/benchmark.cpp`, `tests/benchmark_cli.cmake`다. 라이브러리 구현·공개 헤더·ABI·기능 macro·기존 테스트와 기준 C 원본은 변경하지 않았다. disk/io_uring/장치 조회/perf/trace는 아직 구현·검증하지 않았으며 현재 CLI는 submit만 제공한다. 도구 옵션이 켜진 것을 전체 이식 완료로 해석하지 않는다.
- 표준 vector/map/algorithm/chrono/locale 기반 보고서가 실제 ns 표본의 nearest-rank p50·최소·최대와 Bencher JSON 형태를 제공한다. C의 고정 histogram 범위·버킷 중앙값/최소값 문제를 재현하지 않으며 처리량은 초당 1,000,000,000 ns와 소수 연산 수를 정확히 사용한다. 처리량 helper만 검증했으며 submit 출력은 실제 apply/native write 지연이다. 빈 표본·잘못된 metric/중복/이름·비유한 값·기간을 거부하고 전역 locale과 무관한 JSON 숫자를 생성한다.
- submit은 공개 C++ node/uv_io/tcp_transport와 실제 디스크 writer를 사용하며 독립적으로 동작한다. 첫 명령 payload의 40바이트/후속 32바이트 framing과 두 준비 파일을 10ms마다 확인하는 초기 대기를 보존했다. 첫 표본도 기록하지만 명령 1개이면 대기 전에 종료한다. startup은 측정 밖이며 지원 크기 안에서 스냅샷은 발생하지 않는다. 기본 TCP 주소는 기존 고정 포트 대신 `127.0.0.1:0`이다. trace 제출/완료가 짝을 이루고 실제 writer 표본이 있어야 보고한다. 기한·signal·시작 실패와 정리 실패는 성공 측정으로 출력하지 않는다.
- 입력은 from_chars로 overflow를 검사한다. buf는 48..1MiB·8바이트 배수, size는 C unsigned 범위를 보존한 UINT32_MAX 이하·양수·4096 및 buf의 배수다. 잘못된 작은 buf의 뺄셈 underflow/0 나눗셈과 총량 나머지를 사전에 거부한다. 기존 디렉터리 안의 독립 임시 경로만 소유하고 모든 요청/핸들을 drain한 뒤 삭제한다. JSON을 만든 뒤 stdout flush 결과도 확인하며 `/dev/full` 실패가 종료 코드 0이 되지 않는다.
- 새 검사: `raftpp.benchmark_report`(정확한 홀수/짝수/0/30ms 단일 표본·처리량·오류·locale), `raftpp.benchmark_options`(기본값/짧은 옵션/등호/크기 overflow·정렬·범위·주소·기한), `raftpp.benchmark_submit`(실제 명령 1/4/64개·초기 대기·실제 writer·시작 실패·부모 파일/임시 데이터 정리), `raftpp.benchmark_cli`(실행 파일 JSON 파싱·잘못된 명령/인자·stdout 실패·정리). 테스트/benchmark ON에서만 4개를 등록하며 기존 CTest 등록은 유지한다.

| 구성·범위 | 실제 실행·기록과 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture·예제·benchmark ON | `cmake -S . -B build -DRAFTPP_BUILD_BENCHMARKS=ON` 후 `cmake --build build --target raftpp_benchmark_tests --parallel 2`가 종료 코드 0으로 끝난 뒤 `ctest --test-dir build -R '^raftpp.benchmark_(report\|options\|submit\|cli)$' --output-on-failure`의 새 4개만 실행해 모두 통과했다. `/tmp/raftpp-benchmark-debug-{configure,build}.log`, `/tmp/raftpp-benchmark-debug.log`. size의 32비트 상한 보완 후 options 1개만 재실행해 통과(`/tmp/raftpp-benchmark-debug-bounds.log`); stdout 보완 후 실행 파일만 다시 빌드하고 CLI 1개만 통과(`/tmp/raftpp-benchmark-debug-output.log`). 나머지는 같은 단위의 앞선 일치 결과를 재사용했다. `\|`는 표 이스케이프이며 셸에서는 역슬래시를 뺀다. |
| 엄격한 Release 공유, 같은 선택 기능 ON | `/tmp/raftpp-options-build`에 BENCHMARKS ON을 configure하고 같은 새 타깃을 `-Wall -Wextra -Wpedantic -Werror`로 빌드한 뒤 같은 새 4개만 실행해 모두 통과했다. `/tmp/raftpp-benchmark-release-{configure,build}.log`, `/tmp/raftpp-benchmark-release.log`. stdout 보완은 실행 파일만 재빌드하고 CLI 1개만 재검증해 통과했다(`/tmp/raftpp-benchmark-release-output-build.log`, `/tmp/raftpp-benchmark-release-output.log`). |
| ASan/UBSan Debug 정적, 같은 선택 기능 ON | `/tmp/raftpp-cmake-debug`에 BENCHMARKS ON을 configure하고 새 타깃을 빌드한 뒤 같은 새 4개만 실행해 모두 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 환경 제한으로 누수 검사는 제외했다. `/tmp/raftpp-benchmark-asan-{configure,build}.log`, `/tmp/raftpp-benchmark-asan.log`. stdout 보완 후 실행 파일 빌드·CLI 1개만 다시 통과했다(`/tmp/raftpp-benchmark-asan-output-build.log`, `/tmp/raftpp-benchmark-asan-output.log`). |
| 최소 benchmark 구성·링크 | 기존 `/tmp/raftpp-example-minimal`의 엄격한 Release 정적 UV ON 라이브러리 기준을 재사용하고 LZ4/fixture/tests/예제 OFF·benchmark ON으로 재구성했다. `--target raftpp_benchmark --parallel 2`와 `--help`, C archive 미포함을 확인했다. 공유 실행 파일은 libraftpp.so를 사용하며 libraft.so를 링크하지 않는다. `/tmp/raftpp-benchmark-minimal-{configure,build}.log`, `/tmp/raftpp-benchmark-link.log`. stdout 변경은 실행 파일만 재빌드했다(`/tmp/raftpp-benchmark-minimal-output-build.log`); 무관한 도움말/링크 검사는 반복하지 않았다. |
| UV 거부·OFF 제외 | `/tmp/raftpp-benchmark-no-uv`에서 UV/LZ4/fixture/tests/예제 OFF·PkgConfig 비활성, BENCHMARKS ON의 `RAFTPP_BUILD_BENCHMARKS requires RAFTPP_WITH_UV=ON` configure 거부를 확인했다. 같은 디렉터리를 BENCHMARKS OFF로 재구성해 성공과 benchmark 타깃 제외를 확인했다. `/tmp/raftpp-benchmark-gates.log`. |
| 기존 129개·추가 matrix/외부 소비자 | **생략 — 기존 결과 재사용.** 앞선 예제/legacy/TCP/디스크/writer/core/fixture/메모리의 일치하는 통과 기록을 재사용한다. 관련 라이브러리 구현·공개 헤더/ABI·기능 macro·기존 검사는 바뀌지 않았고, benchmark 및 그 옵션/조건부 테스트만 추가했다. 전체 suite·옵션 matrix·추가 소비자는 반복하지 않았다. 원인 미확정 TCP timeout 및 참조 C fixture transfer sanitizer 실패는 통과로 재사용하거나 이 새 도구의 통과로 대체하지 않았다. |

모든 runtime은 해당 빌드의 종료 코드 0을 확인한 뒤 실행했다. 로컬 TCP가 sandbox에서 제한된 실행은 기존과 같이 로컬 loopback 검사에 대한 승인된 CTest 실행을 사용했다. raw block 장치·io_uring·시스템 perf/trace 실행은 수행하지 않았다. 위 기록의 실행 시점은 2026-11-02이며 GCC 13.3/C++20·Linux·기존 libuv/LZ4 환경을 사용했다. 현재 새 runtime의 실패는 없으며 이전 실패/제한은 별도 기록 그대로 유지한다.

API 문서는 `docs/ko/benchmark.md`/`docs/en/benchmark.md`에 실제 submit 사용법·범위·소유권·측정·종료 계약만 추가하고 examples에서 연결했다. 문서/진행 기록만의 후속 편집에서는 빌드·runtime을 생략했다. API 12개 대응 쌍의 구조·상대 링크 452개와 CTest 목록의 133개 등록을 정적으로 확인했다. UV/fixture ON에서 예제/benchmark 모두 ON이면 133개, benchmark OFF이면 129개, 예제 OFF·benchmark ON이면 130개, 둘 다 OFF이면 126개다. fixture OFF의 예상 값은 모두 ON 118개·모두 OFF 111개이고, core-only는 두 도구 OFF에서 fixture ON/OFF 각각 기존 66/51개다. 목록·예상 수는 전체 suite 실행 결과가 아니다.

### Native benchmark disk/io_uring·장치 정보 검증 (2026-11-02)

- 앞선 goal 작업은 submit 구현·검증 기록을 완료해 진전한 상태이며, 이번에는 실제 남은 disk/io_uring 범위를 구현했다. `raft/tools/benchmark/{disk.c,disk_uring.c,fs.c,fs.h,disk_options.h,disk_parse.c}`와 이미 확인한 report/timer/profiler의 호출·자원·옵션 경계를 참조했다. 새 독립 C++ 구현은 `tools/benchmark/{disk_options.hpp,disk.hpp,disk.cpp,uring.hpp,uring.cpp,device.hpp,device.cpp}`이며 main·도구/테스트 CMake와 테스트를 연결했다. 기준 C 원본은 변경하지 않았다.
- C++20에 native FD/mmap/io_uring 표준 타입은 없으므로 기존 `detail::file_handle`과 scoped 링 소유자를 사용한다. 버퍼는 기존 aligned_buffer, 숫자·경로·컨테이너·통계는 표준 구현을 재사용했다. `common.hpp`에는 표준 `atomic_ref` 별칭만 추가했으며 라이브러리 구현·기존 API/ABI·동작은 바꾸지 않았다. 기존 Linux 헤더 감지 결과를 타깃 전용 `RAFTPP_BENCHMARK_HAS_IO_URING`에 적용하고 헤더가 없으면 submit을 보존한 채 disk가 명시적 비지원 오류를 반환한다. liburing·다른 의존성을 추가하지 않았다.
- 실제 Linux syscalls로 SQ/CQ/SQE를 소유·매핑하고 fixed-file/fixed-buffer·WRITE_FIXED·RWF_DSYNC를 연결했다. SINGLE_MMAP과 분리된 SQ/CQ를 모두 지원하며 깊이 1의 순차 쓰기·CQE byte count/음수 오류/token 검사를 수행한다. lock-free 표준 atomic_ref의 acquire/release를 사용하고 링 크기·offset·alignment·mask를 검증한다. SQ/CQ counter wrap, EINTR의 소비 전/후 구분, 즉시 제출 실패의 미수락 SQE 회수, 부분 생성 실패 정리를 구현했다. timeout/signal 뒤에는 수락된 요청을 drain한 뒤 unregister/munmap/close하며, close만으로 비동기 teardown이 끝났다고 가정하지 않는다. 장치 장애의 drain은 측정 기한을 초과할 수 있다.
- 디렉터리에는 private 임시 파일만 만들고 preallocate·정렬된 0 데이터 prefill·file/directory fsync 후 측정한다. 실제 쓰기 버퍼는 C와 같은 i%128 패턴이다. raw block 장치 경로는 용량을 조회하고 시작부터 size만큼 쓰되 preallocate/truncate/제거하지 않는다. 일반 파일 직접 지정·초과 크기·0/잘못된 정렬을 거부한다. sysfs는 표준 path/stream/from_chars로 읽고 overlay 등 미등록 장치의 generic 초기값, 64비트 start/end overflow, NVMe partition의 부모 write-cache와 nullb 장치를 처리한다. histogram별 장치 해상도는 정확한 표본 통계로 대체한다.
- 워밍업은 표본 밖에서 offset 0에 한 번 수행한다. 이후 정확히 size/buf번 측정한다. 처리량은 부분 MiB도 보존하며 링 설정·워밍업·측정 루프·링 정리를 포함한 실제 초를 사용한다. 단일 `disk:<buf>` JSON의 latency(p50/min/max ns)·throughput(MiB/s)만 실제 성공 후 보고한다. 사용자 API 문서와 CLI는 raw 장치 선택 시 시작부터 데이터를 덮어쓰는 계약과 비지원·시간 초과·정리 의미를 설명한다. 다른 backend를 io_uring으로 표시하는 대체 경로는 없다.
- 새 `benchmark_disk_options/device/target/ring/native` 5개는 각각 인자/overflow, synthetic sysfs의 64비트 sector·cache·손상 입력, 실제 O_DIRECT 파일/초기화/부모 보존/FD·임시 정리, 주입 syscall 경계의 링/실패/수명, 실제 커널 쓰기를 검증한다. 모의 링은 공유/분리 mmap·counter wrap·고정 SQE/pattern·등록/매핑 실패·CQE 오류/short/잘못된 결과·미수락 회수·timeout/signal drain을 검사한다. native를 실행할 수 없을 때만 return77로 별도 생략한다. 기존 CLI에는 실제 512바이트 disk JSON과 잘못된 옵션·비지원 출력 계약을 추가했다.

| 구성·범위 | 실제 실행·기록과 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture/예제/benchmark ON | `cmake -S . -B build -DRAFTPP_BUILD_BENCHMARKS=ON`과 `cmake --build build --target raftpp_benchmark_disk_tests --parallel 2`가 종료 코드 0으로 완료됐다. `ctest --test-dir build -R '^raftpp.benchmark_disk_' --output-on-failure`는 sandbox에서 새 4개 통과·native return77 생략이었다. `/tmp/raftpp-benchmark-disk-debug-{configure,build}.log`, `/tmp/raftpp-benchmark-disk-debug.log`. |
| Debug의 환경 제한과 실제 커널 실행 | 영향받는 CLI 최초 sandbox 실행은 기존 submit의 loopback TCP bind가 제한되어 실패했다(`/tmp/raftpp-benchmark-disk-cli-debug.log`). 코드 변경 없이 같은 검사를 승인된 로컬 실행 권한으로 확인해 통과(`/tmp/raftpp-benchmark-disk-cli-debug-loopback.log`)했으며 512-byte 실제 disk JSON도 파싱했다. 이어 같은 native 바이너리 1개를 승인된 실행에서 `-R '^raftpp.benchmark_disk_native$' --verbose`로 확인해 실제 커널 쓰기·정리 통과(`/tmp/raftpp-benchmark-disk-native-debug-kernel.log`). sandbox 생략/실패를 이 통과와 혼합하지 않는다. 실패한 test harness가 남긴 marker-only 디렉터리 하나는 내용을 확인한 뒤 제거했다. |
| 엄격한 Release 공유 | 기존 `/tmp/raftpp-options-build`, 같은 기능 ON·`-Wall -Wextra -Wpedantic -Werror`. configure와 새 타깃 빌드 종료 코드 0 후 승인된 로컬 실행에서 `ctest --test-dir /tmp/raftpp-options-build -R '^raftpp.benchmark_(disk_.*\|cli)$' --verbose`만 실행해 새 5개+기존 영향 CLI 1개 모두 통과했다. `/tmp/raftpp-benchmark-disk-release-{configure,build}.log`, `/tmp/raftpp-benchmark-disk-release.log`. `\|`는 표 이스케이프이며 실제 셸에서는 역슬래시를 뺀다. |
| ASan/UBSan Debug 정적 | `/tmp/raftpp-cmake-debug`, 같은 기능 ON. configure/새 타깃 빌드 종료 코드 0 후 위 6개 범위만 승인된 로컬 실행에서 모두 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 환경 제한으로 누수 검사는 제외했다. `/tmp/raftpp-benchmark-disk-asan-{configure,build}.log`, `/tmp/raftpp-benchmark-disk-asan.log`. |
| io_uring 헤더 OFF·최소 구성 | `/tmp/raftpp-example-minimal`의 엄격한 Release 정적·UV ON·tests/fixture/LZ4/예제 OFF·benchmark ON을 유지하고 `-DRAFTPP_HAVE_LINUX_IO_URING_H=OFF`로 configure했다. `--target raftpp_benchmark --parallel 1` 빌드가 종료 코드 0으로 끝났고 실제 타깃 정의의 HAS_IO_URING=0을 확인했다. disk 실행의 nonzero·빈 stdout·명시적 build-time 비지원 오류와 parent marker 보존/임시 데이터 미생성을 확인했다. `/tmp/raftpp-benchmark-disk-no-header-{configure,build}.log`, `/tmp/raftpp-benchmark-disk-no-header.log`. |
| 기존 132개·추가 matrix/외부 소비자·UV gate/링크 | **생략 — 기존 결과 재사용.** 직전 submit/report/options와 예제/legacy/TCP/디스크/writer/core/fixture/메모리의 일치하는 기록을 재사용한다. 기존 133개 중 CLI 1개만 main/dispatch 변경의 영향을 받아 재검증했다. 라이브러리 소스·동작·ABI와 기존 검사는 같고 common의 표준 타입 선언 추가만으로 전체를 무효화하지 않았다. BENCHMARKS의 UV gate·OFF 제외·링크 규칙도 바뀌지 않아 앞선 검사와 독립 C++ 링크 기록을 재사용한다. 전체 suite/matrix/소비자를 반복하지 않았으며 이전 TCP timeout·참조 C sanitizer 실패를 통과로 재사용하지 않았다. |

실제 새 runtime은 2026-11-02의 GCC 13.3/C++20·기존 Linux/libuv/LZ4 환경에서 확인했다. 승인된 실행의 I/O 대상도 private 일반 파일과 loopback뿐이며 raw block 장치와 시스템 perf/trace를 실행하지 않았다. raw-device capacity/write 경로는 구현했지만 실제 장치 검증은 미실시다. kernel perf/trace는 아직 구현·검증하지 않았으며 전체 API/호환 범위 대조와 기존 두 실패 제한도 남아 있다.

API는 한·영 `benchmark.md`에 실제 disk 옵션·측정·오류·자원 수명 계약으로 함께 추가했다. 진행·검증 기록은 루트 대응본에만 남겼다. 문서만의 후속 변경은 빌드·runtime을 생략했다. API 12개 대응 쌍과 상대 링크 452개, CTest 목록 138개를 정적으로 확인했다. UV/fixture ON의 예제/benchmark 모두 ON은 138개, benchmark OFF는 129개, 예제 OFF·benchmark ON은 135개, 둘 다 OFF는 126개다. fixture OFF의 예상 값은 둘 다 ON 123개·둘 다 OFF 111개이며 core-only는 두 도구 OFF에서 기존 66/51개다. 목록/예상 수는 전체 suite 재실행 결과가 아니다.

### Kernel perf/trace profiler 구현·선택 검증 (2026-11-02)

- 앞선 disk/io_uring 단위는 구현·검증 기록을 완료해 진전했다. 이번에는 `raft/tools/benchmark/profiler.c/.h` 전체와 disk/옵션/장치/정리 호출을 다시 대조했다. 새 독립 구현은 `tools/benchmark/{profiler.hpp,profiler.cpp,profiler_data.hpp,profiler_data.cpp}`이며 disk_options/disk/main/device와 CMake·테스트를 연결했다. 참조 C·라이브러리 소스·공개 API/ABI·공통 헤더는 변경하지 않았다. Linux perf ABI·tracepoint와 event tracing 공식 자료도 확인했다.
- 고정 C raw 구조체 대신 tracepoint format의 scalar/배열/`__data_loc` 위치·크기를 읽고 memcpy 기반 native 정수 및 NVMe little-endian command word를 해석한다. writable mmap의 head acquire/tail release·경계 순환·불완전/유실/overflow/throttle 기록을 검사한다. 표준 vector/map/algorithm으로 기록을 소유하고 CPU를 가로질러 monotonic 시간순으로 대응시킨다. 별도 표준 컨테이너·스마트 포인터·task/promise를 구현하지 않았다.
- CPU online 목록의 빈 번호를 지원한다. CPU별 perf group이 fixed-size sampling 페이지 64개와 metadata 페이지 1개를 소유하며 child 출력은 SET_OUTPUT ioctl로 leader에 모은다. FD_OUTPUT flag에 의존하지 않는다. FD/mmap·부분 초기화·활성화 실패·중지·읽기·스케줄 multiplexing·명시적 종료 오류를 처리하고 destructor는 남은 자원을 정리한다. 각 쓰기 뒤와 중지 때 ring을 소비하며 최종 명령 대응이 맞아야 보고한다.
- block device/parent 번호·partition offset, NVMe namespace/logical block 크기·controller/queue/command key를 구분한다. CID 재사용과 duplicate rq/bio 완료를 처리하고 외부 I/O 완료를 추적 대상과 섞지 않는다. block와 NVMe는 서로 다른 표본으로 `disk:block:<buf>`, `disk:nvme:<buf>`를 생성한다. 원래 C의 NVMe 보고서가 block 표본을 재사용하는 문제를 재현하지 않는다. 누락/모호/손상/실패·분할/병합과 voluntary context switch가 있으면 측정을 실패 처리하며 generic/미식별 장치에서 빈 성공 보고서를 만들지 않는다. 실제 NVMe/nullb workload에 대한 유효성은 아직 확인하지 못했다.
- --perf/-p, 반복 가능한 --trace/-t와 --tracefs 경로를 추가했다. 이름·중복·경로 이동을 검사하고 xhci-hcd와 같은 실제 하이픈 이름도 허용한다. trace는 subsystem별 event의 개별 enable과 tracing_on을 보존·활성화·복원하므로 mixed enable 값도 보존한다. trace 내용을 지우지 않으며 기존 C와 같은 mount의 trace에서 결과를 읽는다. 예외/부분 start도 복원을 시도한다. 테스트는 private 파일로 만든 tracefs fixture만 변경했고 host 설정은 변경하지 않았다. perf 헤더 감지는 UV 구성의 비필수 check와 타깃 전용 HAS_PERF 정의로 연결했다.
- 새 `benchmark_profiler_options/format/correlation/trace/ring/perf/native` 7개를 등록했다. 앞 6개는 옵션·손상 형식/dynamic field·비연속 CPU·partition/4KiB LBA/CID 재사용/독립 지연·mixed trace 복원/부분 실패·wrapped/lost perf 기록·주입 syscall의 초기화/FD/mmap/enable/multiplexing와 collector→JSON을 검증한다. native는 읽을 수 있는 실제 tracepoint 형식과 disabled perf FD/mmap 초기화 가능 여부만 조회하며 host event를 활성화하지 않는다. 실제 hardware metric 검증으로 해석하지 않는다.

| 구성·범위 | 실제 실행·결과와 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture/예제/benchmark ON | configure 종료 확인 전에 시작한 최초 build는 새 타깃을 찾지 못하고 종료했다(`/tmp/raftpp-profiler-debug-build.log`). configure 성공(`/tmp/raftpp-profiler-debug-configure.log`)을 확인한 뒤 새 타깃 `raftpp_benchmark_profiler_tests`를 빌드해 종료 코드 0을 확인했다(`/tmp/raftpp-profiler-debug-build-final.log`, 후속 준비 빌드 `/tmp/raftpp-profiler-debug-build-ready.log`). 이후 `ctest --test-dir build -R '^raftpp.benchmark_profiler_' --verbose`에서 새 6개 통과·native 1개 생략이었다(`/tmp/raftpp-profiler-debug.log`). |
| 기존 영향·native 환경 확인 | main/옵션/장치 정보/disk 연결이 바뀐 `benchmark_cli`, `benchmark_disk_options/device/native` 4개만 승인된 loopback/임시 일반 파일 실행에서 모두 통과했다. 같은 실행에서 profiler_native도 tracepoint format을 읽을 수 없어 return77 생략이었다(`/tmp/raftpp-profiler-affected-debug.log`). sandbox 안팎의 같은 미접근 상태를 확인했으며 host tracing을 켜거나 실제 kernel metric 통과로 대체하지 않았다. |
| 엄격한 Release 공유 | `/tmp/raftpp-options-build`, 기존 UV/LZ4/fixture/예제/benchmark ON·`-Wall -Wextra -Wpedantic -Werror`. configure/새 타깃 빌드 종료 코드 0 뒤 승인된 로컬 실행으로 `ctest --test-dir /tmp/raftpp-options-build -R '^raftpp.benchmark_(profiler_(options\|format\|correlation\|trace\|ring\|perf)\|cli\|disk_(options\|device\|native))$' --verbose`의 새 6개+기존 영향 4개만 실행해 모두 통과했다. `/tmp/raftpp-profiler-release-{configure,build}.log`, `/tmp/raftpp-profiler-release.log`. `\|`는 표 이스케이프이며 실제 셸에서는 역슬래시를 뺀다. |
| ASan/UBSan Debug 정적 | `/tmp/raftpp-cmake-debug`, 같은 기능 ON. configure/새 타깃 빌드 종료 코드 0 후 위 10개 범위만 승인된 로컬 실행에서 모두 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 환경 제한으로 누수 검사는 제외했다. `/tmp/raftpp-profiler-asan-{configure,build}.log`, `/tmp/raftpp-profiler-asan.log`. |
| perf 헤더 OFF·최소 도구 | `/tmp/raftpp-example-minimal`: 엄격한 Release 정적·UV/benchmark ON·tests/fixture/LZ4/예제 OFF를 유지하고 `-DRAFTPP_HAVE_LINUX_IO_URING_H=ON -DRAFTPP_HAVE_LINUX_PERF_EVENT_H=OFF`로 configure했다. 실제 HAS_PERF=0/HAS_IO_URING=1 정의와 타깃 빌드 종료 코드 0을 확인했다. --perf의 명시적 비지원·nonzero/빈 stdout·임시 데이터 정리가 통과(`/tmp/raftpp-profiler-no-header.log`)했고, 같은 실행 파일의 CLI script를 승인된 로컬 실행에서 확인해 submit/disk/private trace fixture 복원도 통과(`/tmp/raftpp-profiler-no-header-cli.log`)했다. configure/build 로그 `/tmp/raftpp-profiler-no-header-{configure,build}.log`. |
| 마지막 이름 보완 | 하이픈 이름 검증을 보완한 뒤 Debug/공유 Release/ASan·UBSan의 새 타깃과 최소 도구를 다시 빌드해 종료 코드 0을 확인했다(`/tmp/raftpp-profiler-{debug,release,asan,no-header}-names-build.log`). 각 테스트 구성에서는 `-R '^raftpp.benchmark_profiler_options$'` 1개만 재실행해 통과했다(`/tmp/raftpp-profiler-{debug,release,asan}-names.log`). 다른 동작과 최소 도구의 기존 block 입력/비지원 경로는 같은 단위의 앞선 기록을 재사용했다. |
| 기존 134개·추가 matrix/소비자/gate/링크·native 조건 | **생략 — 기존 결과 재사용.** 이전 138개 중 영향을 받은 위 4개만 다시 검증했고 나머지 submit/report/일반 disk target·ring/예제/legacy/core/fixture/메모리/UV/TCP 결과를 재사용한다. 라이브러리 구현·공개 헤더·ABI·기존 gate/링크 규칙은 같으며 optional header check 추가가 라이브러리 기능 구성을 바꾸지 않는다. 전체 suite/matrix/추가 소비자를 반복하지 않았다. Release/ASan에서 동일한 host native 미접근 검사는 반복하지 않았고 skip을 통과로 재사용하지 않는다. 기존 TCP timeout·참조 C transfer sanitizer 실패도 별도 보존한다. |

실행 시점은 2026-11-02, GCC 13.3/C++20·기존 Linux/libuv/LZ4 환경이다. 실제 perf 데이터 획득과 NVMe/nullb workload·raw block 장치는 미검증이다. tracing을 허용하는 외부 환경이 있어야 실제 kernel metrics를 검증할 수 있으며 현재의 모의 ABI·상태 복원·실제 일반 파일 CLI 통과만으로 이를 완료로 간주하지 않는다. 전체 API/호환 범위 대조와 기존 두 실패 제한도 남아 있다.

한·영 benchmark API에 실제 옵션·필수 권한/메타데이터·측정·오류·소유/복원 계약만 추가했다. 진행 기록은 루트 대응본에만 남겼으며 문서만의 후속 편집은 빌드/runtime을 생략했다. API 12개 대응 쌍·상대 링크 452개와 CTest 145개 등록을 정적으로 확인했다. UV/fixture ON에서 두 도구 ON은 145개, benchmark OFF는 129개, 예제 OFF·benchmark ON은 142개, 둘 다 OFF는 126개다. fixture OFF의 예상 값은 두 도구 ON 130개·둘 다 OFF 111개이며 core-only 두 도구 OFF는 기존 66/51개다. 등록/예상 수는 전체 suite 실행 결과가 아니다.

### C 공개 함수·인터페이스 대응 대조 (2026-11-02)

기준 헤더의 선언을 주석과 함수 포인터 멤버를 제외하고 추출한 114개 함수(raft.h.in 56개·uv.h 12개·fixture.h 46개)를 아래에 각각 한 번 배치했다. 대조는 C++ 기능·표준 대체·실제 검증의 대응이다. 선언/구현 위치를 확인한 것을 모든 실행 경로의 완료로 해석하지 않으며, 과거 검증은 이번 재실행과 구분한다. fixture 행은 모두 RAFTPP_WITH_FIXTURE를 필요로 하고 UV 행은 RAFTPP_WITH_UV를 필요로 한다.

| C 공개 함수 | C++ 대응·소스·검증 범위 |
| --- | --- |
| `raft_version_number` | reference_version/reference_version_number · version.hpp; public_types 기존 기록 |
| `raft_strerror`, `raft_errmsg` | error_code/error_info·node::error_message; error.cpp/node.cpp · 기존 오류 기록 |
| `raft_configuration_init`, `raft_configuration_close`, `raft_configuration_add`, `raft_configuration_encode`, `raft_configuration_decode` | configuration 값의 생성/소멸·add/encode/decode; configuration.cpp · 기존 형식 비교 |
| `raft_digest` | checksum.hpp의 digest · checksum.cpp · 기존 C 비교 |
| `raft_malloc`, `raft_free`, `raft_calloc`, `raft_realloc`, `raft_aligned_alloc`, `raft_aligned_free`, `raft_heap_set`, `raft_heap_set_default`, `raft_heap_get` | buffer/aligned_buffer·표준 PMR와 get/set/reset_memory_resource · memory.cpp/buffer.cpp. 표준 소유/resize로 대체하며 raw C allocator ABI를 제공하지 않는다. 기존 메모리 기록 |
| `raft_init`, `raft_close`, `raft_seed`, `raft_bootstrap`, `raft_recover`, `raft_start` | node 생성/소멸·close/seed/bootstrap/recover/start; node.cpp. 이벤트 드라이버는 core_state/start_event/RAII · 기존 노드 기록 |
| `raft_step` | step/core_event/core_update · core.cpp와 독립 합의 엔진 · 기존 이벤트 비교 |
| `raft_current_term`, `raft_voted_for`, `raft_commit_index`, `raft_timeout`, `raft_last_index` | node 조회·core.current_term/voted_for/commit_index·trail.last_index/next_timeout · 기존 public API 기록 |
| `raft_state`, `raft_role`, `raft_leader`, `raft_match_index`, `raft_catch_up`, `raft_transferee` | 이번 core state/local_role/leader/match_index/catch_up/transferee와 node 공통 연결 · core_api.cpp · C 1152사례/소유권/노드 검증 |
| `raft_set_election_timeout`, `raft_set_heartbeat_timeout`, `raft_set_install_snapshot_timeout`, `raft_set_pre_vote`, `raft_set_max_catch_up_rounds`, `raft_set_max_catch_up_round_duration`, `raft_set_max_inflight_entries`, `raft_set_capacity_threshold` | node setter·core timeout helper/설정 필드 · core_api.cpp/node.cpp · 기존 public_settings/node_settings |
| `raft_random`, `raft_state_name`, `raft_role_name` | random_within_range/state_name/role_name · random.cpp/types.hpp · 기존 C 비교 |
| `raft_apply`, `raft_barrier`, `raft_add`, `raft_assign`, `raft_remove`, `raft_transfer` | node apply/barrier/add/assign/remove/transfer · node.cpp. C apply는 실제 n==1 assert; 배열 선언만으로 배치 API가 있다고 판단하지 않는다. 기존 legacy/노드 기록 |
| `raft_last_applied`, `raft_set_snapshot_threshold`, `raft_set_snapshot_trailing` | node last_applied/set_snapshot_threshold/set_snapshot_trailing · node.cpp · 기존 스냅샷 기록 |
| `raft_uv_init`, `raft_uv_close` | UV 필요. uv_io 생성/소멸·비동기 close · uv_io.cpp · 기존 UV 수명 기록 |
| `raft_uv_set_block_size`, `raft_uv_set_segment_size`, `raft_uv_set_disk_retry`, `raft_uv_set_snapshot_compression`, `raft_uv_set_connect_retry_delay`, `raft_uv_set_tracer`, `raft_uv_set_auto_recovery` | UV 필요. 같은 이름의 uv_io setter · uv_io.cpp. C snapshot compression no-op 보존 · 기존 native 설정 기록 |
| `raft_uv_tcp_init`, `raft_uv_tcp_close`, `raft_uv_tcp_set_bind_address` | UV 필요. tcp_transport 생성/소멸·close/set_bind_address · tcp_transport.cpp. tcp_stream_transport의 raw 인계와 사용자 raw 플러그인의 stream_transport/uv_io 연결도 구현·검증했다 |
| `raft_fixture_init`, `raft_fixture_initialize`, `raft_fixture_close` | FIXTURE 필요. fixture 생성/소멸·close · fixture.cpp. C initialize는 init 별칭 · 기존 fixture 기록 |
| `raft_fixture_configuration`, `raft_fixture_bootstrap`, `raft_fixture_start` | fixture configuration/bootstrap/start · fixture.cpp · 기존 C fixture 비교 |
| `raft_fixture_n`, `raft_fixture_time`, `raft_fixture_get`, `raft_fixture_alive`, `raft_fixture_leader_index`, `raft_fixture_voted_for` | fixture size/time/get/alive/leader_index/voted_for · fixture.cpp · 기존 fixture 기록 |
| `raft_fixture_start_elect`, `raft_fixture_elect`, `raft_fixture_depose` | fixture start_elect/elect/depose · fixture.cpp · 기존 선거 비교 |
| `raft_fixture_step`, `raft_fixture_step_n`, `raft_fixture_step_until`, `raft_fixture_step_until_elapsed`, `raft_fixture_step_until_has_leader`, `raft_fixture_step_until_has_no_leader`, `raft_fixture_step_until_applied`, `raft_fixture_step_until_state_is`, `raft_fixture_step_until_term_is`, `raft_fixture_step_until_voted_for`, `raft_fixture_step_until_delivered` | fixture의 같은 step/조건 대기 메서드 · fixture.cpp · 기존 가상시간/순서 기록 |
| `raft_fixture_hook` | fixture::hook의 소유 function과 fixture_event 값 · 기존 callback/수명 기록 |
| `raft_fixture_disconnect`, `raft_fixture_reconnect`, `raft_fixture_saturate`, `raft_fixture_saturated`, `raft_fixture_desaturate` | fixture의 같은 방향별 네트워크 제어 메서드 · 기존 분할/전송 기록 |
| `raft_fixture_kill`, `raft_fixture_revive`, `raft_fixture_grow` | fixture kill/revive/grow · fixture.cpp · 기존 장애/복구 기록 |
| `raft_fixture_set_randomized_election_timeout`, `raft_fixture_set_network_latency`, `raft_fixture_set_disk_latency`, `raft_fixture_set_term`, `raft_fixture_set_snapshot`, `raft_fixture_add_entry`, `raft_fixture_io_fault` | fixture의 같은 설정/주입 메서드 · fixture.cpp · 기존 저장/실패 기록 |
| `raft_fixture_n_send`, `raft_fixture_n_recv` | fixture n_send/n_recv의 6종 메시지 counter · 기존 비교. C TIMEOUT_NOW 배열 오류는 재현하지 않는다. |
| `raft_fixture_event_type`, `raft_fixture_event_server_index` | fixture_event.type/server_index 값 · 기존 이벤트 380개 비교 |

공개 함수 이름 외의 인터페이스도 대조했다. `raft_io`의 init/close/load/start/bootstrap/recover/set_term/set_vote/send/append/truncate/snapshot_put/snapshot_get/time/random 15개는 C++ io 가상 메서드에 대응하고 capacity는 별도 조회다. `raft_fsm`의 apply/snapshot/restore/snapshot_finalize/snapshot_async 5개는 state_machine에 대응한다. 원래 C의 snapshot_async도 legacy 드라이버가 동기 호출하므로 worker/coroutine을 이 이름만으로 추가하지 않는다. tracer emit은 tracer/trace_callback·소유 callable에 대응하고 사용되지 않는 버전·reserved slot은 추가하지 않았다. request data/콜백/queue 수명은 노드의 소유 요청 상태와 완료 callback으로 바꿨다. core 이벤트·update·RPC·설정/스냅샷·버퍼는 독립 값 타입과 기존 바이트 형식 기록에 대응한다.

2026-11-03 보완: 위 114개 함수 선언 목록과 별개인 공개 구조체 필드 `raft.legacy.step_cb`를 추가로 확인했다. 이 기능은 `node::set_step_callback`/표준 소유 callback으로 대응하며, 시작 포함 실제 C 알림 209건과 수명/설치 검증은 아래 step 알림 단위에 기록한다. 함수 개수를 모든 공개 구조체 계약의 완료 근거로 해석하지 않는다.

현재 경계의 상태: raw stream 인계를 제공하는 uv_stream owner·uv_stream_transport·tcp_stream_transport와, 사용자 raw 플러그인을 framing·송신 큐·재연결을 거쳐 uv_io에 연결하는 stream_transport를 구현·검증했다. pipe 사용자 플러그인, 기존 TCP 메시지 전송과의 양방향 상호 운용, 실제 디스크 3노드에서 확인했다. 이 기록은 해당 API 경계의 검증이며 전체 C 실행 경로의 완료 증명이 아니다. 실제 kernel profiler/raw 장치, 최초 TCP timeout과 참조 C fixture sanitizer 실패는 별도 제한으로 남아 있다.

### 공개 이벤트 코어 조회·노드 공통 구현 검증 (2026-11-02)

- 이전 profiler 단위는 실제 구현과 선택 검사/환경 제한 기록을 완료해 진전했다. 이번에는 C 공개 헤더 세 개와 `raft.c`/`state.c`/`progress.c`의 조회, node.cpp의 기존 조회 및 호출자의 lifecycle을 대조했다. C 선언의 배열 인자와 실제 `raft_apply`의 n==1 assert, fixture_initialize의 init 별칭, 동기 호출되는 FSM snapshot_async도 소스에서 확인해 없는 동작을 추정해 추가하지 않았다. 전체 공개 함수 대응은 바로 앞 목록에 기록하며 raw-stream 플러그인 경계는 미완료로 구분했다.
- `core.hpp`와 `core_api.cpp`에 6개 읽기 전용 조회를 추가했다. 리더가 아니면 not_leader, 리더의 미등록 ID는 bad_id, 수동으로 잘못 구성해 progress가 없는 경우는 invalid이며 실패 시 출력은 보존한다. 인계 중에도 progress는 읽을 수 있지만 leader 조회는 비어 있다. leader_info는 types.hpp로 공유하고 소유 string 값으로 코어 변경/해제 후에도 수명을 유지한다. node는 이동/종료 guard를 먼저 적용한 뒤 같은 코어 조회를 사용한다. 합의 알고리즘·I/O·RPC/디스크 형식은 바꾸지 않았다.
- 새 `core_query_reference`는 원본 C public 함수에 직접 질의하는 `query_reference.c/.h` bridge와 상태 3×로컬 역할 3×self 등록 2×알려진 리더 2×인계 2×대상 4×catch 상태 4의 1152사례를 비교한다. state/role/leader/progress/transferee, 실패 우선순위·출력 보존과 update/clock/random 불변을 확인한다. 새 `core_query_ownership`은 긴 리더 주소의 변경/해제 후 수명과 잘못 구성한 progress의 안전한 오류를, 새 `node_queries`는 실제 메모리 backend의 시작/commit·알려진 follower 리더·이동/종료 guard를 검사한다. C bridge는 변경하지 않은 원본 헤더의 타입과 public 함수만 사용한다.

| 구성·범위 | 실제 실행·기록과 재사용 조건 |
| --- | --- |
| Debug 정적, UV/LZ4/fixture/예제/benchmark ON | `cmake -S . -B build` 후 `cmake --build build --target raftpp_core_query_tests raftpp_node_tests --parallel 2`의 종료 코드 0을 확인했다. `ctest --test-dir build -R '^raftpp.(core_query_(reference\|ownership)\|node_(queries\|settings\|cluster))$' --output-on-failure`에서 새 3개·기존 영향 node_settings/node_cluster 2개가 모두 통과했다. `/tmp/raftpp-core-query-debug-{configure,build}.log`, `/tmp/raftpp-core-query-debug.log`. `\|`는 표 이스케이프이며 실제 셸에서는 역슬래시를 뺀다. |
| 엄격한 Release 공유 | `/tmp/raftpp-options-build`, 같은 기능 ON·`-Wall -Wextra -Wpedantic -Werror`. configure/위 두 타깃 빌드 종료 코드 0 뒤 `-R '^raftpp.(core_query_(reference\|ownership)\|node_queries)$'`의 새 3개만 통과했다. `/tmp/raftpp-core-query-release-{configure,build}.log`, `/tmp/raftpp-core-query-release.log`. |
| ASan/UBSan Debug 정적 | `/tmp/raftpp-cmake-debug`, 같은 기능 ON. configure/빌드 종료 코드 0 뒤 같은 새 3개만 모두 통과했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 환경 제한으로 누수 검사는 제외했다. `/tmp/raftpp-core-query-asan-{configure,build}.log`, `/tmp/raftpp-core-query-asan.log`. |
| core-only 엄격한 Release 정적 | `/tmp/raftpp-fixture-core`, UV/LZ4/예제/benchmark OFF·fixture/tests ON. configure/같은 두 타깃 빌드 종료 코드 0 뒤 새 3개를 모두 통과했다. `/tmp/raftpp-core-query-core-{configure,build}.log`, `/tmp/raftpp-core-query-core.log`. |
| 외부 공유 core-only 소비자 | `/tmp/raftpp-query-consumer`: 독립 CMake 프로젝트가 add_subdirectory로 raftpp를 포함하고 raftpp::raftpp에만 링크했다. UV/LZ4/fixture/tests/예제/benchmark OFF·PkgConfig 비활성·엄격한 Release 공유로 configure/build 종료 코드 0 후 실행이 통과했다. 실제 start_event에서 단일 voter 선거 후 새 조회 6개와 코어 해제 후 소유 리더 결과를 확인했다. nm에서 새 6개 export, link 명령/archive를 확인했다. `/tmp/raftpp-core-query-consumer-{configure,build}.log`, `/tmp/raftpp-core-query-consumer.log`. |
| 기존 143개·추가 matrix/다른 소비자 | **생략 — 기존 결과 재사용.** 이전 145개 중 조회 경로를 통합한 기존 node_settings/node_cluster 2개만 회귀 확인했다. 새 C 전수 조합과 node 이동/종료 사례로 기존 유효 상태의 조회 의미가 보존됨을 확인했으며 나머지 합의·영속화·전송·fixture·메모리·도구 코드는 그대로다. 선언 이동과 새 export만으로 전체 suite/matrix/모든 소비자를 반복하지 않았다. native profiler 미접근은 skip으로, 이전 TCP timeout·참조 C fixture sanitizer 실패는 별도 실패 제한으로 보존하며 통과로 재사용하지 않는다. |

실행은 2026-11-02의 GCC 13.3/C++20·기존 Linux 환경에서 확인했으며 새 범위에는 실패가 없었다. 이 단위는 소켓·실제 kernel tracing·raw 장치를 실행하지 않았다. 한·영 core/node API에 실제 조회·소유·오류 계약을 함께 갱신했다. 문서만의 후속 변경에서는 빌드/runtime을 생략했다. API 12개 대응 쌍·상대 링크 452개, CTest의 UV/full 148개·core-only/fixture 69개 등록을 정적으로 확인했다. UV/fixture ON의 두 도구 OFF 기본은 129개, 예제 ON은 +3개, benchmark ON은 +16개다. fixture OFF 기본은 114개이며 core-only/fixture OFF는 54개다. 등록/예상 수는 전체 suite 재실행 결과가 아니다.

### Raw-stream 소유권·TCP 인계 검증 (2026-11-02)

- 앞선 goal 작업은 테스트 재사용 지침과 두 언어 기록 형식을 실제 갱신해 진전했다. 이어 기존 raw-stream 작업의 소스와 빌드 종료를 확인하고, `raft/src/uv_tcp{,_connect,_listen}.c`, `uv_send.c`, `uv_recv.c`, 공개 UV transport 멤버와 C++ TCP/codec/오류/메모리·callback 정리 경로를 범위로 대조했다. C 원본은 변경하지 않았다.
- 새 공개 `uv_stream.hpp`/`uv_stream_transport.hpp`, 독립 `uv_stream.cpp`, 기존 TCP raw 분기와 공유 private `uv_error.hpp`를 추가했다. RAFTPP_WITH_UV로 소스·헤더 선언·새 CTest 6개를 선택한다. fixture/LZ4와 독립이며 C++ product는 C 구현을 호출하지 않는다.
- std 소유 포인터·함수와 resource 표준 컨테이너로 native handle의 원래 할당을 유지한다. 생성 성공 후에만 채택하며 native data를 close 뒤 복원하고 deleter를 호출한다. read chunk와 scatter write buffer를 소유하고, accepted write→native close/deleter→close observer 순서를 유지한다. 추가 close observer의 할당 실패는 명시적 오류이며 무인자 close/destructor는 할당 없이 종료를 시작한다.
- raw TCP는 listen 없이 connect할 수 있으며 DNS 결과의 IPv4 주소를 시도한 뒤 한 번 완료한다. 메시지 모드의 무기한 연결 재시도와 구분한다. 완료된 stream은 connector 종료·실제 파괴 후에도 독립적으로 사용한다. 수신자 인계 전에 handshake read를 멈추고 request/handle을 이동하지 않는다. `uv_stream`은 TCP/pipe/TTY 채택을 선언하지만 이번 native 실행은 TCP만 검증했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 새 raw 6개 Debug | 실행·범위별 재사용 | `/tmp/raftpp-stream-debug.log`의 초기 4개 통과; `/tmp/raftpp-stream-debug-handoff.log`의 보강한 exchange/cancellation 및 새 allocations/interop 4개 통과 | connector를 실제 파괴하는 fixture 보강 뒤 영향받은 검사만 실행했다. failure/adoption의 앞선 결과는 그대로 재사용했다. |
| 최종 Debug 인계 | 실행 | `/tmp/raftpp-stream-debug-final.log`의 exchange/interop 2개 통과 | Release 경고 수정 뒤 두 native 인계 경로만 재확인했다. 빌드 exit 0을 먼저 확인했다. |
| 기존 TCP Debug 영향 15개 | 실행 | `/tmp/raftpp-stream-tcp-debug.log`, 모두 통과 | resolve/listen/init/error-category 공유 코드의 메시지 모드·C handshake·실제 cluster·진단·종료를 확인했다. tcp_memory의 이전 ASan timeout은 원인 미확정으로 보존하며 조사 없이 반복하지 않았다. |
| 엄격한 공유 Release의 새 6개·기존 4개 | 실행 | `/tmp/raftpp-stream-release.log`, 10개 통과 | 기존 4개는 tcp_exchange/shutdown/diagnostics/peer_diagnostics. 첫 빌드의 직접 type-punned 역참조 경고는 `/tmp/raftpp-stream-release-build.log`에 보존했다. 인계 helper를 libuv base 포인터로 받도록 고친 뒤 `/tmp/raftpp-stream-release-handoff-build.log`의 exit 0 이후 실행했다. |
| ASan/UBSan의 새 6개·기존 4개 | 실행 | `/tmp/raftpp-stream-asan.log`, 10개 통과 | `/tmp/raftpp-stream-asan-handoff-build.log`의 exit 0 뒤 실행했다. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 환경 제한으로 누수 검사는 제외했다. |
| 새 UV ON 공유 외부 소비자 | 실행 | `/tmp/raftpp-stream-consumer/{CMakeLists.txt,main.cpp,build}` strict Release 빌드·실행 exit 0 | LZ4/fixture/tests/examples/benchmarks OFF. 새 헤더만으로 문서 예제를 컴파일하고 채택/이동/즉시 실패/close/data 복원을 실행했다. nm/ldd에서 새 공개 심볼과 링크 구성을 확인했다. |
| 기존 UV OFF 공유 외부 소비자의 새 헤더 포함 | 실행 | `/tmp/raftpp-stream-core-consumer-build.log`, query_consumer 실행 exit 0 | `/tmp/raftpp-query-consumer`에서 새 두 헤더와 umbrella를 추가해 선택 의존성·PkgConfig 없이 헤더를 사용할 수 있음을 확인했다. 기존 core query 동작도 유지했다. |
| 관련 없는 기존 Debug 133개·나머지 옵션/소비자 | 생략 — 기존 결과 재사용 | 바로 앞 core-query 및 profiler/디스크/native 단위의 해당 기록 | raw 기능은 UV guard 안이며 코어·디스크·LZ4·fixture·예제/도구의 동작과 옵션 규칙은 변경하지 않았다. 기존 148개 중 선택한 TCP 15개 이외의 검사를 새 통과로 합산하지 않는다. |
| 메시지 adapter/UV 연결·실제 kernel/raw 장치·이전 실패 | 미검증/미완료 유지 | 앞선 API 대조와 profiler/메모리/legacy 기록 | raw owner/TCP 검사를 전체 플러그인 연결 완료나 이전 실패의 해결로 확대하지 않는다. |

실행 명령은 빌드 뒤 선택한 `ctest --test-dir <build> -R <scope> --verbose --output-log <log>`였다. Debug는 최초 `^raftpp.stream_`, fixture 보강 뒤 `^raftpp.stream_(exchange|cancellation|allocations|interop)$`, 기존 TCP는 `^raftpp.tcp_`와 `-E '^raftpp.tcp_memory$'`, 최종 인계는 `^raftpp.stream_(exchange|interop)$`를 사용했다. Release/sanitizer는 `^raftpp.(stream_|tcp_(exchange|shutdown|diagnostics|peer_diagnostics)$)`를 사용했다. 개별 테스트 명령·결과는 로그에 남겼다. loopback socket 검사는 sandbox 밖 승인 실행이며, 실제 raw 장치/전역 tracing 설정은 사용하지 않았다. GCC 13.3/C++20·기존 Linux/libuv 1.48 환경이다. 두 언어 `uv_stream.md`에 실제 API·소유권·콜백·기능 조건을 설명하고 TCP/UV 문서에서 연결했다. 문서만의 후속 수정은 빌드·런타임을 반복하지 않는다.

정적 문서 검사에서 API 13쌍과 루트 포함 상대 링크 466개를 확인했다. 처음 예제 비교는 기존 node/storage의 번역된 주석을 차이로 판단했으므로 실제 차이를 확인한 뒤 코드 비교에서 주석을 제외했다. 이는 검사 도구의 전제 문제이며 라이브러리 테스트 실패가 아니다. CTest 목록으로 전체 옵션 활성 154개 등록(기존 148+UV 전용 새 6개)을 확인했으며 전체 suite 실행 결과가 아니다. 두 도구 OFF의 UV/fixture ON은 135개, fixture OFF는 두 도구 OFF 120개/둘 다 ON 139개가 된다. core-only는 fixture ON/OFF의 69/54개를 유지한다. 새로 확인한 목록은 전체 활성 구성이며 나머지 개수는 기존 gate에서 계산했다.

### Raw 플러그인 메시지 adapter·UV 연결 검증 (2026-11-02)

- 이전 goal 단위는 raw-stream owner/TCP 인계 구현과 검증을 기록하여 진전했다. 이번에는 원본 `raft/src/uv_send.c`의 client·대기 큐·eviction·재시도·주소 변경·write/close와 `uv_recv.c`의 단계별 수신을 참고하고, C++ codec/uv_stream/transport/uv_io/node의 callback·소유권 경계를 추적했다. C 원본과 기존 TCP 메시지 엔진·디스크/합의 알고리즘은 변경하지 않았다.
- 새 `stream_transport.hpp`/`stream_transport.cpp`가 raw connector의 수명주기를 단독 관리하고 메시지 framing·송신 큐·재연결을 제공한다. 공유 상태·resource 표준 컨테이너·std 참조 callable로 실제 완료까지 상태와 메시지를 소유한다. 연결의 즉시 실패는 큐를 보존하고, 수락한 연결의 실패는 최신 3개만 남긴다. 주소 변경·전체 종료는 대기 요청을 취소한다. 쓰기 실패 후 native close가 끝나야 재연결하며 완료·deleter·종료 콜백의 순서를 유지한다.
- uv_stream의 첫 비어 있지 않은 종료 observer를 표준 function 멤버로 이동 저장해 추가 할당을 없앴다. 후속 observer만 별도 list 노드 할당을 요구한다. adapter의 observer는 std::ref라 callable 생성도 할당하지 않는다. 수신 객체 준비에 실패한 stream도 따로 계수하고 native deleter 완료 뒤 전체 종료를 완료한다. 새 API·소스·7개 CTest는 UV로 선택하고 fixture/LZ4를 요구하지 않는다.
- 검사는 TCP뿐 아니라 독립 pipe 플러그인과 즉시 실행되는 connect/accept, stalled connect를 사용한다. 6종 RPC의 양방향 byte 비교와 3/7바이트 읽기 조각, 실제 TCP 메시지 엔진과의 상호 운용, 최신 3개 큐 보존·즉시 제출 실패 재시도·주소 변경·할당 실패·native EOF/과대 frame/콜백 예외·8MiB 미완료 native 쓰기의 취소와 수명·native 디스크 3노드의 선거/4개 명령 복제/종료를 확인했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 새 adapter 7개·기존 stream 종료 3개 Debug | 실행·범위별 재사용 | `/tmp/raftpp-adapter-debug.log`, `-debug-input.log`, `-debug-extra.log`, 최종 `-debug-deleter.log` | 최초 9개 중 2개 실패는 아래에 보존한다. 입력 수정 뒤 2개, 보강한 3개를 확인했다. deleter 공유 변경 뒤 pipe 6개만 다시 통과했고 영향 없는 TCP 1개·stream 3개는 같은 단위의 통과를 재사용했다. |
| 엄격한 공유 Release | 실행·범위별 재사용 | `/tmp/raftpp-adapter-release.log`의 10개 통과, 최종 `-release-deleter.log`의 영향 6개 통과 | `/tmp/raftpp-options-build`의 Release/공유·UV/LZ4/fixture/tests/examples/benchmarks ON, -Wall/-Wextra/-Wpedantic/-Werror. 빌드 exit 0 뒤 실행했다. |
| ASan/UBSan | 실행·범위별 재사용 | `/tmp/raftpp-adapter-asan.log`의 7개 통과/테스트 UAF 3개 실패 보존, 최종 `-asan-deleter.log`의 pipe 6개 통과 | 테스트 deleter만 수정한 뒤 관련 6개를 검증했다. 영향 없는 TCP 1개·stream 3개는 앞선 통과를 재사용했다. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1, UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. 환경 제한으로 누수 검사는 제외했다. |
| UV ON 공유 외부 소비자·문서 예제 | 실행 | `/tmp/raftpp-adapter-consumer-{configure,build}.log`, stream_consumer 실행 exit 0 | `/tmp/raftpp-stream-consumer` strict Release, UV ON/LZ4·fixture·tests·예제·도구 OFF. 새 헤더만의 예제와 adapter 생성/init/start·send 검증 오류/설정/진단/종료를 링크·실행했다. nm/ldd로 공개 export·C 미링크를 확인했다. |
| UV OFF 외부 소비자 새 헤더 | 빌드 실행·런타임 생략 | `/tmp/raftpp-adapter-core-consumer-build.log` exit 0; 기존 query_consumer 런타임 결과 재사용 | `/tmp/raftpp-query-consumer`의 PkgConfig/UV/LZ4/fixture/tests/tools/examples OFF·공유에서 새 전용 헤더/umbrella 포함을 확인했다. 실행 코드·core 동작은 같으므로 런타임을 반복하지 않았다. |
| 기존 영향 없는 151개·추가 옵션/소비자 | 생략 — 기존 결과 재사용 | 이전 raw-stream/core-query/profiler/디스크/native/fixture/예제·도구의 해당 기록 | 기존 154개 중 종료 영향 3개만 재검증했다. 새로운 adapter는 UV guard 내부이며 CMake의 기존 옵션 규칙·core/디스크/LZ4/fixture/기존 TCP 메시지 동작은 그대로다. |
| 이전 환경 제한·실패 | 미검증/미해결 유지 | 앞선 kernel/raw 장치·TCP timeout·C fixture sanitizer 기록 | 이번 성공을 실제 kernel 지표/장치·이전 독립 실패의 해결로 보고하지 않는다. |

최초 잘못된 preamble은 타입 0이어서 기존 decoder의 io_error였는데 테스트는 malformed를 기다렸다. 타입 1/헤더 크기 0으로 정확한 malformed 입력을 사용했다. 최초 native 명령 거부의 숫자 코드는 기록하지 않았으며, 기존 native 통합 검사처럼 capacity_threshold=0으로 용량 전제를 분리한 뒤 통과했다. 원인을 확정한 것처럼 최초 코드를 추정해 쓰지 않는다. sanitizer의 UAF는 테스트 pipe deleter가 bus 안 통계 객체를 참조 캡처하고, peer stream 정리가 bus 파괴 뒤 loop drain에서 끝난 것이었다. deleter가 shared 통계 상태를 소유하도록 고쳤으며 제품 코드를 고치거나 원본 C의 별도 실패를 덮지 않았다.

기본 선택 명령은 빌드 완료 뒤 `ctest --test-dir <build> -R '^raftpp.(adapter_|stream_(adoption|cancellation|exchange)$)' --verbose --output-log <log>`였다. Debug는 초기 tcp 추가 전 9개이고 이후 Release/sanitizer는 10개다. 입력 보완은 adapter_(receive_failure|uv_io), 보강은 adapter_(tcp|close|receive_failure), deleter 수정은 adapter_(exchange|retry|close|receive_failure|allocations|uv_io)만 정확히 선택했다. Linux/GCC 13.3/C++20/libuv 1.48과 기존 구성에서 실행했고 로컬 소켓 검사는 승인된 sandbox 밖 실행이다. 실제 raw 장치·전역 kernel tracing은 사용하지 않았다. API는 두 언어 stream_transport.md를 추가하고 uv_stream/uv_io 문서를 갱신했다. 문서만의 후속 수정은 빌드·런타임을 생략한다.

정적 문서 검사에서 API 14쌍의 제목·표 개수와 C++ 예제(번역 주석 제외), 루트 포함 상대 링크 500개를 확인했다. CTest 목록의 전체 옵션 활성 등록은 161개이며 앞선 154개에 UV 전용 7개를 더한 것이다. 전체 suite 실행을 뜻하지 않는다. 기존 gate에서 계산한 UV/fixture ON·도구 OFF는 142개, fixture OFF는 도구 OFF/둘 다 ON의 127/146개다. core-only fixture ON/OFF의 69/54개는 그대로다. 새로운 목록 확인은 전체 활성 구성만 수행했다.

### Fixture 네트워크·디스크 C 대조와 snapshot fault 수정 (2026-11-02)

- 이전 goal 단위는 stream adapter 구현·선택 검증을 완료해 진전했다. 이번에는 raft/src/fixture.c의 요청 스케줄·ioFaultTick·ioFlushSnapshotPut·ioMethodTruncate·저장/전송·kill/revive·안전성 관찰과 raft/src/legacy.c의 완료 콜백 경계를 C++ fixture/node/io 및 기존 검사와 대조했다. 참조 범위와 실제 수정은 구분한다. 제품 수정은 src/fixture.cpp의 zero-trailing snapshot 완료뿐이고, raft/ 원본은 변경하지 않았다.
- 새 fixture_network_reference는 3·5노드 각각 2300개 이벤트, 합계 4600개를 C와 비교한다. 매번 서버/종류/시간·안정 리더·노드 state/term·commit/last/applied index·영속 표·생존·FSM 적용 수·메시지 1..5의 송수신 수를 확인한다. 지연 변경, 단방향 단절/복원·포화/해제, follower kill/revive, leader kill/옛 leader 복귀를 같은 순서로 적용했다. TIMEOUT_NOW는 이 시나리오에서 보내거나 C 통계로 조회하지 않는다.
- 새 fixture_disk_reference는 양쪽 메모리 I/O를 직접 구동해 term/vote·연속 append·truncate·metadata/entry 바이트와 fault 횟수, 제출/완료 오류·출력 상태를 비교한다. C legacyPersistEntriesCb는 취소 이외의 완료 오류를 assert로 막으므로 해당 오류는 실제 I/O 요청/콜백으로 비교했다. 노드 드라이버 오류 동작 전체가 일치한다고 확대하지 않는다. 원본 C I/O와 콜백을 실행하며 fixture 내부 구조를 복제해 접근하지 않는다.
- 새 fixture_snapshot_fault_reference가 zero-trailing 완료 뒤 다음 vote의 실제 차이 C=18/io_error 대 C++=0을 재현했다. C ioFlushSnapshotPut처럼 truncate(1)을 거쳐 유효한 내부 truncate의 fault tick을 소비한다. 먼저 snapshot을 독립 복사하고 truncate 성공 뒤 새 값을 반영한다. 내부 실패 시 기존 snapshot/log·시작 인덱스를 보존하고 IO 오류로 한 번 완료하며 유한 실패 뒤 재시도할 수 있다. trailing=1은 카운터를 소비하지 않는다. C 자체는 내부 truncate 실패 시 assert하므로 성공 경로의 카운터만 C와 비교하고, 그 실패/보존/재시도는 C++에서 독립 검증했다.
- C 비교 bridge의 io->load 출력 개수를 처음 unsigned로 선언한 오류를 검토 중 확인해 API의 size_t로 수정했다. 해당 수정 전 Debug disk 통과는 근거로 사용하지 않고, 고친 bridge로 disk만 다시 실행했다. term/vote/start 출력도 원본 타입의 지역 변수로 받아 변환한다. 최종 C11 -Wall/-Wextra/-Werror syntax 검사가 통과했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 새 network/disk 비교 Debug | 실행·범위별 재사용 | /tmp/raftpp-fixture-controls-debug.log의 network 통과; 최종 disk는 -debug-count.log 통과 | 초기 disk 통과는 위 ABI 오류 때문에 재사용하지 않았다. network는 해당 load helper를 호출하지 않아 다시 실행하지 않았다. |
| 기존 fixture_reference Debug | 실행 | /tmp/raftpp-fixture-controls-debug-baseline.log 통과 | 새 bridge의 공통 C 생성 helper를 통해 기존 380개 기본 이벤트 비교를 확인했다. |
| 새 network/disk 및 기존 reference 공유 Release | 실행 | /tmp/raftpp-fixture-controls-release.log, 3개 통과 | /tmp/raftpp-fixture-controls-release, Release/shared/UV-LZ4 OFF/fixture-tests ON/examples-benchmarks OFF, -Wall -Wextra -Wpedantic -Werror. 빌드 종료 0 뒤 실행했다. |
| 새 network/disk 및 기존 reference ASan/UBSan | 실행 | /tmp/raftpp-fixture-controls-asan.log, 3개 통과 | /tmp/raftpp-fixture-controls-asan, Debug/static/core-only/fixture-tests ON. C와 C++ 양쪽 -fsanitize=address,undefined -fno-omit-frame-pointer. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1; 환경 제한으로 누수 검사는 제외했다. |
| snapshot fault 회귀 수정 전 | 실행·실패 보존 | /tmp/raftpp-fixture-snapshot-fault-before.log, C=18/C++=0으로 실패 | 같은 요청·지연·trailing·카운터 순서에서 실제 차이를 확인한 뒤 제품 코드를 수정했다. 실패를 통과로 재사용하지 않았다. |
| 새 snapshot fault·기존 storage/network 영향 검사 | 실행 | /tmp/raftpp-fixture-snapshot-fault-{debug,release,asan}.log, 구성별 3개 통과 | 새 카운터·실패/보존/재시도와 기존 snapshot 설치/저장 경로만 재검증했다. 각 빌드의 종료 0을 먼저 확인했다. 앞선 network_reference/disk_reference/기본 reference는 이 snapshot 완료 분기를 호출하지 않아 같은 단위의 결과를 재사용한다. |
| 최종 C bridge 경고 검사 | 실행 | cc -std=c11 -Wall -Wextra -Werror -Ibuild/raft/include -fsyntax-only tests/fixture_reference.c, exit 0 | 함수 포인터 인자·소유 메모리 타입을 확인했다. |
| 기존 영향 없는 158개·추가 옵션/소비자 | 생략 — 기존 결과 재사용 | 이전 adapter/raw/core-query/fixture/메모리/native/도구 등의 해당 기록 | 기존 161개 중 기본 reference·storage·network 3개만 재검증했다. API 시그니처·export·제품 옵션/의존 규칙은 그대로이며 fixture OFF에서는 해당 소스/검사가 제외된다. 공개 헤더·링크 요구사항이 같아 소비자를 반복하지 않았다. |
| 기존 제한 | 미검증/미해결 유지 | kernel/raw 장치·TCP 최초 timeout·C legacy TIMEOUT_NOW sanitizer 기록 | 이번 fixture 통과를 기존 별도 실패의 해결이나 실제 장치 검증으로 기록하지 않는다. |

실행 시점은 2026-11-02, Linux/GCC 13.3/C++20/C11과 기존 Debug의 UV/LZ4/fixture/tests/examples/benchmarks ON이다. 새 core-only 구성 두 개는 이전 /tmp 구성/빌드 핸들이 없는 상태를 확인한 뒤 이번 선택 검사에 필요한 타깃만 생성했다. 최초 남아 있던 build/의 증분 타깃 종료 0을 확인한 뒤 실행했으며 실행 중인 컴파일러와 소스를 동시에 수정하지 않았다. 새 검사는 파일·소켓·실제 kernel 장치를 사용하지 않는다.

선택 명령은 ctest --test-dir <build> -R '^raftpp.fixture_(network|disk)_reference$' --verbose --output-log <log>, 공유 Release/sanitizer는 기존 기본 reference를 포함한 '^raftpp.fixture_((network|disk)_)?reference$'였다. ABI 수정 뒤 Debug disk만, snapshot 수정 뒤에는 '^raftpp.fixture_(snapshot_fault_reference|storage|network)$'만 선택했다. 새 비교 3개는 30초 timeout으로 fixture 옵션 아래 등록한다. CTest 등록은 삭제하거나 영구 비활성화하지 않는다. docs/ko·en/fixture.md에는 실제 fault/상태 보존/재시도 API 계약만 갱신했으며 진행·검증 이력은 이 루트 대응본에만 남긴다.

정적 문서 검사에서 API 14쌍의 제목·표 개수/예제(번역 주석 제외)와 루트 포함 상대 링크 500개를 확인했다. CTest 목록은 전체 활성 Debug 164개·core-only fixture ON 공유 Release 72개이며 목록 확인을 전체 실행으로 보고하지 않는다. 기존 gate로 계산한 UV/fixture ON·도구 OFF는 145개, fixture OFF는 도구 OFF/둘 다 ON의 127/146개를 유지하고 core-only fixture OFF는 54개다. 문서만의 최종 변경에는 빌드·런타임을 생략했다.

### Native recover의 snapshot·로그·격리·반복 복구 C 대조 (2026-11-02)

- 이전 goal 단위는 fixture 내부 snapshot truncate의 실제 차이를 수정·검증하여 진전했다. 이번에는 raft/src/uv.c의 uvRecover/uvLoadSnapshotAndEntries/uvBootstrap, uv_segment.c의 filtering/load/quarantine/configuration 기록과 src/disk.cpp·src/uv_io.cpp의 대응, 기존 segments/snapshot/native 설정 검사를 범위로 읽었다. 기존 C와 C++ 제품 코드는 변경하지 않았다. 검증을 확장할 실제 남은 범위로 기존 snapshot 없는 기본 recover 비교 밖의 구성·복구 경계를 선택했다.
- 새 storage_recover_reference는 9가지 디스크 형태 × 자동 복구 ON/OFF × 1/5명 설정, 36개 초기 조건이다. snapshot 없는 로그, snapshot-only, snapshot 뒤로 남은 old closed, snapshot과 겹친 closed suffix, old closed+open, 유효 batch 뒤 손상된 open tail, 손상된 마지막 closed, index gap, 손상된 첫 open과 후속 open을 사용했다. 5명 구성에는 voter 3명·standby 1명·spare 1명이 있다. 성공 28개는 같은 인스턴스에서 한 번 더 복구하고 C도 같은 디렉터리를 다시 열어 복구하여 총 64회 호출했다.
- 새 C bridge는 실제 io->recover에 auto_recovery를 지정해 호출하며 기존 bridge는 유지한다. 같은 디스크를 복사해 C/C++에 각각 제공하고 오류 코드·모든 디렉터리 파일명/바이트를 비교한다. 격리 파일명은 시간 prefix만 정규화하고 격리된 내용도 비교한다. 성공 시 load 결과의 term/vote/start/엔트리/스냅샷 존재·새 설정 ID/주소/역할과 다음 index를 확인한다. 새 설정 entry의 term은 metadata의 9가 아니라 기존 log/snapshot의 4/6/7이며 metadata 버전·term·vote는 바뀌지 않는다.
- 이번 입력에서는 구현 차이가 발견되지 않았다. 기존 latest-snapshot fallback의 별도 C 비교와 native 오류/OOM 검사는 이전 기록을 재사용한다. 새 검사로 모든 recovery 실패 경로나 실제 crash/power-loss 원자성을 검증했다고 확대하지 않는다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 새 native recover Debug | 실행 | /tmp/raftpp-recover-reference-debug.log, 1개 통과 | 기존 build/의 Debug/static·UV/LZ4/fixture/tests/examples/benchmarks ON에서 대상만 빌드한 뒤 실행했다. |
| 새 native recover 공유 Release | 실행 | /tmp/raftpp-recover-reference-release.log, 1개 통과 | /tmp/raftpp-recover-reference-release, UV ON/LZ4-fixture-examples-benchmarks OFF/tests ON/shared/Release, -Wall -Wextra -Wpedantic -Werror. 대상 빌드 종료 0 뒤 실행했다. |
| 새 native recover ASan/UBSan | 실행 | /tmp/raftpp-recover-reference-asan.log, 1개 통과 | /tmp/raftpp-recover-reference-asan, 같은 선택 옵션의 static/Debug. C/C++ -fsanitize=address,undefined -fno-omit-frame-pointer. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. 빌드 종료 0을 확인했으며 환경 제한으로 누수 검사는 제외했다. |
| 새 C bridge 경고 검사 | 실행 | build/compile_commands.json의 persistence_reference.c 컴파일 인자를 argv로 읽어 -o/-c를 제거하고 -std=c11 -Wall -Wextra -Werror -fsyntax-only를 추가한 검사, exit 0 | 셸 재해석 없이 실행했다. 생성 C 헤더/플랫폼 매크로/함수 포인터 인자와 새 bridge의 타입을 검사했다. |
| 기존 164개·추가 옵션/소비자 | 생략 — 기존 결과 재사용 | 이전 fixture/adapter/raw/core-query/native/도구의 해당 기록 | 새 테스트·C bridge·정확한 새 suite dispatch/등록과 API 문서만 변경했다. 기존 제품 동작·헤더·export·옵션/링크 요구사항은 그대로이며 새 C bridge는 기존 helper를 바꾸지 않는다. 기존 164개는 재실행하지 않았다. |
| kernel/raw 장치 환경 재확인 | 읽기만 수행·runtime 생략 | /sys/kernel/tracing은 nobody 소유 mode 700, tracepoint format에 접근 불가; /dev/loop-control 및 loop0 없음, CapEff=0 | /tmp 구성/핸들이 사라진 세션 변화 이후 읽기만으로 현재 환경을 확인했다. native profiler/raw 장치 검사는 다시 실행하지 않고 앞선 환경 제한을 유지한다. |
| 기존 TCP/C fixture 실패 | 미해결 유지 | 이전 최초 TCP timeout·C TIMEOUT_NOW sanitizer 기록 | 이번 native recover 성공을 별도 실패의 해결로 재사용하지 않는다. |

선택 실행은 Linux/GCC 13.3/C++20/C11·libuv 1.48에서 2026-11-02에 수행한 `ctest --test-dir <build> -R '^raftpp.storage_recover_reference$' --verbose --output-log <log>`다. 새 검사는 45초 timeout이며 UV 옵션 아래 등록한다. 모든 파일은 개별 /tmp/raftpp-segments-* 디렉터리에 만들고 종료 시 정리한다. 사용자 파일·실제 raw 장치·소켓·전역 tracing을 사용하지 않는다. docs/ko·en/uv_io.md에는 force recover의 다음 index/저장 term/metadata 보존·반복·busy 및 auto-recovery와 open-tail salvage의 구분, 파일 rollback 제약과 bootstrap 선행 term 기록을 API 계약으로만 설명한다. 문서만의 후속 수정에는 빌드·런타임을 생략한다.

문서 정적 검사는 API 14쌍의 제목·표 개수/예제(번역 주석 제외)와 루트 포함 상대 링크 500개를 확인했다. CTest 목록은 전체 활성 165개, UV ON/fixture·도구 OFF 공유 Release 128개다. 목록은 전체 runtime 통과를 뜻하지 않는다. 기존 gate로 계산한 UV/fixture ON·도구 OFF는 146개, fixture OFF·둘 다 도구 ON은 147개이며 core-only fixture ON/OFF는 72/54개를 유지한다. 11월 3일의 최종 문서 갱신에는 빌드·런타임을 생략했다.

### 선택 Assertion Backtrace 이식·검증 (2026-11-03)

- 이전 턴은 테스트 기록 보존/재사용 지침을 보완해 진전했다. 이번에는 원본 `configure.ac`의 선택 옵션과 `Makefile.am`의 소스/링크 규칙을 대조하면서 `src/assert.h`의 선택 backtrace가 C++에 빠져 있음을 확인했다. 원본 C 소스는 수정하지 않았다. 이 발견은 공개 함수 대응표만으로 전체 기능 완료를 판단할 수 없다는 실제 근거다.
- `RAFTPP_WITH_BACKTRACE`는 기본 OFF다. ON은 GNU libbacktrace 헤더/라이브러리와 원본의 __assert_fail 사용 가능성을 configure에서 검사한다. 생성된 0/1 매크로와 features::backtrace를 제공한다. UV/LZ4/fixture와 독립이며 비활성 경로는 이 의존성을 찾거나 링크하지 않는다. 정적 타깃의 링크 요구도 소비자에 전달한다. 의존 파일 해시·경로/도구체인·컴파일/링크 입력으로 probe 결과를 구분해, 라이브러리를 바꾼 경우 다른 결과를 재사용하지 않고 이전 입력으로 돌아오면 해당 캐시를 재사용한다.
- 참조 범위는 원본 assert.h/Makefile/configure, C++ core/election/receive/replication/random/progress의 29개 assertion과 호출 경로, common/config/export, 테스트/정적·공유 링크다. 조건과 알고리즘은 바꾸지 않았다. 독립 src/assert.hpp·assert.cpp의 실패 경로는 std::source_location·표준 fprintf/abort와 기존 backtrace_full을 사용한다. 외부 의존성은 자체 메모리를 할당할 수 있다. 실패 수집 오류에도 abort를 유지하고 전역 signal handler나 예외/사용자 tracer를 도입하지 않는다.
- 새 `raftpp.assertions`는 자식 프로세스에서 실제 라이브러리 random_within_range의 minimum<=maximum assertion을 일으킨다. 라이브러리 코드·DSO 경계를 검사하며 옵션 ON의 위치/식/실제 프레임, OFF의 표준 abort, NDEBUG의 비활성 동작을 구분한다. NDEBUG 입력은 명시적 UINT32_MAX 분기로 나누기/오버플로 UB를 피한다. 자식은 core dump를 끈다. 기존 CTest 등록은 유지하고 새 검사 하나만 추가했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 기본 Debug 정적·UV/LZ4/fixture/도구 ON·backtrace OFF | 실행 | 빌드 exit 0, 선택 10개 통과. /tmp/raftpp-backtrace-default-{configure,build}.log 및 -default.log | 새 assertions + random/progress/election과 core_election/replication/conflict/membership/snapshot/cluster를 한 번 실행했다. 표준 assertion은 abort·프레임 0이며 기존 C 비교와 조건 경로를 확인했다. |
| 엄격한 공유 Debug·core-only·fixture/backtrace ON | 실행 | 빌드 exit 0, 선택 3개 통과. /tmp/raftpp-backtrace-debug-{configure,build}.log 및 -debug.log | 새 assertions가 실제 src/random.cpp와 프레임 6개를 출력했다. fixture_network_reference의 C 이벤트 4600개와 fixture_snapshot_fault_reference를 ON 경로에서 확인했다. C 기준 타깃도 RAFT_ASSERT_WITH_BACKTRACE로 빌드했다. 내부 assertion_failed는 동적 공개 심볼에 없다. |
| 엄격한 공유 Release·core-only·fixture/backtrace ON | 실행 | 빌드 exit 0, 새 assertions 1개 통과. /tmp/raftpp-backtrace-release-{configure,build}.log 및 -release.log | 기존 /tmp/raftpp-fixture-controls-release를 사용했다. features::backtrace=true여도 NDEBUG로 실제 라이브러리 assertion이 비활성화되고 assertion_failed/backtrace 심볼이 없다. |
| ASan/UBSan 정적 Debug·core-only·fixture/backtrace ON | 실행 | 빌드 exit 0, Debug ON과 같은 선택 3개 통과. /tmp/raftpp-backtrace-asan-{configure,build}.log 및 -asan.log | /tmp/raftpp-fixture-controls-asan에서 실행했다. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1, UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. 기존 환경 제한으로 누수 검사는 제외했다. |
| 외부 add_subdirectory 소비자·엄격한 정적 Debug·core-only·fixture/tests OFF·backtrace ON | 실행 | configure/build/실행 exit 0. /tmp/raftpp-backtrace-consumer-{configure,build}.log, 소스 /tmp/raftpp-backtrace-consumer | C++ 타깃만 링크해 umbrella/config·실제 버퍼/RNG를 사용한다. libbacktrace.a/-ldl이 자동 전달되며 C 라이브러리를 링크하지 않고 backtrace 미해결 심볼이 없다. PkgConfig 비활성 상태에서도 동작한다. 공유 타깃만 링크하는 새 assertion 검사도 별도로 통과했다. |
| 같은 공유 Debug 빌드 디렉터리 ON→OFF | 실행 | configure/build exit 0, 새 assertions 1개 통과. /tmp/raftpp-backtrace-disable-{configure,build}.log 및 -disable.log | /tmp/raftpp-backtrace-debug에서 없는 의존 경로·PkgConfig 비활성으로 전환했다. 플래그 0·표준 abort/프레임 0, 재링크한 libraftpp.so와 libraft.so 모두 assertion/backtrace 심볼·링크 의존성이 제거됐다. |
| 의존성 부재와 probe 캐시 입력 구분 | 실행 | 의존 부재 ON configure 실패/OFF 성공. /tmp/raftpp-backtrace-{missing,no-dependency}-configure.log. 유효 라이브러리 성공→기존 파일인 libc.so 거부→원래 입력 성공·probe 재실행 없음: /tmp/raftpp-backtrace-gate-{valid,invalid,restored}.log | 같은 구성에서 파일 존재만으로 통과하거나 다른 라이브러리의 성공을 재사용하지 않는다. 마지막 변경은 CMake probe 캐시 선택만 보완했으므로 이미 통과한 제품 runtime/소비자는 반복하지 않았다. |
| 그 외 기존 검사·추가 matrix/소비자 | 생략 — 기존 결과 재사용 | 이전 recover/fixture/adapter/raw/core-query/native/도구 중 해당 원래 기록 | 기본 OFF의 바뀐 6개 파일은 assertion 호출의 기계적 변경이며 조건·상태 전이·형식·소유권·일반 오류/콜백·공개 ABI는 그대로다. 관련 9개와 새 ON fixture 2개만 재확인했다. 무관한 codec/file/transport/도구와 추가 옵션 조합 전체를 반복하지 않았다. 새 구성의 전체 suite 통과를 주장하지 않는다. |
| 기존 환경·실패 제한 | 미검증·미해결 유지 | 이전 실제 kernel/raw 장치 제한, 최초 ASan TCP timeout, C TIMEOUT_NOW sanitizer 기록 | 이번 assertion 경로 통과로 해당 제한/실패의 해결을 주장하거나 다시 실행하지 않는다. |

실행 환경은 2026-11-03 Linux/GCC 13.3/C++20/C11·GNU libbacktrace(GCC 13의 헤더/정적 라이브러리)다. 엄격한 C++ 구성은 -Wall -Wextra -Wpedantic -Werror를 사용한다. 대상 빌드가 exit 0으로 끝난 뒤 runtime을 실행했다. 기본 선택 명령은 `ctest --test-dir build -R '^raftpp\.(assertions|random|progress|election|core_election|core_replication|core_conflict|core_membership|core_snapshot|core_cluster)$' --verbose --output-log /tmp/raftpp-backtrace-default.log`다. 표에서 같은 파일명의 묶음은 각각의 로그를 뜻한다. ON Debug/ASan은 `-R '^raftpp\.(assertions|fixture_network_reference|fixture_snapshot_fault_reference)$'`, Release/옵션 해제는 `-R '^raftpp\.assertions$'`를 사용했다. 정규식의 |는 실제 명령에서 셸 따옴표 안에 둔다.

CTest 목록은 기본 전체 활성 166개와 core-only fixture ON 73개를 읽기만으로 확인했다. UV fixture ON/OFF·도구 OFF 147/129와 core-only fixture OFF 55는 동일한 조건부 등록 규칙으로 계산한다. 등록 개수는 전체 runtime 통과 개수가 아니다. docs/ko·en/tracing.md에는 옵션/기능 조회/NDEBUG/출력·소유권·링크 계약만 추가했다. 이후 문서만 바뀌면 이 검사를 재실행하지 않는다. 전체 원본 소스·빌드 옵션과 실행 경로의 완료 증명은 계속 진행한다.

변경한 문서 3쌍의 정적 확인: 제목/표 행 개수는 AGENTS 12/27, REWRITTEN 42/279, tracing 6/11로 대응하며 상대 링크 352개가 존재하고 tracing의 C++ 예제가 일치한다. 최초 링크 검사기는 코드 블록의 lambda 구문을 링크로 오인해 중단했다. 코드 블록·inline code를 제외하도록 검사기를 고쳐 다시 확인했으며 제품 실패로 분류하지 않는다. 이 최종 기록 보완에는 제품 빌드·runtime을 추가 실행하지 않았다.

### Native Capacity 산술·공간 부족 판정의 C 호환 수정 (2026-11-03)

- 이전 턴은 원본의 선택 assertion backtrace를 이식·검증해 진전했다. 이번 참조는 원본 uv_ip.c·uv_tcp*.c의 IPv4 주소 해석·여러 주소 시도·listen/handshake/완료/close, uv_send.c/uv_recv.c, 이어지는 uv.c·uv_prepare.c·uv_append.c의 capacity 생산과 client.c/core의 소비까지 추적했다. IPv6 지원을 원본 기능으로 추정하지 않았으며 네트워크 구현은 수정하지 않았다.
- 실제 수정은 src/uv_io.cpp와 src/log_writer.cpp의 capacity 산술 두 곳이다. 원본 uvUpdateCapacity는 준비 파일 수×설정 segment 크기와 UvAppendCapacity를 size_t로 합산한 뒤 KiB/unsigned short로 변환한다. C++의 size_t/uint16_t 포화는 다른 보고 값을 만들어 합의 코어의 no-space admission을 바꾼다. unsigned size_t의 정의된 순환 합산과 uint16_t 변환을 유지한다. signed overflow를 도입하지 않으며 할당 크기·append-fit/예약·직렬화·상태 전이 알고리즘과 콜백 계약은 바꾸지 않았다.
- 새 tests/capacity_reference.c는 원본 함수를 복제하지 않고 실제 raft_uv_init/io.init/start/append와 loop prepare/check의 io.capacity를 읽는다. 가짜 transport는 소켓을 열지 않는다. 준비 pool 2개만 있는 상태와 단일 8바이트 명령을 쓴 활성 segment+준비 pool 상태를 4 KiB에서 안정시킨다. 각 상태에서 보고 크기 12개를 바꿔 24개의 C 값을 얻는다. 경계의 65534/65535/0/1/2, 활성 segment의 예약 바이트와 int64 상한 근처의 size_t 합산을 비교한다. 기존 작은 파일을 유지하며 실제 거대한 파일을 할당하지 않는다.
- 새 uv_io_capacity_reference는 두 구현의 실제 결과를 모아 정상 종료한 뒤 모두 비교한다. 수정 전 C=0/C++=65535 및 큰 활성 sum 차이를 재현했으며, 그 최초 실패를 통과로 덮어쓰지 않았다. 수정 후 C/C++ 24개가 모두 일치하고 C++ 파일 크기는 4096 이하이며 종료 capacity는 0이다. 새 uv_io_capacity_admission은 실제 디스크 단일 투표자의 높은 임계값에서 즉시 no_space·콜백 미호출·로그/FSM 보존·건강한 노드 상태, 정상 geometry/임계값 회복 후 한 번의 commit/apply와 종료 정리를 확인한다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 수정 전 실제 C capacity 대조 | 실행 — 실패 보존 | /tmp/raftpp-capacity-before-build.log 빌드 exit 0, /tmp/raftpp-capacity-before.log 새 reference 검사 실패 | 준비 파일 각 32 MiB에서 C=0/C++=65535를 포함한 실제 24개 출력으로 제품 차이를 확인한 뒤 수정했다. 원본 C는 수정하지 않았다. |
| Debug 정적·UV/LZ4/fixture/도구 ON·backtrace OFF | 실행 | /tmp/raftpp-capacity-debug-build.log exit 0; /tmp/raftpp-capacity-debug.log 새 2개와 기존 3개 통과 | 기존 uv_io_settings/log_settings/log_append만 선택했다. 두 수정의 일반 geometry/append와 runtime 설정을 확인한다. |
| 엄격한 공유 Release·UV ON·LZ4/fixture/도구/backtrace OFF | 실행 | /tmp/raftpp-capacity-release-build.log exit 0; /tmp/raftpp-capacity-release.log 새 2개 통과 | /tmp/raftpp-recover-reference-release, C++ -Wall -Wextra -Wpedantic -Werror. Release에서도 assert에 의존하지 않고 24개/공개 노드 계약을 검사한다. |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/도구/backtrace OFF | 실행 | /tmp/raftpp-capacity-asan-build.log exit 0; /tmp/raftpp-capacity-asan.log 새 2개 통과 | /tmp/raftpp-recover-reference-asan. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1, UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. 기존 환경 제한으로 누수 검사는 제외했다. |
| 새 C bridge의 C11/경고 검사 | 실행 | /tmp/raftpp-capacity-c11.log exit 0 | build/compile_commands.json의 해당 argv에서 출력/-c만 제거하고 -std=c11 -Wall -Wextra -Werror -fsyntax-only를 추가했다. 실제 생성 헤더/기능 정의를 유지하고 셸 eval 없이 실행했다. |
| 나머지 기존 163개·추가 matrix/소비자 | 생략 — 기존 결과 재사용 | 이전 backtrace/recover/fixture/native 설정 및 해당 원래 검증 기록 | 앞선 166개 중 영향받는 기존 3개만 Debug에서 재실행했다. core_replication의 기존 C capacity-threshold 비교와 encoding의 uint16 필드는 변경 없으므로 재사용한다. 공개 헤더/ABI/링크 요구·기능 플래그/의존성 선택은 그대로다. UV OFF는 수정 소스·새 검사를 제외한다. |
| 실제 kernel/raw 장치와 이전 TCP/C fixture 실패 | 미검증·미해결 유지 | 앞선 환경 제한·최초 TCP timeout·C TIMEOUT_NOW sanitizer 실패 기록 | 이번 capacity 성공은 해당 제한/실패의 해결 증거가 아니며 해당 검사를 반복하지 않았다. |

모든 runtime은 해당 대상 빌드의 exit 0 확인 뒤 2026-11-03 Linux/GCC 13.3/C++20/C11·libuv 1.48 환경에서 실행했다. 기본 선택은 `ctest --test-dir build -R '^raftpp\.(uv_io_capacity_reference|uv_io_capacity_admission|uv_io_settings|log_settings|log_append)$' --verbose --output-log /tmp/raftpp-capacity-debug.log`다. Release/ASan은 `-R '^raftpp\.uv_io_capacity_(reference|admission)$'`의 새 두 개만 실행했다. 실제 파일은 개별 /tmp/raftpp-uv-* 디렉터리를 사용하고 종료 시 제거한다. 새 CTest 두 개는 UV 아래 등록하며 timeout은 40초다. 목록만 읽어 전체 활성 168개·UV ON/fixture·도구 OFF의 Release/ASan 131개를 확인했으며 전체 suite 통과를 의미하지 않는다.

두 언어 docs/uv_io.md에는 capacity의 unsigned 산술·16비트 순환·32 MiB 예와 임계값/RPC/runtime 설정 계약만 설명한다. 진행 기록은 이 문서에만 유지한다. 문서만의 후속 수정에는 빌드/runtime을 생략한다. 전체 원본 소스/옵션/기능의 실행 경로 완료 증명은 계속 진행한다.

수정한 문서 2쌍의 정적 확인에서는 REWRITTEN의 제목/표 행 43/288, uv_io의 7/25가 대응했고 코드 블록·inline code를 제외한 상대 링크 356개가 존재하며 uv_io의 C++ 예제가 일치했다. 최종 문서 기록 보완에는 빌드·runtime을 추가 실행하지 않았다.

### Fixture 실행 중 확장·멤버십의 실제 C 이벤트 대조 (2026-11-03)

- 이전 native capacity 단위는 실제 C 차이를 수정·검증해 진전했다. 이번에는 공개 fixture API와 원본 fixture.c의 grow/serverInit·연결·시작·seed/step, C++ fixture.cpp의 grow/start/seed/관찰, public node 및 legacy/membership/client/replication의 비동기 요청/정리를 필요한 범위까지 추적했다. 기존 고정 3/5노드 네트워크 대조와 달리 구성 자체가 변하는 경로다.
- 새 fixture_membership_reference는 초기 1/3노드 클러스터에 두 노드를 실행 중 grow하고 개별 start하여 fixture 크기 3/5로 만든다. 각 시나리오의 1960개, 총 3920개 이벤트마다 server/type/time·안정 리더·구성 ID/역할/주소/순서·term/last/commit/applied/vote·FSM 적용 수·구성 변경 완료 횟수/상태를 실제 C와 비교한다. 새 C helper는 원본 raft_fixture_grow/raft_start/raft_add/raft_assign/raft_remove를 호출하고 공개 Raft 구성 필드를 읽는다. 제품 기능을 C에 위임하지 않는다.
- 단계는 spare 추가→아직 복제하지 않은 spare의 voter 승격/따라잡기→명령→두 번째 spare 추가→standby→명령→voter 승격→첫 voter의 standby 강등→제거→중복 추가/없는 ID 제거 오류→명령이다. 성공한 구성 변경은 각 7회 완료하며 즉시 거부는 콜백을 늘리지 않는다. 제거 후에도 fixture 노드/인덱스/FSM이 남는 상태를 포함해 전체 노드를 계속 관찰한다. 각 phase는 이전 구성 변경의 완료 후 다음 연산에 도달하며 총 시간은 가상 시간이다.
- C 요청은 독립 heap 객체로 유지하고 완료/즉시 오류에서 해제한다. C++ 요청은 기존 소유 callback API를 사용한다. C 구성 주소는 비교 호출 중에만 빌린다. 원본 C 소스와 제품 C++ 구현/공개 헤더/옵션/링크 요구는 바꾸지 않았다. 새 helper 상태와 함수, fixture 테스트/선택 등록, API 설명서 및 이 기록만 변경했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적·UV/LZ4/fixture/도구 ON·backtrace OFF | 실행 | 빌드 exit 0, 새 검사 1개 통과. /tmp/raftpp-membership-fixture-debug-build.log, -debug-final-build.log, -debug.log | 새 C helper/대조/선택 등록을 검증했다. runtime 전에 작성한 성공 연산 수를 7로 맞춘 최종 빌드가 끝난 뒤 실행했으며 초기 빌드를 runtime 통과로 기록하지 않는다. |
| 엄격한 공유 Release·core-only·fixture/backtrace ON | 실행 | /tmp/raftpp-membership-fixture-release-build.log exit 0; -release.log 새 1개 통과 | /tmp/raftpp-fixture-controls-release, C++ -Wall -Wextra -Wpedantic -Werror. 두 시나리오/3920개 대조는 assert 없이 Release에서도 실행한다. |
| ASan/UBSan 정적 Debug·core-only·fixture/backtrace ON | 실행 | /tmp/raftpp-membership-fixture-asan-build.log exit 0; -asan.log 새 1개 통과 | /tmp/raftpp-fixture-controls-asan. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1, UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. 기존 환경 제한으로 누수 검사는 제외했다. |
| C bridge C11/경고 | 실행 | /tmp/raftpp-membership-fixture-c11.log exit 0 | compile_commands.json의 fixture_reference.c argv에서 출력/-c만 제거하고 -std=c11 -Wall -Wextra -Werror -fsyntax-only를 추가했다. 실제 생성 헤더·기능 정의를 유지하고 셸 eval 없이 실행했다. |
| 기존 168개·추가 matrix/소비자 | 생략 — 기존 결과 재사용 | 이전 capacity/backtrace/recover/fixture/legacy 등의 해당 원래 기록 | 제품 코드·일반 기존 helper 동작·공개 ABI/헤더·의존성/기능 규칙은 그대로다. C bridge의 추가 private 상태는 같은 TU의 helper만 사용하며 기존 입력/연산/출력은 바꾸지 않는다. 새 검사는 fixture 조건 안에만 등록했다. 전체 suite·다른 소비자/옵션 조합은 반복하지 않았다. |
| 제한·미검증 조합 | 미검증·미해결 유지 | 이전 실제 kernel/raw 장치·최초 TCP timeout·C TIMEOUT_NOW sanitizer 기록 | 새 시나리오는 모든 노드가 살아 있는 grow/구성 변경이다. killed-node/grow/수동 링크 제어와 snapshot 주입의 복합 조합은 이 검사로 검증했다고 확장하지 않는다. transfer RPC를 발생시키지 않아 원본 C TIMEOUT_NOW 오류의 해결 근거로도 사용하지 않는다. |

실행 명령은 Linux/GCC 13.3/C++20/C11 환경에서 2026-11-03에 수행한 `ctest --test-dir <build> -R '^raftpp\.fixture_membership_reference$' --verbose --output-log <log>`다. 각각의 빌드가 exit 0으로 끝난 뒤 선택 runtime을 한 번 실행했다. fixture와 C 참조는 소켓·파일·실제 시간 sleep 없이 가상 시간으로 동작한다. 새 CTest는 fixture 옵션 아래 timeout 40초로 등록한다. 읽기만 수행한 목록은 전체 활성 169개, core-only fixture ON Release/ASan 74개를 확인했다. 등록 수를 전체 통과 수로 합산하지 않는다.

두 언어 fixture API에는 실행 중 grow 후 개별 start·리더 add/assign·완료 대기·제거 후 인덱스 보존 계약만 추가했다. 이후 문서/진행 기록만의 수정에는 빌드·runtime을 생략한다. 새 대조는 이 범위의 기존 구현 호환성을 확인하는 증거이며 전체 라이브러리 재작성 완료를 뜻하지 않는다.

변경 문서 2쌍의 정적 확인은 REWRITTEN의 제목/표 행 44/296, fixture의 6/21 대응과 코드 블록·inline code 밖의 상대 링크 348개 및 fixture C++ 예제 일치를 확인했다. 이 최종 문서 기록 보완에는 빌드/runtime을 추가 실행하지 않았다.

### Native Capability Probe의 AIO 정리 오류 확인 (2026-11-03)

- 이전 fixture 멤버십 단위는 실제 C 이벤트 3920개를 대조해 진전했다. 이번 참조는 원본 uv_fs.c의 direct/async probe·UvFsAllocateFile, uv_os.c의 할당 fallback·AIO syscall wrapper, syscall.c와 C++ file/probe/aio·writer hook·disk 진단/UV 초기화 경로까지다. 원본은 정상 완료 후 UvOsIoDestroy의 실패를 RAFT_IOERR로 반환하지만 기존 C++ context_owner 소멸자는 결과를 버렸다. 수정 전 runtime 실패 재현을 실행했다고 주장하지 않으며 이 누락은 소스에서 확인했다.
- src/probe.cpp의 context_owner에 명시적 close를 두고 성공/미지원 결과 공개 전에 결과를 확인한다. 실제 close 성공 뒤 소유 값을 0으로 만들어 두 번 정리하지 않으며, 실제 실패 시에는 소멸자의 최선 노력 정리를 남긴다. failure는 io_error·probe.io_destroy/디렉터리·POSIX 원인을 보존하고 output은 바꾸지 않는다. 앞선 submit 실패는 원래 오류를 유지한다. 내부 writer_operation 끝에 destroy_result를 추가해 실제 정리 시도 후의 결과만 주입할 수 있다. 기존 enum 숫자·공개 함수 시그니처·일반 writer 동작은 보존한다.
- 새 probe_cleanup_reference는 변경하지 않은 C uv_fs/uv_os/syscall/heap/err를 별도 테스트로 컴파일해 실제 probe를 호출한다. fs/os object 타깃에만 함수명 별칭을 적용해 정리/완료 관찰을 가로채며 다른 C 타깃의 전역 source 속성을 바꾸거나 예약된 wrapper 식별자를 만들지 않는다. C/C++ 모두 실제 kernel context를 먼저 해제한 뒤 보고 오류만 EIO/ENOMEM/EINVAL로 바꾼다. kernel 자체의 io_destroy 실패를 실제 발생시킨 검사는 아니다.
- 실제 direct/AIO 완료를 확인한 뒤 정상/세 오류 4개에서 C/C++ 반환이 0/18로 일치한다. C++는 전체 capability 출력 보존·정리 연산/경로/원인·진단 없는 정상 결과를 확인한다. 별도 C++ 6개는 EOPNOTSUPP/EINVAL 제출의 동기 fallback·정리 오류, EACCES의 우선 오류 보존을 확인한다. 각 경로에서 descriptor 수와 빈 임시 디렉터리를 확인한다. native direct/AIO 완료 경로가 없는 환경은 77로 명시적으로 생략하지만 이번 세 구성에서는 생략 없이 실행했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 새 타깃 등록 전 빌드 시도 | 실행 — 실패 기록 | /tmp/raftpp-probe-cleanup-debug-build.log: No rule to make target raftpp_probe_tests | 새 타깃은 기존 생성 빌드에 아직 없었다. 명시적 configure 후 빌드했으며 이 실패를 통과로 재사용하지 않았다. |
| Debug 정적·UV/LZ4/fixture/도구 ON·backtrace OFF | 실행 | configure/대상 빌드 exit 0. /tmp/raftpp-probe-cleanup-debug-{configure,configured-build,final-configure,final-build}.log; -debug.log 선택 3개 통과 | 최종 C 어댑터/분리 object 타깃을 빌드한 뒤 새 probe_cleanup_reference와 기존 disk_probe/disk_segment_diagnostics를 실행했다. 기존 native direct=1/block=4096/async=1 및 할당 실패 sweep·원인 전파도 유지한다. |
| 엄격한 공유 Release·UV ON·LZ4/fixture/도구/backtrace OFF | 실행 | /tmp/raftpp-probe-cleanup-release-{configure,build}.log exit 0; -release.log 새 1개 통과 | /tmp/raftpp-recover-reference-release, C++ -Wall -Wextra -Wpedantic -Werror. 실제 C probe와 별도 공유 C++ 라이브러리의 오류 경계를 확인했다. |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/도구/backtrace OFF | 실행 | /tmp/raftpp-probe-cleanup-asan-{configure,build}.log exit 0; -asan.log 새 1개 통과 | /tmp/raftpp-recover-reference-asan. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1, UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. 기존 환경 제한으로 누수 검사는 제외했다. |
| 새 C 어댑터 C11/경고 | 실행 | /tmp/raftpp-probe-cleanup-c11.log exit 0 | 실제 compile_commands.json argv에서 출력/-c만 제거하고 -std=c11 -Wall -Wextra -Werror -fsyntax-only를 추가했다. 생성 헤더/기능 정의를 유지하며 셸 eval 없이 실행했다. |
| 나머지 기존 167개·추가 matrix/소비자 | 생략 — 기존 결과 재사용 | 앞선 fixture/capacity/backtrace/native probe·writer·진단 등의 해당 원래 기록 | 기존 169개 중 probe/원인 관련 2개만 Debug에서 재실행했다. 공개 API/ABI·일반 writer·소스 선택/링크 의존 규칙은 그대로며 내부 enum은 끝에만 추가했다. 새 C 참조 별칭은 독립 object에만 적용된다. UV OFF는 수정 probe 소스와 새 테스트를 제외한다. |
| 실제 kernel/raw 장치 및 이전 실패 제한 | 미검증·미해결 유지 | 이전 kernel profiler/raw 장치·최초 TCP timeout·C TIMEOUT_NOW sanitizer 기록 | 이번 정상 kernel AIO 사용/보고 오류 주입은 raw 장치·kernel profiler 또는 실제 kernel teardown 실패의 검증이 아니다. 이전 실패를 재실행하거나 해결로 주장하지 않는다. |

2026-11-03 Linux/GCC 13.3/C++20/C11·libuv 1.48에서 각 대상 빌드의 exit 0을 확인한 뒤 runtime을 실행했다. 기본 명령은 `ctest --test-dir build -R '^raftpp\.(probe_cleanup_reference|disk_probe|disk_segment_diagnostics)$' --verbose --output-log /tmp/raftpp-probe-cleanup-debug.log`다. Release/ASan은 새 `-R '^raftpp\.probe_cleanup_reference$'`만 실행했다. 파일은 전용 /tmp/raftpp-probe-* 디렉터리에 만들고 제거하며 소켓·raw 장치·sysctl/perf/trace를 사용하지 않는다. 새 CTest는 UV 조건 아래 timeout 30초·SKIP_RETURN_CODE 77로 등록한다. 읽기만으로 전체 활성 170개, UV ON/fixture·도구 OFF Release/ASan 132개 목록을 확인했다. 전체 suite 통과 개수가 아니다.

두 언어 docs/errors.md에 capability 공개 전 AIO 정리·출력 보존·원인/우선 오류 계약만 보완했다. 진행 기록은 이 문서에만 둔다. 이후 문서만의 수정에는 빌드·runtime을 생략하며 전체 원본 기능/실행 경로의 완료 증명은 계속 진행한다.

변경 문서 2쌍의 정적 확인은 REWRITTEN 제목/표 행 45/305와 errors 7/7의 대응, 코드 블록·inline code 밖 상대 링크 352개 존재 및 errors C++ 예제 일치를 확인했다. 최종 문서 기록 보완에는 빌드·runtime을 추가 실행하지 않았다.

### 로컬 스냅샷 준비 할당 실패의 노드 생존·재시도 (2026-11-03)

참조 범위는 원본 `legacy.c`의 legacyShouldTakeSnapshot/legacyTakeSnapshot/putSnapshot/takeSnapshotClose/takeSnapshotCb/legacyHandleEvent, `configuration.c`의 configurationCopy/configurationAdd, 공개 FSM·heap·tracer/fixture와 C++ `node.cpp`의 apply_committed/process/maybe_snapshot/launch/pump/install이다. 원본은 요청 malloc·구성 주소 malloc/서버 realloc 실패를 abort로 정리하고 다음 이벤트까지 기다린다. C++에서는 같은 선택 작업의 예외가 process의 일반 오류 처리로 전달되어, 이미 적용된 명령에도 노드를 종료했다.

수정 전 새 `raftpp.node_snapshot_allocation`은 첫 할당 실패에서 노드 생존 검사에 실패했다. 로그 `/tmp/raftpp-snapshot-allocation-before-build.log`와 `-before.log`를 보존한다. `src/node.cpp`의 maybe_snapshot은 기존 본문을 take_snapshot으로 분리하고 이 범위의 bad_alloc/length_error만 흡수한다. 고정 이후 실패는 기존 제출 예외 경로가 taking_snapshot을 복원하고 FSM을 정리한다. 그 밖의 첫 단계 예외와 이미 수락된 저장의 완료 오류는 기존 종료 계약을 유지한다. 원본 `raft/`와 공개 시그니처·생성 헤더·제품 링크 요구사항은 수정하지 않았다.

새 C++ 검사는 적용 직후 할당 실패 32개 위치를 순회한다. 긴 주소(128바이트)의 구성/이벤트 복사, FSM 버퍼, 비동기 요청·콜백/큐·즉시 제출을 포함하여 13개 시도가 생략되고 그중 고정 이후 6개는 정확히 한 번 정리되며 19개는 정상 제출된다. 각 경우 commit/applied=2·명령 결과 콜백 1회·저장 로그·노드 생존, 후속 이벤트의 snapshot index=2/configuration index=1/ID·주소·역할, 다음 명령 applied=3을 확인한다. length_error의 생략/재시도와 임의 FSM 예외의 종료도 구분한다. 기존 node_snapshot/node_restore/node_allocation은 설치 대기·복원·두 번째 FSM 단계·즉시/완료 실패·취소와 일반 할당 계약을 검증한다.

새 C11 helper `tests/snapshot_allocation_reference.c`는 실제 C fixture의 단일 투표자에 명령을 적용하고, 원본 take-snapshot trace 직후의 요청/주소/서버 할당 3개를 하나씩 실패시킨다. 그 밖의 실제 메모리는 이전 heap으로 할당·해제하고 종료 전 heap을 복원한다. 각 경우 state/commit/applied/명령/실패/준비/정리/재시도 상태가 `[3,2,2,1,1,0,0,3,1,1]`로 일치한다. C/C++가 스냅샷을 시도하는 이벤트 시점 자체가 같다는 주장은 하지 않는다. C helper와 참조 링크는 fixture ON인 테스트에만 포함하며 fixture OFF에서는 제외한다.

원본 takeSnapshotCb의 대기 중 설치 분기에는 snapshot_pending을 NULL로 만든 뒤 다시 읽는 경로가 있음을 소스에서 확인했다. 이번 C 대조는 단일 노드 준비 실패로 한정하여 해당 분기를 실행하지 않았다. C++의 동시 로컬 생성·설치는 기존 node_snapshot 검사에서 통과했지만, 해당 원본 C 분기의 runtime 대조는 미검증으로 남긴다.

검증 환경은 2026-11-03 01:43–01:47 KST, Linux·GCC/G++ 13.3.0·C++20/C11이다. 아래 모든 빌드는 exit 0 확인 후 runtime을 실행했다. 최초 짧은 주소 검사가 통과한 뒤 주소 복사·고정 이후 정리·구성 보존 입력을 보강했으며 마지막에는 새 검사만 각 구성에서 다시 실행했다. 기존 영향 3개는 제품 코드가 더 바뀌지 않아 재실행하지 않았다. 중복 실행을 통과 개수에 추가 합산하지 않는다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | `cmake --build build --target raftpp_node_tests --parallel 4`; `ctest --test-dir build --output-on-failure -R '^raftpp.node_(snapshot_allocation\|snapshot\|restore\|allocation)$'`의 새 1개/기존 3개 통과 | 선택 준비 오류 경계·기존 설치/완료/취소·할당 계약. `/tmp/raftpp-snapshot-allocation-debug-build.log`, `-debug.log` |
| 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | `/tmp/raftpp-fixture-controls-release` node 타깃 빌드 및 `-R '^raftpp.node_snapshot_allocation$'` 1개 통과 | NDEBUG·공유 링크의 새 C/C++ 대조. `-release-build.log`, `-release.log` |
| ASan/UBSan 정적 Debug·UV/LZ4 OFF·fixture/backtrace ON | 실행 | `/tmp/raftpp-fixture-controls-asan` node 타깃 빌드 및 위 4개 통과 | FSM/요청/이전 heap 수명·실패 정리. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 누수 검사 제외. `-asan-build.log`, `-asan.log` |
| 엄격한 공유 Release·UV ON·LZ4/fixture/backtrace OFF | 실행 | `/tmp/raftpp-recover-reference-release` node 타깃 빌드 및 새 1개 통과 | fixture 없이 C++ 실패 주입 유지. node 타깃 link.txt에 C 참조 라이브러리/helper 제외. `-fixture-off-build.log`, `-fixture-off.log` |
| 최종 긴 주소/고정 이후 정리 입력 | 실행 | 위 4개 구성의 node 타깃을 빌드하고 새 검사만 각 1개 통과. 모두 `13 skipped (6 after freeze), 19 submitted` | 마지막 변경은 새 테스트 입력뿐. `/tmp/raftpp-snapshot-allocation-{debug,release,asan,fixture-off}-final-build.log`, `-final.log`와 수집한 stdout/`-output.log` |
| 새 C helper C11 경고 | 실행 | `cc -std=c11 -Wall -Wextra -Wpedantic -Werror -isystem build/raft/include -fsyntax-only tests/snapshot_allocation_reference.c` 통과 | 새 원본 대조 bridge만. `/tmp/raftpp-snapshot-allocation-c11.log` |
| 나머지 기존 167개·추가 matrix/외부 소비자 | 생략 — 기존 결과 재사용 | 공개 callback 노드 드라이버/추적·메모리·컨테이너 및 각 후속 단위의 범위별 과거 기록 | 선택 준비 예외 경계 밖 구현·입력·옵션·공개 헤더·제품 CMake/링크·환경은 바뀌지 않았다. 전체 suite 재실행/통과를 주장하지 않는다 |
| 실제 kernel profiler/raw 장치·기존 TCP timeout·C transfer sanitizer 실패 | 미검증 | 기존 disk/profiler/native 컨테이너/legacy callback 단위의 제한과 실패 기록 보존 | 이번 수정으로 원인이 해소되지 않았으며 완료로 재사용하지 않는다 |

표의 로그 축약 이름은 `/tmp/raftpp-snapshot-allocation` 접두사를 갖는다. Release/ASan/fixture-OFF 빌드는 각 기존 디렉터리에서 `cmake --build <dir> --target raftpp_node_tests --parallel 3`, 최종 입력 빌드는 네 구성 모두 `--parallel 2`를 사용했다. 해당 디렉터리에서 `ctest --test-dir <dir> --output-on-failure -R <표의 정규식>`을 실행했다. 표의 `\|`는 Markdown 이스케이프이며 실제 셸에서는 `|`다. 전체 등록 수는 이 새 항목으로 171개이며 전체 통과 수가 아니다.

두 언어 node API에는 할당/길이 예외의 선택 생성 생략·재시도·고정 상태 정리와 치명 오류의 구분만 설명했다. 전체 실패/경계 경로 대조는 계속 진행하며 진행 기록을 API 문서에 넣지 않는다.

문서 정적 확인은 REWRITTEN 제목/표 행 46/315와 node API 6/51의 대응, 상대 링크 362개 존재, 번역 주석을 제외한 C++ 예제 일치를 확인했다. 최초의 예제 전체 텍스트 비교는 기존 번역 주석 때문에 불일치했으며 코드 차이는 없었다. CTest 목록 조회가 build/Testing/Temporary/LastTest.log를 덮어써 Debug stdout은 앞서 수집한 도구 출력으로, 나머지 세 구성 stdout은 목록 조회 전 복사한 `/tmp/raftpp-snapshot-allocation-{release,asan,fixture-off}-output.log`로 보존한다. 목록 확인을 위해 테스트를 재실행하지 않았다. 등록 수는 전체 171/core-only fixture ON 75/UV ON fixture OFF 133이며 통과 개수가 아니다. 이 기록과 API만의 최종 수정에는 빌드·runtime을 생략한다.

### Fixture 스냅샷 주입·로드의 접미사 보존 (2026-11-03)

이전 goal 단위는 로컬 스냅샷 할당 실패의 선택 생략·재시도를 구현·검증하여 진전했다. 이번에는 공개 기능 대응표에서 fixture의 수동 스냅샷/엔트리 주입을 추적했다. 참조 범위는 원본 fixture.c의 ioMethodLoad/raft_fixture_set_snapshot/raft_fixture_add_entry/ioFlushSnapshotPut/ioMethodTruncate/ioClose/초기화·시작, legacy.c의 시작·FSM 복원, entry.c/snapshot.c의 깊은 복사와 C++ fixture.cpp copy_state/set_snapshot/add_entry/complete(put_job)/load/persisted 및 node 시작이다. 원본 ioMethodLoad는 보관 엔트리 전부를 복사하고 snapshot.index+1을 시작 인덱스로 반환한다. C++ copy_state는 이전 저장 start_index로 앞부분을 걸러, 스냅샷보다 먼저 주입한 접미사나 nonzero-trailing 저장의 엔트리를 제거할 수 있었다.

수정 전 새 fixture_snapshot_load_reference가 접미사 보존 검사에 실패했다. `/tmp/raftpp-fixture-snapshot-load-before-build.log`와 `-before.log`를 보존한다. 제품 변경은 src/fixture.cpp copy_state의 snapshot load 분기로 한정한다. 모든 저장 엔트리를 깊은 복사하고 snapshot.index+1부터의 접미사로 반환하며, max-index 오버플로 거부·실패 시 출력 보존·persisted의 원래 저장 상태 복사는 유지한다. 실제 파일 backend의 접두부 처리나 합의 알고리즘·직렬화 형식·공개 시그니처·제품 CMake/링크는 바꾸지 않았다. raft/ 원본도 보존했다.

새 검사는 snapshot index 2/5/10/64, 엔트리 0/1/3개, 스냅샷 이전/이후/양쪽 주입의 36사례를 비교한다. 각 사례에서 반복 load 2회와 노드 시작 후 load 1회를 수행하고, term/vote/start/count·snapshot term/index/configuration index·구성 ID/역할/주소·스냅샷/FSM/엔트리 바이트와 순서를 대조한다. 공개 노드 시작의 상태·term·last/commit/applied와 FSM 복원/명령 수도 일치한다. 별도 9사례는 엔트리 0/1/3개에 trailing 0/1/4인 실제 queued snapshot put의 이벤트/완료와 load 2회를 비교한다. 합계 45사례/126회 C load 비교다. snapshot index 10과 엔트리 3개의 한 입력에는 표준 할당 24개 위치를 주입해 OOM의 출력 보존과 정상 복사를 확인한다.

tests/fixture_reference.c에 새 주입·로드·시작 helper를 추가했다. 기존 생성 helper는 node 시작/기존 raw I/O 시작 흐름을 그대로 두고 새 미시작 구성을 선택할 수 있게 했다. 기존 FSM restore는 바꾸지 않으며 새 주입 시작만 유효한 8바이트 상태를 복원한다. 실제 C가 반환한 snapshot/configuration/버퍼/배치와 fixture가 소유한 주입 데이터를 서로 맞는 경로로 정리한다. C load의 할당 실패는 원본 assert 경로여서 이번 C 런타임 대조에 포함하지 않고, C++ 오류 반환/출력 보존을 별도로 검증한다.

검증 시점은 2026-11-03 02:00–02:04 KST, Linux/GCC·G++ 13.3/C++20·C11이다. 기존 구성의 옵션·환경을 유지하고 각 타깃 빌드 exit 0을 확인한 뒤 runtime을 시작했다. 새 CTest는 fixture 조건 안에 timeout 30초로 추가했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | `cmake --build build --target raftpp_fixture_tests --parallel 3`; `ctest --test-dir build --output-on-failure -R '^raftpp.fixture_(snapshot_load_reference\|snapshot_fault_reference\|disk_reference\|storage\|reference)$'`의 새 1개/기존 4개 통과 | 변경 load 분기·기존 시작/직접 I/O/스냅샷 실패/저장 계약. `/tmp/raftpp-fixture-snapshot-load-debug-build.log`, `-debug.log` |
| 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | `/tmp/raftpp-fixture-controls-release` fixture 타깃 빌드 및 `-R '^raftpp.fixture_snapshot_load_reference$'` 새 1개 통과 | NDEBUG에서도 45사례/126회 대조·할당 실패/출력 검증이 실행된다. `-release-build.log`, `-release.log` |
| ASan/UBSan 정적 Debug·UV/LZ4 OFF·fixture/backtrace ON | 실행 | `/tmp/raftpp-fixture-controls-asan` fixture 타깃 빌드 및 `-R '^raftpp.fixture_(snapshot_load_reference\|snapshot_fault_reference\|storage)$'` 새 1개/기존 2개 통과 | snapshot/배치/주입 값의 소유권·실패 정리. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 누수 검사 제외. `-asan-build.log`, `-asan.log` |
| 변경 C helper C11 경고 | 실행 | `cc -std=c11 -Wall -Wextra -Wpedantic -Werror -isystem build/raft/include -fsyntax-only tests/fixture_reference.c` 통과 | 새 비교 bridge·생성 선택만. `/tmp/raftpp-fixture-snapshot-load-c11.log` |
| 나머지 기존 167개·추가 matrix/외부 소비자·fixture OFF gate | 생략 — 기존 결과 재사용 | 이전 fixture/fixture 네트워크·디스크/멤버십·로컬 snapshot 및 공개 소비자/gate 단위 기록 | 바뀐 snapshot load 밖 구현/입력/옵션·환경은 그대로다. 기존 네트워크/확장 검사의 최초 load에는 snapshot이 없으며, fixture OFF는 제품 source와 이 테스트를 기존 if 조건으로 제외한다. 공개 헤더/제품 링크 요구사항도 그대로다 |
| 실제 kernel profiler/raw 장치·최초 TCP timeout·C TIMEOUT_NOW/대기 설치 분기 | 미검증 | 기존 실패·제한·로컬 snapshot 단위 기록 보존 | 이번 수정으로 원인을 해결하거나 해당 경로를 실행하지 않았다. 이 결과를 완료/통과로 재사용하지 않는다 |

축약 로그 이름은 `/tmp/raftpp-fixture-snapshot-load` 접두사를 사용한다. Release/ASan도 `cmake --build <dir> --target raftpp_fixture_tests --parallel 3` 후 해당 디렉터리에서 표의 CTest 정규식을 실행했다. `\|`는 Markdown 이스케이프이며 실제 셸은 `|`를 사용한다. 세 구성 모두 stdout `45 cases, 126 C comparisons`를 목록 조회 전에 `/tmp/raftpp-fixture-snapshot-load-{debug,release,asan}-output.log`로 보존했다. 읽기만의 CTest 목록은 전체 172/core-only fixture ON 76개 등록을 확인했으며 전체 통과 수가 아니다. fixture OFF의 이전 133개 목록은 관련 gate 변경이 없어 재사용한다.

두 언어 fixture API에서 load가 주입 순서와 무관하게 모든 엔트리를 snapshot 접미사로 해석하는 계약·호출자의 접미사 구성 조건·반복 load/실패 출력 보존을 설명했다. 전체 원본 기능·실패 경로의 완료 증명과 기존 환경/실패 제한은 남아 있다. 최종 API/진행 기록만의 수정에는 빌드·runtime을 생략한다.

변경 문서 2쌍의 정적 확인은 REWRITTEN 제목/표 행 47/323, fixture API 6/21의 대응과 번역 주석을 제외한 C++ 예제 일치·상대 링크 348개 존재를 확인했다. 최종 문서 수정에는 위 실행 로그를 재사용하고 추가 빌드·runtime을 실행하지 않았다.

### 이벤트 코어 리더십 인계의 실제 C 대조 (2026-11-03)

이전 goal 단위는 fixture 스냅샷 주입·로드의 접미사 누락을 수정·검증하여 진전했다. 이번에는 저장/복제의 인덱스 경계를 추적하다 replicationUpdate의 부분 ACK와 인계 시작 관계를 확인하고 인계 검증으로 범위를 구체화했다. 원본 client.c의 ClientTransfer/clientSelectTransferee, replication.c의 replicationUpdate, membership.c의 membershipLeadershipTransferStart, recv_timeout_now.c/recv_request_vote.c, timeout.c/convert.c와 C++ client/replication/membership/receive_core/receive/timeout/convert 및 공개 core/node 조회·문서를 참고했다. 참조한 것만으로 인덱스 전체 범위를 검증했다고 간주하지 않는다.

원본과 C++는 모두 새 부분 복제 ACK에서도 TimeoutNow를 시작할 수 있다. RPC는 리더의 전체 로그 끝을 담고, 수신자의 영속 경계가 다르면 무시된다. 이미 전송 중으로 표시되면 후속 ACK만으로 다시 전송하지 않아, 조기 도착은 인계 만료와 호출자의 새 요청으로 이어질 수 있다. 이 원본 알고리즘을 임의로 변경하지 않았다. 기존 core API 문서의 transfer_event가 구체적인 ID를 요구한다는 설명은 실제 지원하는 ID 0 자동 선택과 달라 두 언어본을 수정했다.

새 raftpp.core_transfer는 9사례/135개 이벤트에서 기존 oracle의 실제 raft_step과 C++ step을 비교한다. 상태·term/vote·commit/stored·모든 로그 term/설정·시계/난수/timeout·progress/catch-up/transfer 필드·update flags·RPC 순서/필드·저장 범위/엔트리 바이트를 매 이벤트 확인한다. 세 자동 선택 사례는 잡힌 voter 2/3 또는 없는 경우의 마지막 voter 3을 선택하고 자신/미등록/standby/spare를 거부한다. 중복 요청은 대상 검사보다 먼저 not_leader다. 네 인계 사례는 pre_vote OFF/ON과 조기/지연 힌트를 조합하고 max_inflight_entries=1, 5개 명령의 부분 복제를 사용한다. 조기 힌트 무시·후속 ACK 재전송 없음·quorum 유지 중 만료·명시적 재시도와, 지연 힌트의 disruptive election/기존 리더의 더 높은 term 전환/실제 새 리더 선출을 확인한다. 단일 voter의 not_found와 follower/candidate의 not_leader도 비교한다.

C fixture 함수를 사용하지 않으므로 알려진 원본 TIMEOUT_NOW 통계 배열 오류를 거치지 않는다. fixture OFF ASan/UBSan에서도 실제 C 인계 코어를 대조했다. 이는 C legacy fixture sanitizer 실패의 수정이나 완료 콜백 순서 재검증을 뜻하지 않는다. node 콜백·종료 계약은 해당 과거 기록을 재사용한다. 제품 소스/공개 헤더/원본 raft/·기능 옵션·제품 링크 요구사항은 바꾸지 않았다.

초기 새 테스트 빌드는 공개 조회 헤더 미포함으로 실패하여 tests/core.cpp에 core.hpp를 포함했다(해당 진단은 도구 출력에 보존하며 초기 파일 로그는 후속 빌드로 대체됐다). 첫 새 Debug runtime은 통과했다. 이후 엄격한 Release 빌드에서 기존 core_cluster의 중복 variant 임시 이동에 GCC 13 maybe-uninitialized 진단이 발생했다. 실패 로그 `/tmp/raftpp-core-transfer-release-initial-build.log`를 보존한다. cluster의 send lambda가 구체적인 이벤트를 std::forward로 전달하도록 바꿔 불필요한 variant 생성을 없앴으며 경고 옵션은 완화하지 않았다. 해당 기존 테스트만 영향 범위에 추가했다. 새 인계 검사 자체는 이 변경과 무관하므로 Debug 결과를 다시 실행하지 않고 재사용했다.

검증 시점 2026-11-03 02:20–02:25 KST, Linux/GCC·G++ 13.3/C++20·C11. 각 최종 타깃 빌드 exit 0을 확인한 뒤 runtime을 실행했다. core_transfer는 fixture/UV/LZ4와 무관하게 timeout 30초로 등록한다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | `cmake --build build --target raftpp_core_tests --parallel 3`, `ctest --test-dir build --output-on-failure -R '^raftpp.core_transfer$'` 새 1개 통과 | 새 C 이벤트 대조. `/tmp/raftpp-core-transfer-debug-build.log`, `-debug.log`, `-debug-output.log` |
| 기존 cluster 이벤트 전달 보완 후 Debug | 실행 / 새 검사 생략 — 기존 결과 재사용 | 최종 core 타깃 빌드 후 `-R '^raftpp.core_cluster$'` 기존 1개 통과. 새 인계는 바로 위 결과 재사용 | 기존 cluster lambda만 바뀌었다. `-debug-final-build.log`, `-debug-cluster.log`, `-debug-cluster-output.log` |
| 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | `/tmp/raftpp-fixture-controls-release` core 타깃 최종 빌드 후 `-R '^raftpp.core_(transfer\|cluster)$'` 새 1개/기존 1개 통과 | GCC 경고 수정·NDEBUG의 전체 비교/조건 검사. C++ `-Wall -Wextra -Wpedantic -Werror`. `-release-final-build.log`, `-release.log`, `-release-output.log` |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | `/tmp/raftpp-recover-reference-asan` core 타깃 최종 빌드 후 위 2개 통과 | 원본 fixture 없이 실제 C 선거/인계·RPC/batch 수명과 변경 cluster 전달. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 누수 검사 제외. `-asan-final-build.log`, `-asan.log`, `-asan-output.log` |
| 나머지 기존 171개·추가 matrix/소비자/기능 gate | 생략 — 기존 결과 재사용 | 이전 core/legacy 호환·node query·fixture·native 및 각 설정/gate/소비자 기록 | 제품 동작·옵션·공개 선언/링크·환경은 그대로이고 기존 core 테스트 중 cluster만 이벤트 생성 방식이 바뀌었다. 새 C++ 테스트 외 C helper/원본 C 소스도 그대로여서 별도 C11 검사를 반복하지 않는다 |
| 기존 C fixture transfer sanitizer 실패·TCP 최초 timeout·kernel/raw 장치·원본 pending install | 미검증 | 이전 실패·제한 기록 보존 | 이번 event-core 통과로 해당 경로를 해결했다고 간주하지 않는다 |

축약 로그 이름은 `/tmp/raftpp-core-transfer` 접두사다. 최종 세 구성 빌드는 `cmake --build <dir> --target raftpp_core_tests --parallel 2`, runtime은 `ctest --test-dir <dir> --output-on-failure -R <표 정규식>`을 사용했다. 표의 `\|`는 Markdown 이스케이프이며 실제 셸은 `|`다. 목록 조회 전에 stdout을 보존했고 세 구성의 새 검사 출력은 모두 `9 cases, 135 C event comparisons`다. CTest 등록은 전체 173/core-only fixture ON 77/UV ON fixture OFF 134개로 확인했으며 전체 통과 개수가 아니다.

core/node API 두 언어본에 자동 선택 순서·실패 우선순위·부분 진행 힌트/수신 영속 경계·pre-vote 우회 선거·만료/재시도 계약을 설명했다. 전체 C 기능/실행 경로의 완료 증명은 계속 진행한다. 최종 API/기록만의 수정에는 추가 빌드/runtime을 생략한다.

변경 문서 3쌍의 정적 확인은 REWRITTEN 제목/표 행 48/331, core API 6/30, node API 6/51의 대응과 번역 주석 제외 C++ 예제 일치·상대 링크 374개 존재를 확인했다. 최종 문서 보완에는 위 실행 결과를 재사용하고 빌드/runtime을 추가 실행하지 않았다.

### 네이티브 파일 예약 대체 경로의 실제 C 대조 (2026-11-03)

이전 goal 단위는 이벤트 코어 인계의 실제 C 대조와 잘못된 API 설명 수정을 완료해 진전했다. 이번에는 configure.ac/Makefile.am의 선택 기능과 native I/O를 대조하던 중, EOPNOTSUPP 대체 파일 예약은 구현되어 있지만 실행 대조 기록이 없는 것을 확인해 범위를 구체화했다. 실제 참조는 원본 uv_fs.c의 UvFsAllocateFile/uvFsAllocate/오류 정리와 uv_os.c의 UvOsFallocate/uvOsFallocateEmulation, C++ file.cpp allocate/open/close 및 disk.cpp prepare_segment/allocate/진단 전달이다. C++20 표준에는 동등한 파일 공간 예약이 없어 기존 POSIX/libuv 의존성을 유지한다. 제품 소스·원본 raft/·공개 헤더/기능 옵션/링크 요구사항은 수정하지 않았다.

새 raftpp.file_allocation_reference는 actual C UvFsAllocateFile과 C++ disk_storage::prepare_segment를 비교한다. 테스트 전용 C11 interposer는 thread-local로 명시적으로 활성화한 예약 대상 fd만 추적하고, 비활성 호출은 libc로 전달한다. posix_fallocate의 반환값/errno를 다르게 만들어 실제 반환값으로 오류를 판단하는지도 확인한다. fstatfs의 블록 크기와 오류를 주입하지만 정상 fallback pwrite는 실제 새 파일에 수행한다. 기본/64비트 libc 진입점을 제공하고 매크로 별칭인 64비트 이름은 중복 정의하지 않도록 한다. C 참조는 기존의 분리된 probe_reference_fs/os OBJECT를 재사용하며 다른 테스트나 제품의 소스 속성·syscall 동작은 바꾸지 않는다. 새 타깃은 UV 아래에서만 생성하고 기존 C 라이브러리 전체를 링크하지 않는다.

대체 경로 성공은 블록 크기 0/512/1024/2048/4096/8192와 크기 1/511/512/513/4095/4096/4097/8192/8193의 54조합이다. native 정상 1개, native EIO/ENOMEM/EINVAL/EACCES 4개, fallback stat EIO/ENOMEM/EPERM 3개, fallback pwrite의 EIO/ENOSPC/EINVAL/EINTR를 0/1/3번째 쓰기에 주입한 12개를 합해 74사례다. C와 결과·예약/stat/write 횟수·모든 쓰기 위치가 일치한다. 성공 파일은 정확한 크기/모든 바이트 0이고, fallback은 최대 4096바이트 간격과 마지막 바이트를 확인한다. 실패는 생성 파일을 제거하고 C descriptor를 닫으며 C++ 호출자의 기존 /dev/null 출력 소유 핸들은 보존한다. C++ 소유 진단은 실제 native 원인과 open-1 경로를 유지하고 성공은 이전 진단을 지운다. 매 사례 끝의 descriptor 개수도 일치한다.

원본이 fallback stat/write 실패를 EOPNOTSUPP로 감싸 최종 io_error로 반환하므로, fallback ENOSPC도 io_error다. C++는 같은 상태 코드를 유지하면서 원인 ENOSPC를 보존한다. native posix_fallocate ENOSPC의 no_space는 기존 disk_no_space 기록을 재사용하여 다시 실행하지 않았다. 실제 musl 실행이나 EOPNOTSUPP를 반환하는 실제 파일시스템을 검증한 것은 아니며, 주입한 분기와 실제 파일 쓰기의 대조다.

검증 환경은 2026-11-03 02:48–02:52 KST, Linux/GCC·G++ 13.3/C++20·C11·기존 libuv다. 새 타깃의 configure와 빌드 exit 0을 확인한 뒤 runtime을 실행했다. 처음 Debug 통과 후 추가한 64비트 별칭 guard는 현재 환경의 전처리 결과가 수정 전과 바이트 단위로 같았다. 해당 비교 SHA256은 `c6b3a5f1473d3e76bb0a32f5220d36799edae9c4ffd751b0ac07020324d50f1c`이며 `/tmp/raftpp-file-allocation-before.i`, `-after.i`에 보존한다. 최종 세 타깃은 다시 빌드했지만 Debug runtime/C11 syntax 결과는 같은 코드이므로 재사용했다. Release/ASan runtime은 최종 빌드 후 처음 실행했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | `cmake -S . -B build`; `cmake --build build --target raftpp_file_allocation_tests --parallel 3`; `ctest --test-dir build --output-on-failure -R '^raftpp.file_allocation_reference$'` 새 1개 통과 | 74사례. `/tmp/raftpp-file-allocation-debug-{configure,build}.log`, `-debug.log`, `-debug-output.log` |
| 엄격한 공유 Release·UV ON·LZ4/fixture/backtrace OFF | 실행 | `/tmp/raftpp-recover-reference-release` configure/새 타깃 빌드 후 새 1개 통과 | 공유 C++ 라이브러리에 대한 interposition과 NDEBUG의 조건 검사. C++ `-Wall -Wextra -Wpedantic -Werror`. `-release-{configure,build,final-build}.log`, `-release.log`, `-release-output.log` |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | `/tmp/raftpp-recover-reference-asan` configure/새 타깃 빌드 후 새 1개 통과 | 실제 C/C++ 파일·진단·출력 소유권·정리. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 누수 검사 제외. `-asan-{configure,build,final-build}.log`, `-asan.log`, `-asan-output.log` |
| 새 C11 helper의 엄격한 구문 검사 | 실행 | `cc -std=c11 -Wall -Wextra -Wpedantic -Werror -DHAVE_LINUX_AIO_ABI_H=1 -DHAVE_LINUX_IO_URING_H=1 -isystem build/raft/include -isystem build/raft/src -isystem build/raft/include/raft-header-lookup -fsyntax-only tests/file_allocation_reference.c` 통과 | POSIX dlsym 함수 포인터 표현을 memcpy로 전달해 ISO C object→function pointer cast를 피했다. `-c11.log` |
| 별칭 guard 후 Debug runtime·C11 반복 | 생략 — 기존 결과 재사용 | 위 Debug/C11 결과와 동일 전처리 비교 | 현재 활성 C 코드가 동일하다. 세 구성 최종 타깃 빌드는 `-debug-final-build.log`와 위 final-build 로그에 보존하며 새 통과 수에 중복 합산하지 않는다 |
| 기존 173개·native disk_no_space·추가 matrix/소비자/UV OFF gate | 생략 — 기존 결과 재사용 | 이전 메타데이터/파일/segment·writer·probe·설정/gate/소비자 단위 기록 | 제품 구현/입력/의존성·환경과 기존 테스트 및 재사용 C OBJECT의 컴파일 정의/옵션은 그대로다. 새 interposer는 새 실행 파일에만 포함하고 UV OFF는 기존 if 조건으로 이를 제외한다 |
| 실제 musl/unsupported 파일시스템·kernel/raw 장치·이전 실패 제한 | 미검증 | 기존 TCP timeout·C fixture TIMEOUT_NOW/pending-install·kernel/raw 제한 보존 | 합성 반환/블록 크기 주입으로 해당 환경 자체를 검증했다고 보지 않으며, 기존 실패를 해결/통과로 재사용하지 않는다 |

축약 로그는 `/tmp/raftpp-file-allocation` 접두사다. Release/ASan configure는 `cmake -S . -B <dir>`, 최초 빌드는 위 타깃 `--parallel 3`, 최종 세 구성 빌드는 `--parallel 2`다. 각 dir에서 위 정규식의 CTest만 실행했다. 세 stdout은 모두 `74 C comparisons, 54 fallback successes, 19 failures`이며 목록 조회 전에 보존했다. 새 검사는 UV 조건·30초 timeout이고 등록 수는 전체 174/UV ON fixture OFF 135개다. UV OFF의 이전 77개 목록은 gate 불변으로 재사용하며 전체 suite 통과 수가 아니다.

UV API 두 언어본에는 예약 fallback·native/fallback 오류 구분·실제 원인과 정리 계약만 추가했다. 진행 기록은 이 루트 대응본에만 기록하며 전체 C 기능/실행 경로의 완료 증명은 계속 진행한다. 최종 API/기록만의 수정에는 추가 빌드/runtime을 생략한다.

변경 문서 2쌍의 정적 확인은 REWRITTEN 제목/표 행 49/340, UV API 7/25의 대응과 번역 주석 제외 C++ 예제 일치·상대 링크 356개 존재를 확인했다. 최종 문서 보완에는 위 실행 기록을 재사용하고 추가 빌드/runtime을 실행하지 않았다.

### 전체 완료 요건 점검과 코어 최대 인덱스 거부 (2026-11-03)

이전 goal 단위는 native 예약 fallback의 실제 C 대조를 완료해 진전했다. 이번에는 전체 범위를 완료로 볼 근거를 점검했다. CMake의 독립 C++ 타깃/기능 조건과 config, common의 표준 별칭/소유권, 공개 core/node/io/FSM, client/core/replication/progress/log/trail/node의 시작·복제·정리 경로와 기존 단위 기록을 대조했다. 기준 기록의 현재 구성요소 표에는 TCP/디스크 문맥 오류와 legacy 호환이 아직 미구현이라고 남아 있었지만 실제 소스와 후속 검증 기록에는 연결되어 있어, 관련 9행을 현재 구현·범위별 검증으로 정리했다. 과거 단위의 당시 남은 범위/실행 결과는 덮어쓰지 않았다.

전체 완료는 다음 근거만으로 선언할 수 없다. 아래 구조 확인과 기존 결과 재사용은 모든 상태/실패 경로의 완료 증명으로 확대하지 않는다.

| 요건 | 현재 근거 | 판정과 남은 범위 |
| --- | --- | --- |
| 독립 C++20 구현 | CMake raftpp 타깃의 cpp 소스·cxx_std_20/확장 OFF·C 참조 타깃과 별도 링크, 이번 공유/정적 코어 소비 실행 | 구조 확인. C 타깃 빌드 성공만으로 완료로 보지 않으며 전체 기능 의미 대조는 별도 |
| 표준 타입·소유권·콜백 모델 | common 표준 별칭/PMR·node/io/FSM 요청/완료/종료와 메모리/native 컨테이너 단위 기록 | 해당 구현/검증 범위 확인. 코루틴/task/promise/future 대체 API는 추가하지 않음 |
| 알고리즘·순서·형식 보존 | 독립 core/직렬화/파일/TCP와 C 공개 함수 114개·I/O/FSM/transport 대응 목록, 단위별 C 대조 | 범위별 근거가 있으며 전체 실행 경로 증명은 미완료. 이번 unsafe terminal 값은 C++ 검증 대상으로 분리 |
| 입력·인덱스 경계 | 기존 build_progress의 max 거부와 원본/복사된 last+1 계산, 이번 실제 Release 재현과 8사례 | 실제 공백을 발견·수정. term/time 등 다른 수치 경계의 전체 검증까지 완료했다고 보지 않음 |
| 기능 조건·생성 헤더·정적/공유·외부 소비자 | 현재 CMake/config/export 조건과 같은 입력의 과거 matrix/gate/consumer 기록 | 이번 guard는 선택 기능/공개 선언/링크를 바꾸지 않음. 영향받은 코어만 선택 검증 |
| 한국어/영어 API·현황 분리 | docs/ko·docs/en 상대 경로 집합 14쌍, 루트 AGENTS/REWRITTEN 대응본 | 파일 대응 확인. 변경 API/진행 기록은 양 언어 동시 갱신하며 docs에는 API만 둠 |
| 기준 구현·출처 보존 | 이번 수정은 C++ 3소스/새 C++ 테스트/CMake 테스트/문서에 한정, 기존 port 출처/raft/LICENSE 안내 유지 | 원본 raft/에 이번 변경 없음. 원본 C의 별도 결함을 C++ 완료 근거로 숨기지 않음 |
| 실환경·기존 실패 | kernel profiler/raw 장치의 접근 제한, 최초 TCP timeout 원인 미확정, 원본 C fixture/pending-install 제한 기록 | 여전히 미검증/미해결. 구현 존재·새 좁은 검사 통과가 이 제한을 해소하지 않음 |

인덱스 경계 재현은 `/tmp/raftpp-fixture-controls-release`에서 새 core_index_limits를 수정 전 먼저 빌드/실행했다. 결과는 `terminal submission status=0 last=18446744073709551615 next=0`이며 검사가 실패했다. `/tmp/raftpp-core-index-before-{configure,build}.log`, `-before.log`를 보존한다. Debug assert로 중단하는 대신 NDEBUG Release에서 실제 unsigned wrap을 관찰했다. next index를 같은 log_index에 보관하는 progress/build_progress의 기존 요구와 불일치하는 입력 수락이었다.

제품 수정은 src/client.cpp의 제출 범위 검사, src/core.cpp의 시작 metadata/접미사 검사, src/replication.cpp의 AppendEntries/InstallSnapshot 범위 검사다. 실제 로그/스냅샷 index는 max-1까지 허용하고 max는 next index로만 사용한다. 초과 시작/제출은 too_big, 처리 대상 수신 범위 오류는 malformed다. 수신 term/상태 처리나 이벤트 시계까지의 전체 rollback을 약속하지 않는다. wire 타입/인코딩·API 시그니처·일반 범위 알고리즘은 유지하며 원본 C는 수정하지 않았다. callback 노드도 같은 엔진을 사용한다.

새 tests/core_limits.cpp는 C 참조를 링크하지 않는 공개 C++ 이벤트 소비자다. 8사례는 (1) pipeline의 terminal 두 엔트리 거부와 max-1 정상 append/persist, 큰 remote ACK clamp/추가 제출 거부/heartbeat, (2–6) terminal snapshot·terminal/overflow 접미사 거부와 가장 큰 정상 snapshot/접미사 시작, (7) 원격 append/예약 index heartbeat 거부와 정상 prefix 보존/ACK, (8) 원격 terminal snapshot 거부와 max-1 설치 완료를 확인한다. 메모리는 소수의 엔트리만 사용하며 큰 인덱스를 순회하지 않는다. Release에서도 조건을 실행하며 assert에만 의존하지 않는다. 원본 C의 unsafe terminal 범위는 실행 대조하지 않고, 기존 일반 범위 C 비교(core_cluster/core_snapshot)를 다시 확인했다.

검증 시점 2026-11-03 03:10–03:16 KST, Linux/GCC·G++ 13.3/C++20. 각 build exit 0 후 runtime을 시작했고 새 검사는 선택 기능과 독립적으로 30초 timeout으로 등록했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 수정 전 엄격한 공유 Release 재현 | 실행 — 실패 | `cmake -S . -B /tmp/raftpp-fixture-controls-release`; core_limits 타깃 빌드 후 `-R '^raftpp.core_index_limits$'` 실패, 위 next=0 관찰 | 기존 오류의 실제 증거. before 로그를 새 통과로 덮어쓰지 않음 |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | `cmake --build build --target raftpp_core_limits_tests raftpp_core_tests --parallel 3`; `ctest --test-dir build --output-on-failure -R '^raftpp.core_(index_limits\|cluster\|snapshot)$'` 새 1개/기존 2개 통과 | 새로운 경계와 일반 C 상태/RPC·스냅샷 비교. `/tmp/raftpp-core-index-debug-{configure,build}.log`, `-debug.log`, `-debug-output.log` |
| 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | `/tmp/raftpp-fixture-controls-release` core_limits 타깃 빌드 후 새 1개 통과 | NDEBUG·공유 라이브러리·선택 native 의존성 없는 코어. C++ `-Wall -Wextra -Wpedantic -Werror`. `-release-build.log`, `-release.log`, `-release-output.log` |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | `/tmp/raftpp-recover-reference-asan` core_limits/core_failure/node/core 타깃 빌드 후 `-R '^raftpp.(core_index_limits\|core_failure\|core_snapshot\|node_allocation)$'` 새 1개/기존 3개 통과 | 경계와 log/config rollback·요청 할당/콜백·정상 C 설치 비교. `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; 누수 검사 제외. `-asan-{configure,build}.log`, `-asan.log`, `-asan-output.log` |
| 나머지 기존 170개·추가 matrix/gate/외부 소비자 | 생략 — 기존 결과 재사용 | 앞선 코어/노드/진단/fixture/파일·네트워크·소비자 단위별 과거 기록 | guard 밖 구현/입력·공개 선언/링크·기능 조건/환경은 그대로다. 일반 작은 인덱스의 기존 조건 경로는 동일하며, 과거 전체 suite 기록을 새 경계 검증으로 쓰지 않는다 |
| term/time 수치 경계 전체·실제 kernel/raw·이전 실패 제한 | 미검증 | 위 완료 요건 표와 기존 TCP/C fixture/kernel 제한 기록 | 이번 인덱스 수정으로 해결하거나 실행하지 않은 범위를 완료/통과로 간주하지 않음 |

축약 로그는 `/tmp/raftpp-core-index` 접두사다. Release 빌드는 위 dir에서 `cmake --build <dir> --target raftpp_core_limits_tests --parallel 3`, ASan은 `--target raftpp_core_limits_tests raftpp_core_failure_tests raftpp_node_tests raftpp_core_tests --parallel 3`이다. default/ASan configure는 `cmake -S . -B <dir>`로 기존 옵션을 유지했다. 표의 `\|`는 Markdown 이스케이프이며 실제 셸에서는 `|`다. 새 stdout은 세 구성 모두 `core index limits: 8 cases`이고 목록 조회 전에 보존했다. 등록 수는 전체 175/core-only fixture ON 78/UV ON fixture OFF 136개이며 전체 통과 개수가 아니다.

core/node API 두 언어본에는 상한/다음 인덱스·거부 코드·시작/수신 조건과 더 작은 native 파일명 한도를 설명했다. 진행·전체 완료 점검은 이 루트 대응본에만 둔다. 전체 재작성 목표를 이 8사례로 축소하지 않으며 다음 수치/호환 범위를 계속 확인한다. 최종 API/기록만의 수정에는 빌드/runtime을 추가 실행하지 않는다.

변경 문서 3쌍의 정적 확인은 REWRITTEN 제목/표 행 50/358, core API 6/30, node API 6/51의 대응과 번역 주석 제외 C++ 예제 일치·상대 링크 380개 존재를 확인했다. 다음 작업 번호 1–5도 두 언어에서 일치하며 최초 재현 실패/선택 통과/stdout 기록을 확인했다. 최종 문서 보완에는 추가 빌드/runtime을 실행하지 않았다.

### 선거 term·이벤트 시계 소진 거부와 원본 C 대조 (2026-11-03)

직전 상태 보고 턴은 구현 변경 없는 no progress였으며 이번 단위에서 새 재현·제품 수정·검증을 수행했다. AGENTS와 두 언어 기존 검증 기록을 확인하고 election/convert/timeout/receive_core/core/core_api의 입력·역할 전이·term/vote·타이머·난수·update 경로를 추적했다. node의 process/pump/start/종료와 요청 완료까지 참조했으며 실제 수정 범위는 아래 제품 6소스·내부 선언·테스트/CMake·API/진행 대응 문서다. 기준 raft/는 수정하지 않았다.

먼저 새 core_numeric 테스트를 엄격한 공유 Release에서 빌드했다. 수정 전 core_term_limits는 `exhausted election status=0 term=0`, core_clock_limits는 `terminal clock status=0 now=18446744073709551614 next=1569`를 출력하며 둘 다 실패했다. 최초 configure/build/runtime 로그 `/tmp/raftpp-core-numeric-before-{configure,build}.log`, `-before.log`를 보존한다. unsigned wrap을 확인한 실제 실패이며 후속 통과로 덮어쓰거나 통과로 재사용하지 않는다.

수정은 src/election.cpp의 다음 term 사전 검사와 error_code 반환, src/convert.cpp의 역할/할당 전 검사, src/timeout.cpp·src/receive_core.cpp의 오류 전달과 안전한 미래 term 차이 비교, src/core.cpp의 이벤트 시계 admission, src/core_api.cpp의 election timeout 변경 admission이다. include/raftpp/detail/election.hpp의 내부 exported helper가 error_code를 반환하며 공유 경계 호출은 consensus 검사로 검증했다. 공개 step/node 선언·wire·디스크 형식·기능 옵션·새 의존성은 바꾸지 않았다.

최대 term 자체는 허용하고 그 이후 term을 요구하는 선거/pre-vote/인계만 too_big으로 거부한다. 마지막 진행 중인 선거의 승리와 term을 증가시키지 않는 단일 voter 시작 최적화도 보존했다. 시계는 현재 election_timeout의 최대 무작위 기간을 더할 수 있도록 `uint64_t max - 2 * election_timeout`까지 admission한다. 범위를 넘는 step은 clock/capacity/update 교체 전에, setter는 설정/RNG 변경 전에 거부한다. 실행 중 시간 설정은 setter를 사용한다. 선거 오류까지 모든 step 상태가 rollback된다고 약속하지 않는다. callback 노드는 큐 이벤트의 오류를 기록하고 기존 비동기 종료·미완료 요청 처리 경로를 사용한다.

새 tests/core_numeric.cpp의 두 검사는 assert에 의존하지 않는 term 8사례/clock 4사례다. exhausted follower의 pre-vote OFF/ON, 마지막 actual round/실패한 retry 이후 승리, 마지막 pre-vote 재시도/실제 선거, disruptive transfer, 단일 voter OFF/ON의 commit, 최대 term 수신을 확인했다. clock 시작/실행 중 오류의 이전 update 보존, 정확한 horizon과 setter 실패/축소, 리더 quorum/heartbeat 덧셈과 상한+1 거부를 확인했다. 상한 부근에서도 소수의 엔트리만 사용한다.

새 core_term_reference는 기존 tests/core.cpp의 실제 C oracle로 7사례/34개 이벤트를 대조한다. max-2/max-1에서 pre-vote OFF/ON 선거·재시도·최대 term 승리·저장·복제 ACK·commit, 최대 term 단일 voter OFF/ON 시작과 최대 term RequestVote 수신의 역할·term/vote·RNG/timer/deadline·구성·로그/commit·update flags·RPC·저장 바이트가 일치한다. 로그 인덱스는 작게 유지하며 unsafe C term/time overflow는 실행하지 않는다. 원본 fixture counter를 사용하지 않아 해당 sanitizer 제한과 분리된다.

새 node_numeric_limits는 tests/node.cpp의 기존 memory backend를 사용한 6사례다. 최대 term follower OFF/ON의 종료와 term/vote/RPC 미변경, 최대 term 단일 voter 명령의 저장/적용/완료 1회, 지원 밖 시계의 시작 실패, 실행 중 시계 소진에서 대기 append 정리/명령 미적용/leadership_lost 콜백 1회, horizon에서 setter 실패 후 실행 보존/축소를 확인한다. backend 완료를 실제로 drain하며 단순 코어 조회만으로 콜백 계약을 단정하지 않는다.

검증 시점 2026-11-03 03:51–03:57 KST, Linux/GCC·G++ 13.3/C++20. 각 build exit 0을 확인한 뒤 runtime을 실행했다. 마지막 node 테스트 추가 이후에는 node 타깃/새 검사만 빌드·실행했고 앞선 코어/기존 노드 설정 결과는 해당 경로가 바뀌지 않아 재사용했다. 새 네 CTest는 모두 30초 timeout이다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 수정 전 공유 Release 재현 | 실행 — 실패 | 새 core_term_limits/core_clock_limits 둘 다 실패, 위 term=0/next=1569 | 최초 실제 오류 증거. before 로그 보존 |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 core 3개+기존 election/core_election/core_transfer/core_cluster/public_settings/node_settings 6개 통과, 이어 새 node 1개 통과 | 새 term/time/콜백 경계와 영향받는 일반 선거·인계·클러스터·설정 |
| 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 같은 새 4개/기존 6개 통과 | NDEBUG·공유 내부 helper 반환과 선택 native 의존성 없는 코어. C++ -Wall -Wextra -Wpedantic -Werror |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | 같은 새 4개/기존 6개 통과 | 메모리·콜백 수명·상태/실제 C 이벤트. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1, UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1. 누수 검사 제외 |
| 변경 없는 기존 나머지 169개·추가 matrix/gate/외부 소비자 | 생략 — 기존 결과 재사용 | 앞선 코어 인덱스·노드/할당·fixture·파일/TCP·기능/소비자 단위의 최초 실행 기록 | 새 guard 밖 로직/입력·공개 step/node 선언·링크/기능 조건/환경은 그대로. 내부 election helper의 변경은 이번 공유 consensus 호출로 검사했다. 같은 디렉터리/TU라는 이유로 반복하지 않음 |
| 실제 kernel/raw·최초 TCP timeout·원본 C fixture/pending-install·기타 전체 경로 | 미검증 | 이전 환경/참조 실패 제한과 전체 완료 요건 점검 | 새 numeric 검사로 해당 제한을 해소하거나 전체 기능/경로 완료를 증명하지 않음 |

코어 단계의 configure/build/runtime/stdout 로그는 `/tmp/raftpp-core-numeric-{debug,release,asan}-{configure,build}.log`, `-<구성>.log`, `-<구성>-output.log`다. node 후속 단계는 같은 형태의 `/tmp/raftpp-node-numeric` 접두사다. 각 stdout은 목록 조회 전에 별도로 보존했다. 세 구성의 새 stdout은 term 8/clock 4/C reference 7사례·34 events/node 6사례로 일치한다. 현재 CTest 등록 수는 전체 179/core-only fixture ON 82/UV ON fixture OFF 140이며 전체 통과 개수가 아니다.

재현·실행 명령은 `cmake -S . -B <dir>`로 기존 옵션을 유지하고, 코어 단계 `cmake --build <dir> --target raftpp_core_numeric_tests raftpp_core_tests raftpp_consensus_tests raftpp_public_core_tests raftpp_node_tests --parallel 2`, `ctest --test-dir <dir> --output-on-failure -R '^raftpp.(core_(term_limits|clock_limits|term_reference|election|transfer|cluster)|election|public_settings|node_settings)$'`다. node 추가 후 같은 configure와 `--target raftpp_node_tests --parallel 2`, `-R '^raftpp.node_numeric_limits$'`만 실행했다. `<dir>`는 build, /tmp/raftpp-fixture-controls-release, /tmp/raftpp-recover-reference-asan이다. 최초 재현은 첫 dir 대신 Release dir에서 numeric 타깃과 두 limits 검사만 실행했다. 표/명령의 파이프 문자는 정규식 OR다.

core/node API 두 언어본에 term·시계 상한, too_big과 보존 범위, setter와 callback 종료 계약을 함께 설명했다. 전체 목표는 유지하며 기록/문서만의 후속 수정에는 빌드/runtime을 추가 실행하지 않는다.

변경 문서 3쌍의 정적 확인은 REWRITTEN 제목/표 행 51/366, core API 6/30, node API 6/51과 번역 주석 제외 코드 예제·상대 링크 382개·API 문서 경로 14쌍·다음 작업 1–5 대응을 확인했다. 최초 실패와 선택 통과/stdout 기록도 확인했으며 이 문서 보완에는 추가 빌드/runtime을 실행하지 않았다.

### Wire 길이 선언·단계별 payload 경계의 C 대조 (2026-11-03)

직전 goal 턴은 선거 term/시계 guard·테스트·API/기록을 실제 수정·검증한 progress였다. 이번에는 AGENTS와 기존 바이트/영속화/writer/전송 기록을 확인하고 encoding.cpp·detail/byte.hpp의 크기 산술·파싱/조립·소유권을 추적했다. metadata/segment_buffer/disk의 길이·다음 인덱스·signed offset, writer의 iovec 합계/offset, TCP/stream의 preamble→header→payload 할당/수신까지 참조했다. 해당 제품 guard는 이미 있었고 이번에 제품 구현을 수정하지 않았다. 실제 수정은 tests/encoding_limits.cpp, 기존 encoding 테스트 dispatcher/CMake, encoding API/진행 기록의 한·영 대응본이다. 전체 디스크/네트워크 경계가 이번 runtime 범위였다고 확대하지 않는다.

기존 encoding_malformed의 전체 잘림/손상·5000개 무작위 입력·설정 초과와 encoding_failure의 8경로 할당 실패 기록은 보존한다. 새 검사는 그 결과를 얻기 위해 반복하지 않고, 기존 기록에 없던 폭 경계/길이 선언과 단계별 조립 입력을 추가한다. 실제 데이터가 없는 주소 범위나 수 GiB 버퍼를 만들지 않고, 수십 바이트 헤더에 큰 숫자를 기록해 검사한다.

새 encoding_limits의 호스트 64비트 25사례는 (1) 길이 0/UINT32_MAX/UINT32_MAX-7·두 최대 길이 합계·0 길이를 섞은 8종 배치와 정확한 헤더/개수/payload 한도 및 한 단위 초과의 encode/decode/RPC 출력 보존, (2) 개별 UINT32_MAX+1 길이 거부, (3) 2^32/UINT64_MAX의 악성 개수 거부, (4) 0/INT64_MAX/UINT64_MAX 헤더 길이 선언과 full-frame 경계, (5) 0/1/UINT64_MAX 스냅샷 데이터 길이, (6) 혼합 0/8바이트 엔트리의 조립·공유 수명과 잘못 구성한 단계 헤더 7종의 거부다. 전체 메시지 bounds는 뺄셈으로 확인하므로 큰 길이에 preamble/헤더를 더하다 wrap하지 않고 기존 output/consumed를 보존한다. 약한 참조는 이 배치 하나의 최종 소유자 해제를 확인하며 전체 heap 누수 검증으로 확대하지 않는다. 작은 size_t 구성의 조건 분기는 코드에 있지만 실제 32비트 빌드는 실행하지 않았다.

UV 구성에서는 변경하지 않은 C uvEncodeBatchHeader/uvDecodeBatchHeader/uvDecodeMessage를 직접 호출한다. 8종 배치의 바이트·필드·RPC 총 길이 24건과 스냅샷 길이 3건, 총 정상 27건이 일치한다. payload bytes는 읽거나 할당하지 않는다. 별도 1건에서 원본 uvDecodeBatchHeader의 64→unsigned 절단을 확인했다. wire 개수 2^32가 C의 n=0/성공으로 바뀌지만 C++는 too_big이며 출력/consumed를 보존한다. 이 값은 C가 추가 할당/엔트리 읽기를 하지 않아 안전하게 실행했다. UINT64_MAX 등 다른 악성 개수의 C allocation/read는 실행하지 않는다. 잘못된 입력의 C 동작까지 그대로 복제해야 한다고 보지 않으며 원본 raft/는 수정하지 않는다.

검증 시점 2026-11-03 04:08–04:09 KST, Linux/GCC·G++ 13.3/C++20·64비트 size_t. 각 build exit 0 후 runtime을 실행했다. tests/encoding.cpp의 기존 함수는 변경하지 않고 dispatcher만 추가했으므로 기존 5개 encoding 검사를 다시 실행하지 않았다. C 참조는 기존 UV 조건/격리 include/c_std_11 요구/GC 규칙을 그대로 사용하며 새 선택 의존성·공개 선언·제품 링크/옵션 변경은 없다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 encoding_limits 1개 통과, 25사례/28 C 호출 | 정상 C 헤더 27건 일치·원본 count 절단 1건 차이와 단계별 수명/출력 경계 |
| 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 같은 새 1개 통과, 25사례/C 호출 0 | libuv 없는 C++ 검사·NDEBUG 조건, C++ -Wall -Wextra -Wpedantic -Werror. UV OFF에서는 C wire helper를 참조하지 않음 |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | 같은 새 1개 통과, 25사례/28 C 호출 | C 헤더 메모리 경계와 C++ 단계별 조립/최종 소유자 해제. detect_leaks=0이며 누수 검사는 제외 |
| 기존 179개·추가 matrix/gate/외부 소비자 | 생략 — 기존 결과 재사용 | 바이트 형식·체크섬 단위의 wire/records/encoding_malformed/encoding_failure·할당/소비자와 각 후속 코어/native 단위의 최초 기록 | 제품 코드·기존 테스트 로직/입력·공개 헤더/링크·옵션/환경은 그대로다. 이번에는 새 검사의 TU/dispatcher/등록만 추가했으며 같은 executable이라는 이유로 기존 검사를 반복하지 않음 |
| 수 GiB payload의 실제 할당·32비트 실행·디스크 전체 경계·실제 kernel/raw·기존 참조/실패 제한 | 미검증 | 기존 디스크/writer/TCP/kernel 및 완료 요건 기록 | 새 검사는 길이 선언과 작은 실데이터 조립 범위다. 기존 제한 해소나 전체 실행 경로 완료로 간주하지 않음 |

명령은 `cmake -S . -B <dir>`, `cmake --build <dir> --target raftpp_encoding_tests --parallel 2`, `ctest --test-dir <dir> --output-on-failure -R '^raftpp.encoding_limits$'`다. `<dir>`는 build, /tmp/raftpp-fixture-controls-release, /tmp/raftpp-recover-reference-asan이며 기존 옵션을 유지했다. ASan 실행에는 `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했다. 로그는 `/tmp/raftpp-encoding-limits-{debug,release,asan}-{configure,build}.log`, `-<구성>.log`, `-<구성>-output.log`이며 stdout을 목록 조회 전에 보존했다. 새 검사는 TIMEOUT 30으로 등록했다. 전체 등록 수는 180/core-only fixture ON 83/UV ON fixture OFF 141이며 전체 통과 개수와 다르다.

encoding API 두 언어본에는 한도의 포함 경계, 형식/플랫폼 폭, 헤더 성공과 실제 payload의 구분, 직접 조립한 단계별 헤더의 일관성·오류·소유권 계약을 설명했다. docs에는 작업 기록을 넣지 않는다. 후속 문서/진행 기록만의 변경에는 빌드/runtime을 추가 실행하지 않는다. 전체 재작성 목표와 이전 실환경/참조 실패 제한은 유지한다.

변경 문서 2쌍의 정적 확인은 REWRITTEN 제목/표 행 52/373, encoding API 7/0의 대응과 번역 주석 제외 코드 예제·상대 링크 352개·API 문서 경로 14쌍·다음 작업 1–5 대응을 확인했다. 세 구성의 선택 검사 결과와 보존 stdout도 확인했다. 문서 보완에는 추가 빌드/runtime을 실행하지 않았다.

### 실제 native pipe의 stream 소유권·완료·종료 검증 (2026-11-03)

직전 goal 턴은 wire 경계 25사례·C 비교와 API/기록을 추가한 progress였다. 이번에는 루트 지침과 현황·114개 공개 C 대응 목록, node의 요청·설정·멤버십과 C legacy/client/FSM/I/O 계약, UV/TCP의 init/연결/읽기/쓰기를 대조했다. 기존 51개 raft/src C 소스와 CMake의 독립 C++ 소스 목록도 확인했으나 파일 존재/대응을 모든 실행 경로 완료로 판정하지 않았다. 공개 uv_stream은 TCP/pipe/TTY를 채택하며 기존 native 실행은 TCP만 검증했다는 실제 공백을 골랐다. 참조 범위는 uv_stream.cpp/uv_error.hpp와 기존 stream 테스트·원본 raw transport의 uv_stream_t 소유/인계 계약까지다. 제품 구현·공개 시그니처·알고리즘·wire/disk·기준 raft/는 이번에 수정하지 않았다.

최초 ASan tcp_memory 실패의 원인 조사에서는 기록된 loop::until의 12초 조건과 cluster의 선거·commit·스냅샷·인계·재시작 대기를 읽었다. 당시 3개 /tmp/raftpp-native-memory-asan.log, -asan-tcp-diagnostic.log, -asan-tcp-investigation.log는 현재 파일이 없다. 과거 실행/실패 기록을 삭제하지 않으며 원인을 특정하거나 새로 해결했다고 주장하지 않는다. 같은 실패를 로그 재생성을 위해 반복 실행하지 않았다. pipe 검사는 AF_INET socket·실제 TCP 클러스터를 실행하지 않는다.

새 raftpp.stream_pipe는 tests/uv_stream.cpp에서 실제 Linux pipe2와 uv_pipe_init/open으로 두 native 핸들을 만들고 공개 uv_stream에 채택한다. 다섯 사례는 (1) 3바이트 read chunk·빈 중간 scatter·이동된 writer·완료까지 버퍼 소유/해제·native 종료 이후 chunk 유지, (2) F_GETPIPE_SZ보다 큰 실제 대기 쓰기의 취소 1회와 버퍼 유지·native deleter·두 종료 관찰자의 순서, (3) 쓰기 끝 종료로 실제 EOF를 발생시켜 원인 UV_EOF/구독 종료/재진입 close 1회, (4) 활성 pipe의 채택 거부가 기존 data/소유권/deleter를 건드리지 않음, (5) 읽기 전용 끝 쓰기의 즉시 오류·콜백 미호출·native 원인 보존이다. 각 native data는 해제 전에 원래 값으로 복원하고 두 할당은 한 번씩 해제되며 루프가 닫힐 때까지 drain한다. 전체 heap 누수 검증이나 TTY/실제 pipe connector 위의 메시지 adapter 검증으로 확대하지 않는다.

첫 세 구성 실행은 마지막 오류 원인을 테스트가 EBADF로 가정해서 실패했다. 제품 오류라고 단정하지 않고 독립 C11/libuv 실험 /tmp/raftpp-pipe-native-probe.c를 빌드·실행했다. 출력은 `readable=1 writable=0 immediate=-32 (broken pipe)`이며 완료 콜백은 호출되지 않았다. 제품은 같은 원인을 보존하므로 테스트 기대값만 broken_pipe로 수정했다. 최초 실패/runtime/stdout은 /tmp/raftpp-stream-pipe-{debug,release,asan}.log와 -<구성>-initial-output.log, 초기 build/configure 로그에 보존했다. /tmp/raftpp-pipe-native-probe.log와 소스/실행 파일도 남겼다. 변경 없는 실패를 반복한 것이 아니라 독립 원인 근거와 기대값 수정을 확인한 뒤 재실행했다.

검증 시점 2026-11-03 04:22–04:24 KST, Linux/GCC·G++ 13.3/C++20·libuv 1.48. 각 build exit 0을 확인한 뒤 runtime을 실행했다. 새 검사는 기존 UV 조건과 stream 타깃에 추가했으며 TIMEOUT 25다. 최초 실패와 마지막 통과를 구분하고 기존 stream/TCP 검사와 전체 suite는 실행하지 않았다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 최초 native pipe 세 구성 | 실행 — 실패 | 마지막 EBADF 기대값에서 실패, 앞선 I/O·취소·EOF·활성 채택 사례는 실행됨 | 최초 실패 로그 보존. 전체 새 검사 통과로 합산하지 않음 |
| 독립 C/libuv 원인 실험 | 실행 | cc -std=c11 -D_GNU_SOURCE /tmp/raftpp-pipe-native-probe.c -luv -o /tmp/raftpp-pipe-native-probe 후 /tmp/raftpp-pipe-native-probe 실행, 실제 EPIPE=-32 즉시 거부·콜백 없음 | 제품과 독립적인 native 원인 확인. probe source/log 보존 |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 기대값 수정 후 새 stream_pipe 1개/5사례 통과 | 실제 pipe kernel I/O·소유/콜백·종료 계약 |
| 엄격한 공유 Release·UV ON·LZ4/fixture/backtrace OFF | 실행 | 같은 새 1개/5사례 통과 | NDEBUG·공유 API, C++ -Wall -Wextra -Wpedantic -Werror |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | 같은 새 1개/5사례 통과 | native/요청/공유 payload 수명. detect_leaks=0이며 누수 검사는 제외 |
| 기존 180개·core-only·추가 matrix/gate/외부 소비자 | 생략 — 기존 결과 재사용 | 원래 raw-stream 여섯 검사·native 전송/설정·이전 wire/numeric와 각 단위의 최초 결과 | 제품/기존 테스트 경로·공개 선언/링크·옵션/환경은 그대로. test TU의 추가 함수/dispatch/UV 등록만 변경. 같은 타깃이라는 이유로 기존 검사를 반복하지 않음 |
| TTY·pipe connector framing·최초 TCP timeout·kernel/raw·기존 참조 실패 | 미검증 | 이전 원인 미확정/환경 제한 기록, 위 누락된 과거 TCP 로그 | 이번 plain pipe I/O 결과로 전체 native 종류·adapter·TCP 실패 해결이나 전체 재작성 완료를 주장하지 않음 |

configure는 `cmake -S . -B <dir>`, 최초/최종 빌드는 `cmake --build <dir> --target raftpp_stream_tests --parallel 2`, runtime은 `ctest --test-dir <dir> --output-on-failure -R '^raftpp.stream_pipe$'`다. <dir>는 build, /tmp/raftpp-recover-reference-release, /tmp/raftpp-recover-reference-asan이며 옵션은 유지했다. ASan 환경은 `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`이다. 로그 접두사는 /tmp/raftpp-stream-pipe, 최초 -<구성>-{configure,build}.log, -<구성>.log와 최종 -<구성>-final-{build,output}.log, -<구성>-final.log다. stdout은 목록 조회 전에 따로 보존했다. 세 stdout 모두 `native pipe streams: 5 cases`이며 현재 등록 수 전체 181/UV ON fixture OFF 142는 전체 통과 개수가 아니다. UV OFF 등록은 이전 83개 결과/조건을 재사용한다.

uv_stream API 두 언어본에는 단방향 pipe의 지원 방향·즉시 오류/원인·EOF 콜백에서의 종료 계약만 보완했다. 진행 기록은 루트 대응본에만 둔다. 최종 문서/기록 변경에는 추가 빌드/runtime을 실행하지 않는다. 전체 재작성의 미검증 범위와 기존 제한은 유지한다.

변경 문서 2쌍의 정적 확인은 REWRITTEN 제목/표 행 53/382, uv_stream API 5/0, 번역 주석 제외 코드 예제·상대 링크 358개·API 경로 14쌍·다음 작업 1–5 대응을 확인했다. /tmp/raftpp-stream-pipe-doc-check.py로 원본 C 소스 51개·최초 실패/독립 EPIPE 원인/세 최종 stdout도 확인했다. 문서/기록 보완에는 추가 빌드/runtime을 실행하지 않았다.

### 전용 PTY에서 native TTY stream 계약 검증 (2026-11-03)

직전 goal 턴은 실제 pipe 5사례와 API/진행 기록을 추가한 progress였다. 이번에는 AGENTS·두 언어 현황과 uv_stream의 TCP/pipe/TTY 채택·공유 write/read chunk·이동/EOF/종료 구현, 기존 raw-stream/pipe 검증 및 API를 대조했다. native TTY 실행 근거가 없던 공개 계약을 실제 PTY로 검증한다. 실제 수정은 tests/uv_stream.cpp와 UV 조건의 새 CTest 등록, 한·영 uv_stream API/현황이다. 제품 코드·공개 선언·기능 의존성·합의 알고리즘·형식과 원본 raft/는 변경하지 않았다.

먼저 /tmp/raftpp-tty-native-probe.c의 독립 C11/libuv 실험으로 새 PTY를 생성해 raw 바이트 읽기/쓰기와 master 종료의 native 결과를 확인했다. 출력은 original_fd=4/native_fd=11의 소유 복제, `received=4 written=1 master_read=5 ended=-4095 (end of file)`, `loop_close=0`이다. fd 번호는 실행 환경 값이며 테스트 조건은 아니다. 실제 uv_tty_t와 현재 libuv 동작의 근거이며 cowsql/raft의 별도 TTY 구현과 비교했다는 뜻은 아니다.

새 stream_tty는 posix_openpt·grantpt·unlockpt·ptsname_r로 생성한 전용 master/slave만 사용한다. O_NOCTTY로 제어 터미널을 채택하지 않으며 기존 stdin/stdout/사용자 터미널·uv_tty_set_mode/reset_mode의 전역 모드를 바꾸지 않는다. termios raw와 창 크기 설정은 방금 생성한 slave에만 적용한다. libuv가 fd를 복제한 경우 원래 slave fd는 별도 RAII로 닫고, 채택한 native 할당/복제 fd는 비동기 close까지 소유한다. 채택 생성자 예외에서는 아직 소유한 native 할당을 uv_close 콜백으로 정리한다.

다섯 실제 사례는 (1) UV_TTY 타입·73×21 창 크기 조회·NUL/CR/LF를 포함한 양방향 scatter 전송·2바이트 read chunk·완료까지 write 소유/해제·종료 후 read chunk 보관, (2) 구독 중지→master에 쓴 보류 데이터→콜백 없음→구독 재개와 바이트 수신, (3) master 종료의 UV_EOF 원인·재구독 거부·EOF 콜백에서 close/deleter/관찰자 1회, (4) 활성 TTY 채택 거부의 data/기존 소유자 보존, (5) 같은 루프의 두 TTY 사이 이동 대입으로 이전 native 정리·이동 대상 주소 보존·새 소유자 양방향 I/O와 원래 deleter 복원이다. 모든 조건은 Release에서도 실행된다. 이 소유자/할당 관찰은 테스트 자원 범위이며 전체 heap 누수 검증으로 확대하지 않는다.

검증 시점은 독립 probe 2026-11-03 04:29 KST, 새 CTest 04:31 KST다. Linux/GCC·G++ 13.3/C++20/libuv 1.48 환경이며 각 build exit 0을 확인한 뒤 runtime을 실행했다. 새 검사는 UV 조건의 기존 stream 타깃에 TIMEOUT 25로 등록했다. 기존 native TCP/pipe 및 전체 suite는 반복하지 않았다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 독립 C11/libuv TTY 실험 | 실행 | 새 PTY 양방향 전송·fd 복제·UV_EOF·loop_close=0 | native 계약을 제품과 독립적으로 확인. /tmp/raftpp-tty-native-probe.c/.log/실행 파일 보존 |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 stream_tty 1개/5사례 통과 | 실제 TTY 타입·mode에 따른 바이트·소유/구독·종료/이동 |
| 엄격한 공유 Release·UV ON·LZ4/fixture/backtrace OFF | 실행 | 같은 새 1개/5사례 통과 | NDEBUG·공유 API, C++ -Wall -Wextra -Wpedantic -Werror |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | 같은 새 1개/5사례 통과 | native/요청/버퍼 수명. detect_leaks=0으로 누수 검사는 제외 |
| 기존 181개·core-only·추가 matrix/gate/외부 소비자 | 생략 — 기존 결과 재사용 | 앞선 pipe/원래 raw-stream 검사·wire/numeric·각 native/코어/fixture 단위의 최초 기록 | 제품/기존 테스트 로직·입력·공개 헤더/링크/기능 조건/환경은 그대로다. 같은 TU/타깃이라는 이유로 반복하지 않으며 이전 실패/미검증의 상태도 그대로 유지한다. 생략을 새 통과로 합산하지 않음 |
| 실제 pipe/TTY connector의 메시지 adapter·최초 TCP timeout·kernel/raw·기존 참조 실패 | 미검증 | 이전 원인 미확정/환경 제한과 전체 완료 요건 기록 | plain native TTY/PTY 검증으로 모든 adapter·TCP 실패 해결·전체 재작성 완료를 주장하지 않음 |

probe 명령은 `cc -std=c11 -D_GNU_SOURCE /tmp/raftpp-tty-native-probe.c -luv -o /tmp/raftpp-tty-native-probe` 후 `/tmp/raftpp-tty-native-probe`다. configure/build/runtime은 `cmake -S . -B <dir>`, `cmake --build <dir> --target raftpp_stream_tests --parallel 2`, `ctest --test-dir <dir> --output-on-failure -R '^raftpp.stream_tty$'`다. <dir>는 build, /tmp/raftpp-recover-reference-release, /tmp/raftpp-recover-reference-asan이며 기존 옵션을 유지했다. ASan 실행은 `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했다. /tmp/raftpp-stream-tty-{debug,release,asan}-{configure,build}.log, -<구성>.log, -<구성>-output.log를 보존했고 stdout은 목록 조회 전에 복사했다. 세 stdout은 `native TTY streams: 5 cases`다. 현재 등록 수 전체 182/UV ON fixture OFF 143은 전체 통과 개수가 아니며 UV OFF의 이전 83개/조건은 재사용한다.

API 두 언어본에는 native 설정 조회(TTY 창 크기 등), 호출자가 터미널 모드를 선택하고 wrapper가 텍스트 변환을 추가하지 않는다는 바이트 계약을 설명했다. 진행 기록은 루트에만 두고 최종 문서/기록 변경에는 추가 빌드/runtime을 실행하지 않는다. 전체 재작성 목표와 미검증·기존 제한은 유지한다.

변경 문서 2쌍의 정적 확인은 REWRITTEN 제목/표 행 54/390, uv_stream API 5/0, 번역 주석 제외 코드 예제·상대 링크 358개·API 경로 14쌍·다음 작업 1–5 대응을 확인했다. /tmp/raftpp-stream-tty-doc-check.py로 독립 PTY/libuv 실험과 세 구성의 결과/stdout 보존도 확인했다. 최종 문서/기록 보완에는 추가 빌드/runtime을 실행하지 않았다.

### TTY connector 메시지 framing·backpressure·종료와 기존 pipe 근거 정정 (2026-11-03)

직전 goal 턴은 전용 PTY의 native TTY 5사례와 문서를 실제 추가·검증한 progress였다. 이번에는 AGENTS·두 언어 현황, stream_transport/uv_stream/codec의 제출·단계 수신·완료·종료, 기존 tests/stream_transport.cpp와 최초 adapter 기록을 대조했다. 구현이 이미 존재하는 범위는 중복 작업하지 않고 새 실제 TTY connector 경계를 검증했다. 수정은 테스트의 선택적 PTY factory/통계·3사례·dispatcher/CTest 등록과 한·영 API/현황이며 제품 구현·공개 시그니처·옵션/의존성·알고리즘/형식·원본 raft/는 바꾸지 않았다.

먼저 최근 현황의 제한을 정정한다. 2026-11-02의 “Raw 플러그인 메시지 adapter·UV 연결 검증”과 현재 소스에는 AF_UNIX socketpair를 실제 uv_pipe_t로 채택하는 pipe connector가 있다. adapter_exchange/retry/close/receive_failure/allocations/uv_io는 실제 stream과 kernel I/O를 사용하며 native 디스크 3노드의 선거·4명령 복제·종료도 이미 기록했다. 따라서 최근 pipe·TTY plain stream 단위가 pipe connector framing도 미검증으로 분류한 부분은 잘못된 요약이다. 과거 pipe 실행 시점/최초 실패·최종 통과/제한은 원래 절대로 보존하고, 현재 증거로 이 부분만 정정한다. 이번에 원래 pipe 검사를 새로 통과한 것으로 보고하거나 로그를 재생성하기 위해 실행하지 않았다. TTY connector는 그 기존 검증에 포함되지 않았으므로 이번 새 범위다.

독립 C11/libuv 실험 /tmp/raftpp-tty-adapter-native-probe.c는 전용 PTY master를 uv_pipe_t, slave를 uv_tty_t로 초기화하고 각각 비차단 쓰기를 설정한 뒤 닫았다. `master_type=7 slave_type=14 nonblocking=0/0`, `loop_close=0`을 확인했다. 새 테스트 factory는 같은 새 PTY의 slave에만 raw termios를 적용하며 제어/기존 터미널·전역 TTY 모드를 바꾸지 않는다. native 자원의 deleter는 connector를 대여하지 않고 shared 통계를 소유한다. bus의 tty=false 기본값과 기존 pipe 경로·실패 주입·native 호출은 그대로 유지하며 tty=true인 새 사례에서만 factory/방향 교체를 선택한다.

새 adapter_tty의 3사례는 다음과 같다. 첫 사례는 즉시 accept/connect를 실행하는 사용자 connector로 두 adapter를 연결하고 ID에 따라 master pipe와 slave TTY의 송신 방향을 바꾼다. 3/5바이트 read chunk로 6종 RPC를 양방향 12개 전송하여 바이트·순서·송신자 ID/주소·요청 소유권의 완료/최종 해제를 대조하고 2쌍의 native deleter가 종료 전에 모두 실행됨을 확인한다. 둘째는 읽지 않는 peer에 TTY로 4MiB AppendEntries를 제출하고 실제 write_queue_size>0을 확인한 뒤 adapter/connector wrapper를 파괴한다. 수락한 요청은 메시지가 살아 있는 동안 canceled로 한 번 완료하고, native TTY 해제 뒤 adapter 종료 콜백이 실행된다. 독립 peer stream도 이어서 닫아 두 할당을 정리한다. 이미 쓰인 prefix의 철회를 보장한다고 해석하지 않는다. 셋째는 8바이트 preamble만 쓴 PTY master를 종료하여 TTY의 실제 UV_EOF 원인을 adapter까지 보존하고 미완성 메시지를 전달하지 않으며 두 native 할당을 정리한다.

새 비동기 테스트 상태는 shared 값으로 콜백이 소유한다. wrapper 파괴나 실패 시 loop drain에서 테스트의 지역 변수 참조가 먼저 사라지는 UAF를 만들지 않는다. 이 소유/할당 관찰은 해당 자원 범위이고 전체 heap 누수 검증이 아니다. 이번 12개는 메시지 전송이며 TTY 위 새 Raft/디스크 클러스터 12개 명령을 검증했다는 뜻은 아니다. 기존 실제 pipe+native 디스크 통합과 이번 TTY 메시지 경계를 구분한다.

검증 시점은 probe 2026-11-03 04:38:20 KST, 새 CTest 04:42 KST다. Linux/GCC·G++ 13.3/C++20/libuv 1.48에서 각 build exit 0 후 실행했다. 새 CTest는 기존 UV 타깃에 TIMEOUT 35로 등록했다. 해당 범위의 새 실패는 없으며 변경 없는 검사는 결과·제한을 재사용했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 독립 C11/libuv PTY stream 구성 | 실행 | pipe/TTY native 타입·비차단 설정 0/0·loop_close=0 | 새 TTY connector의 native 전제, probe source/log/실행 파일 보존 |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 adapter_tty 1개/3사례·12개 완전한 RPC 통과 | 실제 TTY framing·메시지/요청 수명·backpressure/취소·EOF |
| 엄격한 공유 Release·UV ON·LZ4/fixture/backtrace OFF | 실행 | 같은 새 1개 통과 | NDEBUG·공유 API, C++ -Wall -Wextra -Wpedantic -Werror |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | 같은 새 1개 통과 | 콜백 상태·native/메시지 수명, detect_leaks=0으로 누수 검사 제외 |
| 기존 182개·core-only·추가 matrix/gate/외부 소비자 | 생략 — 기존 결과 재사용 | 원래 adapter·pipe/TTY stream·wire/numeric·각 native/코어/fixture 단위의 최초 기록 | 제품/기존 테스트 입력·public/link/feature/환경은 그대로. helper의 기본 tty=false 분기는 기존 pipe 동작·할당/실패 주입을 유지하고 새 tty=true 분기만 실행했다. 기존 실패·미검증 상태도 보존하며 생략을 새 통과로 합산하지 않음 |
| 기존 실제 pipe connector framing·native 디스크 클러스터 | 생략 — 기존 결과 재사용/현황 정정 | 2026-11-02 Raw 플러그인 메시지 adapter·UV 연결 검증의 7개 adapter/관련 stream·소비자 결과 | 위 기존 182개에 포함된 범위다. 소스의 실제 socketpair/uv_pipe와 과거 검증을 확인해 최근 미검증 요약만 정정함 |
| 최초 TCP timeout·kernel/raw 장치·기존 참조 C 실패·전체 완료 요건 | 미검증/미해결 유지 | 앞선 환경/원인 미확정/완료 요건 기록 | 새 TTY 통과가 이 제한의 해결이나 모든 원본 실행 경로 완료 증명이 아님 |

probe 명령은 `cc -std=c11 -D_GNU_SOURCE /tmp/raftpp-tty-adapter-native-probe.c -luv -o /tmp/raftpp-tty-adapter-native-probe` 후 해당 실행 파일을 실행한 것이다. configure/build/runtime은 `cmake -S . -B <dir>`, `cmake --build <dir> --target raftpp_adapter_tests --parallel 2`, `ctest --test-dir <dir> --output-on-failure -R '^raftpp.adapter_tty$'`다. <dir>는 build, /tmp/raftpp-recover-reference-release, /tmp/raftpp-recover-reference-asan이며 기존 옵션을 유지했다. ASan은 `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`을 사용했다. 로그는 /tmp/raftpp-adapter-tty-{debug,release,asan}-{configure,build}.log, -<구성>.log, -<구성>-output.log이며 stdout은 목록 조회 전에 보존했다. 모두 `TTY message adapter: 3 cases, 12 complete RPCs`다. 현재 등록 수 전체 183/UV ON fixture OFF 144는 전체 통과 개수가 아니며 UV OFF의 이전 83개/조건은 재사용한다.

한·영 stream_transport API에는 connector가 바이트 순서/내용을 보존하는 비차단 stream을 제공하고 TTY raw/비차단 설정을 인계 전에 준비해야 한다는 계약을 보완했다. 진행 기록은 루트 대응본에만 둔다. 최종 문서/기록 변경에는 빌드/runtime을 반복하지 않으며 전체 목표·미검증/기존 제한은 유지한다.

변경 문서 2쌍의 정적 확인은 REWRITTEN 제목/표 행 55/399, stream_transport API 4/6, 번역 주석 제외 코드 예제·상대 링크 356개·API 경로 14쌍·다음 작업 1–5 대응을 확인했다. /tmp/raftpp-adapter-tty-doc-check.py로 기존 실제 pipe 분기, 독립 PTY 구성과 세 구성의 새 결과/stdout 보존도 확인했다. 이 문서 보완에는 추가 빌드/runtime을 실행하지 않았다.

### C++ 설치·재배치 가능한 CMake 패키지와 선택 의존성 검증 (2026-11-03)

원본 raft/Makefile.am의 라이브러리·헤더·pkgconfig 설치와 현재 CMakeLists.txt·외부 소비 경로를 대조해 C++ 설치/검색 경로가 없음을 확인했다. cmake/install.cmake와 raftpp-config.cmake.in을 추가하고 RAFTPP_INSTALL의 최초 기본값을 최상위 ON/하위 프로젝트 OFF로 정했다. 생성 config/export 헤더·지원 헤더·C++ 라이브러리와 원본 라이선스를 GNUInstallDirs 경로에 설치한다. C 기준 타깃/raft.h는 build-tree 비교용이며 설치 export는 raftpp::raftpp만 제공한다. 원본 raft/와 합의·I/O 제품 소스는 변경하지 않았다.

소비 환경에서 공개 UV와 정적 라이브러리의 비공개 LZ4/backtrace 의존성을 다시 찾는다. 공유 비공개 의존성은 소비 개발 파일을 요구하지 않는다. 기존 GNU backtrace imported 타깃 이름을 raftpp::backtrace로 바꿔 export에서 같은 의미를 전달하며 소스/생산 의존성의 절대 경로를 고정하지 않는다. C++ package 버전/ABI를 임의로 정하지 않고 버전 없는 find_package(raftpp CONFIG)를 제공한다. INSTALL을 끄면 이전 build-tree package config를 제거하고 설치 규칙도 제외한다.

tests/package의 C++ 전용 독립 소비자는 단일 투표자 시작·명령 영속/commit, 압축 객체의 정적 링크, 선택 fixture와 실제 libuv loop를 사용한다. 새 raftpp.package_install은 설치 prefix를 실제 이동한 뒤 외부 소비자를 configure/build/run한다. 테스트는 INSTALL ON·native·상대 GNU 설치 경로에서 등록하며 절대 설치 경로나 cross 환경에 임의 쓰기/실행하지 않는다. 아래 실행은 2026-11-03 05:03–05:14 KST의 Linux x86_64·CMake 3.28.3·GNU C++ 13.3.0·libuv 1.48.0·LZ4 1.9.4 환경이며 sanitizer는 사용하지 않았다. 관련 런타임 구현을 바꾸지 않아 기존 sanitizer 결과는 재사용한다. 과거 LSan 제외 제한도 유지한다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 설치 소비 — build, Debug 정적, UV/LZ4/fixture ON, backtrace OFF, tests/examples/benchmarks ON | 실행 | 새 package_install 1개 통과(32.10초). /tmp/raftpp-package-full-static-{configure,build}.log, -full-static.log, -full-static-output.log, -full-static-consumer-output.log | C++ 타깃만 링크하고 생성 헤더·공개 UV·정적 비공개 LZ4와 이동한 prefix를 사용한다. 소비 출력 uv=1/lz4=1/fixture=1/backtrace=0 |
| 설치 소비 — /tmp/raftpp-options-build, 엄격한 공유 Release, UV/LZ4/fixture ON, backtrace OFF, tests ON, examples/benchmarks OFF | 실행 | 새 package_install 1개 통과(10.17초). /tmp/raftpp-package-full-shared-final-{configure,build}.log, -full-shared.log, -full-shared-output.log, -full-shared-consumer-output.log | 명시적으로 Release/BUILD_SHARED_LIBS=ON을 선택한 뒤 빌드 종료 0을 확인했다. 소비 출력 1/1/1/0 |
| 설치 소비 — /tmp/raftpp-fixture-controls-release, 엄격한 공유 Release, UV/LZ4 OFF, fixture/backtrace ON, tests ON, examples/benchmarks OFF | 실행 | 새 package_install 1개 통과(32.40초). /tmp/raftpp-package-core-shared-{configure,build}.log, -core-shared.log, -core-shared-output.log, -core-shared-consumer-output.log | 소비 PkgConfig 검색을 비활성화한다. 공유 비공개 backtrace SDK를 요구하지 않으며 출력 0/0/1/1 |
| 설치 소비 — /tmp/raftpp-package-core-backtrace-build, 엄격한 Debug 정적, UV/LZ4/fixture OFF, backtrace/tests ON, examples/benchmarks OFF | 실행 | 새 package_install 1개 통과(19.84초). /tmp/raftpp-package-core-static-{configure,build}.log, -core-static.log, -core-static-output.log, -core-static-consumer-output.log | PkgConfig 없이 GNU libbacktrace/필요한 dl 링크를 소비 환경에서 찾는다. 출력 0/0/0/1 |
| 누락 의존성의 QUIET/REQUIRED 검색 | 실행 | /tmp/raftpp-package-missing-{pkgconfig,backtrace,lz4}.log의 configure 3개 통과; -required-missing-lz4.log는 의도한 configure 실패와 원인 문구 확인 | UV package의 PkgConfig 비활성화, 존재하지 않는 정적 backtrace 경로, libuv.pc만 있는 환경의 정적 LZ4 누락을 구분한다. QUIET는 FOUND=false/타깃 미정의, REQUIRED는 실패 |
| 공백 prefix·공유 비공개 LZ4 요구 제외 | 실행 | /tmp/raftpp-package-spaces-uv-only-{configure,build}.log, -spaces-uv-only.log의 소비자 1개 통과 | 공유 설치본을 /tmp/raftpp package with spaces로 복사하고 PKG_CONFIG_PATH 비움/PKG_CONFIG_LIBDIR=/tmp/raftpp-package-uv-only-pc로 libuv.pc만 제공한다. LZ4.pc 없이 실제 실행하며 링크에 C 기준/LZ4 직접 요구가 없다 |
| add_subdirectory 기본값·동일 빌드 INSTALL ON→OFF | 실행 | /tmp/raftpp-package-parent-{default,on,off}.log, -parent-disabled-install.log; configure 3개와 비활성 설치 통과 | 외부 CXX 부모·코어 전용·PkgConfig 비활성 조건에서 최초 OFF, 명시 ON config 생성, OFF config 제거를 확인했다. --install --prefix /tmp/raftpp-package-parent-disabled-prefix는 파일을 설치하지 않는다. 라이브러리는 이 configure-only 검사에서 빌드하지 않았다 |
| 설치 파일·export 경로·등록 확인 | 실행 | /tmp/raftpp-package-metadata-check.log, -registration.log와 -{full-static,full-shared,core-shared,core-static}-list.json | 구성별 52파일/라이브러리 1개·생성 헤더·원본과 동일한 라이선스, C 라이브러리/raft.h 및 생산/의존 경로 누출 없음. CTest 등록은 184/165/84/64개이며 구성별 새 검사 1개만 실행했다. 등록 수를 전체 통과 수로 보고하지 않는다 |
| 변경 없는 기존 합의·노드·파일/TCP·fixture runtime, sanitizer와 추가 옵션 matrix | 생략 — 기존 결과 재사용 | 이전 코어/네트워크/복구/수명·TTY 단위와 해당 구성별 원래 기록 | 알고리즘·비동기 정리·입력 및 그 검사는 변경하지 않았다. 설치/링크 변경은 새 외부 소비자에 한정해 검증했다. 기본 구성의 기존 183개는 이번 새 통과 수에 합산하지 않는다 |
| 실제 kernel profiler/raw 장치·최초 TCP timeout·참조 C 제한 | 미검증 | 기존 profiler/메모리/legacy/snapshot 단위의 실패·환경 제한 유지 | 설치 package 검사로 실제 장치나 기존 미확정 실패 원인을 해결했다고 주장하지 않는다 |

네 구성은 cmake -S . -B <producer>로 해당 옵션을 선택하고 cmake --build <producer> --target raftpp --parallel 2가 종료 0인 뒤 ctest --test-dir <producer> --output-on-failure -R '^raftpp.package_install$'를 각각 한 번 실행했다. 엄격한 구성의 CMAKE_CXX_FLAGS는 -Wall -Wextra -Wpedantic -Werror다. 테스트 driver의 실제 install/rename/consumer 명령은 tests/package/install.cmake와 위 stdout 보존 로그에 있다. 최초 실행 stdout와 안쪽 소비 stdout는 --show-only 등록 조회 전에 따로 보존했다.

과거 /tmp/raftpp-options-build가 없어 첫 configure/build는 기본 정적·빌드 종류 없음·tests OFF 구성으로 생성됐다. /tmp/raftpp-package-full-shared-{configure,build}.log는 그 중간 결과이며 공유 검증으로 재사용하지 않는다. 이후 명시적 공유 Release 구성의 -full-shared-final- 로그와 실제 소비 통과만 최종 근거다. 최초 TCP resource는 할당 실패를 주입하지 않음을 소스에서 다시 확인했다. 이전 snapshot OOM 수정이 이 timeout의 원인을 해결했다고 단정하거나 동일 검사를 재실행하지 않았다.

한·영 AGENTS에 설치/export 원칙을 함께 반영하고 docs/ko/cmake.md·docs/en/cmake.md에 기능/설치/의존성/검색 오류의 API 계약만 작성했다. core API에서 새 문서를 연결했다. 설치·패키지 단위의 구현/선택 검증은 완료했으나 전체 원본 실행 경로 감사와 기존 환경·실패 제한은 남아 있어 전체 목표는 진행 중이다.

변경 문서 4쌍의 정적 확인은 AGENTS 제목/표 행 12/28, REWRITTEN 56/411, CMake API 5/11, core API 6/30과 코드 예제·상대 링크 380개·API 경로 15쌍·다음 작업 1–5의 대응을 확인했다. /tmp/raftpp-package-doc-check.py는 네 구성의 원래 통과/소비 stdout 보존도 확인한다. 최종 문서 변경으로 빌드/runtime을 반복하지 않았다.

### 디스크 선언 길이·파일명 개수 경계의 실제 C 읽기 대조 (2026-11-03)

직전 설치·패키지 단위는 구현과 선택 검증을 완료해 진전했다. 이번에는 src/disk.cpp의 read/read_segment/load_snapshot/load/finalize, metadata.cpp·encoding.cpp·configuration.cpp의 길이/CRC/범위 검사와 segment_buffer.cpp·파일 backend의 호출/출력 보존, 원본 uv_segment.c/uv_snapshot.c/uv_fs.c/uv_encoding.c/configuration.c를 필요한 읽기·정리 경로까지 참고했다. 기존 encoding_malformed·metadata_formats·segment 복구/실패·recover C 대조는 일반 길이/잘린 입력/할당 실패를 이미 다루므로 반복하지 않았다. 남은 실제 경계로 파일명 개수의 32비트 절단, 작은 파일의 큰 선언 길이, snapshot 설정의 정확한 1 MiB 경계와 payload 상한을 선택했다.

새 tests/persistence_limits.cpp의 storage_limits는 실제 C reader 29회와 C++를 비교한다. closed 파일명 범위 8개는 실제 1개 엔트리와 정상/불일치 개수, 시작 인덱스 2^32+17 및 16자리 최댓값, 2^32·2^32+1·2^33+1 선언을 포함한다. 추가 record 7개는 개수 0/262144/262145/2^32/uint64 최댓값, 정렬된 4294967288바이트 payload 선언과 올바른 header CRC를 가진 잘못된 타입을 작은 파일로 다룬다. snapshot metadata 14개는 0/1 MiB+1/uint32 최댓값/2^32/2^63/uint64 최댓값, 상한 이하의 짧은 파일·format 오류 우선순위·CRC 불일치·CRC 상위 32비트 무시·추가 바이트 무시·정확히 1 MiB인 유효한 설정을 다룬다. 별도 payload 한도 7/8바이트 두 경우는 거부 시 data open 전 중단·출력 보존과 정확한 한도 성공·진단 초기화를 확인했다. 가짜 span이나 대형 payload 없이 실제 메타데이터는 최대 1 MiB+32바이트다.

27회는 원본 C와 일치한다. 2회는 원본 uvSegmentLoadClosed의 expected_n=(unsigned)(end-first+1) 변환 때문에 C가 1개 엔트리를 가진 2^32+1/2^33+1 범위 파일을 성공으로 처리하지만 C++는 전체 범위의 불일치를 corrupt로 거부한다. 이 원본 절단을 C++에 도입하지 않았다. 높은 시작 인덱스/개수 1개인 정상 파일은 양쪽 모두 성공한다. 관련 C++ 제품 구현과 raft/ 원본은 변경하지 않았다.

C 비교는 기존 persistence_reference.c에 성공 때만 결과 개수를 공개/해제하는 reference_segment_probe를 추가해 실제 reader를 호출한다. 원본 오류 경로는 해제한 entries 포인터를 비우지 않을 수 있어 이를 재해제하지 않는다. 기존 bridge 함수는 유지했다. 새 C++ 검사는 실패 시 snapshot의 term/index/configuration/configuration_index/buffer 소유자 및 entry 출력/진단, 메타데이터·segment 바이트/크기와 FD를 확인한다. C++ payload 상한도 읽기 전에 검사된다. 소스/테스트/빌드 입력이 같은 기존 검사를 새 통과로 합산하지 않는다.

검증은 2026-11-03 05:22–05:23 KST의 Linux x86_64·GNU 13.3.0·CMake 3.28.3·libuv 1.48.0 환경에서 수행했다. C++ 수치 범위는 64비트 실행이며 32비트 아키텍처나 실제 수 GiB payload/파일의 실행 결과가 아니다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 새 storage_limits — Debug 정적, UV/LZ4/fixture ON, backtrace OFF | 실행 | 1개 통과(0.35초). /tmp/raftpp-storage-limits-debug-{configure,build}.log, -debug.log, -debug-output.log | C 비교 29회/절단 차이 2회/payload 한도 2회. 기존 persistence target에 새 source와 dispatcher/CTest 등록만 추가 |
| 새 storage_limits — 엄격한 공유 Release, UV ON, LZ4/fixture/backtrace OFF | 실행 | 1개 통과(0.08초). /tmp/raftpp-storage-limits-release-{configure,build}.log, -release.log, -release-output.log | -Wall -Wextra -Wpedantic -Werror와 NDEBUG에서 실제 C reader/출력 보존·정상 경계를 확인 |
| 새 storage_limits — ASan/UBSan 정적 Debug, UV ON, LZ4/fixture/backtrace OFF | 실행 | 1개 통과(0.70초). /tmp/raftpp-storage-limits-asan-{configure,build}.log, -asan.log, -asan-output.log | malformed 입력·C bridge·공유 payload의 읽기/정리 검증. detect_leaks=0이며 누수 검사는 제외 |
| 변경 없는 기존 184개·추가 matrix/설치 소비자 | 생략 — 기존 결과 재사용 | 앞선 encoding/metadata/segment/복구/노드·네트워크·메모리 단위 및 직전 설치·패키지의 각 원래 기록 | 제품/공유 헤더·기능·의존성과 기존 검사 경로는 유지했다. 새 bridge/dispatcher는 새 suite에서만 호출된다. UV OFF는 기존 if(RAFTPP_WITH_UV) 안에서 소스와 CTest를 제외하며 새 구성 실행을 주장하지 않는다 |
| 실제 kernel/raw 장치·최초 TCP timeout·원본 C fixture/대기 snapshot 제한 | 미검증/미해결 유지 | 이전 환경·실패 기록 | 이번 읽기 경계 통과를 이 제한의 해결이나 전체 실행 경로 완료로 확대하지 않는다 |

명령은 세 build 디렉터리에 cmake -S . -B <dir>, cmake --build <dir> --target raftpp_persistence_tests --parallel 2를 실행해 종료 0을 확인한 뒤 ctest --test-dir <dir> --output-on-failure -R '^raftpp.storage_limits$'를 한 번씩 실행했다. <dir>은 build, /tmp/raftpp-recover-reference-release, /tmp/raftpp-recover-reference-asan이며 기존 옵션을 유지한다. ASan은 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1을 사용했다. stdout을 먼저 보존하고 --show-only=json-v1 결과를 /tmp/raftpp-storage-limits-{debug,release,asan}-list.json에 기록했다. 등록 수 185/146/146은 전체 통과 수가 아니다. 모두 storage boundaries: 29 C reader comparisons, 2 count-truncation differences, 2 payload-limit cases를 출력했다.

한·영 uv_io API에는 closed 파일명의 전체 범위 검증/자동 복구 관계를 설명했다. encoding API의 개수 상한 인코딩 오류를 too_big로 명확히 하고, 기존의 모호한 invalid 설명과 디코딩 count/잘린 입력의 오류 계약을 보완했다. API 문서에는 테스트 내역을 넣지 않는다. 이 단위는 기존 reader의 경계 검증 근거를 확장했고 전체 디스크/다른 계층의 모든 실행 경로 완료를 증명하지 않는다. 전체 목표와 기존 실패·환경 제한은 유지한다.

변경 문서 3쌍의 정적 확인은 REWRITTEN 제목/표 행 57/418, encoding API 7/0, uv_io API 7/25, 코드 예제·상대 링크 374개·API 경로 15쌍·다음 작업 1–5의 대응을 확인했다. /tmp/raftpp-storage-limits-doc-check.py는 세 구성의 실제 C reader 통과/stdout 보존도 확인한다. 최종 문서 보완에는 빌드/runtime을 반복하지 않았다.

### 노드 시작 중 재진입·명시 종료의 단계 중단 (2026-11-03)

직전 디스크 입력 경계 단위는 C 읽기 29회와 새 선택 검증을 추가해 진전했다. 이번에는 node.cpp의 guarded/start/admit/process/pump/begin_close와 I/O·FSM virtual 호출/소유 상태, 원본 legacy.c의 raft_start/요청/종료, node.hpp/io.hpp/state_machine.hpp와 기존 node_lifetime/restore/failure/allocation/errors를 참고했다. FSM snapshot_async는 원본도 동기 호출하므로 모델을 바꾸지 않았다. 실제 공백은 초기 load/restore 동안 core.started만 검사하면 재진입한 start가 또 로드·복원할 수 있고 bootstrap/recover도 시작 단계에 들어올 수 있다는 점이었다. load에서 노드를 닫은 뒤에도 FSM을 복원하거나 io.start에서 닫은 뒤 running을 다시 설정할 수 있는 흐름도 확인했다.

제품 수정 전 엄격한 공유 Release에서 새 node_startup_reentrancy/node_startup_close를 실행해 두 검사의 첫 사례가 실패했다. 중첩 시작은 outer=20/nested=0/loads=2/restores=2로 내부 시작이 성공하고 외부 시작은 invalid로 끝났다. load 중 종료는 status=9인데도 restores=1로 종료 후 복원을 호출했다. /tmp/raftpp-node-startup-before-{configure,build}.log, -before.log, -before-output.log를 보존하며 실패를 통과로 재사용하지 않는다. 재귀 hook은 한 번만 실행해 무한 재귀 대신 두 번째 호출까지만 관측했다.

src/node.cpp의 내부 상태에 starting을 추가해 첫 외부 호출 전에 설정하고 정상/예외 반환에서 해제한다. 종료 요청이 없는 진행 중인 start/bootstrap/recover는 busy이며, 이미 완료한 시작의 중복 start는 기존 invalid다. io.load/FSM restore/core 시작 처리/io.start 이후 closing을 확인해 명시 종료 시 shutdown을 반환하고 후속 단계를 멈춘다. 종료 후 applied/running을 변경하지 않는다. io.start 안에서 handling 때문에 미뤄진 종료 pump도 외부 시작이 돌아올 때 진행해 종료 완료 콜백과 소유 참조가 남지 않게 한다. 이미 시작된 명시 종료를 새 fatal start 오류로 기록하지 않으며 기존 실제 시작 실패의 종료/진단은 유지한다. 합의 상태 전이·파일/네트워크 형식과 raft/ 원본은 변경하지 않았다.

새 tests/node.cpp의 재진입 4사례는 load/restore/start 중 재진입 및 restore 안에서 facade 이동을 검증한다. 바깥 시작 성공, 안쪽 세 lifecycle 호출 busy, load/restore/start 각각 한 번, 영속 entries/term·configuration·commit/applied/last index 보존과 이후 정상 중복 시작/종료를 확인한다. 종료 6사례는 load/restore/start × 핸들 유지/파괴에서 shutdown, 단계 중단·구독 비활성·명시 종료 오류 없음·applied 보존·종료 콜백 1회를 확인한다. 실제 원본 C의 중첩 start는 logNumEntries/logSnapshotIndex/last_stored==0 사전조건을 깨므로 그 재진입을 runtime 비교하지 않았다. 원본의 정상 load→restore→START→io.start 순서와 기존 C 대조 기록은 유지하고 C++의 안전한 재진입/종료 계약을 검증했다.

검증 시점은 2026-11-03 05:33–05:35 KST, Linux x86_64·GNU 13.3.0·CMake 3.28.3·libuv 1.48.0이다. 모든 실제 빌드 종료 0을 확인한 뒤 해당 검사를 실행했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 수정 전 공유 Release 재현 | 실행 — 실패 보존 | 새 2개 모두 첫 사례에서 실패. /tmp/raftpp-node-startup-before-{configure,build}.log, -before.log, -before-output.log | 제품 수정 없이 중첩 load/restore와 종료 후 복원을 관측했다. 이후 제품 guard 수정으로만 재실행 |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 2개 + 기존 node_lifetime/failure/restore/allocation/errors 5개, 총 7개 통과. /tmp/raftpp-node-startup-debug-{configure,build}.log, -debug.log, -debug-output.log | 시작 admission/예외/진단·정상 복원과 종료/수명 영향 범위 |
| 엄격한 공유 Release·UV ON·LZ4/fixture/backtrace OFF | 실행 | 새 2개 + 기존 node_restore 1개, 총 3개 통과. -before-configure.log와 /tmp/raftpp-node-startup-release-build.log, -release.log, -release-output.log | -Wall -Wextra -Wpedantic -Werror/NDEBUG에서 수정 전 실패가 해결되고 정상 시작이 유지됨 |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | 새 2개 + 기존 node_lifetime/restore 2개, 총 4개 통과. /tmp/raftpp-node-startup-asan-{configure,build}.log, -asan.log, -asan-output.log | 시작 내부 핸들 이동·파괴와 완료/정리 수명. detect_leaks=0이며 누수 검사 제외 |
| 코어 전용 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 새 2개 통과. /tmp/raftpp-node-startup-core-{configure,build}.log, -core.log, -core-output.log | 새 admission/수명 처리는 UV·LZ4·fixture에 의존하지 않으며 기존 backtrace 구성에서도 빌드/실행 |
| 변경 없는 기존 180개·추가 matrix/소비자 | 생략 — 기존 결과 재사용 | 앞선 합의/파일/네트워크/fixture·직전 디스크 경계·설치/외부 소비자의 원래 범위별 기록 | 기존 기본 구성 185개 중 영향받는 5개만 재실행했다. 공개 선언/export/의존성·코어/형식은 유지되며 시작 guard의 새 경로만 추가했다. 생략 개수를 새 통과로 합산하지 않음 |
| 다른 I/O 훅의 재진입·기존 환경/실패 제한·전체 완료 | 미검증/미해결 유지 | 기존 TCP timeout·kernel/raw 장치·C fixture/대기 snapshot 제한 기록 | 시작 단계의 수정/통과를 set_term/truncate/append 등 모든 사용자 I/O 호출 재진입이나 전체 라이브러리 완료로 확대하지 않음 |

명령: cmake -S . -B <dir>, cmake --build <dir> --target raftpp_node_tests --parallel 2, ctest --test-dir <dir> --output-on-failure -R '<범위>'. <dir>은 build, /tmp/raftpp-recover-reference-release, /tmp/raftpp-recover-reference-asan, /tmp/raftpp-fixture-controls-release다. Debug 범위는 ^raftpp.node_(startup_reentrancy|startup_close|lifetime|failure|restore|allocation|errors)$, Release는 startup_reentrancy|startup_close|restore, ASan은 startup_reentrancy|startup_close|lifetime|restore, core는 ^raftpp.node_startup_(reentrancy|close)$다. ASan은 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1을 사용했다. 각 최종 stdout는 등록 조회 전에 별도 보존했다. /tmp/raftpp-node-startup-{debug,release,asan,core}-list.json의 187/148/148/86 등록 수는 전체 suite 통과 수가 아니다. 모두 startup reentry: 4 cases와 startup close: 6 cases를 출력했다.

한·영 node API에 시작 중 busy·완료 후 invalid의 구분, 시작 호출 안에서 close/핸들 파괴, 정상 반환 후 shutdown과 단계 중단·수명/종료 콜백 계약을 함께 설명했다. API 문서에는 실행 기록을 넣지 않았다. 이 시작 경계 단위는 구현과 선택 검증을 완료했으나 다른 사용자 호출 경계 감사와 기존 실패/환경 제한은 남아 있어 전체 목표는 진행 중이다.

변경 문서 2쌍의 정적 확인은 REWRITTEN 제목/표 행 58/427, node API 6/51, 코드 예제·상대 링크 378개·API 경로 15쌍·다음 작업 1–5의 대응을 확인했다. /tmp/raftpp-node-startup-doc-check.py는 최초 실패 2개와 네 구성의 최종 통과/stdout 보존도 확인한다. 최종 문서 보완에는 빌드/runtime을 반복하지 않았다.

### 실행 중 I/O·FSM에서 종료할 때 후속 작업 중단 (2026-11-03)

직전 노드 시작 재진입 단위는 실제 실패를 수정하고 선택 검증해 진전했다. 이번에는 node.cpp의 process/persist/launch/apply_committed/send/install/take_snapshot/pump/begin_close와 사용자 I/O/FSM의 호출·완료·소유 참조, 원본 legacy.c의 업데이트 처리 순서 및 기존 node/legacy 비교를 필요한 범위까지 대조했다. 실제 문제는 truncate가 정상 반환하기 전에 종료하면 여전히 append를 호출하고, FSM apply/restore에서 종료한 뒤 applied를 바꾸는 경로였다. 조회/term/vote/송신 단계도 같은 이벤트 안에서 다음 단계로 진행할 수 있었다.

제품 수정 전 엄격한 공유 Release의 새 node_io_close/node_fsm_close는 각각 첫 사례에서 실패했다. truncate hook에서 종료한 뒤 append에 진입해 status=9/after_close=1이 됐고, FSM apply hook에서 종료한 뒤 applied_before=1/applied_after=2가 됐다. /tmp/raftpp-node-event-close-before-{configure,build}.log, -before.log, -before-output.log를 보존한다. 원본 C++ 제품의 두 결함을 재현한 결과이며 이후 통과로 덮어쓰지 않는다.

src/node.cpp는 time 이후 capacity를 호출하기 전과 capacity 이후 core step/업데이트를 적용하기 전에 closing을 확인한다. term/vote/entries/snapshot/RPC/FSM/로컬 snapshot 단계의 후속 시작과 RPC 사이에서도 종료를 확인한다. persist는 truncate 이후 종료했으면 새 append를 제출하지 않고, 이미 admission에 들어온 클라이언트 요청은 기존 close 절차로 완료한다. FSM apply/restore에서 종료했다면 applied 진행을 멈춘다. 받아들인 작업의 완료·취소와 snapshot_finalize 같은 정리는 계속 pump로 처리한다. 명령이나 사용자 FSM 함수의 부작용을 rollback하지 않는다. 종료하지 않은 경로의 영속화·응답 순서와 합의 알고리즘/형식, raft/ 원본은 유지했다.

새 tests/node.cpp의 event_io/event_machine은 종료한 backend에서 새 작업 진입을 세고, 실제로 시작한 append/send는 기존 memory_io처럼 inline 또는 대기 큐로 완료/취소한다. 새 node_io_close는 truncate/time/capacity/term/vote/append/send × 핸들 유지/파괴 × inline/지연 완료의 28사례다. node_fsm_close는 실행 중 command apply/설치 snapshot restore × 핸들 유지/파괴 × inline/지연 완료의 8사례다. 각 hook은 한 번만 호출된다. 새 I/O/FSM 진입 0회, 종료 1회·구독/큐 정리, 명시 종료 오류 없음·applied 보존과 client completion→close completion 순서를 확인한다. 첫 vote send 안에서 닫으면 두 번째 RPC를 제출하지 않는다. pending I/O를 이미 받아들인 경우에도 종료 완료까지 필요한 소유권을 보존한다.

검증은 2026-11-03 05:46–05:49 KST, Linux x86_64·GNU 13.3.0·CMake 3.28.3·libuv 1.48.0에서 수행했다. 빌드가 각각 종료 0인 것을 확인한 뒤 검사를 실행했다. 새 hook의 명시적 종료는 C++ 수명 계약 검사이며 원본 C에 같은 비지원 재진입을 강제하지 않았다. 정상 barrier/membership은 기존 실제 C 비교를 다시 실행해 확인했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 수정 전 엄격한 공유 Release | 실행 — 실패 보존 | 새 2개 모두 첫 사례에서 실패. /tmp/raftpp-node-event-close-before-{configure,build}.log, -before.log, -before-output.log | 종료 후 append와 applied 변경을 관측했으며 이후 제품 guard 수정 뒤에만 재실행 |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 2개 + 기존 node_lifetime/failure/snapshot/cluster/restore 5개 + legacy_barrier_normal/legacy_membership_normal 2개, 총 9개 통과. /tmp/raftpp-node-event-close-debug-{configure,build}.log, -debug.log, -debug-output.log | 정상 이벤트/스냅샷/실패·수명/복원과 원본 C barrier·멤버십의 영속/응답 순서 대조 |
| 엄격한 공유 Release·UV ON·LZ4/fixture/backtrace OFF | 실행 | 새 2개 + 기존 node_cluster 1개, 총 3개 통과. -before-configure.log, /tmp/raftpp-node-event-close-release-build.log, -release.log, -release-output.log | -Wall -Wextra -Wpedantic -Werror/NDEBUG에서 수정 전 실패 해결과 정상 클러스터 확인 |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | 새 2개 + 기존 node_lifetime/snapshot/cluster/restore 4개, 총 6개 통과. /tmp/raftpp-node-event-close-asan-{configure,build}.log, -asan.log, -asan-output.log | 사용자 호출 내부 핸들 파괴·받아들인 작업의 완료/취소·snapshot 정리 수명. detect_leaks=0, 누수 검사 제외 |
| 코어 전용 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 새 2개 통과. /tmp/raftpp-node-event-close-core-{configure,build}.log, -core.log, -core-output.log | callback 드라이버의 변경은 UV·LZ4·fixture에 의존하지 않음 |
| 변경 없는 기존 180개·추가 matrix/소비자 | 생략 — 기존 결과 재사용 | 앞선 core/저장/네트워크/fixture·시작/수치/진단/설치·소비자의 원래 기록 | 기존 기본 구성 187개 중 영향받는 7개만 재실행했다. 공개 선언/export/기능·의존성과 core/형식은 유지했다. 종료하지 않은 경로의 조건은 기존과 같으며 생략을 새 통과에 합산하지 않음 |
| 기타 사용자 호출 경계·기존 실패/환경 제한·전체 완료 | 미검증/미해결 유지 | 최초 TCP timeout·실제 kernel/raw 장치·C fixture/대기 snapshot의 원래 제한 기록 | 실제 I/O의 별도 재진입 조합, 사용자 allocator/tracer/예외·다른 모든 경로 완료까지 이번 36사례로 확대하지 않음 |

명령은 네 기존 구성에 cmake -S . -B <dir>, cmake --build <dir> --target raftpp_node_tests --parallel 2 후 ctest --test-dir <dir> --output-on-failure -R '<범위>'다. Debug는 추가로 raftpp_legacy_normal_tests를 같은 build 명령의 target 목록에 포함했다. <dir>은 build, /tmp/raftpp-recover-reference-release, /tmp/raftpp-recover-reference-asan, /tmp/raftpp-fixture-controls-release다. Debug 범위는 ^raftpp.(node_(io_close|fsm_close|lifetime|failure|restore|cluster|snapshot)|legacy_(barrier|membership)_normal)$, Release는 ^raftpp.node_(io_close|fsm_close|cluster)$, ASan은 ^raftpp.node_(io_close|fsm_close|lifetime|restore|cluster|snapshot)$, core는 ^raftpp.node_(io_close|fsm_close)$다. ASan은 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1이다. 최종 stdout는 등록 조회 전에 보존했다. /tmp/raftpp-node-event-close-{debug,release,asan,core}-list.json의 등록 189/150/150/88개는 전체 통과 수가 아니다. 네 구성 모두 event I/O close: 28 cases와 event FSM close: 8 cases를 출력한다.

한·영 node API에 실행 중 사용자 호출에서 close/핸들 파괴, 새 작업 중단과 받아들인 작업·snapshot 정리의 지속, client의 leadership_lost 완료 및 close 순서, FSM 커서와 rollback 구분을 설명했다. API 문서에는 실행 기록을 넣지 않았다. 이 단위의 실제 결함 수정과 선택 검증은 완료했으나 전체 라이브러리 실행 경로 감사와 기존 환경/실패 제한은 남아 있으므로 전체 목표는 진행 중이다.

변경 문서 2쌍의 정적 확인은 REWRITTEN 제목/표 행 59/436, node API 6/51, 코드 예제·상대 링크 378개·API 경로 15쌍·다음 작업 1–5의 대응을 확인했다. /tmp/raftpp-node-event-close-doc-check.py는 최초 실패 2개와 네 구성의 최종 통과/36사례 stdout 보존도 확인한다. 최종 문서 보완에는 빌드/runtime을 반복하지 않았다.

### 공개 legacy.step_cb의 소유 step 알림 이식 (2026-11-03)

직전 실행 중 종료 단위는 두 결함을 수정하고 선택 검증해 진전했다. 이번에는 최초 TCP memory 시간 초과의 원래 로그 세 개가 현재 없고, 현재 memory_cluster/resource가 할당 실패를 주입하지 않음을 다시 확인했다. 원인이나 앞선 snapshot OOM/종료 수정과의 인과를 단정하지 않았고 변경 없는 TCP 검사를 재실행하지 않았다. 관련 공개 인터페이스 감사에서 raft.h.in의 RAFT__EXTENSIONS_LEGACY에 있는 실제 step_cb 필드와 legacyShouldFireStepCb/legacyHandleEvent에 C++ node 대응이 없음을 찾았다. 기존 114개 함수 목록은 함수 포인터 필드를 제외했으므로 이 누락을 검출하지 못했다. 이 필드는 reserved ABI 공간이 아니라 실제 사용자 알림이다. state_cb는 원본 주석의 이름일 뿐 별도 필드는 아니다.

include/raftpp/node.hpp와 src/node.cpp에 표준 function<void()>의 step_callback 별칭 및 set_step_callback을 추가했다. 등록한 callable을 표준 shared 소유로 보관하고 성공한 core step의 상태/요청 검사 뒤, 같은 step의 I/O/FSM 전에 호출한다. 원본의 완료 대기 NOSPACE apply 억제 조건을 보존한다. 기본 비활성 경로는 요청 큐를 추가 순회하지 않는다. 현재 호출은 교체/해제/노드 종료·파괴를 넘어 생존하고, 이후 콜백은 새 소유자를 사용한다. 이동 전용 캡처·일반 함수/포인터와 해제를 지원하며 등록 실패는 이전 콜백을 보존한다. 종료한 노드도 해제할 수 있다. 기존 이벤트 예외 경계와 진행 중 admission/종료 단계를 사용하고 코루틴/future·새 의존성·기능 옵션을 추가하지 않았다. 원본 raft/는 변경하지 않았다.

새 node_step_observer는 startup 알림, submit/persisted step의 commit/applied 시점과 I/O 전 호출, 재진입 busy, 이동 전용 캡처, 자기 교체 중 약한 참조의 생존/이후 해제, 등록 OOM 보존, 무할당 nullptr 해제, 이동한 원본의 shutdown, 종료 후 캡처 해제, observer 안의 close/핸들 파괴와 client→close 순서, 일반 예외의 io_error 종료 및 즉시 no_space 거부의 무알림을 확인한다. 완료 큐의 NOSPACE 억제는 원본 조건을 소스에서 보존했다. 현재 공개 경로는 해당 오류를 즉시 거부하므로 사적인 request 상태를 조작해 완료 큐 상태를 강제로 만들지는 않았다.

실제 C 비교는 tests/legacy_reference.c에서 원본 raft.legacy.step_cb를 설정하고 node ID/state/term/vote/last/commit/applied 7필드를 각 호출마다 기록한다. callback의 원본 raft->data는 이 테스트 컨텍스트를 보관한다. C++ node의 같은 관측 스트림과 기존 전체 상태/완료/영속 비교를 유지한 채 시작·선거·명령/barrier·거부·승격·추가/삭제를 비교했다. 일반 모드 97알림/106외부 이벤트, 강제 barrier 모드 112알림/125이벤트, 구성마다 합계 209알림이 원본과 일치한다. C++ 수명·재진입 확장 검사와 이 C 관측 대조는 서로 다른 범위다.

초기 새 3개는 /tmp/raftpp-step-observer-debug-initial.log에서 통과했고 C 관측은 startup 뒤부터 91/106알림이었다. 그 결과를 startup 검증으로 확대하지 않았다. 관측을 start 전으로 확장한 /tmp/raftpp-step-observer-startup-debug-{build,output}.log와 -startup-debug.log의 새 C 비교 2개가 97/112알림으로 통과했다. 이후 비활성 observer의 큐 순회를 생략하는 제품 수정 뒤 최종 Debug를 다시 빌드/링크해 관련 검사만 실행했다. 초기 결과는 따로 보존하고 최종 결과로 덮어쓰지 않았다.

검증은 2026-11-03 06:06–06:16 KST, Linux x86_64·GNU 13.3.0·CMake 3.28.3 환경이다. 각 빌드 종료 0을 확인한 뒤 검사를 실행했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 최종 Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 3개 + 기존 node_lifetime/failure/snapshot/restore와 legacy_barrier_normal/membership_normal 6개, 총 9개 통과. /tmp/raftpp-step-observer-debug-final-{configure,build,link}.log, -debug-final.log, -debug-final-output.log | 공개 알림·빈 observer 경로/수명·정상 C 호환 영향. 초기 configure/build/결과는 -debug-configure.log, -debug-build.log, -debug-initial.log/-output.log로 별도 보존 |
| 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 새 node_step_observer/legacy_step_notifications_normal/forced 3개 통과. /tmp/raftpp-step-observer-release-{configure,build}.log, -release.log, -release-output.log | -Wall -Wextra -Wpedantic -Werror/NDEBUG·libuv 없는 공개 node 알림과 실제 C 시점 대조 |
| ASan/UBSan 정적 Debug·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 같은 새 3개 통과. /tmp/raftpp-step-observer-asan-{configure,build}.log, -asan.log, -asan-output.log | 활성 callable·캡처·교체·파괴·C 관측 배열의 수명. detect_leaks=0이며 누수 검사는 제외 |
| 이동한 설치본의 독립 소비 — Debug 정적 UV/LZ4/fixture ON 및 위 공유 코어 Release | 실행 | 기존 package_install의 새 소비 동작이 구성별 1개씩 통과(7.46/7.02초). /tmp/raftpp-step-observer-package-{debug,release}.log, -<구성>-output.log, -<구성>-consumer-output.log | tests/package/main.cpp에서 fixture/node를 생성하고 새 setter를 실제 호출해 startup 알림이 0보다 큼을 검사한다. 헤더/API/link/export 변경의 외부 CXX-only 소비이며 C 라이브러리를 설치/링크하지 않음 |
| 새 node API 예제·문서 대응 | 실행 | /tmp/raftpp-step-observer-example.cpp를 C++20·-Wall -Wextra -Wpedantic -Werror -fsyntax-only로 컴파일해 통과. -example.log | 새로운 count_steps 예제는 node.hpp만 포함한다. 최종 문서만의 편집에는 runtime을 반복하지 않음 |
| 나머지 기존 182개·추가 matrix/소비 구성 | 생략 — 기존 결과 재사용 | 앞선 core/노드/네트워크/저장/fixture/설치의 원래 범위별 기록 | 기존 189개 중 일반 영향 6개와 새 API를 쓰도록 수정한 설치 소비 1개만 다시 실행했다. 나머지 C++ 알고리즘·형식/기능·의존성은 유지한다. 생략을 새 통과에 합산하지 않음 |
| 최초 TCP timeout·실제 kernel/raw 장치·C fixture/대기 snapshot 제한 | 미검증/미해결 유지 | 이전 실패/환경 제한 기록 | 로그 부재를 통과나 원인 해결로 해석하지 않는다. 새 알림 구현/검사를 전체 원본 실행 경로 완료로 확대하지 않음 |

명령은 `cmake -S . -B <dir>`, `cmake --build <dir> --target raftpp_node_tests raftpp_legacy_normal_tests raftpp_legacy_forced_tests --parallel 2`, `ctest --test-dir <dir> --output-on-failure -R '<범위>'`다. <dir>은 build, /tmp/raftpp-fixture-controls-release, /tmp/raftpp-fixture-controls-asan이다. Debug 최종 범위는 ^raftpp.(node_(step_observer|lifetime|snapshot|restore|failure)|legacy_(step_notifications_(normal|forced)|barrier_normal|membership_normal))$이고 다른 두 구성은 ^raftpp.(node_step_observer|legacy_step_notifications_(normal|forced))$다. ASan은 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1을 사용했다. Debug와 공유 Release에서 ^raftpp.package_install$를 각각 실행했으며 install/rename/독립 configure/build/run은 기존 driver를 따른다. stdout와 안쪽 소비 stdout를 등록 조회 전에 보존했다. /tmp/raftpp-step-observer-{debug,release,asan}-list.json의 등록 192/91/91개는 전체 suite 통과 수가 아니다.

한·영 AGENTS에 공개 함수뿐 아니라 공개 구조체의 콜백/설정/사용자 컨텍스트를 감사하는 원칙을 추가했다. 이 문서의 C 인터페이스 대응 목록도 이 보완을 구분한다. 한·영 node API에는 시점·소유·재등록·오류·종료와 예제를 설명하고 작업 내역은 넣지 않았다. 이 알림 계층의 구현/선택 검증은 완료했으며 전체 이식의 다른 계약 감사와 기존 환경·실패 제한은 남아 있다.

변경 문서 3쌍의 정적 확인은 AGENTS 제목/표 행 12/28, REWRITTEN 60/445, node API 7/51, 번역 주석 제외 코드 예제·상대 링크 382개·API 경로 15쌍·다음 작업 1–5 대응을 확인했다. /tmp/raftpp-step-observer-doc-check.py는 세 구성의 209알림/C 관측과 두 설치 소비 결과·예제 보존도 확인한다. 예제는 c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -I/repo/raftpp/include -I/repo/raftpp/build/include /tmp/raftpp-step-observer-example.cpp로 검증했다. 최종 문서 보완에는 빌드/runtime을 반복하지 않았다.

### Native TCP 클러스터의 리더 선택 사전조건 보완 (2026-11-03)

직전 step 알림 단위는 누락된 공개 기능을 이식하고 실제 C 관측/소비자를 검증해 진전했다. 이번에는 시작/복원과 멤버십 계약을 검토했다. core.cpp의 마지막 두 configuration 복원, membership.cpp의 rollback/역할과 client.cpp·replication.cpp의 quorum을 원본 RestoreEntries/RestoreSnapshot·client/membership/legacy의 해당 입력/출력에 맞춰 읽었다. 이전 설정을 committed configuration으로 보관하면서 commit_index를 거기까지 올리지 않는 원본의 의도적 처리도 보존되어 있었다. 이 읽기 대조를 새 runtime 통과나 모든 복원/멤버십 경로 증명으로 보고하지 않았고 제품 코드는 변경하지 않았다.

tests/tcp_transport.cpp의 실제 디스크·TCP cluster를 검토하면서 테스트의 사전조건 누락을 확인했다. 모든 FSM의 명령 count와 snapshot 완료 인덱스가 원하는 값이어도 현재 리더가 있다는 보장은 없다. 기존 lagger/transfer 선택은 그 대기 뒤 leader()의 실패 sentinel 3을 nodes 배열 인덱스로 사용할 수 있었다. 이는 소스에서 확인한 허용 상태의 잘못된 접근 경로이며, 수정 전 그 상태를 강제로 실행해 오류를 재현하거나 최초 TCP timeout의 원인으로 단정하지 않았다.

새 await_leader는 healthy 조건과 실제 리더 존재를 기존 until로 기다리고 인덱스 범위를 확인해 반환한다. 명령 제출·중지할 follower 선택·리더십 이전에서 이 결과를 사용하며 transfer는 한 번 선택한 출발 리더와 살아 있는 대상 인덱스를 검사한다. source_location을 원래 호출 위치에서 전달해 대기 실패 진단을 유지한다. 12초 대기 한도·45초 CTest 제한·명령 수 26·양쪽 snapshot >=20·추적/resource 해제 성공 조건은 유지했다. C++ 라이브러리·공개 API/헤더·옵션·원본 raft/는 변경하지 않았다.

관련 cluster helper를 공유하는 기존 tcp_cluster/tcp_tracing/tcp_memory만 구성별 한 번씩 검증했다. 실행은 2026-11-03 06:50–06:53 KST의 Linux x86_64·GNU 13.3.0·CMake 3.28.3·libuv 1.48.0 환경이다. 같은 호스트의 포트·디스크·스케줄 부하가 간섭하지 않도록 세 runtime 구성은 순차 실행했다. 빌드는 먼저 각각 종료 0을 확인했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| configuration 복원·멤버십 소스 대조 | 실행 — 읽기 확인 | core.cpp/membership.cpp/client.cpp/replication.cpp와 raft/src/restore.c/client.c/membership.c/legacy.c의 관련 경로 | 마지막/이전 구성 및 기존 commit 처리 선택을 확인했다. 구현·검증 완료를 새로 주장하지 않으며 해당 영역의 기존 테스트 결과는 재사용 |
| 기본 sandbox Debug TCP | 실행 — 환경 실패 보존 | /tmp/raftpp-cluster-leader-debug.log, -debug-sandbox-output.log: 3개 모두 socket 생성 위치에서 실패 | 클러스터 본문 이전의 sandbox socket 제한. 제품/리더 선택 실패로 해석하지 않는다. 같은 입력은 로컬 TCP가 허용된 실행으로 확인 |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF, 허용된 로컬 TCP 실행 | 실행 | tcp_cluster/tracing/memory 3개 통과(3.22/3.01/3.72초). /tmp/raftpp-cluster-leader-debug-{configure,build}.log, -debug-native.log, -debug-native-output.log | 실제 디스크/TCP의 선거·26명령·리더십 이전·snapshot 따라잡기·재시작, tracing/native metric 및 resource 해제 |
| 엄격한 공유 Release·UV ON·LZ4/fixture/backtrace OFF, 허용된 로컬 TCP 실행 | 실행 | 같은 3개 통과(3.46/3.41/3.35초). /tmp/raftpp-cluster-leader-release-{configure,build}.log, -release-native.log, -release-native-output.log | C++ -Wall -Wextra -Wpedantic -Werror/NDEBUG에서도 범위 검사를 실행하며 같은 정상 흐름 유지 |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF, 허용된 로컬 TCP 실행 | 실행 | 같은 3개 통과(3.75/3.36/3.18초). /tmp/raftpp-cluster-leader-asan-{configure,build}.log, -asan-native.log, -asan-native-output.log | 실제 native 요청/핸들·allocator/resource의 완료/정리. detect_leaks=0이며 누수 검사 제외 |
| 변경 없는 기존 189개·추가 matrix/소비자/문서 예제 | 생략 — 기존 결과 재사용 | 이전 전체 범위별 기록과 직전 step 알림/설치 소비자 검증 | 기존 192개 중 이 helper를 사용하는 3개만 입력이 바뀌었다. 제품·공개 헤더/export·형식/기능·의존성은 유지했으며 API 설명서는 변경하지 않음 |
| 최초 TCP memory timeout·kernel/raw 장치·C fixture/대기 snapshot 제한 | 미검증/미해결 유지 | 원래 native 메모리 단위의 실패와 후속 별도 통과, 환경/원본 C 제한 기록 | 이번 수정된 검사 3.18초 통과가 최초 실패의 원인/해결 증명은 아니다. 원래 로그가 없어 실패 단계와 sentinel 3 접근 여부는 확인할 수 없음 |

명령은 세 build 디렉터리에서 `cmake -S . -B <dir>`, `cmake --build <dir> --target raftpp_tcp_tests --parallel 2` 후 `ctest --test-dir <dir> --output-on-failure -R '^raftpp.tcp_(cluster|tracing|memory)$'`다. <dir>은 build, /tmp/raftpp-recover-reference-release, /tmp/raftpp-recover-reference-asan이다. 기본 sandbox의 socket 실패 이후 local-only TCP 실행을 허용받아 각 구성의 본 검사를 실행했다. ASan은 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1이다. 위 native stdout를 따로 보존했으며 현재 테스트 등록을 바꾸거나 반복 until-fail을 실행하지 않았다. ASan tcp_memory의 과거 실패 결과는 그대로 남겨 새 통과와 구분한다.

이 단위는 테스트의 리더 선택 안전성을 보완했고 관련 실제 native 흐름을 확인했다. 전체 기능/계약 감사와 기존 미확정 원인·외부 환경/참조 제한은 유지하며 전체 목표는 진행 중이다. 이번 수정은 테스트와 루트 진행 기록뿐이므로 API 전용 docs를 변경하거나 작업 내역을 넣지 않았다.

한·영 진행 기록의 정적 확인은 제목/표 행 61/454, 번역 주석 제외 코드 예제·상대 링크 350개·다음 작업 1–5 대응을 확인했다. /tmp/raftpp-cluster-leader-doc-check.py는 리더 대기/선택·기존 12초 한도/snapshot 조건 및 sandbox 실패와 세 native 구성의 원래 stdout 보존을 확인한다. 최종 기록 보완에는 빌드/runtime을 반복하지 않았다.

### 전체 소스 대응 감사·압축 snapshot의 native TCP 전달 (2026-11-03)

직전 현황 보고 턴은 상태만 설명했으므로 no progress로 분류했다. 이번에는 원본 Makefile.am/CMake의 C 소스 목록, 실제 C++ 타깃과 common.hpp의 표준 별칭, 공개 함수·구조체 대응 기록과 기능 옵션 표를 대조했다. 원본 51개 소스를 아래 10개 기능군에 중복/누락 없이 배치했다. 표는 소스·연결 대응의 감사 결과이며 파일 수만으로 전체 동작이 완료되었다고 판정하지 않는다. 공개 함수 114개와 별개인 step_cb 같은 기능성 구조체 필드도 감사 범위에 유지한다.

| 기능군 | 원본 raft/src C 소스 | 독립 C++ 대응 | 기능 검증의 기존 근거 또는 이번 보완 |
| --- | --- | --- | --- |
| 공통·표준 대체 (7) | byte, compress, configuration, err, heap, random, tracing | detail/byte.hpp, checksum, compression/compression_lz4, configuration, error, memory/buffer, random, tracing | buffer/configuration/compression, error_messages/context, memory_*, tracing_*, C 난수/체크섬 비교의 기존 기록 |
| 저장 값·배치·term 추적 (4) | entry, log, snapshot, trail | entry/log/snapshot/trail | log/log_lifetime, 공유 배치·스냅샷·trail의 기존 기록 |
| 이벤트 합의·수신 (18) | client, convert, election, membership, message, progress, raft, recv, recv_append_entries, recv_append_entries_result, recv_install_snapshot, recv_request_vote, recv_request_vote_result, recv_timeout_now, replication, restore, state, timeout | core/core_api, client, convert, election, membership, message, progress, receive/receive_core, replication, timeout | core_election/replication/conflict/membership/snapshot/transfer/cluster, C term·조회 비교와 수치/할당 실패 기록 |
| 콜백 드라이버 (1) | legacy | node, io/state_machine 인터페이스 | node_* 및 legacy_* 기록; 시작/실행 중 종료·요청 수명·step 알림 포함 |
| 파일·복구·native 경계 (6) | syscall, uv_fs, uv_os, uv_list, uv_metadata, uv_segment | file, disk, probe, metadata 및 writer의 native 호출 | metadata/snapshot/segment·storage_recover_reference/storage_limits/file_allocation_reference/probe_cleanup_reference 기록 |
| 비동기 로그 쓰기 (5) | uv_prepare, uv_append, uv_finalize, uv_truncate, uv_writer | probe, segment_buffer, writer, log_writer | writer_*/log_* 및 실제 C 파일·barrier·진단·정리 기록 |
| 파일 backend·스냅샷 조정 (2) | uv, uv_snapshot | file_backend, uv_io, disk | file_snapshot_*/uv_io_*와 이번 compressed_transfer |
| Wire 코덱 (1) | uv_encoding | encoding, checksum, detail/byte.hpp | 6종 RPC/C 바이트 비교와 encoding_malformed/failure/limits 기록 |
| 네트워크·raw stream (6) | uv_ip, uv_tcp, uv_tcp_connect, uv_tcp_listen, uv_recv, uv_send | tcp_transport, uv_stream, stream_transport | tcp_*/stream_*/adapter_*의 C 상호 운용·실제 파일/TCP/pipe/TTY 기록과 이번 compressed_transfer |
| Fixture (1) | fixture | fixture | fixture_*의 C 이벤트·네트워크/디스크·멤버십/주입 비교 기록 |

대응 C++ 이름은 별도 표시한 헤더를 제외하면 src/*.cpp를 뜻한다. 소스 목록 대조 결과는 /tmp/raftpp-compressed-transfer-source-groups.txt에 보존했다. 현재 build/libraftpp.a의 nm -u 결과에서 raft_* 미정의 참조는 0개다. 이번 새 실행 파일도 네 구성 모두 C 기준 라이브러리·직접 LZ4 test 링크 없이 raftpp::raftpp에만 연결된다. 이 독립성 확인은 알고리즘의 의미 비교를 대신하지 않는다.

이번에 닫은 구체적 검증 공백은 기능 옵션 표에 남아 있던 압축 snapshot의 실제 전송 통합이다. src/disk.cpp의 load_snapshot/해제, file_backend의 worker snapshot_get, node의 needs_data 조회→소유 payload 전송과 수신 설치→snapshot_put, TCP framing 및 노드 복구 경로를 추적했다. 제품 소스·공개 시그니처·형식·기준 raft/는 수정하지 않았다. tests/compressed_transfer.cpp와 UV 조건부 타깃/CTest 한 개를 추가했다.

고정 입력은 기존 liblz4의 LZ4F_compressFrame(default preferences)으로 생성한 46바이트 frame이다. 내용은 little-endian count=41과 4088개 0xa5인 4096바이트다. fixture 바이트를 소스에 보관하므로 LZ4 OFF 검사도 같은 유효 입력을 쓰며 테스트용 압축기 의존성을 요구하지 않는다. 두 디스크 노드에 term=3/index=20/configuration_index=1의 이 snapshot을 저장하고, 3-voter 설정의 과반을 시작해 첫 명령으로 count=42를 만든다. index 1 bootstrap만 있는 세 번째 노드를 늦게 시작해 실제 TCP snapshot 설치와 명령 catch-up을 확인한다. 수신 측 index=20 snapshot의 실제 파일 4096바이트가 해제 payload와 정확히 일치하고, 전체 노드 재시작·두 번째 명령 뒤 count=43이 일치한다. 송신 측 두 원본 압축 파일은 종료/재시작 뒤에도 46바이트가 모두 보존된다. LZ4 OFF에서는 두 시작이 invalid이며 FSM restore 0회, 종료·원본 보존을 확인한다.

최초 Debug/공유 Release ON/OFF 검사는 통과했지만 수신 파일 바이트를 직접 비교하기 전이었다. 해당 /tmp/raftpp-compressed-transfer-{debug,release-on,release-off}-initial.log를 보존했다. 조회가 해제한 반환 버퍼만으로 디스크의 실제 비압축 저장을 증명하지 않도록 파일 바이트 비교를 추가한 뒤, 바뀐 검사 한 개만 다시 빌드/실행했다. ASan 구성은 최종 입력으로 처음 실행했다. 이는 제품 오류 수정이나 최초 TCP timeout 원인의 입증이 아니다.

검증 시점은 2026-11-03 07:11–07:19 KST, Linux/GCC·G++ 13.3/C++20, libuv 1.48.0/LZ4 1.9.4다. 각 타깃 빌드 exit 0 뒤 아래 실행을 구성별로 순서대로 수행했다. 기존 sandbox의 로컬 소켓 차단 근거에 따라 승인된 loopback 실행을 사용했고 같은 sandbox 실패를 다시 실행하지 않았다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 compressed_transfer 1개 통과 3.33초. build, /tmp/raftpp-compressed-transfer-debug.log | 실제 파일 해제→TCP 설치/수신 파일 바이트→전체 재시작/두 명령과 원본 보존 |
| 엄격한 공유 Release·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 1개 통과 3.75초. /tmp/raftpp-options-build, -release-on.log | -Wall -Wextra -Wpedantic -Werror/NDEBUG/공유 라이브러리의 동일 경로 |
| 엄격한 공유 Release·UV ON·LZ4/fixture/backtrace OFF | 실행 | 새 1개 통과 0.09초. /tmp/raftpp-recover-reference-release, -release-off.log | 두 invalid 시작·FSM 미호출·종료와 원본 보존; TCP catch-up은 이 비활성 구성에서 실행하지 않음 |
| ASan/UBSan 정적 Debug·UV/LZ4 ON·fixture/backtrace OFF | 실행 | 새 1개 통과 3.47초. /tmp/raftpp-compressed-transfer-asan-on, -asan-on.log | 실제 해제/worker/전송·설치·재시작 수명. detect_leaks=0으로 누수 검사 제외 |
| 기존 192개·추가 matrix/외부 소비자/기존 C 파일·wire 대조 | 생략 — 기존 결과 재사용 | 앞선 기능군별 기록, 설치 package·step 알림·native 클러스터 리더 선택 단위 | 제품·공개 헤더/export/링크/의존성/옵션 의미를 바꾸지 않았고 새 UV 전용 검사만 추가. 기존 실패/미검증은 통과로 재사용하지 않음 |
| 전체 기능 의미 대응·실제 kernel/raw·최초 TCP timeout/C 참조 제한 | 미검증 | 위 구조 대응과 기존 완료 요건/실패·환경 기록 | 51개 소스나 새 통합 검사로 전체 의미 대응을 입증하지 않음. 최초 실패 원인/원본 C 결함/하드웨어 제약은 별도 유지 |

실행 명령은 cmake -S . -B <dir>, cmake --build <dir> --target raftpp_compressed_transfer_tests --parallel <n>, ctest --test-dir <dir> --output-on-failure -V -R '^raftpp.compressed_transfer$' --output-log <log>다. 최초 빌드는 --parallel 2 또는 3, 최종 검사 파일 재빌드는 1을 사용했다. ASan 새 구성은 C/CXX에 -fsanitize=address,undefined -fno-omit-frame-pointer를 지정하고 ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1로 실행했다. configure/초기 build/final-build 로그와 /tmp/raftpp-compressed-transfer-evidence.json의 소스 hash·구성·등록/독립 링크 근거를 보존했다. CTest 목록 명령 대신 생성된 tests/CTestTestfile.cmake를 읽어 기존 실행 로그를 보존했다. 등록 수 193/174/152/152는 전체 suite 통과 수가 아니다.

현행 기능 옵션 표의 오래된 오류 문맥/fixture-예제 연결 TODO·170개 등록 수를 현재 구현과 독립 조건으로 정정하고 누락된 INSTALL 행을 추가했다. 과거 단위의 당시 기록은 덮어쓰지 않았다. 한·영 uv_io API에는 디스크 해제→전송 payload→그대로 저장·송신 원본 보존과 LZ4 OFF의 파일 읽기 계약만 설명했다. 다음 전체 감사의 유한 단위는 위 10개 기능군의 원본 테스트별 동작/오류 계약과 C++ 검증 근거 대응이며, 대응이 없는 항목을 구체적인 남은 요건으로 확정해야 한다. 이번은 그 전체 의미 감사 완료 선언이 아니다.

최종 정적 확인: /tmp/raftpp-compressed-transfer-doc-check.py/.log에서 두 언어 REWRITTEN의 제목/표 행 62/475, uv_io API 7/25, API 경로 15쌍·상대 링크 366개·다음 작업 1–5를 확인했다. 실제 원본 소스 집합 51개와 기능군 집합이 일치하고, 최종 검증 입력/구현·빌드 관련 hash 97개가 바뀌지 않았음을 확인했다. 이 문서/기록 정리에는 추가 빌드·runtime을 실행하지 않았다.

### 원본 테스트 계약 목록·fuzzy 이식과 늦은 pre-vote 응답 (2026-11-03)

직전 압축 snapshot 통합 단위는 실제 새 검증을 추가한 progress였다. 이번에는 원본 unit/integration/fuzzy의 test_*.c 43개 파일에서 TEST 정의 494개를 추출해 /tmp/raftpp-contract-catalog.json에 파일·suite·case·행을 보존했다. 매개변수 조합과 조건부 구성을 펼친 실행 수가 아니라 소스 정의 수다. 아래 목록은 각 파일의 기존 C++ 근거 위치를 빠짐없이 지정한다. CTest 이름을 대응시킨 것만으로 모든 원본 사례의 검증 완료를 뜻하지 않는다. fuzzy 4개 파일/12개 시나리오는 이번에 이식·선택 검증했으며, 나머지 39개 파일/482개 정의는 기존 구현·검증을 이용한 사례별 계약 감사 대상이다. 이는 482개 기능이 미구현이라는 뜻이 아니다.

| 원본 raft/test/ 파일 | TEST 정의 | 기존 근거 위치 또는 이번 선택 검사 (raftpp. 접두사) |
| --- | --- | --- |
| `fuzzy/test_election.c` | 4 | fuzzy_election |
| `fuzzy/test_liveness.c` | 1 | fuzzy_liveness |
| `fuzzy/test_membership.c` | 3 | fuzzy_membership |
| `fuzzy/test_replication.c` | 4 | fuzzy_replication |
| `integration/test_catch_up.c` | 1 | core_membership, fixture_membership_reference |
| `integration/test_digest.c` | 1 | checksums |
| `integration/test_election.c` | 20 | election, vote_request, core_election, core_cluster, core_stale_vote |
| `integration/test_fixture.c` | 10 | fixture_reference, fixture_ordering, fixture_election |
| `integration/test_heap.c` | 4 | buffer, memory_alignment, memory_failure |
| `integration/test_init.c` | 2 | node_lifetime, node_allocation |
| `integration/test_legacy.c` | 11 | legacy_barrier_normal, legacy_barrier_forced, legacy_membership_normal, node_snapshot |
| `integration/test_membership.c` | 8 | core_membership, legacy_membership_normal, fixture_membership_reference |
| `integration/test_replication.c` | 31 | core_replication, core_conflict, core_cluster, node_failure |
| `integration/test_snapshot.c` | 7 | core_snapshot, node_snapshot, file_snapshot_order |
| `integration/test_start.c` | 8 | node_restore, node_startup_reentrancy, node_startup_close |
| `integration/test_strerror.c` | 1 | error_messages |
| `integration/test_submit.c` | 1 | uv_io_capacity_admission, core_replication |
| `integration/test_tick.c` | 5 | core_election, core_cluster, core_clock_limits |
| `integration/test_transfer.c` | 8 | core_transfer, legacy_transfer_normal, legacy_transfer_cpp_normal |
| `integration/test_uv_append.c` | 37 | log_append, log_barrier, log_close, log_failure, native_node |
| `integration/test_uv_bootstrap.c` | 2 | metadata_files, segment_snapshot |
| `integration/test_uv_init.c` | 10 | metadata_files, uv_io_lifetime, disk_failures |
| `integration/test_uv_load.c` | 58 | segment_recovery, segment_snapshot, segment_failures, storage_limits |
| `integration/test_uv_recover.c` | 1 | storage_recover_reference |
| `integration/test_uv_recv.c` | 13 | tcp_exchange, tcp_fragments, tcp_malformed, tcp_shutdown |
| `integration/test_uv_send.c` | 19 | tcp_exchange, tcp_retry, tcp_allocation, tcp_failure, tcp_shutdown |
| `integration/test_uv_set_term.c` | 7 | metadata_files, disk_metadata_diagnostics |
| `integration/test_uv_snapshot_put.c` | 9 | file_snapshots, file_snapshot_order, file_snapshot_failure, file_snapshot_close |
| `integration/test_uv_tcp_connect.c` | 12 | tcp_retry, tcp_settings, tcp_shutdown, stream_cancellation |
| `integration/test_uv_tcp_listen.c` | 14 | tcp_lifetime, tcp_malformed, tcp_allocation, tcp_interop |
| `integration/test_uv_truncate.c` | 7 | segment_files, log_barrier, log_close, log_failure |
| `integration/test_uv_truncate_snapshot.c` | 1 | file_snapshot_order, segment_snapshot |
| `unit/test_byte.c` | 11 | checksums, wire, records, encoding_malformed |
| `unit/test_compress.c` | 6 | compression, compressed_snapshot, compressed_transfer |
| `unit/test_configuration.c` | 31 | configuration |
| `unit/test_err.c` | 6 | error_messages, error_context |
| `unit/test_log.c` | 54 | log, log_lifetime, entries, shared_buffer |
| `unit/test_queue.c` | 13 | memory_containers, writer_close, log_close, node_lifetime |
| `unit/test_random.c` | 3 | random, public_settings |
| `unit/test_trail.c` | 10 | trail |
| `unit/test_uv_fs.c` | 22 | disk_probe, file_allocation_reference, probe_cleanup_reference, disk_failures |
| `unit/test_uv_os.c` | 6 | metadata_files, snapshot_files, disk_failures |
| `unit/test_uv_writer.c` | 12 | writer_basic, writer_async, writer_close, writer_failure, writer_allocation |

테스트 큐 자체의 intrusive ABI는 표준 list/deque·소유 요청으로 대체하므로 queue 매크로를 다시 구현하지 않는다. heap 원시 ABI도 표준 PMR/버퍼의 소유·resize·정렬 계약으로 대응한다. 위 나머지 목록의 개별 계약은 표준 대체, 같은 동작의 실제 검사, 원본/환경 제한을 구분해 감사해야 한다. 이번에 참조한 범위는 원본 fuzzy 네 파일과 lib/legacy의 randomize/kill/add/assign/진행 helper, fixture·node·합의 수신/전환 및 원본 RequestVoteResult·공개 union 배치까지다. 실제 수정은 tests/fuzzy.cpp/fuzzy_reference.c/stale_vote.cpp, 테스트 CMake, src/receive_core.cpp와 한·영 core/node API 및 이 기록뿐이다. raft/ 원본은 유지했다.

fuzzy 이식은 85213/70011/0x3914306f 세 고정 시드를 쓴다. election은 3/4/5/7노드×pre_vote OFF/ON×네 시나리오, replication은 3/5/7노드×네 시나리오, membership은 3/4/5노드×추가 standby/추가 voter/리더 자기 제거, liveness는 3/4노드×pre_vote OFF/ON이다. 원본처럼 disk latency=10을 유지해 완료 순서를 보존하고, network latency 25–50/선거 timeout 1000–2000을 바꾼다. 단절은 노드 쌍마다 10% 확률/50–150 이벤트 유지/최대 25000 이벤트다. 새 타깃은 FIXTURE에만 의존하고 UV와 독립이다. 결과는 다음과 같다. 이벤트와 C 비교는 구성별 같은 입력에서 각각 일치했으며, 구성 간 단순 합산을 새 사례 수로 보고하지 않는다.

| 시나리오군 | 선택 사례 | C++ fixture 이벤트 | 유효 C 이벤트 비교 | C 비교 중단 사례 |
| --- | --- | --- | --- | --- |
| election | 96 | 61209 | 50366 | 60 |
| replication | 36 | 20901 | 2836 | 36 |
| membership | 27 | 6230 | 2135 | 25 |
| liveness | 12 | 302737 | 149350 | 12 |
| 합계 | 171 | 391077 | 204687 | 133 |

C 비교는 이벤트 index/type/time과 각 노드의 state/term/last_index/commit/applied/FSM 및 stable leader를 확인한다. C++의 요청 최대 한 번 완료·minority의 선거/커밋 금지·리더 상실/오래된 노드 회복·멤버십 전파·자기 제거 뒤 새 커밋도 검증한다. oracle은 테스트 전용 C adapter였다. oracle 중단 뒤의 C++ 이벤트를 C 비교 통과로 합산하지 않았다.

초기 실험에서 세 가지 테스트 전제 문제를 확인했다. 선거 timeout을 고정하면 원본 C도 n=4/seed=0x3914306f에서 term 19까지 split vote가 반복됐다. 원본처럼 tick의 timer-reset 지점에서 timeout을 재설정하고, C 비교가 끝난 이후에는 같은 유효 범위의 tick 갱신으로 독립 C++ 실행을 계속한다. 연결 복구 직후 cached leader로 낸 명령은 양쪽 모두 leadership_lost가 가능하므로 링크 변경을 관찰하고 모든 살아 있는 노드의 term/역할이 정렬될 때까지 기다린다. 원본 partitioned의 리더-과반 간 edge 차단은 다른 피어 간 통신을 허용한다. 실제 C도 n=7/seed=0x3914306f에서 후임 리더가 접수된 이전 명령을 커밋했으므로, 명령 철회를 가정하는 전역 count=0 대신 기존 term의 minority 리더가 자체 커밋하지 않음/한 명령 최대 한 번 적용/복구 후 새 커밋을 확인한다.

liveness 원본의 마지막 progress assertion은 주석 처리되어 있다. 초기 추가 실험의 전체 노드 10000ms 이내 적용 조건은 C/C++ 모두 실패했다. 예를 들어 n=3/pre_vote ON/seed=85213에서 두 노드는 count=129, 늦은 노드는 count=118이었다. 이를 원본보다 강한 완료 계약으로 간주하지 않는다. 최종 검사는 단절 종료 후 실제 새 명령의 성공과 과반의 동일 적용을 요구한다. 선택한 12개 일정 모두 단절 중 진행은 관찰되지 않았으며, 단절 중 가용성이나 모든 follower의 제한 시간 내 수렴까지 검증했다고 주장하지 않는다. 이 과거 전체 수렴 실패도 별도 제한으로 보존한다.

실제 C 비교에서 추가로 발견한 기준 구현 제한: recv_request_vote_result.c는 상태를 검사하기 전에 candidate_state.in_pre_vote를 읽는다. 현재 x86-64 배치는 해당 bool offset=185, follower.current_leader.address offset=184/크기=8이므로 follower에서는 리더 주소의 두 번째 바이트를 읽게 된다. 이는 이전 선거 단계가 보존된 의미 있는 상태가 아니다. 초기의 그 해석을 폐기했고 비활성 union 바이트를 C++ variant 밖의 가짜 프로토콜 상태로 복제하지 않았다. 초기 무보호 C 실행은 현상 조사 기록이며 완료 근거로 재사용하지 않는다. 최종 adapter는 각 C io의 start/receive 콜백을 보관해, candidate가 아닌 상태의 VoteResult를 원본 handler에 전달하기 전에 비교 불가로 표시한다. 해당 이벤트를 비교 수에서 제외하고 C fixture를 정리한 뒤 C++ 실행만 계속한다. 이 133개 중단은 원본 C의 해당 분기나 전체 C 동작을 검증한 통과가 아니다.

C++ 자체에서도 늦은 version>=2/pre_vote=true 응답의 제안 term을 follower/leader가 실제 term으로 영속화하려는 문제를 작은 독립 검사로 확인했다. 수정 전 공유 Release core_stale_vote는 before=2/after=3으로 실패했다. receive_core.cpp에서 candidate가 아닌 노드의 이 응답을 먼저 무시하도록 수정했다. active candidate의 기존 필터/집계와 실제 투표·version 1의 더 높은 term 갱신은 유지한다. 회귀 검사는 초기 follower/후보에서 전환한 follower/단일 voter leader×version 1/2×pre_vote false/true×낮은/높은 term의 24개다. term/vote/state·영속화 플래그·로그/commit/메시지 불변을 확인하며 C의 비활성 union 분기를 oracle로 사용하지 않는다. 별도 union 복제 필드나 새 공개 시그니처는 추가하지 않았다.

검증 환경은 2026-11-03 07:23–07:51 KST, Linux/GCC·G++ 13.3/C++20이다. 새 C++ 검사는 Release에서도 check를 실행한다. 최종 세 구성의 각 타깃 빌드 exit 0을 확인하고 실행했으며, ASan은 C/CXX 모두 계측하고 detect_leaks=0으로 누수 검사를 제외했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 수정 전 작은 공유 Release 회귀 | 실행 — 실패 | core_stale_vote의 2→3 term 변경. /tmp/raftpp-stale-vote-before-{configure,build}.log, -before.log | C의 비활성 union 결과와 분리한 독립 C++ 재현 |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 5개/기존 core_election/transfer/cluster/term_reference 4개 통과, 총 9개. /tmp/raftpp-fuzzy-debug-guarded.log | 171 시나리오·유효 C 비교·24 회귀와 기존 pre-vote/다중 선거·인계·최대 term |
| 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 같은 새 5개/기존 4개 통과. /tmp/raftpp-fuzzy-release-guarded.log, -release-existing.log | -Wall -Wextra -Wpedantic -Werror/NDEBUG·선택 native 의존성 없는 fixture/core |
| ASan/UBSan 정적 Debug·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 같은 새 5개/기존 4개 통과. /tmp/raftpp-fuzzy-asan-guarded.log | C와 C++ 요청/역할/fixture 수명, 참조 guard와 실제 분기. 누수 검사 제외 |
| 초기 테스트·C 조사/빌드 오류 | 실행 — 실패/구형 바이너리 제외 | -release-initial/diagnostics, -reference-before, -release-generator, -reference-progress-valid, -release-final 로그와 관련 build 로그 보존 | 새 테스트의 존재하지 않는 address() 사용을 고쳤다. 진단 C의 progress.h 누락 빌드 실패 뒤 구형 바이너리를 한 번 실행한 -reference-progress.log는 새 검증에서 제외했다. 수정/근거 없이 같은 실패를 통과로 재사용하지 않음 |
| 기존 189개·추가 matrix/설치 소비자 | 생략 — 기존 결과 재사용 | 앞선 영역별·압축 snapshot/step 알림/설치 package 기록 | 기존 기본 193개 중 pre-vote 수신 영향이 있는 코어 네 개를 선택했다. 새 guard 외 구현/공개 선언·링크·선택 의존성/소비자 동작은 유지했다. 새 테스트 타깃 추가는 제품 export를 바꾸지 않음 |
| 원본 39파일/482정의의 개별 계약 감사 | 미검증 | 위 유한 목록과 기존 근거 위치 | 파일/CTest 이름 대응만으로 각 원본 case가 완료되었다고 판단하지 않음 |
| C 비활성 union·모든 follower 수렴·기존 TCP/kernel/raw/C fixture 제한 | 미검증 | 위 제한과 기존 실패/환경 기록 | C adapter guard 이후를 C와 일치한 것으로 보고하지 않음. 이전 TIMEOUT_NOW/pending-install·최초 TCP timeout·실장치 제한도 유지 |

명령은 cmake -S . -B <dir>, cmake --build <dir> --target raftpp_fuzzy_tests raftpp_stale_vote_tests raftpp_core_tests --parallel 2다. <dir>은 build, /tmp/raftpp-fixture-controls-release, /tmp/raftpp-fixture-controls-asan이다. 최종 CTest는 -V --output-on-failure -R '^raftpp.(fuzzy_(election|replication|membership|liveness)|core_(stale_vote|election|cluster|transfer|term_reference))$'를 사용했다. 공유 Release에서는 새 다섯 개와 기존 네 개를 별도 실행했다. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1을 사용했다. /tmp/raftpp-fuzzy-<구성>-fixed-{configure,build}.log와 /tmp/raftpp-fuzzy-evidence.json/.py에 최종 옵션/소스 hash·198/96/96개 등록·실행 및 독립 C++ 라이브러리의 C 미참조를 기록했다. 등록 수는 전체 suite 통과 수가 아니다. core/node API 두 언어본에는 지연 pre-vote 응답의 메시지·term·영속화 계약만 설명했다.

최종 정적 확인은 /tmp/raftpp-fuzzy-doc-check.py/.log에 보존했다. 한·영 REWRITTEN 제목/표 행 63/537, core API 6/30, node API 7/51과 API 경로 15쌍·상대 링크 390개·다음 작업 1–5가 대응한다. 43개 파일의 행별 정의 합계 494, 최종 소스/입력 hash 99개의 불변, FIXTURE 조건과 UV 독립 타깃·C guard 전달 전 검사를 확인했다. 이 문서 정리에는 추가 빌드·runtime을 실행하지 않았다.

### 공통·저장 값의 원본 계약 감사와 할당 실패 보완 (2026-11-03)

직전 fuzzy/지연 pre-vote 단위는 실제 수신 처리 수정·새 검증을 완료한 progress였다. 이번에는 남은 39개 파일/482개 TEST 정의 중 원본 core-unit 8개 파일/134개 정의를 원본 기대 동작, C++ 구현·호출자, 기존 검사의 입력/관찰값에 대응시켰다. 다음 표는 사례군별 대응 근거를 확정한 감사 범위다. 원본 Munit 실행 파일이나 모든 매개변수 조합을 이번에 새로 실행했다는 뜻은 아니다. 원본 이름만 나열한 직전 목록과 달리 정상·오류·수명 및 표준 대체의 차이를 함께 판정했다.

| 원본 unit 파일 / 정의 수 | 계약과 구현 대응 | 검증 근거와 판정 |
| --- | --- | --- |
| test_byte.c / 11 | CRC 초기값/차이, 16/32/64-bit little-endian, NUL 문자열 경계, SHA-1 증분/빈 update. checksum.cpp·detail/byte.hpp 및 configuration/encoding의 bounded parser | 기존 checksums의 실제 C CRC·증분 SHA-1/known/million-byte 벡터와 wire/records·encoding_malformed·configuration의 C 형식/모든 잘린 길이를 재사용한다. C 내부 byteFlip/GetString API는 독립 바이트 reader/writer·표준 view/find로 대응하며 같은 이름의 raw API를 추가하지 않는다. |
| test_compress.c / 6 | frame magic/짧거나 빈 입력·다른 magic·LZ4 ON 해제/OFF 오류. compression.cpp/compression_lz4.cpp | 기존 compression의 알려진/모르는 content size·빈 frame·상한/잘림/체크섬·출력 보존과 compressed_snapshot/compressed_transfer 기록을 재사용한다. 새 압축기를 이 범위에 추가하지 않는다. |
| test_configuration.c / 31 | 조회/복사/add/remove·중복/잘못된 역할·인코딩/디코딩/할당 실패. configuration.cpp의 vector/server/string 값 | 기존 configuration의 600개 C 동작 비교·전체 바이트/패딩·분리 복사·잘린 주소/헤더와 새 실패 검사를 대응시켰다. 값 복사 생성 실패는 표준 bad_alloc, add/encode/decode는 out_of_memory로 표현한다. remove는 표준 erase의 무할당 성공/unknown-ID 계약으로 대체한다. |
| test_err.c / 6 | 기본/인자 포맷과 prefix wrapping의 부분/전체 잘림. error.cpp와 표준 snprintf | 기존 error_messages/error_context의 C 문구와 0/1/252/253/254/255/256/1000 길이 wrapping·NUL/무할당 값 복사를 재사용한다. C variadic raw errmsg ABI는 공개 C++ API로 복제하지 않는다. |
| test_log.c / 54 | empty/offset/wrapped 조회, append·공유 batch·busy·OOM, acquire 범위/OOM, 참조 중 truncate/snapshot/restore. log.cpp·entry.cpp·표준 deque/map/shared/weak | 기존 log의 6000개 C 동작·유지 중 헤더/바이트 비교와 log_lifetime의 batch·로그 소멸 이후 생존, memory_containers의 resource/ref-index 실패를 재사용한다. 새 append/growing_append/append_configuration/acquire 두 overload의 OOM·재시도/출력 보존을 보완했다. C ring/ref-table의 크기/배치는 표준 저장소로 대체하며 term/index busy 계약은 유지한다. |
| test_queue.c / 13 | empty, FIFO push/head/tail/remove, 순회. 표준 list/deque·소유 요청 큐 | intrusive 매크로/내부 주소 ABI는 재구현하지 않는다. 기존 fixture_ordering·C 이벤트 비교, memory_containers의 allocator/splice, writer/log/node 종료·대기 요청 수명 검사를 재사용한다. 순서와 실제 요청 수명은 유지하며 표준 컨테이너 자체를 다시 테스트하는 큐 구현을 추가하지 않는다. |
| test_random.c / 3 | seed=42 첫 값 1650/10개 범위·중복 없음, seed=0의 20개 고정 벡터. random.cpp | 이번 검사에 원본 세 벡터 계약을 그대로 추가했다. 기존 random의 C 난수/상태 약 80000회 대조는 재사용한다. singleton 범위의 C 나눗셈 UB는 기존 C++ 단일값 반환 계약으로 구분하며 생성 알고리즘을 std RNG로 교체하지 않는다. |
| test_trail.c / 10 | empty/start/같은·새 term 추가, 조회, 잘라내기/랩, 스냅샷/복원. trail.cpp·표준 deque의 run-length 기록 | 기존 trail의 10000개 C 동작 비교·snapshot term/인덱스/has_entry, 입력/최대 index 검사를 재사용한다. C circular-array 배치 대신 논리 인덱스/term과 유지 범위를 대조한다. |

원본에서 요구하는 관찰 가능한 계약과 표준 대체를 위 범위에서 대응시켰다. 이번 감사로 해당 8개 파일을 남은 사례 대응 목록에서 정리하며, 아직 감사할 범위는 31개 파일/348개 정의다. 이는 남은 기능 수나 이식 비율이 아니다. 라이브러리 전체 완료 선언은 하지 않는다. 참조 범위는 위 원본 테스트/소스와 C++ buffer/entry/log/trail/configuration/error/random/compression/byte·메모리 allocator, 비교 테스트와 그 정리 경로다. 실제 제품 구현은 변경하지 않았다. 새로운 파일은 tests/foundation_failure.cpp 및 docs/ko·en/configuration.md이고, 테스트 CMake/기존 core·node API의 링크/이 기록을 함께 갱신했다. 기준 raft/는 유지한다.

새 foundation_failure는 C 기준 라이브러리를 링크하지 않는 C++ 공개/내부 값 소비자다. 긴 주소 네 서버로 표준 string/vector 할당을 유도하고, configuration add·copy·encode·decode, log append·확장 경계 append·configuration append 및 일반/resource-vector acquire를 한 번의 할당 실패 위치마다 확인한다. 각 연산은 첫 성공까지 검사하므로 이미 성공한 위치를 반복하지 않는다. 실패 시 입력과 기존 출력·로그 prefix·기존 소유 핸들이 보존되고, 같은 연산의 재시도에 stale busy 키가 남지 않는지 확인한다. 기본/31-entry prefix를 사용해 참조 등록 후 컨테이너 확장 실패의 rollback도 검증한다.

| 연산 | 이번 도구 환경에서 확인한 실패 위치 수 |
| --- | --- |
| configuration.add / copy / encode / decode | 2 / 5 / 1 / 7 |
| log.append / growing_append / append_configuration | 2 / 3 / 4 |
| log.acquire / acquire_resource | 1 / 1 |
| 합계 | 26 |

그 외 remove의 첫/중간/끝/없는 ID 네 사례는 fail-next가 소비되지 않으며 순서/오류를 보존한다. 두 acquire overload의 실패/성공 뒤 truncate 네 사례 역시 할당 없이 기존 획득 값을 보관하고, 같은 term/index는 busy인 상태에서 마지막 소유 핸들을 해제하면 재사용할 수 있다. C remove의 새 배열 할당 실패와 C log ref-table의 allocation fault를 표준 컨테이너에 인위적으로 재현하지 않는다. 복사 생성의 표준 예외와 noexcept 메서드의 오류 코드를 구분하고 Release에서도 check를 실행한다.

최초 새 검사는 세 구성에서 통과했다. 이후 원본 난수 고정 벡터와 확장 경계의 rollback을 실제 감사에 추가해 입력이 바뀐 새 검사 한 개만 다시 빌드/실행했다. 초기 -initial.log 및 -golden.log와 최초/final/growth build 로그를 보존하고 최종 결과와 구분했다. 제품/기존 테스트를 수정하지 않았으므로 기존 전체 suite를 재실행하지 않았다.

검증 시점은 2026-11-03 KST, Linux/GCC·G++ 13.3/C++20이다. 각 구성의 build exit 0을 확인하고 실행했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 foundation_failure 1개 통과, 26개 실패 위치/재시도·고정 난수 벡터. /tmp/raftpp-foundation-audit-debug.log | 새 실패·소유/출력·확장 정리 계약만 실행 |
| 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 같은 새 1개 통과. /tmp/raftpp-foundation-audit-release.log | NDEBUG/-Wall -Wextra -Wpedantic -Werror·native 의존성 없는 C++ 소비 |
| ASan/UBSan 정적 Debug·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 같은 새 1개 통과. /tmp/raftpp-foundation-audit-asan.log | 실패 unwind·기존 핸들/교체·마지막 소유 해제, detect_leaks=0으로 누수 검사 제외 |
| 기존 198개 및 추가 matrix/설치 소비자 | 생략 — 기존 결과 재사용 | 위 계약별 기존 기록과 직전 fuzzy·지연 pre-vote의 최종 검증 | 직전 증거의 소스/입력 hash 98개가 그대로다. 테스트 CMake에서 새 타깃 블록만 제거하면 직전 hash와 일치하며 기존 타깃/옵션·제품/헤더/export/링크를 바꾸지 않았다. 과거 실패/미검증은 통과로 재사용하지 않음 |
| configuration API 예제 | 실행 | /tmp/raftpp-configuration-example.cpp/.log, c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsyntax-only와 source/build include로 종료 0 | 새 한·영 설명서의 동일 예제 한 개를 문법 검증. 설치/런타임 소비자 재실행을 뜻하지 않음 |
| 남은 31파일/348정의·원본/환경 제한 | 미검증 | 원본 목록의 이번 8개를 제외한 나머지와 직전 제한 기록 | C VoteResult 비활성 union·모든 follower 수렴·TIMEOUT_NOW/pending-install·최초 TCP timeout/kernel/raw/누수 제한은 유지 |

명령은 cmake -S . -B <dir>, cmake --build <dir> --target raftpp_foundation_failure_tests --parallel <n>, ctest --test-dir <dir> --output-on-failure -V -R '^raftpp.foundation_failure$' --output-log <log>다. <dir>은 build, /tmp/raftpp-fixture-controls-release, /tmp/raftpp-fixture-controls-asan이고 최초 build는 --parallel 2, 변경된 새 검사 파일만의 후속 build는 1이다. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1을 사용했다. 옵션·hash·등록 199/97/97개·새 실행 파일의 C 미링크 근거는 /tmp/raftpp-foundation-audit-evidence.json/.py에 보존했다. 등록 수는 전체 suite 통과 수가 아니다.

configuration API 두 언어본은 값/주소 소유·복사 예외, 무할당 remove, 조회의 lifetime/index sentinel, 버전 1/패딩/빈 설정·잘린 입력/출력 보존과 예제만 설명한다. 작업/검증 이력을 docs에 넣지 않았다. core/node에서 새 API 문서로 연결했으며 API 경로는 16쌍이다.

최종 정적 확인: /tmp/raftpp-foundation-audit-doc-check.py/.log에서 한·영 REWRITTEN 제목/표 행 64/561, configuration API 6/13, core 6/30, node 7/51, API 경로 16쌍·상대 링크 400개·다음 작업 1–5를 확인했다. 최종 소스/입력 hash 98개와 새 예제 코드의 동일성, 감사 8파일/134정의 및 남은 31파일/348정의 집합을 확인했다. 문서 정리에는 추가 빌드·runtime을 실행하지 않았다.

### 코어·노드의 원본 integration 계약 감사 (2026-11-03)

직전 턴은 상태 보고만 했으므로 no progress로 분류하고, 이미 수정·빌드한 스냅샷 검사의 실제 재검증부터 이어갔다. 이번에는 남은 31파일/348개의 문자 그대로의 TEST 정의 중 native UV 전용을 제외한 integration 15파일/118정의를 원본 기대 동작·독립 C++ 구현·검증 입력과 대응시켰다. 원본 Munit의 전체 매개변수 조합을 새로 실행했다는 뜻은 아니다. 아래 정의 수는 기존 목록과 같은 소스상의 TEST 호출 수이며, 전처리 매크로가 생성한 개별 오류 코드 검사나 매개변수 확장 횟수는 아니다. test_strerror.c의 ERR_CODE_MAP 18개 코드도 기존 error_messages의 전체 코드 반복으로 대응한다.

| 원본 integration 파일 / 정의 수 | 계약·구현 대응 | 기존/새 검증 근거와 판정 |
| --- | --- | --- |
| test_catch_up.c / 1 | 응답 없는 promotee의 제한 시간·중단과 재시도. membership.cpp·timeout.cpp | 새 core_catch_up_timeout은 0/90/5000ms의 정확한 경계/초과·재시도/완료를 C query/event와 비교한다. 기존 node_cluster/fixture_membership_reference의 catch-up 승격은 재사용한다. |
| test_digest.c / 1 | 주소와 little-endian 숫자의 SHA-1 기반 서버 ID. checksum.cpp | 기존 checksums의 600개 실제 C digest/CRC/증분 SHA-1 및 known/million-byte 벡터를 재사용한다. std::hash로 대체하지 않는다. |
| test_election.c / 20 | 동일/더 긴/높은 term 로그의 투표, 재투표·이미 투표/리더/non-voter 거부, quorum·pre-vote/지연 응답, standby·쓰기/설치 중 선거 억제. election.cpp·receive_core.cpp·receive.cpp·timeout.cpp | 기존 election/vote/vote_request의 C 타이머·15000개 vote/3000개 수신 입력, core_election/cluster와 fuzzy/24개 stale_vote를 재사용한다. 새 restore_contracts는 비투표자·미영속 쓰기의 선거 억제/해제·재선거, snapshot_interleaving은 설치 중 억제를 명시적으로 확인한다. C 비활성 VoteResult union의 비교 중단 제한을 유지한다. |
| test_fixture.c / 10 | tick/election/send/delivery 순서, 임의 voter 선출/재선출·depose·적용 대기. fixture.cpp·node.cpp | 기존 fixture_ordering/elections/reference/controlled-reference·실제 C 이벤트 380/4600개 기록을 재사용한다. fixture OFF 제외와 종료/버퍼 수명은 과거 gate/lifetime 기록에 대응한다. |
| test_heap.c / 4 | 할당·zero fill·확장 prefix 보존·정렬/해제. memory.cpp·buffer.cpp·표준 PMR | 기존 memory_defaults/alignment/lifetime/failures/containers의 C calloc/realloc 비교·resource 교체·정렬·마지막 소유 해제를 재사용한다. raw heap API를 중복 구현하지 않는다. |
| test_init.c / 2 | I/O·FSM 없는 이벤트 엔진 초기화/종료와 초기 할당 실패. core_state·node 소유 context | 저수준 core는 I/O·FSM 없이 사용할 수 있다. 공개 node는 유효한 공유 io/FSM을 요구한다. 기존 core/queries/node_allocation·memory_objects/failures의 초기화/실패 정리를 재사용하며 C 포인터 초기화 API를 복제하지 않는다. |
| test_legacy.c / 11 | 로컬 스냅샷 제출 실패 정리·설치 중 선거 차단, committed 충돌/느린 로컬 저장·barrier 실패, assign 오류 우선순위, I/O/FSM version 필수 | 기존 node_restore/snapshot/failure·legacy_barrier/membership 일반/강제 모드의 콜백·오류 기록과 새 지연 commit/설치 검사를 대응시켰다. C version=0 ABI 검증은 필수 메서드가 있는 typed io/state_machine 인터페이스로 대체한다. 완료된 C++ snapshot은 finalize 한 번, 즉시 실패는 이미 적용한 명령을 되돌리지 않는다. |
| test_membership.c / 8 | add/remove의 즉시 effective·uncommitted→committed, 중복/병행/비리더 거부, 자기 제거 중 복제와 최종 step-down. membership.cpp·node.cpp | 기존 configuration/core_membership·legacy_membership·fixture_membership_reference와 fuzzy_membership의 원본 self-remove/역할 변경·오류 우선순위 비교를 재사용한다. 새 replication_contracts는 세 가지 설정 rollback의 정확한 committed index를 보완한다. |
| test_replication.c / 31 | 초기 barrier 정책/기능·용량, heartbeat/probe/pipeline·spare 제외·disconnect, 중복/충돌/누락, rollback, term/역할별 응답·ACK/commit/저장 경계·inflight | 기존 progress의 12000개 실제 C 상태 조작, core_election/replication/conflict/cluster/term_reference·transfer의 inflight 1/2, legacy_barrier·fuzzy_replication·capacity 기록을 재사용한다. 새 replication_contracts의 6사례는 초기/이전 로그/스냅샷 설정 rollback 3개, 새 리더 또는 알려진 리더 없음에서 늦은 쓰기 ACK 2개, 원격 quorum commit 후 로컬 저장 지연 1개를 명시적으로 대조한다. |
| test_snapshot.c / 7 | 설치·재시도·오프라인 제외·일치/오래된 거부, 설치 중 AppendEntries/term 변경, 이전 쓰기의 소유 수명 | 새 snapshot_interleaving은 6개 follower/3개 sender 사례에서 동일 C 입력·출력을 비교한다. 기존 core_snapshot/node_snapshot/node_restore·실제 compressed_transfer의 설치/파일 바이트/전체 재시작은 재사용한다. 이전 쓰기 완료는 node가 snapshot_installing과 현재 cache term으로 걸러낸다. C legacy pending-install 결함을 raw core의 유효하지 않은 persisted 이벤트로 우회하지 않는다. |
| test_start.c / 8 | 빈 디스크/term만·bootstrap·snapshot/후속/보존 로그·단일 voter self-elect·다른 non-voter 시작. core.cpp·node.cpp | 새 restore_contracts의 복원 8사례와 기존 node_restore·fixture_snapshot_load_reference/원본 C 전체 suffix 비교를 대응시켰다. 새 heartbeat로 복원 prefix를 확인하며 retained 로그와 snapshot commit 경계를 구분한다. |
| test_strerror.c / 1 | 알려진 오류 코드 및 unknown fallback. error.cpp·표준 error_code | 기존 error_messages가 모든 Raft 오류 코드를 실제 C와 비교하며 unknown/success를 구분한다. 매크로 확장된 원본 코드별 검사도 이 루프에 포함된다. |
| test_submit.c / 1 | 용량 quorum 부족 시 no_space·상태 보존·회복. client.cpp·progress/UV capacity | 기존 core_replication·native capacity-node 및 C 24개 capacity 비교 기록을 재사용한다. accepted 명령 완료 실패와 즉시 제출 거부를 구분한다. |
| test_tick.c / 5 | follower→candidate/self-vote, non-member/non-voter 억제, leader quorum 상실·재선거. timeout.cpp | 기존 core_election/cluster·fuzzy_election/liveness와 새 restore_contracts의 명시적인 3개 비투표 구성·pending write·두 선거 라운드를 대응시켰다. 시간/term 상한 guard 기록을 유지한다. |
| test_transfer.c / 8 | caught-up/catch-up 대상·자동 선택·중복/잘못된 ID·만료·pre-vote 우회. membership.cpp·receive_core.cpp | 기존 core_transfer 9사례/135개 C 이벤트·callback transfer의 C++ sanitizer 기록을 재사용한다. C legacy fixture TIMEOUT_NOW 통계 배열 오류로 실패한 비교는 통과로 재사용하지 않는다. |

이번 대응 감사로 해당 15파일을 사례 목록에서 정리했다. 남은 원본 사례 감사는 native unit 3파일/40정의와 native integration 13파일/190정의, 합계 16파일/230정의다. 위 표는 기능 계약과 검증 근거의 대응이며 모든 원본 trace 문자열·전처리/매개변수 조합의 동일성을 주장하지 않는다. 이미 남긴 참조 구현/환경 제한과 전체 완료 증명은 별도로 남아 있다. 제품·공개 시그니처·옵션/설치/export/의존성과 기준 raft/는 변경하지 않았다. 실제 수정은 tests/core.cpp의 새 함수 네 개/선택 분기, tests/CMakeLists.txt의 같은 core 타깃에 대한 등록 네 개·timeout, 한·영 core API와 이 진행 기록이다.

새 네 검사 합계는 구성마다 30사례/123개 C 이벤트 비교다. start/step/조회와 상태 variant·term/vote·configuration/인덱스·trail/cache·timeout·메시지 필드·영속화/스냅샷 출력·바이트를 기존 comparator로 대조한다. snapshot_interleaving에서 보관한 스냅샷과 이전 쓰기 batch는 후속 step 이후에도 바이트가 유효하고 마지막 핸들 해제 시 헤더가 소멸한다. 원본 core의 사전조건 밖인 폐기된 쓰기 완료를 C에 전달해 비교를 만들지 않는다.

첫 공유 Release의 스냅샷 재시도 검사는 heartbeat 응답 없이 연속 timeout을 넣어 피어를 응답 없는 상태로 만든 테스트 전제로 실패했다. 그 지점까지 C와 C++ comparator는 일치했다. heartbeat=50ms, snapshot deadline=102ms 사이 82ms에 유효한 응답을 넣어 원본 재시도 조건을 갖춘 뒤 해당 검사만 다시 실행해 통과했다. 같은 최초 실행의 catch-up 통과는 재사용했다. 추가 복원/복제 검사의 최초 빌드는 configuration의 존재하지 않는 operator== 사용으로 실패했고, 서버 필드 비교로 고친 뒤 build 종료 0을 확인하고 처음 실행했다. 두 실패 로그는 보존하며 오래된 바이너리를 새 검증에 사용하지 않았다.

검증 시점은 2026-11-03 KST, Linux/GCC·G++ 13.3/C++20이다. 별도 설치/전체 matrix나 기존 runtime을 다시 실행하지 않았다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 4개 통과, 30사례/123개 C 이벤트. /tmp/raftpp-core-contract-debug.log 및 -debug-extended.log | 처음 snapshot/catch-up 두 개, 이후 입력이 새로 추가된 restore/replication 두 개만 실행 |
| 엄격한 공유 Release·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 새 4개 통과. -release.log의 catch-up, -release-fixed.log의 snapshot, -release-extended.log의 나머지 두 개 | NDEBUG/-Wall -Wextra -Wpedantic -Werror, build 종료 0 이후 실행; 수정하지 않은 catch-up 재실행 생략 |
| ASan/UBSan 정적 Debug·UV/LZ4 OFF·fixture/backtrace ON | 실행 | 새 4개 통과. -asan.log 및 -asan-extended.log | C/C++ 이벤트·retained update/batch·정리 수명, detect_leaks=0으로 누수 검사 제외 |
| 최초 timing 가정/추가 검사 컴파일 | 실행 — 실패 보존 | -release.log의 snapshot 실패; -release-extended-build.log의 operator== 컴파일 오류 | 피어 응답 조건/서버 필드 비교로 각각 수정하고 영향 검사만 검증; 제품 결함으로 기록하지 않음 |
| 기존 199개·추가 matrix/설치 소비자 | 생략 — 기존 결과 재사용 | 위 표의 영역별 과거 기록·직전 foundation/fuzzy 최종 기록 | 이전 입력 hash 97개가 일치한다. CMake에서 새 suite 4개와 timeout 한 줄을 제거하면 이전 hash와 일치한다. 기존 함수·공유 helper는 유지하고 새 함수/분기만 추가했다. 옵션/link/export/제품이 그대로이며 과거 실패/미검증은 통과로 재사용하지 않음 |
| 남은 native 16파일/230정의와 기존 제한 | 미검증 | 아래 다음 작업과 앞선 C/환경/수렴 제한 기록 | 새 core 검사로 native 파일/소켓·kernel/raw 장치와 누수 검증을 대체하지 않음 |

명령은 cmake --build <dir> --target raftpp_core_tests --parallel 2 및 ctest --test-dir <dir> --output-on-failure -V -R '^raftpp.core_(snapshot_interleaving|catch_up_timeout)$' 또는 '^raftpp.core_(restore_contracts|replication_contracts)$' --output-log <log>다. <dir>은 build, /tmp/raftpp-fixture-controls-release, /tmp/raftpp-fixture-controls-asan이다. 공유 Release의 timing 수정 후에는 '^raftpp.core_snapshot_interleaving$'만 실행했다. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1을 사용했다. 각 build 로그·옵션·소스/입력 hash·등록 203/101/101개와 15/118·16/230 집합은 /tmp/raftpp-core-contract-evidence.py/.json에 보존했다. 등록 수는 전체 suite 통과 수가 아니다.

core API 두 언어본은 늦은 완료의 현재 로그/term 필터링과 persisted index 사전조건, 원격 quorum commit과 로컬 저장·현재 리더 ACK 경계, 설치 중 이벤트/현재 리더 응답·응답성 기반 재시도와 catch-up의 strict 경계만 설명한다. docs에는 작업/검증 이력을 넣지 않았다. API 문서 경로는 기존 16쌍이다.

최종 정적 확인: /tmp/raftpp-core-contract-doc-check.py/.log에서 한·영 진행 기록 제목/표 행 65/586, core API 6/30, 전체 API 경로 16쌍과 상대 링크 544개, 다음 작업 1–5를 확인했다. 최종 소스/입력 hash 99개·새 함수/등록 네 개·15파일/118정의와 남은 native 16파일/230정의 집합이 일치한다. 새 API prose와 기록만 정리한 뒤 추가 빌드/runtime을 실행하지 않았다.

### Native I/O·TCP 원본 계약 감사 (2026-11-03)

직전 core/node 단위는 새 C 비교·계약 대응을 완료한 progress였다. 이번에는 남은 native unit 3파일/40정의와 integration 13파일/190정의, 합계 16파일/230개의 소스상 TEST 정의를 독립 C++ 구현·기존 실행 기록·추가 C 대조에 대응시켰다. 원본 파일의 매개변수 확장이나 모든 파일시스템·커널을 새로 실행한 것은 아니다. 원본의 빈/TODO 검사와 환경상 skip도 실제 검증과 구분한다.

| 원본 native 파일 / 정의 수 | 계약·구현 대응 | 검증 근거와 제한 |
| --- | --- | --- |
| unit/test_uv_fs.c / 22 | 디렉터리·존재/권한/잘못된 prefix, 파일 예약·독점 생성/rename·동기화·capability probe. disk.cpp/file.cpp/probe.cpp | 기존 metadata_files/snapshot_files/disk_failures/disk diagnostics, 실제 C probe·file_allocation_reference 74사례와 오류/출력/파일/FD 정리 기록을 재사용한다. tmpfs/ZFS/btrfs별 실장치를 모두 새로 실행했다고 하지 않으며 해당 미지원/강제 오류와 실제 현재 FS 결과를 구분한다. |
| unit/test_uv_os.c / 6 | 경로 결합·길이 경계와 O_TMPFILE open/close. 표준 fs::path·소유 string/file_handle/prepared_snapshot | 고정 C 경로 배열/Join API는 표준 경로 값으로 대체하고 OS 오류와 소유 진단을 전달한다. 기존 snapshot_files의 임시 파일·마지막 owner/FD 정리와 native 파일 오류/예약 기록을 재사용한다. C의 고정 버퍼 크기를 새 C++ 경로 타입의 ABI 제한으로 복제하지 않는다. |
| unit/test_uv_writer.c / 12 | 단일/벡터·반복/EOF 뒤 쓰기, KAIO 자원 한도·worker/poll 종료. writer.cpp·file_handle | 기존 writer_basic의 C 바이트 비교, writer_async/close/failure/allocation/diagnostics와 실제 NOWAIT·다중 요청 기록을 재사용한다. 원본 concurrent/concurrentSame은 TODO 본문이므로 원본 통과를 병렬성 증거로 쓰지 않는다. C++ 실제 병렬 worker/poll 요청과 완료 후 커널 자원 정리로 대응한다. |
| integration/test_uv_append.c / 37 | 8바이트 정렬·크기/블록/배치·준비 pool/번호·finalize, 쓰기/준비 재시도·OOM·종료, blocking/nonblocking·여러 barrier | segment_buffer/log_writer/writer의 기존 C staging·native append 파일 바이트/복원, 크기 경계·무공간/재시도·할당·close/유지 소유·그룹 barrier 기록을 재사용한다. 새 file_snapshot_truncate로 실제 C의 pending append/snapshot/truncate/후속 append 조합을 보완한다. 원본 TODO/early-exit/시간 의존 검사를 완전한 검증으로 세지 않는다. |
| integration/test_uv_bootstrap.c / 2 | 새 저장소 bootstrap·기존 term 거부와 설정/term 저장 순서. disk.cpp·uv_io.cpp | 기존 segment_files의 실제 C bootstrap/recover 설정 엔트리·term/파일 바이트와 오류 기록을 재사용한다. term 기록 뒤 설정 생성 실패는 디렉터리 전체 rollback이 아니다. |
| integration/test_uv_init.c / 10 | 경로/접근/공간, direct/async probe OOM, metadata 짧음/format/version/같은 generation. uv_io/disk/probe | 기존 metadata_files/formats의 실제 C 슬롯 비교·잘린 슬롯 삭제/다른 슬롯 선택·같은 generation corrupt, uv_io_allocation와 probe 실패/정리 기록을 재사용한다. C 고정 디렉터리 버퍼 대신 표준 경로/OS 조건을 사용하고 원본별 permission/device skip을 새 통과로 세지 않는다. |
| integration/test_uv_load.c / 58 | unknown/tmp/orphan 정리, 빈/zero/open·prefix salvage/정렬·closed 범위, snapshot 선택·겹침/접미사/gap, CRC·형식·잘림·자동 격리/재시도 | 기존 segment_recovery/snapshot/files/failures·snapshot_files·storage_limits와 실제 C recover 36초기 조건/64호출을 재사용한다. 새 storage_load_contracts 30개 C load는 실제 EOF 첫 배치 잘림, closed CRC/빈/format, snapshot 뒤 마지막/중간 closed·open suffix 및 0/2 format과 자동 복구 ON/OFF를 직접 비교한다. |
| integration/test_uv_recover.c / 1 | 마지막 로그/snapshot term으로 새 설정 추가·기존 메타데이터/유효 prefix 보존·반복 복구. disk.cpp | 기존 storage_recover_reference의 실제 C 36초기 조건/64호출·설정 voter/standby/spare·바이트/파일 결정·반복 복구를 재사용한다. |
| integration/test_uv_recv.c / 13 | RPC 여섯 종류·heartbeat/반복, protocol/size/type 거부·단계별 프레임과 종료 | 기존 tcp_exchange/fragments/malformed/interop/shutdown, encoding의 C 전체 필드/바이트·길이 경계 및 stream adapter의 pipe/TTY/native 디스크 클러스터 기록을 재사용한다. 원본 closeAfterAppendEntriesHeader는 TODO 본문이며 원본의 무검사를 완료 근거로 삼지 않는다. C++ 실제 partial frame/EOF/종료 수명으로 대응한다. |
| integration/test_uv_send.c / 19 | 연결 재사용/병렬 송신·여섯 RPC·주소 변경·pending 최신 세 개 유지·쓰기 오류 후 재연결·OOM/종료 | 기존 tcp_exchange/retry/failure/peer_diagnostics/shutdown/allocation/interop의 실제 C handshake·바이트와 native 오류/재시도·큰 쓰기 취소/소유·정확히 한 번 완료를 재사용한다. 새 resolver 검사는 연결 후보의 성공/실패·취소를 별도로 C와 대조한다. |
| integration/test_uv_set_term.c / 7 | metadata 슬롯 교대·큰 generation 선택·기존 슬롯 덮어쓰기·vote 초기화·sync 순서. disk/metadata | 기존 metadata_files/formats·disk_metadata_diagnostics에서 실제 C read/write·전체 bytes·new slot fsync+directory sync/기존 fdatasync·실패 후 다른 슬롯 덮어쓰기 차단을 재사용한다. |
| integration/test_uv_snapshot_put.c / 9 | local/설치·trailing 보존·연속 설치/중간 append·준비 공간 재시도/close | 기존 file_snapshots의 trailing 0/3 각각 세 번 실제 C native put·metadata/data/segment·최신 두 파일 보존, file_snapshot_order/failure/close/allocation·node/압축 snapshot 전달 기록을 재사용한다. 새 combined barrier 검사도 연동한다. |
| integration/test_uv_tcp_connect.c / 12 | 이름/첫·다음 주소 연결, 거부·DNS/handshake/취소·OOM·후보 진행 중 close | 새 tcp_resolver_contracts는 첫 후보 성공/첫 실패 뒤 다음 성공/모두 거부/dispatch 전 취소의 네 C/C++ 사례와 handshake 40바이트를 비교한다. 기존 stream_connector/tcp_failure/shutdown/interop 수명·오류 기록을 재사용한다. 원본의 libuv≥1.44.2 handshake 타이밍 skip과 C++ 실제 완료/종료 검증을 구분한다. |
| integration/test_uv_tcp_listen.c / 14 | 여러 endpoint/중복·일부 bind 실패·DNS 오류, peer handshake/protocol/abort·pending/close·OOM | 새 resolver의 다섯 사례는 두 listener/중복 두 endpoint/첫·둘째 bind 실패/DNS 실패를 실제 C와 비교한다. 종료 후 포트 재bind와 resolver 결과 해제를 확인하며 C의 두 실패 경로는 아래 지연 해제 allocator로만 최종 대조한다. 기존 tcp_malformed/fragments/failure/allocation/stream_connector/실제 C interop를 재사용한다. 원본 세 번 중복/glibc 조건은 두 endpoint 중복 검사와 구분하고 partial peerAbort skip을 새 통과로 세지 않는다. |
| integration/test_uv_truncate.c / 7 | whole/끝/partial segment·pending/multiple truncate와 교체 append·종료 | 기존 segment_files의 C truncate 파일 바이트·log_barrier의 여러 그룹/비동기 truncate/후속 index·log_close/메모리 수명과 새 snapshot_truncate를 대응시켰다. 단순 반환 성공과 비동기 디스크 완료를 구분한다. |
| integration/test_uv_truncate_snapshot.c / 1 | snapshot 준비/barrier와 바로 뒤 truncate의 조합 및 최종 로그 prefix | 새 file_snapshot_truncate 3사례는 8/512/4096바이트 payload의 24개 pending entry, index 6 snapshot/trailing 8192, truncate 8과 index 8 replacement를 C/C++에 동일 제출한다. 후속 append 완료로 실제 truncate를 기다리고 snapshot·파일 바이트·복구된 1–8 prefix/교체 바이트를 대조한다. |

이로써 유한 원본 사례 목록 43파일/494개 문자 그대로의 TEST 정의는 fuzzy 4/12, core-unit 8/134, core/node integration 15/118, native 16/230의 계약 대응으로 정리했다. 이는 기능 비율·전체 Munit 매개변수 통과·모든 실행 경로 증명이 아니다. 새 미대응 소스 파일을 계속 늘리는 대신 다음 작업에서 전체 요구사항/현재 소스/공개 API·인터페이스/옵션·소유권/검증 제한을 기준으로 완료 판정표를 대조한다. 환경상 불가능한 실장치/커널 검증과 원본 C의 undefined behavior·최초 TCP timeout 및 수렴 제한은 여전히 명시적으로 남겨 두며 전체 완료로 표시하지 않는다.

제품·공개 시그니처·의존성/옵션·설치/export와 기준 raft/는 변경하지 않았다. 실제 수정은 tests/segments.cpp의 새 load 함수, persistence.cpp의 선택 분기, 별도 resolver.cpp/실행 타깃, uv_io.cpp의 새 조합 검사/분기와 log_writer_reference.c의 새 C bridge 함수 및 CTest 등록 세 개다. 기존 함수/shared helper의 동작은 유지한다. 한·영 uv_io/TCP API와 이 기록을 함께 갱신했다.

새 storage_load_contracts는 원본과 별도 디렉터리에 같은 파일을 복사해 status, term/vote/start index, 모든 entry/snapshot metadata·payload, 실패 출력 보존, 정리·격리 파일명과 비격리 파일의 전체 바이트를 비교한다. 원본의 corrupt 타임스탬프는 정규화하며 기존 compare_load helper를 수정하지 않았다. 4/16/32/44바이트에서 잘린 첫 배치는 자동 복구 ON/OFF 모두 io_error이며, 중간 closed 손상은 자동 복구로 버리지 않는다.

새 resolver 실행 파일의 getaddrinfo/freeaddrinfo 주입은 raftpp.test에만 적용하고 다른 호출은 원래 시스템 함수로 전달한다. 동기 listener와 libuv worker가 같은 mutex/할당 registry를 사용하며 실제 C/C++ 구현 모두 이를 거친다. 네 연결·다섯 listener 사례의 상태/콜백 수·식별자/주소·handshake bytes가 일치했고 최종 Debug/Release/ASan 실행에서 resolver 결과 14/14/15개가 각각 해제되어 registry가 비었다. 취소한 worker는 resolver dispatch 전 중단될 수 있다. 모든 실제 연결은 loopback이며 비로컬 주소는 실패 bind에만 사용했다. 주입은 기존 tcp_failure 실행 파일이나 제품에 링크하지 않는다. 초기 검사는 init 뒤 set_bind_address를 호출해 busy로 실패했다. 초기화할 주소를 직접 지정하도록 사전조건을 고친 뒤 새 검사만 다시 통과했고 제품 결함으로 기록하지 않았다.

최종 원본 소스 대조에서 raft/src/uv_tcp_listen.c의 UvTcpListenOnMultipleIP 오류 경로가 uv_close(..., NULL) 직후 listener 배열을 raft_free하는 것을 확인했다. libuv 핸들의 저장 공간은 close 완료까지 필요하므로, 초기 기본 heap 실행의 두 failed-bind 경로는 참조 수명 안전성 근거에서 제외했다. 새 deferred_heap은 오직 C 첫/둘째 bind 실패 두 사례에서만 공개 raft_heap_get/set으로 원래 allocator에 위임하되 free를 보관하고, 해당 루프의 close/drain·uv_loop_close 뒤 실제 해제/원래 allocator 복원을 수행한다. C 요청·반환값·콜백·bind 로직은 수정하지 않는다. 따라서 최종 9개 C 비교 중 7개는 기본 heap, 2개는 지연 해제 heap 조건의 결과이며 원본 C의 조기 free가 고쳐졌다고 주장하지 않는다. -resolver-<구성>-deferred-build.log 및 -deferred.log가 최종 입력/결과이고 앞선 기본 heap 로그와 구분한다. C++은 기존 표준 owner와 native close 콜백으로 이 수명을 보장한다.

원본의 중복 회피는 정확히 두 주소일 때만 적용하고, 3개 이상 주소 순회도 매회 두 번째 노드로 돌아가는 소스 차이가 있다. C++은 실제 목록을 순회하고 모든 동일 IP/port를 건너뛰는 계약을 사용한다. 새 런타임 비교는 두 후보/두 listener에 한정하며 원본 triple-duplicate 사례와 모든 resolver 목록 길이를 새로 비교했다고 하지 않는다.

새 file_snapshot_truncate는 실제 C native snapshot/truncate/barrier/append 경로를 별도 helper로 실행한다. C 요청·snapshot·entry payload는 close/drain까지 살아 있다. C truncate에 완료 콜백이 없으므로 실제 후속 쓰기의 완료를 기다린다. 기존 C helper 본문을 바꾸거나 원본 코드를 수정하지 않았다.

검증 시점: 2026-11-03 KST, Linux/GCC·G++ 13.3/C++20·C11 bridge. 각 build 종료 0 이후에 실행했다. 최종 등록은 Debug 206개·다른 두 native 구성 각각 161개이며 전체 suite 통과 수가 아니다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| Debug 정적·UV/LZ4/fixture ON·backtrace OFF | 실행 | 새 3개 통과, C 대조 30+9+3=42사례. /tmp/raftpp-native-contract-debug.log, -resolver-debug-deferred.log, -barrier-debug.log | 새 load/DNS/결합 barrier 계약만 실행 |
| 엄격한 공유 Release·UV ON·LZ4/fixture/backtrace OFF | 실행 | 같은 새 3개 통과. -contract-release.log, -resolver-release-deferred.log, -barrier-release.log | NDEBUG/-Wall -Wextra -Wpedantic -Werror, fixture·LZ4에 의존하지 않는 native 계약 |
| ASan/UBSan 정적 Debug·UV ON·LZ4/fixture/backtrace OFF | 실행 | 같은 새 3개 통과. -contract-asan.log, -resolver-asan-deferred.log, -barrier-asan.log | C/C++ parser·resolver registry/요청/소켓·C native queued 요청 수명. detect_leaks=0으로 누수 검사 제외 |
| 새 resolver 최초 구성/사전조건 | 실행 — 실패 보존 | -resolver-release-build.log의 미생성 타깃; -resolver-release.log의 init 뒤 bind 변경 busy | CMake 재구성 뒤 빌드, init 주소로 수정한 뒤 build 종료 0과 해당 검사만 재검증; 오래된 바이너리를 새 통과로 사용하지 않음 |
| TCP 실제 실행 환경 | 실행 | resolver 세 구성은 승인된 require_escalated CTest로 loopback 실행 | 앞선 sandbox AF_INET 제한의 기존 근거를 재사용했으며 같은 제한을 다시 실패시키지 않았다. 파일 검사에는 escalation이 필요 없었음 |
| 기존 203개·추가 옵션 matrix/설치 소비자 | 생략 — 기존 결과 재사용 | 위 영역별 과거 기록과 직전 core/node/foundation/fuzzy 최종 기록 | 이전 소스/입력 hash 98개가 일치하며 새 CMake 타깃/등록 세 개와 timeout을 제거하면 이전 hash다. 제품/shared helper·헤더/export/link·기존 검사 입력은 유지한다. 이전 실패/미검증은 통과로 재사용하지 않음 |
| 원본 전체 FS/권한/커널·raw 장치 및 이전 참조/수렴 제한 | 미검증 | 원본의 환경별 skip·앞선 실제/모의 probe·kernel/header-OFF 기록과 C TIMEOUT_NOW/pending-install/VoteResult/listen 조기 free·최초 TCP/수렴 기록 | 새 42사례나 파일 대응만으로 모든 환경/경로와 누수·최초 실패 원인을 증명하지 않음 |

명령: cmake -S . -B <dir>, cmake --build <dir> --target raftpp_persistence_tests/raftpp_resolver_tests/raftpp_uv_io_tests --parallel 2를 각 변경 범위에 실행했다. CTest는 -V --output-on-failure -R '^raftpp.storage_load_contracts$', '^raftpp.tcp_resolver_contracts$', '^raftpp.file_snapshot_truncate$'와 개별 --output-log를 사용했다. <dir>은 build, /tmp/raftpp-recover-reference-release, /tmp/raftpp-recover-reference-asan이다. ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1을 사용했다. 각 build/configure 로그·옵션·최종 소스/입력 hash 104개·등록과 16/230 집합은 /tmp/raftpp-native-contract-evidence.py/.json에 보존했다. 마지막 timeout 등록의 Release 재구성만 수행하고 이미 제한 안에 완료한 runtime은 다시 실행하지 않았다.

한·영 uv_io API는 복구/짧은 읽기의 구분·중간 closed 손상 거부, 연속 snapshot/truncate/append의 비동기 완료 계약을, TCP API는 여러 주소/중복 endpoint·부분 bind 실패의 정리/다음 연결 시도를 설명한다. API 경로는 16쌍이며 docs에 작업/검증 이력을 넣지 않았다.

최종 정적 확인: /tmp/raftpp-native-contract-doc-check.py/.log에서 한·영 진행 기록 제목/표 행 66/613, TCP API 6/15, UV API 7/25, 전체 API 경로 16쌍·상대 링크 544개·최종 소스/입력 hash 104개·새 UV gate 세 개와 다음 작업 1–5를 확인했다. native 16/230과 전체 43/494 집합, C failed-listen 두 사례의 지연 해제/루프 뒤 복원 조건과 기본 heap의 초기 수명 근거 제외를 확인했다. 문서 정리에는 추가 build/runtime을 실행하지 않았다.

### 전체 요구사항 최종 감사와 완료 판정 (2026-11-03)

직전 사용자 상태 응답은 no progress로 분류했다. 이번 감사는 루트 AGENTS 전체와 실제 파일을 기준으로 위 다음 작업 1–5를 수행했다. 적용되는 별도 하위 지침은 없었다. 참조 범위는 원본 51개 소스의 기존 기능군/계약 감사, 공개 세 헤더의 함수·구조체/콜백, C++ 구현·공유 헤더·선택 규칙·설치·예제/도구·API 문서와 해당 검증 기록이다. 실제 수정 범위는 한·영 AGENTS/REWRITTEN 및 memory API뿐이며 원본 raft/·제품·테스트·CMake는 수정하지 않았다.

| 요구사항 | 판정 | 현재 구현과 직접 확인한 근거 | 재사용한 검증 및 적용 한계 |
| --- | --- | --- | --- |
| 라이브러리 전체 계층의 독립 재작성 | 충족 | 원본 raft/src/*.c 51개가 기존 10개 기능군에 정확히 한 번씩 포함된다. CMake의 독립 C++ 소스·연결은 기능군 표의 실제 구현과 일치한다. 세 C++ 라이브러리의 undefined 심볼과 C 전역 정의 371개의 교집합은 비어 있다. | 전체 소스 대응 및 이후 계약 감사 기록. 이름 대응이나 링크만으로 동작 일치를 판정하지 않고 아래 영역별 근거를 함께 사용한다. |
| 공개 함수·구조체·플러그인 기능 | 충족 | 세 C 헤더의 함수 114개와 별개로 core 상태/이벤트/update, legacy.step_cb, io의 15개 hook 및 capacity/diagnostics, FSM의 5개 hook, raw transport의 init/listen/connect/close, tracer와 fixture 계약을 확인했다. node/core/io/state_machine/uv_stream_transport/fixture에 대응한다. 사용자 data는 소유 callable·객체 상태로 대체한다. | 공개 함수 표, core_query_reference 1152사례, legacy step 209알림, raw/adapter·fixture 기록. ABI version/reserved 슬롯·intrusive queue 배치를 C++ API로 복제하지 않는다. snapshot_async는 원본 legacy와 같이 동기 반환한다. |
| 선거·term·투표·역할 전이 | 충족 | core/election/convert/receive/receive_core/timeout의 pre-vote·정족수·기한·상위 term·이전/늦은 응답 경로를 해당 원본 계약에 대응시켰다. | election/vote/core_*·term/clock 경계 및 fuzzy 171개 일정·stale_vote 24사례. 비활성 C union 접근 전 중단한 133사례의 이후 이벤트는 독립 C++ 검증이며 C 일치로 세지 않는다. |
| 복제·로그 충돌·commit·멤버십·인계 | 충족 | client/progress/replication/membership·로그/trail과 core의 update, callback node를 연결했다. 단일 변경·승격 catch-up·rollback·리더 상실·TimeoutNow를 제공한다. | core/node 계약 30사례·123개 C 이벤트, 인계 9사례·135이벤트, fixture membership 3920이벤트와 legacy 일반/강제 barrier. 추가 10초 내 전체 follower 수렴 조건은 양쪽 실패 기록을 보존하며 보장하지 않는다. |
| 영속화와 응답·적용 순서 | 충족 | node.process가 term/vote 저장을 메시지보다 먼저 처리하며 accepted operation의 persisted 이벤트를 통해 로그/스냅샷 결과를 반영한다. 늦은 완료는 현재 cache/leader 경계로 제한한다. log_writer/file_backend가 append·barrier·snapshot·truncate를 조정한다. | core_replication_contracts/restore/interleaving 및 file_snapshot_truncate의 실제 C queue 비교. 원격 정족수 commit과 로컬 저장 완료를 혼동하지 않는 원본 계약을 유지한다. 실제 전원 차단 실험을 주장하지 않는다. |
| 버퍼·entry·공유 배치·스냅샷 수명 | 충족 | buffer/aligned_buffer/shared_buffer·entry/log/snapshot은 값 소유와 std 공유 aliasing을 사용한다. entry 뷰와 공유 배치를 분리하고 요청이 완료까지 보관한다. 정렬 deleter는 원래 resource/크기/정렬을 유지한다. | storage/log_lifetime/entries/snapshot·memory_*·writer/stream 취소·ASan/UBSan 기록. 비소유 뷰 자체는 수명을 연장하지 않는다. |
| 비동기 접수·실패·콜백·종료·재진입 | 충족 | node의 request/operation·weak 구독·closing_owner와 native 상태 소유권, ready 처리·요청 완료·drain 이후 close를 확인했다. 시작/실행 중 사용자 hook 뒤 closing 검사와 예외 containment가 연결된다. 코루틴/future 대체 API가 없다. | node_lifetime/allocation/startup와 I/O 28/FSM 8 종료 사례, step 알림·writer/TCP/stream/adapter close 기록. 이벤트 루프 구동과 사용자 구현의 io 계약 준수는 호출자 책임이다. |
| Wire·디스크 형식·체크섬·난수·압축 | 충족 | encoding/checksum/metadata/disk/configuration과 6종 RPC·handshake·batch/CRC·SHA-1·dual metadata·snapshot 형식, 원본 PRNG·LZ4 reader를 연결했다. snapshot compression setter는 원본 no-op을 보존한다. | C 바이트/파일 상호 읽기·encoding/storage limits·난수 벡터·compressed_transfer의 실제 TCP/수신 파일/재시작. 원본의 32비트 개수 절단·정의되지 않은 입력을 안전하게 거부하는 차이는 기존 기록/API에 명시했다. |
| 파일·복구·writer·OS fallback | 충족 | file/disk/probe/writer/segment_buffer/log_writer/file_backend/uv_io에 native FD/요청 RAII, fsync·공개·보존·자동 복구, direct/KAIO/NOWAIT/threadpool·예약 fallback을 구현했다. | recover 36초기조건/64회 C 호출, file allocation 74사례, load 계약 30회, writer/probe/진단/ENOSPC 기록. 모든 FS/권한/커널 조합을 실행한 것은 아니다. |
| TCP·DNS·raw stream·사용자 전송 | 충족 | tcp_transport와 tcp_stream_transport가 handshake·주소 순차 시도·listener·재시도·수명/오류를 공유하며 uv_stream/stream_transport가 native 소유 인계와 framing을 연결한다. | 실제 C resolver 9사례, TCP 상호 운용·실제 디스크 3노드·pipe/PTY adapter 기록. C failed-listen 두 사례는 루프 drain까지 free를 미루는 테스트 heap 조건이다. 최초 TCP timeout의 원인은 미확정이며 후속 통과와 구분한다. |
| 메모리·오류·추적·선택 backtrace | 충족 | PMR와 표준 allocator/컨테이너·error_code/error_info·source_location/소유 tracer·writer metric 및 NDEBUG/backtrace 경계를 확인했다. resource 교체가 기존 할당의 deleter를 바꾸지 않는다. | memory/foundation_failure 26위치·C 오류 문구/숫자·진단·trace/ASan/외부 소비·backtrace ON/OFF 기록. 표준 값 컨테이너/외부 라이브러리의 모든 할당을 라이브러리 resource로 재지정하지 않으며 상세 진단 문구의 ABI 호환성을 제공하지 않는다. |
| C++20·표준 우선·이름·기존 의존성 | 충족 | cxx_std_20/CXX_EXTENSIONS OFF, common.hpp의 std 별칭·full template interface, snake_case와 RAFTPP_ 공개 매크로, 원본 라이선스 안내를 확인했다. 새 coroutine/task/promise/future·C++23 전용 대체·미구현 표시나 새 외부 프레임워크는 없다. | 엄격한 Release/정적·공유 및 C++20 외부 소비 기록. 표준에 동등 구현이 없는 CRC/SHA/형식·압축/OS 경계는 독립 구현 또는 기존 의존성을 유지한다. 원본 C는 별도의 C11 타깃이다. |
| CMake의 모든 선택 옵션·생성 헤더 | 충족 | UV/LZ4/fixture/backtrace/tests/examples/benchmarks/install/BUILD_SHARED_LIBS의 기본값·gate·소스·링크·0/1 config/features·선택 공개 선언·기능 해제와 generated header 위치를 확인했다. UV OFF는 선택 의존성을 요구하지 않는다. | 기존 6개 유효 조합/잘못된 UV OFF+LZ4 ON/ON→OFF/헤더·심볼/의존성 검사와 이후 기능별 gate 기록. 새 테스트의 UV gate만으로 전체 matrix를 재실행하지 않았다. |
| 공유 export·설치·외부 소비 | 충족 | 현재 umbrella/transitive 공개 헤더의 concrete 비-inline 선언을 AST로 추출한 manifest와 공유 심볼을 대조했다. install/export는 raftpp::raftpp·생성 헤더·원본 라이선스를 제공하고 소비 의존성을 다시 찾는다. | 공유 279개/fixture OFF 228개 누락 0, 이동 설치본의 정적·공유 네 구성 및 새 step API 두 소비, 공백 prefix·의존성 누락·add_subdirectory 기본 OFF/ON→OFF 기록. export 검사는 runtime 동작 검사를 대신하지 않는다. |
| fixture·원본 테스트·예제·도구 | 충족 | 독립 fixture와 node/io/FSM·가상 시간/장애/6종 counter, tests·C bridge, examples server/cluster, benchmark submit/disk/io_uring/perf/trace의 구현·선택 연결을 확인했다. 원본 43파일/494 literal TEST 정의의 기능/오류/표준 대체 대응이 정리되어 있다. | fixture network 4600이벤트·주입 load 126회, fuzzy/영역별 C 비교와 예제·도구의 Debug/Release/ASan·최소 헤더 OFF 기록. 실제 kernel profiler/raw 장치 실행은 미검증이며 skip을 통과로 계산하지 않는다. |
| 한국어·영어 API와 진행 기록 분리 | 충족 | docs/ko와 docs/en의 16개 같은 경로 쌍·공개 계약/예제·상대 링크를 확인했다. 누락된 common 소유권 별칭 표·비소유 수명/동기화·표준 템플릿 설명을 memory API 양쪽에 보완했다. | 이번 문서 정적 검사와 기존 API 예제 컴파일 기록. AGENTS의 일시적 진행 문장을 기록 참조 원칙으로 바꿨다. docs에는 이번 작업/테스트 이력을 넣지 않았다. |
| VSCode 자동완성 설정 | 충족 | .vscode의 CMake Tools provider·C++20/C11·compile_commands 생성·생성 헤더 lookup·확장 추천을 확인했다. 원본 raft/src나 raft/include/raft를 일반 include 검색 경로에 넣지 않는다. | 설정 파일의 실제 값/JSON과 기존 configure 산출물 확인. IDE UI의 실행 화면을 검증했다는 의미는 아니다. |
| 변경 영향에 따른 검증 재사용·실패 보존 | 충족 | 최종 제품/공유 입력 104개 hash가 기존 기록과 일치한다. 현재 등록 206개를 확인하고 실제 실행/재사용/참조 불가/환경 skip을 구분했다. | 아래 표. 이번 감사에서 build/runtime·sanitizer·matrix·소비자를 새로 통과했다고 보고하지 않는다. 누수 검사는 계속 제외한다. |

위 판정은 요청된 전체 기능의 구현·연결과 각 계약의 검증 근거에 대한 완료 판정이다. 입력 공간 전체의 형식적 증명, 모든 장치/운영체제 조합의 인증, 내부 배치·정의되지 않은 동작의 재현을 뜻하지 않는다. 구현을 생략한 계층이나 계획만 있는 기능을 완료로 포함하지 않았다. C 참조 오류는 C++의 기능 미제공으로 남기지 않고 독립 검사·안전한 소유권/입력 처리와 구분해 판정했다.

| 검사 | 실행/생략/미검증 | 결과 또는 참조할 이전 기록 | 변경 영향 또는 생략 이유 |
| --- | --- | --- | --- |
| 현재 source/option/interface 감사·104개 hash 재확인 | 실행 — 정적 확인 | /tmp/raftpp-completion-link-independence.json, native-contract-evidence.json; 51개 소스 중복/누락 0·114개 함수 대응 | 원본 및 C++ 기능성 필드·콜백/정리·생성/설치 규칙을 위 범위로 대조. 실제 runtime 재실행은 아님 |
| 공개 AST·공유 export | 생략 — 기존 결과 재사용 | 이번 전체 감사의 먼저 생성한 -api-ast.jsons/-symbols.py/.json/.log; Clang 18.1.3의 C++20 umbrella AST 35 namespace roots, concrete 비-inline 279/228개 누락 0 | clang++ -std=c++20 -Iinclude -Ibuild/include -Xclang -ast-dump=json -Xclang -ast-dump-filter=raftpp -fsyntax-only /tmp/raftpp-completion-api.cpp. 본문/inline/constexpr/pure virtual·정의가 헤더에 있는 template의 자동 instantiation은 export 요구에서 제외한다. 이후 제품/헤더 변경 없음 |
| C 위임 없는 링크 | 생략 — 기존 결과 재사용 | -link-independence.json의 build/libraftpp.a·core-only 공유·native-only 공유와 C reference global 371개 교집합 0 | nm -g --defined-only build/libraft.a 및 nm -u <C++ library>의 실제 심볼 집합. C++ 타깃의 링크 규칙/원본 include 부재를 함께 확인 |
| 이번 한·영 문서/API/링크 일관성 | 실행 — 정적 확인 | /tmp/raftpp-completion-doc-check.py/.log 및 -final-evidence.json | memory의 소유권 표와 두 루트 문서 쌍만 변경. 표/코드/경로·상호 링크·104개 기존 hash 보존을 확인 |
| 전체 runtime·기본 개발 명령·6옵션·정적/공유·sanitizer·설치 소비 | 생략 — 기존 결과 재사용 | 최초 실행 및 후속 영향 단위의 원래 기록, 마지막 native/core/fuzzy/foundation·package/step API 결과 | 문서/기록만 수정. 첫 전체 실행과 후속 선택 검사 합성을 새 전체 206개 통과로 표현하지 않는다. 일부 오래된 임시 stdout가 없어도 보존한 최초 기록은 덮어쓰거나 기록 갱신용 재실행하지 않음 |
| 실제 perf/trace·raw 장치·모든 FS/권한·32비트·LSan | 미검증 | 기존 profiler/native 환경 skip·64비트/누수 제외 기록 | 구현/주입 syscall·형식/비지원 경로는 검증되어 있으나 이 외부 환경의 실제 측정 결과는 없다. 완료 판정으로 그 제한을 통과로 바꾸지 않음 |
| 최초 TCP timeout 원인·원본 C 결함·추가 전체 follower 수렴 조건 | 미해결/비교 제한 유지 | 원래 실패/로그 부재·후속 별도 통과, C TIMEOUT_NOW/pending-install/비활성 VoteResult/listen free 및 fuzzy 단위 기록 | 현재 독립 C++ 검사와 구분한다. 원본 소스를 변경하거나 과거 실패를 삭제하지 않았고 원인 해결을 주장하지 않음 |

최종 문서 정적 검사는 2026-11-03 10:09 KST에 종료 코드 0으로 완료했다. 한·영 문서 18쌍(API 16쌍)의 제목/표 구조·번역 주석을 제외한 예제 코드·상대 링크 548개를 확인했다. AGENTS는 제목/표 행 12/28, REWRITTEN은 67/642, memory API는 7/22로 대응하며 최종 판정은 18개 요구사항 행이다. 기존 104개 소스/입력 hash·51/10 소스 집합·43/494 원본 검사 목록·세 독립 라이브러리와 보존된 여섯 설치 소비 stdout도 확인했다. 명령은 python3 /tmp/raftpp-completion-doc-check.py이며 결과는 -doc-check.log/-final-evidence.json에 있다. 이 결과는 문서/근거의 정적 확인이며 새 runtime 통과가 아니다.

## 후속 유지와 검증 제한

요청된 전체 이식에 남은 필수 구현·연결 작업은 없다. 이후 코드를 변경하면 영향받은 기존 기록만 무효화하고 필요한 검사·옵션·소비자를 실행한다. 실제 kernel profiler/raw 장치·다른 파일시스템/권한·32비트 환경·LSan을 사용할 수 있는 환경에서의 추가 검증과 최초 TCP timeout의 원인 조사는 위 제한을 좁히는 후속 작업이며 이번 완료 기록에 새 통과로 포함하지 않는다. 원본 C 구현/비교는 제거했으며 runtime 의존성으로 다시 도입하지 않는다.

## 작업 이력

- 2026-11-03: core/uv 모듈과 public/private/src 경계·생성 include 미러·모듈별 라이브러리/export/설치를 연결했다. 구현 41개 토큰 동등, 코어 8/native 17/최소 구성 4개 선택 검사와 헤더 70건·옵션 6조합을 확인했다. UV 단독 공유 소비자의 상대 로딩 경로 회귀를 수정하고 두 언어 문서·개발 환경을 갱신했다. 최종 정적 검사도 통과했다.

- 2026-11-03: 독립 C++ 사용 설명을 기준으로 API 문서 7쌍과 루트 지침·기록 2쌍을 정리했다. 문서 표현·두 언어 대응·링크를 정적으로 확인하고 구현·빌드·테스트 입력은 동일하여 기존 결과를 재사용했다.

- 2026-11-03: `include/raftpp.hpp`를 설치 가능한 공개 API 엄브렐라로 추가했다. 기존 설치 소비자와 한/영 CMake API를 갱신하고 선택 기능 제외 소스 헤더 검사·정적/공유 설치 소비자 각 1개의 통과를 확인했다. 구현/runtime의 변경 없는 결과는 재사용했다.

- 2026-11-03: 번들 C 소스/타깃/비교 bridge를 제거하고 C++ 자체 검사로 전환했다. 원본 라이선스를 보존하고 C++ 159개를 포맷했으며 제품 112개의 토큰은 동일하다. 코어 92/native 32개가 최종 통과했고 테스트 이식의 최초 실패와 수정 후 해당 검사만의 재실행을 구분했다. 기본 전체 빌드와 최종 정적 검사도 통과했으며 결과·생략·제한은 위 제거 단위를 따른다.

- 2026-11-03: 전체 요구사항 최종 판정표를 작성해 독립 C++20 구현·공개 함수/구조체·알고리즘/순서·소유권·형식·옵션·설치·fixture·예제/도구·문서의 구현 완료를 판정했다. 104개 최종 입력 유지와 기존 공개 export/독립 링크 근거를 확인하고 common 소유권 별칭의 한·영 API 설명을 보완했다. 제품/테스트/빌드는 변경하지 않아 runtime·matrix·소비자는 재사용했다. 실제 장치/커널·LSan 및 원본 참조/수렴/최초 TCP 실패 제한은 통과로 바꾸지 않고 보존했다.

- 2026-11-03: native 16파일/230정의의 계약을 구현/기존 검증에 대응시키고 load/DNS 다중 주소/snapshot-truncate의 새 3개·42개 C 시나리오가 세 구성에서 통과했다. 제품/기존 helper는 유지하고 기존 203개는 재사용했다. resolver 최초 설정 사전조건 실패를 수정 후 통과와 구분해 보존했으며 한·영 UV/TCP API·기록을 보완했다. 원본 유한 목록 43파일/494정의의 대응을 정리했지만 전체 요구사항 최종 감사와 기존 참조/수렴/환경 제한은 남아 있다.

- 2026-11-03: 원본 core/node integration 15파일/118정의의 계약 대응을 정리하고 snapshot/catch-up/restore/replication의 새 4개 검사·30사례/123개 C 이벤트를 세 구성에서 확인했다. 제품/기존 helper는 유지하고 기존 199개는 재사용했다. 최초 timing 전제·컴파일 실패와 수정 후 영향 검사 결과를 구분해 보존했으며 한·영 core API/기록을 갱신했다. 남은 사례 감사는 native 16파일/230정의이고 전체 완료·기존 C/수렴/환경 제한은 남아 있다.

- 2026-11-03: 원본 core-unit 8파일/134정의의 정상·오류·표준 대체 계약 대응을 정리했다. 새 foundation_failure의 26개 할당 실패·재시도/확장 rollback·기존 소유/출력 보존과 원본 난수 벡터가 세 구성에서 통과했다. 제품/기존 테스트는 유지하고 기존 198개를 재사용했다. 한·영 configuration API와 예제를 추가해 16개 문서 쌍을 유지하며, 남은 사례 감사는 31파일/348정의다. 기존 기준 구현/수렴/환경 제한은 유지한다.

- 2026-11-03: 원본 43파일/494 TEST 정의를 목록화하고 fuzzy 12시나리오의 171개 고정 시드 조합을 이식했다. 지연 pre-vote 제안 term의 불필요한 갱신을 Release에서 재현·수정하고 24개 회귀를 추가했다. 새 5개/기존 영향 4개의 세 구성 통과와 391077개 C++ 이벤트/204687개 유효 C 비교를 기록했다. 비활성 union 접근 직전 133개 사례의 C 비교를 중단하고 이후는 C++ 검증으로 구분했다. 기존 189개는 재사용하며 원본 39파일/482정의의 사례 감사·원본/수렴/환경 제한은 유지한다.

- 2026-11-03: 원본 51개 소스/10개 기능군의 구조 대응과 독립 링크를 확인하고 현행 기능 옵션 표를 정정했다. 새 압축 snapshot 통합 검사의 Debug/공유 Release/LZ4 OFF 공유 Release/ASan·UBSan 통과로 실제 TCP 설치·수신 파일 바이트·전체 재시작·송신 원본 보존을 기록했다. 기존 192개·추가 matrix/소비자는 재사용하고 한·영 uv_io API와 진행 기록을 갱신했다. 전체 기능 의미 감사와 기존 TCP/환경/참조 제한은 유지한다.

- 2026-11-03: TCP 클러스터 테스트의 적용/스냅샷 완료 뒤 리더 선택 사전조건을 보완해 실패 sentinel의 배열 접근을 막았다. 같은 helper를 쓰는 3개가 Debug/공유 Release/ASan·UBSan의 실제 로컬 TCP에서 통과했다. sandbox socket 실패와 과거 timeout 미확정 상태를 구분하고 기존 189개는 재사용했다. 제품/API는 유지하며 전체 계약 감사와 기존 제한은 남아 있다.

- 2026-11-03: 함수 목록만으로 누락됐던 공개 구조체 legacy.step_cb를 발견해 표준 소유 step callback API를 추가했다. 시작 포함 209건의 실제 C 알림/상태와 수명·교체·등록 실패·종료 검사가 Debug/공유 Release/ASan·UBSan에서 통과했고 설치 소비자 두 구성도 새 API를 실행했다. 기존 182개 재사용·공개 구조체 감사 원칙과 한/영 API를 기록했다. TCP 최초 원인은 단정하지 않았으며 전체 계약 감사와 기존 제한은 남아 있다.

- 2026-11-03: 사용자 I/O/FSM 안에서 종료한 뒤 append/applied가 진행되는 결함을 공유 Release로 재현하고 후속 단계 중단을 수정했다. I/O 28/FSM 8사례의 새 2개가 네 구성에서 통과했으며 영향받는 기존 node/C barrier·membership만 재검증했다. 기존 180개 재사용·최초 실패·한/영 API 계약을 기록했다. 전체 경로 감사와 기존 환경/실패 제한은 남아 있다.

- 2026-11-03: 시작 중 재진입과 load에서 종료 후 복원을 공유 Release로 재현하고 startup admission/단계 중단/종료 drain을 수정했다. 새 재진입 4·종료 6사례의 2개 검사가 Debug/공유 Release/ASan·UBSan/코어 전용에서 통과했으며 영향받는 기존 검사만 선택 재검증했다. 최초 실패와 기존 180개 재사용·한/영 API 계약을 기록했다. 다른 사용자 I/O 경계·전체 완료 증명과 기존 제한은 남아 있다.

- 2026-11-03: 작은 실제 디스크 입력으로 원본 C reader 29회와 snapshot/segment 선언 경계를 비교했다. 27회 일치·원본 파일명 개수의 32비트 절단 차이 2회 및 payload 한도 2경우를 Debug/공유 Release/ASan·UBSan에서 확인했다. 제품은 유지하고 새 검사 1개씩만 실행했으며 기존 184개는 재사용했다. 한·영 API의 closed 범위·개수 오류 계약을 보완했다. 전체 완료 증명과 기존 제한은 남아 있다.

- 2026-11-03: C++ 라이브러리의 설치·재배치 가능한 CMake package와 선택 의존성 재탐색을 구현했다. 새 설치 소비 검사가 네 정적/공유 구성에서 통과했고 공백 경로·공유 LZ4 개발 파일 제외·누락 의존성·add_subdirectory 기본 OFF/ON→OFF·설치 파일을 확인했다. 기존 런타임과 추가 matrix/sanitizer는 재사용하고 한·영 API/지침/기록을 갱신했다. 전체 완료 증명과 기존 실패·환경 제한은 남아 있다.

- 2026-11-03: 기존 실제 pipe connector framing·native 디스크 클러스터 근거를 확인해 최근 미검증 요약을 정정했다. 새 전용 PTY/TTY connector의 양방향 12개 RPC·4MiB 대기 쓰기 취소/소유·잘린 frame EOF의 3사례가 Debug/공유 Release/ASan·UBSan에서 통과했다. 기존 182개는 생략하고 제품을 유지했으며 한·영 API/기록을 갱신했다. 최초 TCP/환경/참조 제한과 전체 완료 증명은 남아 있다.

- 2026-11-03: 새 전용 PTY에서 native TTY 채택·바이너리 송수신·구독 중지/재개·EOF 종료·이동/정리 5사례를 검증했다. 독립 C11/libuv 실험과 Debug/공유 Release/ASan·UBSan의 새 1개 통과를 기록하고 기존 181개는 생략했다. 제품과 기존 터미널은 유지하고 한·영 API/기록을 보완했다. 실제 connector framing·이전 실패/환경 제한과 전체 완료 증명은 남아 있다.

- 2026-11-03: 실제 native pipe stream의 소유·송수신·취소·EOF·실패 5사례를 추가했다. 최초 테스트 기대값 오류를 독립 C/libuv 실험으로 EPIPE임을 확인해 수정하고 실패를 보존했다. 새 1개가 Debug/공유 Release/ASan·UBSan에서 통과했으며 기존 180개는 재사용했다. 제품 구현은 유지하고 한·영 API/기록을 보완했다. 최초 TCP timeout·TTY와 전체 완료 증명/기존 제한은 남아 있다.

- 2026-11-03: wire 길이 선언과 단계별 payload 조립의 새 25사례를 추가했다. 원본 C 정상 헤더 27건 일치·2^32 count 절단 차이 1건과 C++ 거부/출력 보존·공유 소유자 해제를 Debug/core-only 공유 Release/ASan·UBSan 범위별 확인했다. 제품 코드는 유지하고 기존 179개를 재사용했으며 한·영 encoding API/기록을 보완했다. 전체 완료 증명과 기존 제한은 남아 있다.

- 2026-11-03: 선거 term/시계 wrap을 Release에서 재현하고 사전 거부·오류 전달을 구현했다. 새 term/clock/C 대조/callback node 4개와 기존 영향 6개가 세 구성에서 통과했다. 기존 나머지 169개는 재사용했으며 한·영 API/진행 기록을 갱신했다. 전체 완료와 기존 환경/참조 제한은 유지한다.

- 2026-11-03: 전체 완료 근거 점검에서 코어 terminal index 수락 후 pipeline next=0을 Release로 재현하고 시작/제출/수신 guard를 보완했다. max-1 정상 처리·거부/상태 보존의 새 8사례와 관련 C 비교/할당 실패를 Debug/공유 Release/ASan·UBSan에서 검증했다. 오래된 구성요소 표 9행·한/영 core/node API·완료 요건별 근거/제한을 갱신하고 기존 170개·추가 matrix/소비자는 재사용했다. term/time 경계와 전체 완료 증명·이전 제한은 남아 있다.

- 2026-11-03: 기존 파일 예약 fallback을 실제 C와 74사례로 비교했다. 54개 fallback 성공·native 정상 1개·오류 19개에서 쓰기 위치/크기/바이트·정리·출력 핸들/진단을 확인했다. Debug/공유 Release/ASan·UBSan/C11 결과와 별칭 guard의 동일 전처리 근거를 기록하고 변경 없는 Debug 반복·기존 173개·matrix/소비자는 재사용했다. 제품 구현은 유지하고 두 언어 UV API를 보완했으며 전체 완료 증명과 기존 제한은 남아 있다.

- 2026-11-03: 실제 C event-core 인계 9사례/135개 이벤트를 비교하고 ID 0 자동 선택·부분 복제의 조기/지연 힌트·pre-vote·실제 선거/만료/재시도를 확인했다. 제품 알고리즘은 보존하고 두 언어 core/node API의 잘못된 설명과 계약을 보완했다. 새 검사의 Debug/core-only 공유 Release/fixture-OFF ASan·UBSan 통과와 기존 cluster variant 전달 보완의 관련 결과를 기록했다. 변경 없는 새 Debug 결과·나머지 171개·matrix/소비자는 재사용했으며 기존 실패·제한과 전체 완료 증명은 남아 있다.

- 2026-11-03: fixture가 스냅샷 이전에 주입한 엔트리를 load에서 제거하던 차이를 실제 C 대조로 재현하고, 모든 엔트리를 snapshot 접미사로 반환하도록 수정했다. 주입 순서/길이/index·노드 시작·trailing 45사례/126회 load와 24개 할당 위치를 검증했다. Debug 새 1개/기존 4개, 공유 Release 새 1개, ASan·UBSan 새 1개/기존 2개 및 C11 결과를 기록하고 나머지 167개·matrix/소비자/gate는 재사용했다. 두 언어 fixture API를 갱신했으며 전체 완료 증명과 기존 제한은 유지한다.

- 2026-11-03: 로컬 스냅샷 준비 할당 실패가 적용 완료 노드를 종료하던 차이를 재현하고, 선택 준비의 bad_alloc/length_error를 건너뛰도록 수정했다. 32개 할당 위치·고정 이후 정리·재시도/구성/다음 명령과 실제 C의 요청/구성 실패 3개를 검증했다. Debug/공유 Release/ASan·UBSan/fixture OFF 범위와 최종 테스트 입력만의 재검증을 기록하고 나머지 167개·matrix/소비자는 재사용했다. 두 언어 node API를 보완했으며 전체 완료 증명과 기존 제한은 남아 있다.

- 2026-11-03: native capability probe가 AIO 정리 오류를 버리던 경로를 수정하고 실제 C probe의 정상/EIO/ENOMEM/EINVAL 반환과 대조했다. capability 출력 보존·소유 진단·미지원/앞선 오류·실제 context/파일/descriptor 정리를 검증하는 새 검사를 추가했다. Debug 새 1개/기존 영향 2개, 공유 Release·ASan/UBSan 새 1개 및 C11 검사를 기록하고 나머지 167개·추가 matrix/소비자는 재사용했다. 두 언어 오류 API를 갱신했으며 전체 완료 증명과 이전 제한은 유지한다.

- 2026-11-03: 실제 C fixture의 실행 중 grow/개별 시작·멤버십 변경 3920개 이벤트를 대조하는 검사를 추가했다. 1→3/3→5노드 확장, 미복제 spare/standby 승격·강등·제거·오류와 콜백/구성/term/로그/FSM이 일치했다. 새 검사 1개의 Debug/core-only 공유 Release/ASan·UBSan/C11 경고 결과를 기록하고 기존 168개·추가 matrix/소비자는 재사용했다. 제품 구현은 바꾸지 않고 두 언어 fixture API를 보완했다. 전체 완료 증명과 기존 제한은 유지한다.

- 2026-11-03: native capacity의 실제 C 대조 24개와 단일 노드 공간 부족 거부/상태 보존/회복 검사를 추가했다. 기존 포화가 C=0/C++=65535로 달랐던 경계와 size_t 포화까지 원본 unsigned 산술로 고쳤다. 새 2개의 Debug/공유 Release/ASan·UBSan·C11 경고 검증과 관련 기존 Debug 3개를 기록하고 나머지 검사/matrix/소비자는 재사용했다. capacity API 두 언어본을 보완했으며 전체 완료 증명과 기존 제한은 유지한다.

- 2026-11-03: 원본의 선택 assertion backtrace 기능을 기본 OFF CMake 옵션으로 이식했다. 기존 29개 assertion을 연결하고 Debug ON/OFF·NDEBUG 공유 Release·ASan/UBSan·정적 외부 소비자·같은 빌드의 기능 해제·의존성 부재/잘못된 라이브러리/캐시 복원을 검증했다. 새 검사와 관련 기존 검사만 실행하고 나머지 결과를 재사용했다. 두 언어 지침·추적 API·진행 기록을 갱신했으며 전체 완료 증명과 이전 제한은 유지한다.

- 2026-11-03: 관련 변경 없는 테스트는 기존 기록을 참조해 생략하고 최초 실행 시점·결과를 보존하도록 두 언어 지침을 보완했다. 이번 문서 보완의 빌드·테스트는 생략했으며 새 통과 결과를 기록하지 않았다.

- 2026-11-02: native 강제 복구의 36개 초기 조건/64회 C 비교를 추가했다. snapshot-only·closed/open·손상 suffix·gap·자동 복구 ON/OFF·voter/standby/spare 구성과 반복 복구에서 상태/파일 바이트가 일치했다. 새 검사의 Debug/공유 Release/ASan·UBSan 및 C11 경고 검사를 기록하고 164개·추가 matrix/소비자를 재사용했다. recover API 두 언어본을 보완했으며 기존 환경·실패 제한과 전체 완료 증명은 남아 있다.

- 2026-11-02: fixture의 네트워크 이벤트 4600개와 직접 I/O·snapshot fault C 비교 3개를 추가했다. zero-trailing snapshot의 내부 truncate fault tick 누락(C=18/C++=0)을 고쳐 실패 시 상태 보존/재시도를 검증했다. 새 3개·기존 영향 3개의 Debug/core-only 공유 Release/ASan·UBSan 결과와 C bridge size_t 수정·참조 C assert 한계를 기록하고, 변경 없는 158개·추가 matrix/소비자를 재사용했다. fixture API 두 언어본을 갱신했으며 전체 완료 증명과 이전 환경·실패 제한은 남아 있다.

- 2026-11-02: 사용자 raw-stream 플러그인을 framing·큐·재시도·소유 진단·uv_io에 연결하는 stream_transport를 구현했다. pipe/TCP/실제 디스크 3노드의 새 7개와 기존 종료 3개를 Debug/공유 Release/ASan·UBSan에서 범위별 검증했다. 첫 종료 observer의 무할당 저장, 실패한 수신 인계의 종료 대기와 8MiB 쓰기 취소를 확인했다. 새 테스트의 입력/용량 전제와 deleter 통계 수명 오류를 수정하고 영향 검사만 다시 실행했다. UV ON 소비자 실행/UV OFF 헤더 빌드와 두 언어 API를 확인하고 151개·추가 matrix는 재사용했다. 전체 완료 증명과 기존 환경·실패 제한은 남아 있다.

- 2026-11-02: uv_stream RAII owner와 raw TCP 연결 플러그인을 구현했다. native 주소·인계·취소·할당 실패·C 양방향 상호 운용의 새 6개, 기존 영향 Debug 15개/공유 Release·ASan·UBSan 4개와 UV OFF/ON 외부 소비자를 검증했다. 최초 Release 포인터 변환 경고를 고쳐 인계만 다시 확인했고 나머지는 기록을 재사용했다. 두 언어 API를 추가했으며 메시지 adapter/UV 연결과 기존 환경·실패 제한은 남아 있다.

- 2026-11-02: C 공개 함수 114개와 인터페이스 멤버의 대응 목록을 만들고 core 조회 6개를 공개해 node와 공통화했다. C 1152사례·소유권·노드 수명의 새 3개가 Debug/공유 Release/ASan·UBSan/core-only에서, 기존 영향 2개가 Debug에서 통과했다. 외부 core-only 공유 소비자의 export/실행/C 미링크를 확인하고 기존 143개·추가 matrix를 재사용했다. raw-stream 플러그인 경계와 기존 kernel/실패 제한은 남은 범위로 기록했다.

- 2026-11-02: 동적 tracepoint·perf ring·CPU별 수집/소유·block/NVMe 대응과 trace 상태 복원을 구현했다. 새 6개·기존 영향 4개의 Debug/공유 Release/ASan·UBSan 통과, native 접근 제한의 생략과 perf 헤더 OFF 최소 도구 계약을 구분해 기록했다. 하이픈 보완은 옵션만 재검증하고 기존 134개·추가 matrix/소비자는 재사용했다. 한·영 API를 갱신했으며 실제 kernel metric/raw 장치·전체 API 대조와 기존 실패 제한은 남아 있다.

- 2026-11-02: native disk/io_uring·고정 자원·직접 I/O·파일/장치 조회와 RAII drain을 구현했다. 새 5개·기존 영향 CLI 1개의 Debug/공유 Release/ASan·UBSan 통과, sandbox의 생략/실패와 승인된 커널 실행, 헤더 OFF 최소 구성의 비지원 계약을 구분해 기록했다. 기존 132개·추가 matrix/소비자/gate/링크 기록을 재사용했다. 한·영 API를 갱신했으며 raw 장치 실행·kernel perf/trace·전체 API 대조와 기존 실패 제한은 남아 있다.

- 2026-11-02: 표준 통계/JSON·native submit benchmark·선택 옵션과 새 검사 4개를 추가했다. Debug/공유 Release/ASan·UBSan 및 최소 구성·UV gate·C 미링크를 확인하고, 후속 입력 상한/stdout 변경은 영향 검사만 다시 실행했다. 기존 129개·추가 matrix/소비자는 생략하고 기존 기록을 재사용했다. 한·영 API와 검증 기록을 갱신했으며 disk/io_uring/perf/trace·전체 API 대조와 이전 실패 제한은 남아 있다.

- 2026-11-02: native C++ counter/server/프로세스 cluster와 선택 옵션을 추가했다. Debug/공유 Release/ASan·UBSan 새 3개, 최소 예제 구성·UV 옵션 거부/타깃 제외·C 미링크 검사를 기록했다. 실제 재시작/동시 종료의 두 예제 문제를 고쳐 해당 cluster만 재검증했고 기존 126개·추가 matrix/소비자는 재사용했다. 한·영 예제 API를 추가했으며 benchmark/전체 API 대조와 기존 TCP timeout·C fixture sanitizer 제한은 남아 있다.

- 2026-11-02: 일반/unit-test legacy callback 호환 드라이버·명시적 barrier 정책을 연결하고 멤버십 검증 우선순위를 C와 맞췄다. Debug/공유 Release/core-only의 새 8개와 기존 Debug 영향 2개를 확인했다. ASan/UBSan은 C 비교 4개·C++ transfer 2개 통과와 참조 C 배열 오류의 transfer 비교 2개 실패를 분리해 기록했다. 기존 116개·추가 matrix/소비자를 재사용했으며 전체 API 대조·예제/도구와 기존 TCP timeout 조사는 남아 있다.

- 2026-11-02: TCP 요청별 원인·peer 문맥과 선택적 transport 조회→UV→node 전달을 구현했다. Debug 새 3개/기존 영향 12개, 공유 Release·ASan/UBSan 새 3개와 외부 core-only/공유 소비자가 통과했다. 예외 문맥 보완은 backend 항목만 재검증했고 기존 103개·추가 matrix는 재사용했다. 전체 이식·호환/예제/도구와 이전 TCP timeout 조사는 진행 중이다.

- 2026-11-02: 디스크 metadata/스냅샷/segment/probe의 호출별 진단과 worker·UV·node 전파를 추가했다. Debug 새 4개/기존 영향 17개, 공유 Release·ASan/UBSan 새 4개가 통과했고 기존 94개·추가 matrix/소비자를 재사용했다. 실제 metadata EISDIR와 snapshot worker ENOENT, 동시 출력·fallback·최초 실패 보존을 확인했다. 전체 이식은 TCP 상세 진단·호환·예제/도구와 기존 TCP timeout 조사를 포함해 진행 중이다.

- 2026-11-02: native writer worker/poll/제출/초기화/종료 원인을 요청별 소유 값으로 보관하고 로그/파일/UV 진단에 연결했다. Debug 새 3개/기존 영향 9개, 공유 Release·ASan/UBSan 새 3개와 callback/cancel 최종 항목만의 재확인을 기록했다. 실제 NOWAIT poll을 확인했으며 기존 99개·추가 matrix/소비자를 재사용했다. 디스크 나머지 syscall·TCP·호환·예제/도구 등 전체 이식은 진행 중이다.

- 2026-11-02: 표준 값 기반 소유 오류 정보·무할당 문맥 구성·C 문구/경계 비교와 native 파일/초기화·I/O/node 전달을 추가했다. Debug 새 4개/기존 영향 2개, Release/ASan·UBSan 새 4개 및 외부 core-only/공유 소비자가 통과했다. 첫 새 테스트의 잘못된 단일 투표자 전제를 실제 실패 경로로 고친 기록을 보존했다. 변경 없는 102개·추가 matrix를 재사용하고 worker/TCP 상세 진단은 남은 범위로 기록했다.

- 2026-11-02: UV/TCP 설정 API·runtime 전파와 prepare 할당 크기 캡처를 추가했다. Debug 새 3개/기존 영향 3개, 엄격한 공유 Release·ASan/UBSan 새 3개와 core-only/공유 외부 소비자가 통과했다. 기존 98개·추가 matrix를 재사용하고 열린 writer의 정렬 변경 제한을 API 문서에 설명했다. native 오류 문맥·일부 호환·예제/도구 등 전체 이식은 남아 있다.

- 2026-11-02: 독립 이벤트 엔진을 공개 별칭으로 연결하고 timeout 설정/조회·C 호환 이름/난수/참조 버전과 legacy callback timeout 보정을 추가했다. 새 Debug/공유 Release 4개·sanitizer 2개, core-only 정적/공유 헤더 소비자와 대응 API 문서를 기록했다. 변경하지 않은 runtime 97개·추가 matrix를 재사용했으며 누수 검사는 제외했다. 전체 이식은 진행 중이다.

- 2026-11-02: 전체 C fixture를 참고해 독립 C++ 가상 시간 fixture와 메모리 I/O·제어·안전성 검사·옵션 gate를 구현했다. C와 이벤트 380개/명령 결과를 비교하고 7개 새 검사의 Debug·Release 공유·ASan/UBSan·core-only 결과, 마지막 완료 예외 경계만의 재검증, ON→OFF 제외와 외부 예제를 기록했다. 변경 없는 90개는 재사용했으며 한·영 API 문서를 함께 작성했다. 누수 검사는 제외했다.
- 2026-11-02: native node/writer/log/file/UV/TCP 요청 큐와 배열·내부 주소에 resource allocator를 연결하고 splice 동등성과 완료·취소 임시 큐를 보완했다. 새 세-resource barrier 검사와 Debug 영향 41개·Release 7개, ASan/UBSan 14개 및 별도 TCP 재검증을 기록했다. 처음 ASan TCP 시간 초과는 원인 미확정으로 보존했고 진단 추가 후 단독·조사 3회는 통과했다. 변경 없는 Debug 49개·전체 matrix·외부 소비자는 재사용했다. 누수 검사는 제외했다.
- 2026-11-02: resource 표준 컨테이너 별칭과 로그·trail·합의 코어 allocator 전파를 구현했다. 새 실패/상태 보존 검사와 Debug 영향 범위 29개·Release 6개·ASan/UBSan 6개 및 core-only 외부 소비자를 확인했다. 변경 없는 Debug 60개와 추가 matrix는 기존 결과를 재사용했고 native 요청 큐는 다음 단위로 구분했다. 누수 검사는 제외했다.
- 2026-11-02: 표준 PMR resource 선택·소유 allocator와 buffer/aligned/shared control block 및 library 객체 할당을 연결했다. 마지막 weak 참조·resource 교체·정렬·실패와 실제 디스크/TCP 클러스터 resource 정리를 검증했다. Debug 74개·현재 단위의 재사용 8개, Release 9개·ASan/UBSan 16개 및 두 외부 소비자를 기록하고 한·영 memory/storage API를 갱신했다. 변경 없는 helper·matrix는 생략했으며 일반 컨테이너/boxing 등 별도 allocator 경계를 명시했다. 누수 검사는 제외했다.
- 2026-11-02: 표준 타입을 활용한 독립 추적 API·stderr/환경 변수·동기 콜백 수명·writer metric을 구현하고 core/node/UV/TCP에 연결했다. C 비교·실제 디스크/TCP 클러스터·tracer 교체를 포함해 Debug 영향 범위 50개, 재사용한 새 단위 3개, Release 6개·ASan/UBSan 17개와 두 외부 소비자를 기록했다. 변경 없는 검사와 전체 matrix는 생략했고 한·영 API 문서를 갱신했다. 누수 검사는 제외했다.
- 2026-11-02: 독립 TCP 전송을 구현해 공개 UV backend에 연결했다. 새 TCP 10개 검사의 Debug·엄격한 Release 공유·ASan/UBSan 범위별 통과, C TCP handshake 상호 운용, 실제 디스크·TCP 클러스터의 스냅샷 따라잡기·이전·전체 재시작과 새 외부 소비자 검증을 기록했다. 관련 코드가 바뀌지 않은 기존 검사는 재사용·생략했다. 누수 검사는 제외했다.
- 2026-11-02: C++ 기반 구성요소의 첫 이식 단위와 기능 옵션별 빌드·테스트를 구현했다. 상세 범위와 이전 검증 결과는 위 표에 기록했다.
- 2026-11-02: `REWRITTEN.md`를 생성하고, `AGENTS.md`에 진행 기록·재귀 참조 범위·API 문서 전용 규칙을 추가했다. 문서 링크와 기록의 일관성만 검사했으며 구현 코드는 변경하지 않았다.
- 2026-11-02: 프로젝트 문서를 한국어·영어 두 언어로 관리하도록 정했다. 루트 문서의 영어 대응본과 상호 링크를 추가하고 API 설명서 경로를 `docs/ko/`, `docs/en/`로 정했다. 문서 구조·링크·대응본의 일관성만 검사했으며 빌드·테스트를 다시 실행하지 않았다.
- 2026-11-02: 정렬·공유 버퍼, 엔트리·배치 복사, 메모리 로그 캐시와 스냅샷 값 복사를 이식하고 한국어·영어 API 설명서를 추가했다. 기본 테스트 9개, 기능 옵션 6개 조합, ASan/UBSan과 외부 정적·공유 소비자를 검증했다. 누수 검사는 환경 제한으로 제외했다. 합의 코어와 실제 비동기 I/O 연결은 남아 있다.
- 2026-11-02: 선거·복제 진행 상태, 내부 RPC 큐, term 갱신과 RequestVote 수신·follower 전환을 독립 C++으로 이식했다. 기본·기능 옵션 6개 조합·ASan/UBSan에서 테스트 15개가 통과했다. C++ 노드 전체의 이벤트 처리·candidate/leader 전환·복제·멤버십·I/O 연결은 남아 있다.
- 2026-11-02: 메모리 로그와 이벤트 구동 코어에 역할 전환·복제·commit·멤버십·catch-up·이전·스냅샷을 연결했다. C 이벤트 비교, 3노드 손실·분할·복구, 표준 할당 실패 주입을 포함한 테스트 22개와 6개 옵션 조합·ASan/UBSan·외부 정적/공유 소비자를 검증했다. 공개 callback API, 실제 I/O·직렬화·추적·사용자 allocator와 fixture는 남아 있다.
- 2026-11-02: 공개 callback 노드·I/O·FSM API와 이벤트 드라이버를 구현하고 한국어·영어 API 설명서를 추가했다. 기본 및 기능 옵션 6개 조합의 테스트 28개, ASan/UBSan과 외부 정적·공유 소비자를 검증했다. 환경 제한으로 누수 검사는 제외했다. 실제 backend·직렬화·추적·사용자 allocator·fixture는 남아 있다.
- 2026-11-02: RPC·로그 배치 레코드 인코딩과 CRC32·SHA-1·digest를 독립 구현하고 공개 노드 모의 전송에 연결했다. 기본 테스트 33개와 ASan/UBSan이 통과했다. 기능 옵션 6개 조합과 외부 정적·공유 소비자도 통과했으며 누수 검사는 환경 제한으로 제외했다. 실제 파일·소켓 backend와 fixture 등 전체 이식은 남아 있다.
- 2026-11-02: 영속 메타데이터·스냅샷 코덱과 동기 네이티브 파일 계층을 구현하고 RAII 준비·공개, 두 슬롯 복구, C 상호 읽기와 네이티브 ENOSPC 회귀 검사를 추가했다. 기본 테스트 39개, 기능 옵션 6개 조합(UV 활성 39개·비활성 34개), ASan/UBSan과 외부 정적·공유 소비자를 검증했다. 누수 검사는 제외했다. segment 복구·비동기 I/O 조정·네트워크 backend는 남아 있다.

- 2026-11-02: 동기 segment 파일 생성·읽기·open 복구·잘라내기·보존, 스냅샷 fallback을 포함한 전체 저장 상태 로드와 bootstrap/recover를 독립 C++으로 이식했다. C 비교·파일 연산 및 할당 실패 검사를 포함한 기본/ASan/UBSan 43개 테스트, 기능 옵션 6개 조합(UV 비활성 34개), 외부 정적·공유 소비자를 검증했다. 누수 검사는 제외했다. 할당 실패 시 종료되는 libstdc++ 디렉터리 순회는 libuv 스캔 요청으로 대체했다. 비동기 writer·prepare pool·barrier·direct I/O·네트워크 backend와 fixture는 남아 있다.

- 2026-11-02: RWF_DSYNC·direct I/O·KAIO·eventfd/poll·EAGAIN threadpool 재시도와 수명을 소유하는 비동기 writer를 이식하고 파일 RAII를 공유 구성요소로 분리했다. 기본/ASan/UBSan 48개 테스트, 기능 옵션 6개 조합(UV 비활성 34개), C 파일 바이트 비교와 외부 정적·공유 소비자를 검증했다. 마지막 누적 길이·offset 보완 후 기본 48개와 Release/ASan/UBSan writer 5개를 다시 검증했다. 누수 검사는 제외했다. libuv 사용 조건 전달을 API 문서 두 언어본에 설명했다. prepare pool·append·barrier·capability probe·상위 backend·네트워크·tracing·fixture는 남아 있다.

- 2026-11-02: capability probe·정렬 배치 staging·두 파일 prepare pool·append/finalize·blocking/nonblocking barrier·비동기 truncate를 이식했다. 실제 C native backend의 파일·버퍼·probe 비교, 종료·재시도·할당 실패와 실제 디스크 단일 노드 재시작을 검사했다. 기본 및 ASan/UBSan 56개 테스트, 기능 옵션 6개 조합(UV 비활성 34개)과 외부 공유·정적 소비자가 통과했다. 누수 검사는 제외했다. 문서 대응본을 함께 갱신했다. 상위 스냅샷 조정·전체 공개 I/O backend·네트워크·tracing·사용자 allocator·fixture는 남아 있다.

- 2026-11-02: native 스냅샷·파일 coordinator와 공개 `uv_io`·`transport`를 구현하고 두 언어 API 문서를 추가했다. 기본 및 ASan/UBSan 전체 66개와 외부 소비자 3개 구성이 통과했다. C native 비교, 실제 디스크 재시작·분할·스냅샷 따라잡기·리더십 이전과 구성별 실행/재사용을 구분해 기록했다. 누수 검사는 제외했다. TCP 전송과 나머지 전체 이식 계층은 남아 있다.

- 2026-11-02: 관련 코드가 바뀌지 않은 검사를 생략하도록 사용자 요청을 두 언어 지침에 반영했다. 영향 범위·재사용 조건을 기록하고 중복 전체 옵션 재실행을 중단했다. 수정된 3노드 검사의 공유/LZ4 결과가 없던 경우만 단독 확인했으며 문서만의 변경에는 추가 빌드·런타임 테스트를 실행하지 않았다.
