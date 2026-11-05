'use strict';

const path = require('node:path');
let addon;
function native() {
  if (!addon) {
    if (!process.env.RAFTPP_NODE_ADDON) {
      throw new Error('Set RAFTPP_NODE_ADDON to the absolute path of raftpp_nodejs.node');
    }
    addon = require(path.resolve(process.env.RAFTPP_NODE_ADDON));
  }
  return addon;
}

const Role = Object.freeze({ standby: 0, voter: 1, spare: 2 });
const CatchUpStatus = Object.freeze({ none: 0, running: 1, aborted: 2, finished: 3 });
const NodeState = Object.freeze({ unavailable: 0, follower: 1, candidate: 2, leader: 3 });
class RaftError extends Error {
  constructor(code) { super(`Raft operation failed (${code})`); this.name = 'RaftError'; this.code = code; }
}
function check(code) { if (code) throw new RaftError(code); }
function id(value, allowZero = false) {
  if (typeof value !== 'bigint' || value < (allowZero ? 0n : 1n) || value > 0xffffffffffffffffn) {
    throw new TypeError('ID must be an unsigned 64-bit bigint');
  }
  return value;
}
function text(value) {
  if (typeof value !== 'string' || value.length === 0 || value.includes('\0')) {
    throw new TypeError('Expected a nonempty string without NUL');
  }
  return value;
}
function role(value) {
  if (!Number.isInteger(value) || value < 0 || value > 2) throw new TypeError('Invalid role');
  return value;
}
class RaftNode {
  #handle;
  #timer;
  #pending = new Map();
  #nextId = 1n;
  #closing = false;
  #closed = false;
  #polling = false;
  #inMachine = false;
  #closeCallbacks = [];
  #pollInterval;
  #transport;
  #local;
  #stepCallback;
  #tracer;
  #observerErrors = [];
  lastMachineError = undefined;

  constructor(options, stateMachine) {
    const settings = { electionTimeout: 1000, heartbeatTimeout: 100,
      snapshotThreshold: 1024, snapshotTrailing: 2048, pollInterval: 5,
      installSnapshotTimeout: 30000, maxCatchUpRounds: 10, maxCatchUpRoundDuration: 5000,
      maxInflightEntries: 32, capacityThreshold: 4096, preVote: false, forceBarrier: false,
      payloadEncoding: 'bytes', randomSeed: 1, ...options };
    id(settings.id); text(settings.address);
    if (typeof settings.directory !== 'string' || settings.directory.includes('\0')) throw new TypeError('Invalid directory');
    for (const key of ['electionTimeout', 'heartbeatTimeout', 'snapshotThreshold', 'snapshotTrailing', 'pollInterval', 'installSnapshotTimeout', 'maxCatchUpRounds', 'maxCatchUpRoundDuration', 'maxInflightEntries', 'capacityThreshold', 'randomSeed']) {
      if (!Number.isInteger(settings[key]) || settings[key] < 0 || settings[key] > 0xffffffff) {
        throw new TypeError(`Invalid ${key}`);
      }
    }
    if (!settings.pollInterval) throw new TypeError('pollInterval must be positive');
    for (const method of ['apply', 'snapshot', 'restore']) {
      if (typeof stateMachine?.[method] !== 'function') throw new TypeError(`Missing stateMachine.${method}`);
    }
    if (typeof settings.preVote !== 'boolean' || typeof settings.forceBarrier !== 'boolean') throw new TypeError('Expected boolean options');
    if (!['bytes', 'raw'].includes(settings.payloadEncoding)) throw new TypeError('Invalid payloadEncoding');
    settings.preVote = Number(settings.preVote); settings.forceBarrier = Number(settings.forceBarrier);
    settings.rawPayloads = Number(settings.payloadEncoding === 'raw');
    settings.snapshotAsync = Number(typeof stateMachine.snapshotAsync === 'function' || typeof stateMachine.snapshotFinalize === 'function');
    settings.io = { factory: 'composed', storage: 'uv', timer: 'uv', network: 'uv', codec: 'none',
      configuration: Buffer.alloc(0), segmentSize: 8 * 1024 * 1024, blockSize: 0, maxEntrySize: 8 * 1024 * 1024, maxEntries: 262144, diskRetry: 5000, connectRetry: 1000, autoRecovery: true, maxSnapshotSize: 8 * 1024 * 1024, maxEncodedSnapshotSize: 16 * 1024 * 1024, ...options.io };
    this.#transport = settings.io.transport;
    if (this.#transport) {
      settings.io.network = 'custom';
      for (const method of ['start', 'send', 'close']) if (typeof this.#transport[method] !== 'function') throw new TypeError(`Missing transport.${method}`);
    }
    for (const key of ['factory', 'storage', 'timer', 'network', 'codec']) text(settings.io[key]);
    if (settings.io.network === 'custom' && !this.#transport) throw new TypeError('Custom network requires transport');
    if (!Buffer.isBuffer(settings.io.configuration)) throw new TypeError('io.configuration must be a Buffer');
    for (const key of ['maxSnapshotSize', 'maxEncodedSnapshotSize', 'segmentSize', 'blockSize', 'maxEntrySize', 'maxEntries']) {
      if (!Number.isSafeInteger(settings.io[key]) || settings.io[key] < 0) throw new TypeError(`Invalid ${key}`);
      settings.io[key] = BigInt(settings.io[key]);
    }
    for (const key of ['diskRetry', 'connectRetry']) if (!Number.isInteger(settings.io[key]) || settings.io[key] < 0 || settings.io[key] > 0xffffffff) throw new TypeError(`Invalid ${key}`);
    if (typeof settings.io.autoRecovery !== 'boolean') throw new TypeError('Expected boolean autoRecovery');
    settings.io.autoRecovery = Number(settings.io.autoRecovery);
    this.#local = { id: settings.id, address: settings.address };
    this.#pollInterval = settings.pollInterval;
    this.#handle = native().create(settings, (operation, data) => {
      if (operation >= 5) {
        try {
          if (operation === 5) this.#stepCallback?.();
          else {
            const [message, file = '', functionName = '', line = '0'] = data.subarray(9).toString().split('\0');
            this.#tracer?.({ type: data.readUInt32LE(1), level: data.readUInt32LE(5), message, file, functionName, line: Number(line) });
          }
        } catch (error) { this.#observerErrors.push(error); }
        return;
      }
      this.#inMachine = true;
      try {
        if (operation === 2) {
          const result = stateMachine.restore(data);
          if (result?.then) throw new TypeError('restore must be synchronous');
          return;
        }
        if (operation === 4) { stateMachine.snapshotFinalize?.(); return; }
        const result = operation === 0 ? stateMachine.apply(data) : operation === 3 ? (stateMachine.snapshotAsync?.(data) ?? data) : stateMachine.snapshot();
        if (!Buffer.isBuffer(result)) throw new TypeError('State machine must return a Buffer synchronously');
        return result;
      } catch (error) { this.lastMachineError = error; throw error; }
      finally { this.#inMachine = false; }
    });
  }
  #ready() {
    if (this.#closed) throw new Error('Node is closed');
  }
  #configuration(servers, recover) {
    this.#ready();
    if (!Array.isArray(servers)) throw new TypeError('Expected an array of servers');
    const config = servers.map(s => ({ id: id(s.id), address: text(s.address), role: role(s.role) }));
    check(native().bootstrap(this.#handle, config, recover));
  }
  bootstrap(servers) { this.#configuration(servers, 0); }
  recover(servers) { this.#configuration(servers, 1); }
  start() {
    this.#ready(); check(native().start(this.#handle));
    this.#ensureTimer();
    if (this.#transport && !this.#closing) {
      try { this.#transport.start(packet => { if (!this.#closing && !this.#closed) this.receive(packet); }, this.#local); }
      catch (error) { this.close(); throw error; }
    }
  }
  #ensureTimer() {
    if (!this.#timer) this.#timer = setInterval(() => this.poll(), this.#pollInterval);
  }
  get status() { this.#ready(); return native().status(this.#handle); }
  get closed() { return this.#closed; }
  #submit(operation, data, serverId, address, serverRole, callback) {
    this.#ready();
    if (this.#closing) throw new RaftError(9);
    if (typeof callback !== 'function') throw new TypeError('A completion callback is required');
    if (this.#nextId > 0xffffffffffffffffn) throw new RangeError('Request IDs exhausted');
    const requestId = this.#nextId++;
    this.#pending.set(requestId, callback);
    try { check(native().submit(this.#handle, operation, requestId, data, serverId, address, serverRole)); }
    catch (error) { this.#pending.delete(requestId); throw error; }
  }
  apply(command, callback) {
    if (!Buffer.isBuffer(command)) throw new TypeError('command must be a Buffer');
    this.#submit(0, command, 0n, '', 0, callback);
  }
  barrier(callback) { this.#submit(1, Buffer.alloc(0), 0n, '', 0, callback); }
  add(serverId, address, callback) { this.#submit(2, Buffer.alloc(0), id(serverId), text(address), 0, callback); }
  assign(serverId, serverRole, callback) { this.#submit(3, Buffer.alloc(0), id(serverId), '', role(serverRole), callback); }
  remove(serverId, callback) { this.#submit(4, Buffer.alloc(0), id(serverId), '', 0, callback); }
  transfer(serverId, callback) { this.#submit(5, Buffer.alloc(0), id(serverId, true), '', 0, callback); }
  poll() {
    this.#ready();
    if (this.#polling || this.#inMachine) throw new RaftError(17);
    this.#polling = true;
    const errors = [];
    try {
      const completions = native().poll(this.#handle);
      let packet;
      while ((packet = native().transport(this.#handle))) {
        check(native().transportComplete(this.#handle, packet.requestId, -1));
        let completed = false;
        const done = error => {
          if (completed || this.#closed) return;
          completed = true;
          const code = error ? (Number.isInteger(error.code) && error.code > 0 && error.code <= 23 ? error.code : 18) : 0;
          const status = native().transportComplete(this.#handle, packetId, code);
          if (status && status !== 19) throw new RaftError(status);
        };
        const packetId = packet.requestId;
        try {
          if (packet.closing) this.#transport.close(done);
          else this.#transport.send({ peerId: packet.peerId, address: packet.address, data: packet.data }, done);
        } catch (error) { done(error); }
      }
      while (native().read(this.#handle, 1).length) { /* Drain copied native event records. */ }
      errors.push(...this.#observerErrors.splice(0));
      for (const item of completions) {
        const callback = this.#pending.get(item.id); this.#pending.delete(item.id);
        try { callback?.(item.status ? new RaftError(item.status) : null, item.data); }
        catch (error) { errors.push(error); }
      }
      if (this.#closing && !this.#pending.size && native().status(this.#handle).closed) {
        clearInterval(this.#timer); this.#timer = undefined;
        native().dispose(this.#handle); this.#handle = undefined; this.#closed = true;
        for (const callback of this.#closeCallbacks.splice(0)) {
          try { callback(); } catch (error) { errors.push(error); }
        }
      }
    } finally { this.#polling = false; }
    // One throwing callback must not abandon other accepted completions.
    for (const error of errors) queueMicrotask(() => { throw error; });
  }
  close(callback = () => {}) {
    if (typeof callback !== 'function') throw new TypeError('callback must be a function');
    if (this.#closed) { queueMicrotask(callback); return; }
    this.#ready();
    check(native().close(this.#handle)); this.#closing = true;
    this.#closeCallbacks.push(callback); this.#ensureTimer();
  }
  static get features() {
    const flags = native().features();
    return Object.freeze(Object.fromEntries(['uv', 'asio', 'sqlite', 'rocksdb', 'lz4', 'zstd', 'zlib', 'asyncpp', 'mvfst', 'msquic'].map((name, bit) => [name, Boolean(flags & (1n << BigInt(bit)))])));
  }
  static loadBackendPlugin(filename) { check(native().loadPlugin(text(filename))); }
  receive(packet) {
    this.#ready(); id(packet.peerId); text(packet.address);
    if (!Buffer.isBuffer(packet.data)) throw new TypeError('Packet data must be a Buffer');
    check(native().receive(this.#handle, packet.peerId, packet.address, packet.data));
  }
  get details() { this.#ready(); const value = native().details(this.#handle); value.leaderAddress = native().read(this.#handle, 2).toString(); return value; }
  get configuration() {
    this.#ready(); const bytes = native().read(this.#handle, 0); const servers = [];
    if (!bytes.length) return servers;
    let offset = 9; const count = bytes.readBigUInt64LE(1);
    for (let i = 0n; i < count; ++i) {
      const serverId = bytes.readBigUInt64LE(offset); offset += 8;
      const end = bytes.indexOf(0, offset); if (end < 0) throw new Error('Invalid native configuration');
      const address = bytes.toString('utf8', offset, end); offset = end + 1;
      servers.push({ id: serverId, address, role: bytes[offset++] });
    }
    return servers;
  }
  get state() { return this.status.state; }
  get currentTerm() { return this.status.term; }
  get votedFor() { return this.details.votedFor; }
  get lastIndex() { return this.status.lastIndex; }
  get commitIndex() { return this.status.commitIndex; }
  get lastApplied() { return this.status.lastApplied; }
  get timeout() { return this.details.timeout; }
  get localRole() { const value = this.details.localRole; return value < 0 ? null : value; }
  get droppedEvents() { return this.details.droppedEvents; }
  get transferee() { return this.details.transferee; }
  get leader() { const value = this.status; return value.leaderId ? { id: value.leaderId, address: this.details.leaderAddress } : null; }
  get errorDetails() { const d = this.details; return { code: d.errorCode, category: d.errorCategory, causeCode: d.causeCode, causeCategory: d.causeCategory, message: d.message }; }
  get errorMessage() { return this.errorDetails.message; }
  get error() { return this.status.errorCode; }
  peerStatus(serverId) { this.#ready(); return native().peer(this.#handle, id(serverId)); }
  matchIndex(serverId) { return this.peerStatus(serverId).matchIndex; }
  catchUp(serverId) { return this.peerStatus(serverId).catchUpStatus; }
  #configure(key, value) {
    this.#ready(); if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) throw new RangeError('Invalid setting');
    check(native().configure(this.#handle, key, BigInt(value)));
  }
  setElectionTimeout(value) { this.#configure(0, value); }
  setHeartbeatTimeout(value) { this.#configure(1, value); }
  setInstallSnapshotTimeout(value) { this.#configure(2, value); }
  setPreVote(value) { if (typeof value !== 'boolean') throw new TypeError('Expected boolean'); this.#configure(3, Number(value)); }
  setSnapshotThreshold(value) { this.#configure(4, value); }
  setSnapshotTrailing(value) { this.#configure(5, value); }
  setMaxCatchUpRounds(value) { this.#configure(6, value); }
  setMaxCatchUpRoundDuration(value) { this.#configure(7, value); }
  setMaxInflightEntries(value) { this.#configure(8, value); }
  setCapacityThreshold(value) { this.#configure(9, value); }
  seed(value) { this.#configure(10, value); }
  setStepCallback(callback) {
    if (callback != null && typeof callback !== 'function') throw new TypeError('Expected callback');
    this.#configure(11, Number(callback != null)); this.#stepCallback = callback;
  }
  setTracer(callback) {
    if (callback != null && typeof callback !== 'function') throw new TypeError('Expected callback');
    this.#configure(12, Number(callback != null)); this.#tracer = callback;
  }
  ref() { this.#timer?.ref(); return this; }
  unref() { this.#timer?.unref(); return this; }
}
module.exports = { RaftNode, RaftError, Role, NodeState, CatchUpStatus };
