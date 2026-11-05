'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const net = require('node:net');
const { RaftNode, Role, NodeState, RaftError } = require('..');

async function address() {
  const socket = net.createServer();
  await new Promise(resolve => socket.listen(0, '127.0.0.1', resolve));
  const result = `127.0.0.1:${socket.address().port}`;
  await new Promise(resolve => socket.close(resolve));
  return result;
}
async function until(predicate) {
  const end = Date.now() + 12000;
  while (!predicate()) {
    if (Date.now() > end) throw new Error('Timed out');
    await new Promise(resolve => setTimeout(resolve, 5));
  }
}
function machine() {
  return {
    value: 0, snapshots: 0, restores: 0,
    apply(command) { this.value += command.readUInt32LE(); return this.snapshotBytes(); },
    snapshotBytes() { const output = Buffer.alloc(4); output.writeUInt32LE(this.value); return output; },
    snapshot() { ++this.snapshots; return this.snapshotBytes(); },
    restore(bytes) { ++this.restores; this.value = bytes.readUInt32LE(); }
  };
}
function apply(node, value) {
  const bytes = Buffer.alloc(4); bytes.writeUInt32LE(value);
  return new Promise((resolve, reject) => node.apply(bytes, (error, result) => error ? reject(error) : resolve(result)));
}
function close(node) { return new Promise(resolve => node.close(resolve)); }
function options(id, address, directory) {
  return { id, address, directory, electionTimeout: 150, heartbeatTimeout: 30,
    snapshotThreshold: 2, snapshotTrailing: 0, pollInterval: 2 };
}

test('single node: buffers, snapshots, restart, rejection, close ordering and 64-bit IDs', { skip: !RaftNode.features.uv }, async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'raftpp-node-'));
  const endpoint = await address(); const id = 9007199254740993n;
  let node;
  try {
    let fsm = machine(); node = new RaftNode(options(id, endpoint, directory), fsm);
    assert.throws(() => node.apply(Buffer.alloc(4), () => {}), RaftError);
    assert.throws(() => node.bootstrap([{ id: 1n, address: endpoint, role: 99 }]), TypeError);
    node.bootstrap([{ id, address: endpoint, role: Role.voter }]); node.start();
    await until(() => node.status.state === NodeState.leader);
    const bytes = Buffer.alloc(4); bytes.writeUInt32LE(3);
    const result = new Promise((resolve, reject) => node.apply(bytes, (error, result) => error ? reject(error) : resolve(result)));
    bytes.fill(0xff); global.gc?.();
    assert.equal((await result).readUInt32LE(), 3);
    for (let i = 0; i < 5; ++i) await apply(node, 1);
    await until(() => fsm.snapshots > 0 && fs.readdirSync(directory).some(name => name.startsWith('snapshot-')));
    assert.equal(node.status.leaderId, id);
    const order = [];
    node.barrier(() => order.push('barrier'));
    await new Promise(resolve => node.close(() => { order.push('close'); resolve(); }));
    assert.deepEqual(order, ['barrier', 'close']); assert.equal(node.closed, true);
    assert.throws(() => node.start(), /closed/);
    fsm = machine(); node = new RaftNode(options(id, endpoint, directory), fsm);
    node.start();
    await until(() => fsm.value === 8 && node.status.state === NodeState.leader);
    assert.ok(fsm.restores > 0); await close(node);
  } finally {
    if (node && !node.closed) await close(node);
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('three-node durable TCP replication and follower submission rejection', { skip: !RaftNode.features.uv }, async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'raftpp-cluster-'));
  const config = await Promise.all([1n,2n,3n].map(async id => ({ id, address: await address(), role: Role.voter })));
  config.forEach((_, i) => fs.mkdirSync(path.join(root, String(i))));
  const machines = config.map(machine);
  const nodes = config.map((server, i) => new RaftNode(options(server.id,server.address,path.join(root,String(i))),machines[i]));
  try {
    for (const node of nodes) { node.bootstrap(config); node.start(); }
    await until(() => nodes.filter(node => node.status.state === NodeState.leader).length === 1);
    const leader = nodes.find(node => node.status.state === NodeState.leader);
    const follower = nodes.find(node => node !== leader);
    let rejectedCallback = false;
    assert.throws(() => follower.apply(Buffer.alloc(4), () => { rejectedCallback = true; }), error => error.code === 7);
    assert.equal((await apply(leader, 7)).readUInt32LE(), 7);
    await until(() => machines.every(machine => machine.value === 7));
    assert.equal(rejectedCallback, false);
    const complete = operation => new Promise((resolve, reject) => operation(error => error ? reject(error) : resolve()));
    await complete(callback => leader.add(4n, '127.0.0.1:1', callback));
    await complete(callback => leader.assign(4n, Role.standby, callback));
    await complete(callback => leader.assign(4n, Role.spare, callback));
    await complete(callback => leader.remove(4n, callback));
    const targetId = config[nodes.indexOf(follower)].id;
    await complete(callback => leader.transfer(targetId, callback));
    await until(() => follower.status.state === NodeState.leader);
  } finally {
    await Promise.all(nodes.map(close)); fs.rmSync(root,{ recursive: true, force: true });
  }
});

test('state-machine exception is contained and constructor options are validated', { skip: !RaftNode.features.uv }, async () => {
  assert.throws(() => new RaftNode({ id: 1, address: 'x', directory: 'x' }, machine()), TypeError);
  const root = fs.mkdtempSync(path.join(os.tmpdir(),'raftpp-errors-'));
  const endpoint = await address(); const fsm = machine();
  fsm.apply = () => { throw new Error('machine failure'); };
  const node = new RaftNode(options(1n,endpoint,root),fsm);
  try {
    node.bootstrap([{ id: 1n,address: endpoint,role: Role.voter }]); node.start();
    await until(() => node.status.state === NodeState.leader);
    await assert.rejects(apply(node,1), RaftError);
    assert.match(node.lastMachineError.message,/machine failure/);
  } finally { await close(node); fs.rmSync(root,{ recursive: true, force: true }); }
});


test('empty commands, empty results and empty snapshots survive restart', { skip: !RaftNode.features.uv }, async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'raftpp-empty-'));
  const endpoint = await address();
  let snapshots = 0, restores = 0;
  const fsm = {
    apply(bytes) { assert.equal(bytes.length, 0); return Buffer.alloc(0); },
    snapshot() { ++snapshots; return Buffer.alloc(0); },
    restore(bytes) { ++restores; assert.equal(bytes.length, 0); }
  };
  let node = new RaftNode(options(1n, endpoint, directory), fsm);
  try {
    node.bootstrap([{ id: 1n, address: endpoint, role: Role.voter }]); node.start();
    await until(() => node.status.state === NodeState.leader);
    await new Promise((resolve, reject) => node.apply(Buffer.alloc(0), (error, result) => {
      if (error) reject(error); else { assert.equal(result.length, 0); resolve(); }
    }));
    await until(() => snapshots > 0 && fs.readdirSync(directory).some(name => name.startsWith('snapshot-')));
    await close(node);
    node = new RaftNode(options(1n, endpoint, directory), fsm); node.start();
    await until(() => restores > 0);
  } finally { await close(node); fs.rmSync(directory, { recursive: true, force: true }); }
});

function transportBus() {
  const members = new Map();
  return () => ({
    local: undefined,
    start(receive, local) { this.local = local; this.receiver = receive; this.reconnect(); },
    disconnect() { members.delete(this.local.id); },
    reconnect() { members.set(this.local.id, { receive: this.receiver, local: this.local }); },
    send(packet, done) {
      const source = this.local;
      setImmediate(() => {
        const peer = members.get(packet.peerId);
        if (!peer || !members.has(source.id)) return done(new RaftError(16));
        try { peer.receive({ peerId: source.id, address: source.address, data: packet.data }); done(); }
        catch (error) { done(error); }
      });
    },
    close(done) { if (this.local) members.delete(this.local.id); setImmediate(done); }
  });
}

test('runtime composition: custom transport, full queries/settings, tracing and FSM phases', { skip: !RaftNode.features.uv && !RaftNode.features.sqlite }, async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'raftpp-composed-binding-'));
  const makeTransport = transportBus();
  const storage = RaftNode.features.sqlite ? 'sqlite' : 'uv';
  const timer = RaftNode.features.asio ? 'asio' : 'poll';
  const codec = RaftNode.features.zstd ? 'zstd' : 'none';
  const members = [1n, 2n, 3n].map(id => ({ id, address: `member-${id}-${'x'.repeat(2500)}`, role: Role.voter }));
  const machines = members.map(() => machine());
  const transports = members.map(() => makeTransport());
  let phases = 0, finalized = 0, steps = 0, traces = 0;
  const nodes = members.map((member, i) => {
    const parent = path.join(root, `${i}`); fs.mkdirSync(parent);
    const directory = storage === 'sqlite' ? path.join(parent, 'state.sqlite') : parent;
    const fsm = machines[i]; fsm.snapshotAsync = bytes => { ++phases; return bytes; };
    fsm.snapshotFinalize = () => { ++finalized; };
    return new RaftNode({ ...options(member.id, member.address, directory), preVote: true,
      io: { storage, timer, codec, transport: transports[i] } }, fsm);
  });
  try {
    for (const node of nodes) {
      assert.deepEqual(node.configuration, []);
      node.setStepCallback(() => ++steps); node.setTracer(event => { assert.ok(event.file); ++traces; });
      node.setElectionTimeout(180); node.setHeartbeatTimeout(30); node.setInstallSnapshotTimeout(2000);
      node.setPreVote(true); node.setSnapshotThreshold(2); node.setSnapshotTrailing(0);
      node.setMaxCatchUpRounds(8); node.setMaxCatchUpRoundDuration(1000); node.setMaxInflightEntries(16);
      node.setCapacityThreshold(0); node.seed(Number(node === nodes[0] ? 12 : node === nodes[1] ? 16 : 22));
      node.bootstrap(members); node.start();
    }
    await until(() => nodes.filter(node => node.state === NodeState.leader).length === 1);
    const leader = nodes.find(node => node.state === NodeState.leader);
    assert.deepEqual(leader.configuration, members);
    assert.equal(leader.leader.id, members[nodes.indexOf(leader)].id);
    assert.equal(leader.leader.address, members[nodes.indexOf(leader)].address);
    assert.equal(leader.localRole, Role.voter); assert.ok(leader.currentTerm > 0n);
    assert.equal(typeof leader.votedFor, 'bigint'); assert.equal(typeof leader.timeout, 'bigint');
    assert.equal(leader.transferee, 0n); assert.equal(leader.error, 0); assert.equal(leader.errorDetails.code, 0);
    for (let i = 0; i < 6; ++i) await apply(leader, 1);
    await until(() => machines.every(fsm => fsm.value === 6));
    const lagging = nodes.findIndex(node => node !== leader);
    transports[lagging].disconnect();
    for (let i = 0; i < 6; ++i) await apply(leader, 1);
    await until(() => machines.every((fsm, i) => i === lagging || fsm.value === 12));
    transports[lagging].reconnect();
    await until(() => machines[lagging].value === 12 && machines[lagging].restores > 0);
    const follower = members.find(server => server.id !== leader.leader.id);
    assert.ok(leader.matchIndex(follower.id) >= 1n); assert.equal(typeof leader.catchUp(follower.id), 'number');
    assert.ok(leader.lastIndex >= leader.commitIndex); assert.ok(leader.lastApplied > 0n);
    assert.ok(steps > 0 && traces > 0 && phases > 0 && finalized > 0);
    assert.throws(() => leader.setCapacityThreshold(65536));
    assert.throws(() => leader.receive({ peerId: 2n, address: 'member-2', data: Buffer.from('bad') }), RaftError);
    for (const node of nodes) { node.setStepCallback(null); node.setTracer(null); }
  } finally {
    await Promise.all(nodes.map(close)); fs.rmSync(root, { recursive: true, force: true });
  }
});

for (const storage of ['sqlite', 'rocksdb']) for (const codec of ['none', 'lz4', 'zstd', 'zlib']) {
  test(`${storage}/${codec} snapshot persistence through composed binding`, { skip: !RaftNode.features[storage] || (codec !== 'none' && !RaftNode.features[codec]) }, async () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'raftpp-binding-db-'));
    const database = path.join(directory, storage === 'sqlite' ? 'state.sqlite' : 'state.rocksdb');
    const settings = { ...options(1n, 'local', database), snapshotTrailing: 0,
      io: { storage, timer: 'poll', codec, transport: transportBus()() } };
    let fsm = machine(); let node = new RaftNode(settings, fsm);
    try {
      node.bootstrap([{ id: 1n, address: 'local', role: Role.voter }]); node.start();
      await until(() => node.state === NodeState.leader);
      for (let i = 0; i < 4; ++i) await apply(node, 2);
      await close(node);
      fsm = machine(); node = new RaftNode({ ...settings, io: { ...settings.io, transport: transportBus()() } }, fsm);
      node.start(); await until(() => fsm.value === 8);
      assert.ok(fsm.restores > 0);
    } finally { await close(node); fs.rmSync(directory, { recursive: true, force: true }); }
  });
}

test('plugin runtime provides core-only operation and raw payload interoperability', { skip: !process.env.RAFTPP_BINDING_PLUGIN }, async () => {
  RaftNode.loadBackendPlugin(process.env.RAFTPP_BINDING_PLUGIN);
  let value = Buffer.alloc(8); let finalized = 0;
  const node = new RaftNode({ id: 1n, address: 'plugin', directory: '', payloadEncoding: 'raw',
    electionTimeout: 150, heartbeatTimeout: 30, snapshotThreshold: 2, snapshotTrailing: 0,
    io: { factory: 'test-memory' } }, {
    apply(bytes) { assert.equal(bytes.length, 8); value = bytes; return value; },
    snapshot() { return value; }, restore(bytes) { value = bytes; },
    snapshotAsync(bytes) { return bytes; }, snapshotFinalize() { ++finalized; }
  });
  try {
    node.bootstrap([{ id: 1n, address: 'plugin', role: Role.voter }]); node.start();
    await until(() => node.state === NodeState.leader);
    const command = Buffer.from('12345678');
    const result = await new Promise((resolve, reject) => node.apply(command, (error, data) => error ? reject(error) : resolve(data)));
    assert.deepEqual(result, command); await until(() => finalized > 0);
  } finally { await close(node); }
});


test('native argument validation rejects NaN before integer conversion', () => {
  const addon = require(process.env.RAFTPP_NODE_ADDON);
  assert.throws(() => addon.create({ address: 'validation', directory: '', electionTimeout: NaN }, () => Buffer.alloc(0)), /invalid option/);
});

test('FSM query and close reentry settles requests before close notification', { skip: !process.env.RAFTPP_BINDING_PLUGIN }, async () => {
  // The plugin is already registered by the plugin-runtime case.
  let node, queried = false;
  const order = [];
  let done;
  const closed = new Promise(resolve => { done = resolve; });
  node = new RaftNode({ id: 1n, address: 'reentrant', directory: '', io: { factory: 'test-memory' } }, {
    apply(command) {
      assert.equal(node.state, NodeState.leader); queried = true;
      assert.throws(() => node.poll(), error => error.code === 17);
      node.close(() => { order.push('close'); done(); });
      return command;
    },
    snapshot() { return Buffer.alloc(0); }, restore() {}
  });
  try {
    node.bootstrap([{ id: 1n, address: 'reentrant', role: Role.voter }]); node.start();
    await until(() => node.state === NodeState.leader);
    node.apply(Buffer.from('close'), () => order.push('apply'));
    await closed; assert.equal(queried, true); assert.deepEqual(order, ['apply', 'close']);
  } finally { if (!node.closed) await close(node); }
});
