import { RaftNode, NodeOptions, PacketTransport, Role, TraceEvent, CatchUpStatus } from '..';
const transport: PacketTransport = {
  start(receive, local) { const id: bigint = local.id; void receive; void id; },
  send(packet, done) { const bytes: Buffer = packet.data; void bytes; done(); },
  close(done) { done(); }
};
const options: NodeOptions = { id: 1n, address: 'node-1', directory: 'state', preVote: true,
  io: { storage: 'sqlite', timer: 'poll', transport, codec: 'zstd' } };
const node = new RaftNode(options, { apply: command => command, snapshot: () => Buffer.alloc(0), restore() {}, snapshotAsync: bytes => bytes });
node.setTracer((event: TraceEvent) => { const file: string = event.file; void file; });
node.setStepCallback(() => { const index: bigint = node.lastApplied; void index; });
node.bootstrap([{ id: 1n, address: 'node-1', role: Role.voter }]);
const index: bigint = node.matchIndex(1n);
const status: number = node.catchUp(1n);
void index; void status; void CatchUpStatus.finished;
// @ts-expect-error IDs cannot silently lose precision through number.
const invalid: NodeOptions = { id: 1, address: 'x', directory: 'x' };
void invalid;
