/// <reference types="node" />
export const Role: Readonly<{ standby: 0; voter: 1; spare: 2 }>;
export type ServerRole = 0 | 1 | 2;
export const CatchUpStatus: Readonly<{ none: 0; running: 1; aborted: 2; finished: 3 }>;
export const NodeState: Readonly<{ unavailable: 0; follower: 1; candidate: 2; leader: 3 }>;
export interface Server { id: bigint; address: string; role: ServerRole; }
export interface NodeOptions {
  id: bigint; address: string; directory: string;
  electionTimeout?: number; heartbeatTimeout?: number;
  snapshotThreshold?: number; snapshotTrailing?: number; pollInterval?: number;
  installSnapshotTimeout?: number; maxCatchUpRounds?: number; maxCatchUpRoundDuration?: number;
  maxInflightEntries?: number; capacityThreshold?: number; preVote?: boolean; forceBarrier?: boolean;
  payloadEncoding?: 'bytes' | 'raw'; randomSeed?: number; io?: IoOptions;
}
export interface StateMachine {
  apply(command: Buffer): Buffer;
  snapshot(): Buffer;
  restore(snapshot: Buffer): void;
  snapshotAsync?(snapshot: Buffer): Buffer;
  snapshotFinalize?(): void;
}
export interface NodeStatus {
  term: bigint; lastIndex: bigint; commitIndex: bigint; lastApplied: bigint;
  leaderId: bigint; state: 0 | 1 | 2 | 3; closed: 0 | 1; errorCode: number;
}
export type Completion = (error: RaftError | null, result: Buffer) => void;
export class RaftError extends Error { constructor(code: number); readonly code: number; }
export class RaftNode {
  constructor(options: NodeOptions, stateMachine: StateMachine);
  readonly status: NodeStatus;
  readonly closed: boolean;
  lastMachineError: unknown;
  bootstrap(servers: Server[]): void;
  recover(servers: Server[]): void;
  start(): void;
  apply(command: Buffer, callback: Completion): void;
  barrier(callback: Completion): void;
  add(id: bigint, address: string, callback: Completion): void;
  assign(id: bigint, role: ServerRole, callback: Completion): void;
  remove(id: bigint, callback: Completion): void;
  transfer(id: bigint, callback: Completion): void;
  poll(): void;
  close(callback?: () => void): void;
  static readonly features: Readonly<Record<'uv' | 'asio' | 'sqlite' | 'rocksdb' | 'lz4' | 'zstd' | 'zlib' | 'asyncpp' | 'mvfst' | 'msquic', boolean>>;
  static loadBackendPlugin(path: string): void;
  readonly details: NodeDetails;
  readonly configuration: Server[];
  readonly state: NodeStatus['state'];
  readonly currentTerm: bigint; readonly votedFor: bigint; readonly lastIndex: bigint;
  readonly commitIndex: bigint; readonly lastApplied: bigint; readonly timeout: bigint; readonly transferee: bigint; readonly droppedEvents: bigint;
  readonly localRole: ServerRole | null; readonly leader: { id: bigint; address: string } | null;
  readonly errorDetails: ErrorDetails; readonly errorMessage: string; readonly error: number;
  peerStatus(id: bigint): PeerStatus; matchIndex(id: bigint): bigint; catchUp(id: bigint): number;
  receive(packet: Packet): void;
  seed(value: number): void;
  setElectionTimeout(value: number): void; setHeartbeatTimeout(value: number): void;
  setInstallSnapshotTimeout(value: number): void; setPreVote(value: boolean): void;
  setSnapshotThreshold(value: number): void; setSnapshotTrailing(value: number): void;
  setMaxCatchUpRounds(value: number): void; setMaxCatchUpRoundDuration(value: number): void;
  setMaxInflightEntries(value: number): void; setCapacityThreshold(value: number): void;
  setStepCallback(callback?: (() => void) | null): void;
  setTracer(callback?: ((event: TraceEvent) => void) | null): void;
  ref(): this;
  unref(): this;
}

export interface Packet { peerId: bigint; address: string; data: Buffer; }
export interface PacketTransport {
  start(receive: (packet: Packet) => void, local: { id: bigint; address: string }): void;
  send(packet: Packet, completed: (error?: RaftError | null) => void): void;
  close(completed: () => void): void;
}
export interface IoOptions {
  factory?: string; storage?: 'uv' | 'sqlite' | 'rocksdb'; timer?: 'uv' | 'asio' | 'poll';
  network?: 'uv' | 'custom'; codec?: 'none' | 'lz4' | 'zstd' | 'zlib';
  transport?: PacketTransport; configuration?: Buffer;
  maxSnapshotSize?: number; maxEncodedSnapshotSize?: number;
  segmentSize?: number; blockSize?: number; diskRetry?: number; connectRetry?: number; autoRecovery?: boolean;
  maxEntrySize?: number; maxEntries?: number;
}
export interface TraceEvent { type: number; level: number; message: string; file: string; functionName: string; line: number; }
export interface ErrorDetails { code: number; category: string; causeCode: number; causeCategory: string; message: string; }
export interface PeerStatus { matchIndex: bigint; catchUpStatus: number; }
export interface NodeDetails {
  votedFor: bigint; timeout: bigint; transferee: bigint; droppedEvents: bigint; localRole: number;
  errorCode: number; causeCode: number; errorCategory: string; causeCategory: string; message: string; leaderAddress: string;
}
