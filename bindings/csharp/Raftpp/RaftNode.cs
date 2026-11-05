using System.Runtime.InteropServices;
using System.Collections.Concurrent;
using System.Buffers.Binary;
using System.Text;

namespace Raftpp;

public enum ServerRole { Standby, Voter, Spare }
public enum NodeState { Unavailable, Follower, Candidate, Leader }
public enum CatchUpStatus { None, Running, Aborted, Finished }
public sealed record Server(ulong Id, string Address, ServerRole Role = ServerRole.Voter);
public sealed record NodeOptions(ulong Id, string Address, string Directory)
{
    public uint ElectionTimeout { get; init; } = 1000;
    public uint HeartbeatTimeout { get; init; } = 100;
    public uint SnapshotThreshold { get; init; } = 1024;
    public uint SnapshotTrailing { get; init; } = 2048;
    public uint InstallSnapshotTimeout { get; init; } = 30000;
    public uint MaxCatchUpRounds { get; init; } = 10;
    public uint MaxCatchUpRoundDuration { get; init; } = 5000;
    public uint MaxInflightEntries { get; init; } = 32;
    public ushort CapacityThreshold { get; init; } = 4096;
    public bool PreVote { get; init; }
    public bool ForceBarrier { get; init; }
    public bool RawPayloads { get; init; }
    public uint RandomSeed { get; init; } = 1;
    public IOOptions IO { get; init; } = new();
}
public sealed record IOOptions
{
    public string Factory { get; init; } = "composed";
    public string Storage { get; init; } = "uv";
    public string Timer { get; init; } = "uv";
    public string Network { get; init; } = "uv";
    public string Codec { get; init; } = "none";
    public byte[] Configuration { get; init; } = [];
    public ulong MaxSnapshotSize { get; init; } = 8 * 1024 * 1024;
    public ulong MaxEncodedSnapshotSize { get; init; } = 16 * 1024 * 1024;
    public ulong SegmentSize { get; init; } = 8 * 1024 * 1024;
    public ulong BlockSize { get; init; }
    public ulong MaxEntrySize { get; init; } = 8 * 1024 * 1024;
    public ulong MaxEntries { get; init; } = 262144;
    public uint DiskRetry { get; init; } = 5000;
    public uint ConnectRetry { get; init; } = 1000;
    public bool AutoRecovery { get; init; } = true;
    public IPacketTransport? Transport { get; init; }
}
public sealed record Packet(ulong PeerId, string Address, byte[] Data);
public interface IPacketTransport
{
    void Start(Action<Packet> receive, Server local);
    void Send(Packet packet, Action<RaftException?> completed);
    void Close(Action completed);
}
public sealed record TraceEvent(uint Type, uint Level, string Message, string File, string FunctionName, uint Line);
public sealed record ErrorDetails(int Code, string Category, int CauseCode, string CauseCategory, string Message);
public sealed record PeerStatus(ulong MatchIndex, CatchUpStatus CatchUpStatus);
public sealed record LeaderInfo(ulong Id, string Address);
public sealed record NodeStatus(ulong Term, ulong LastIndex, ulong CommitIndex, ulong LastApplied,
    ulong LeaderId, NodeState State, bool Closed, int ErrorCode);
public interface IStateMachine
{
    byte[] Apply(byte[] command);
    byte[] Snapshot();
    void Restore(byte[] snapshot);
    byte[] SnapshotAsync(byte[] snapshot) => snapshot;
    void SnapshotFinalize() { }
}
public sealed class RaftException(int code) : Exception($"Raft operation failed ({code})")
{
    public int Code { get; } = code;
}
public delegate void CompletionCallback(RaftException? error, byte[] result);

public sealed class RaftNode : IDisposable
{
    private readonly NodeHandle _handle;
    private readonly MachineContext _machine;
    private readonly int _threadId = Environment.CurrentManagedThreadId;
    private readonly Dictionary<ulong, CompletionCallback> _pending = new();
    private readonly List<Action> _closeCallbacks = new();
    private ulong _nextId = 1;
    private bool _closing, _polling, _disposed;
    private readonly IPacketTransport? _transport;
    private readonly Server _local;
    private readonly ConcurrentQueue<Packet> _received = new();
    private readonly ConcurrentQueue<(ulong Id, int Status)> _sent = new();
    public bool Closed { get; private set; }
    public Exception? LastMachineError => _machine.LastError;

    public unsafe RaftNode(NodeOptions options, IStateMachine stateMachine)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(stateMachine);
        ValidateId(options.Id); ValidateText(options.Address);
        ArgumentNullException.ThrowIfNull(options.Directory);
        if (options.Directory.Contains('\0')) throw new ArgumentException("Invalid directory");
        if (Native.AbiVersion() != 2) throw new NotSupportedException("Unsupported raftpp bindings ABI");
        _transport = options.IO.Transport;
        _local = new Server(options.Id, options.Address);
        if (options.IO.Network == "custom" && _transport == null) throw new ArgumentException("Custom network requires Transport");
        _machine = new MachineContext(stateMachine);
        var context = GCHandle.Alloc(_machine);
        try
        {
            var io = options.IO;
            var settings = new Native.RuntimeOptions {
                Node = new Native.Options { ElectionTimeout = options.ElectionTimeout, HeartbeatTimeout = options.HeartbeatTimeout,
                    SnapshotThreshold = options.SnapshotThreshold, SnapshotTrailing = options.SnapshotTrailing },
                InstallSnapshotTimeout = options.InstallSnapshotTimeout, MaxCatchUpRounds = options.MaxCatchUpRounds,
                MaxCatchUpRoundDuration = options.MaxCatchUpRoundDuration, MaxInflightEntries = options.MaxInflightEntries,
                CapacityThreshold = options.CapacityThreshold, PreVote = options.PreVote ? 1u : 0u,
                ForceBarrier = options.ForceBarrier ? 1u : 0u, RawPayloads = options.RawPayloads ? 1u : 0u,
                SnapshotAsync = 1, Factory = io.Factory, Storage = io.Storage, Timer = io.Timer,
                Network = _transport != null ? "custom" : io.Network, Codec = io.Codec,
                MaxSnapshotSize = io.MaxSnapshotSize, MaxEncodedSnapshotSize = io.MaxEncodedSnapshotSize, RandomSeed = options.RandomSeed,
                SegmentSize = io.SegmentSize, BlockSize = io.BlockSize, MaxEntrySize = io.MaxEntrySize, MaxEntries = io.MaxEntries,
                DiskRetry = io.DiskRetry, ConnectRetry = io.ConnectRetry, AutoRecovery = io.AutoRecovery ? 1u : 0u };
            foreach (var value in new[] { io.Factory, io.Storage, io.Timer, settings.Network, io.Codec }) ValidateText(value);
            ArgumentNullException.ThrowIfNull(io.Configuration);
            IntPtr pointer;
            fixed (byte* bytes = io.Configuration)
            {
                settings.Configuration = (IntPtr)bytes; settings.ConfigurationSize = (nuint)io.Configuration.Length;
                Check(Native.CreateRuntime(options.Id, options.Address, options.Directory, settings,
                    Native.Callback, GCHandle.ToIntPtr(context), out pointer));
            }
            try { _handle = new NodeHandle(pointer, context); }
            catch { Native.Release(pointer); throw; }
        }
        catch { context.Free(); throw; }
    }
    private static void Check(int code) { if (code != 0) throw new RaftException(code); }
    private static void ValidateText(string value)
    {
        if (string.IsNullOrEmpty(value) || value.Contains('\0')) throw new ArgumentException("Invalid string");
    }
    private static void ValidateId(ulong value)
    {
        if (value == 0) throw new ArgumentOutOfRangeException(nameof(value));
    }
    private void CheckAccess()
    {
        if (_disposed || Closed) throw new ObjectDisposedException(nameof(RaftNode));
        if (_threadId != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Use the creating thread");
    }
    public NodeStatus Status
    {
        get
        {
            CheckAccess(); Check(Native.GetStatus(_handle, out var value));
            return new(value.Term, value.LastIndex, value.CommitIndex, value.LastApplied,
                value.LeaderId, (NodeState)value.State, value.Closed != 0, value.ErrorCode);
        }
    }
    public void Bootstrap(IReadOnlyList<Server> servers) => Configure(servers, false);
    public void Recover(IReadOnlyList<Server> servers) => Configure(servers, true);
    private void Configure(IReadOnlyList<Server> servers, bool recover)
    {
        CheckAccess(); ArgumentNullException.ThrowIfNull(servers);
        var native = new Native.Server[servers.Count];
        try
        {
            for (var i = 0; i < servers.Count; ++i)
            {
                var server = servers[i]; ValidateId(server.Id); ValidateText(server.Address);
                if (!Enum.IsDefined(server.Role)) throw new ArgumentOutOfRangeException(nameof(servers));
                native[i] = new() { Id = server.Id, Role = (int)server.Role,
                    Address = Marshal.StringToCoTaskMemUTF8(server.Address) };
            }
            Check(Native.Bootstrap(_handle, native, (nuint)native.Length, recover ? 1 : 0));
        }
        finally { foreach (var server in native) Marshal.FreeCoTaskMem(server.Address); }
    }
    public void Start()
    {
        CheckAccess(); Check(Native.Start(_handle));
        try { _transport?.Start(packet => _received.Enqueue(packet), _local); }
        catch { Close(); throw; }
    }
    private void Submit(int operation, byte[] data, ulong id, string address, int role, CompletionCallback callback)
    {
        CheckAccess(); ArgumentNullException.ThrowIfNull(callback);
        if (_closing) throw new RaftException(9);
        if (_nextId == ulong.MaxValue) throw new InvalidOperationException("Request IDs exhausted");
        var requestId = _nextId++;
        // Reserve managed callback storage before native admission.
        _pending.Add(requestId, callback);
        try { Check(Native.Submit(_handle, operation, requestId, data, (nuint)data.Length, id, address, role)); }
        catch { _pending.Remove(requestId); throw; }
    }
    public void Apply(byte[] command, CompletionCallback callback)
    {
        ArgumentNullException.ThrowIfNull(command); Submit(0, command, 0, "", 0, callback);
    }
    public void Barrier(CompletionCallback callback) => Submit(1, [], 0, "", 0, callback);
    public void Add(ulong id, string address, CompletionCallback callback)
    {
        ValidateId(id); ValidateText(address); Submit(2, [], id, address, 0, callback);
    }
    public void Assign(ulong id, ServerRole role, CompletionCallback callback)
    {
        ValidateId(id);
        if (!Enum.IsDefined(role)) throw new ArgumentOutOfRangeException(nameof(role));
        Submit(3, [], id, "", (int)role, callback);
    }
    public void Remove(ulong id, CompletionCallback callback) { ValidateId(id); Submit(4, [], id, "", 0, callback); }
    public void Transfer(ulong id, CompletionCallback callback) => Submit(5, [], id, "", 0, callback);
    public void Poll()
    {
        CheckAccess();
        if (_polling) throw new InvalidOperationException("Recursive Poll");
        _polling = true;
        List<Exception> errors = [];
        try
        {
            while (_received.TryDequeue(out var packet)) if (!_closing) Receive(packet);
            while (_sent.TryDequeue(out var sent))
            {
                var code = Native.TransportComplete(_handle, sent.Id, sent.Status);
                if (code != 19) Check(code);
            }
            Check(Native.Poll(_handle));
            int packetFound;
            while ((packetFound = Native.TransportPeek(_handle, out var packet)) == 1)
            {
                var copy = new Packet(packet.ServerId, Marshal.PtrToStringUTF8(packet.Address) ?? "", Native.Copy(packet.Data, packet.Size));
                Check(Native.TransportComplete(_handle, packet.RequestId, -1));
                var requestId = packet.RequestId; int completed = 0;
                void Done(RaftException? error) {
                    if (Interlocked.Exchange(ref completed, 1) != 0) return;
                    _sent.Enqueue((requestId, error == null ? 0 : error.Code is > 0 and <= 23 ? error.Code : 18));
                }
                try { if (packet.Closing != 0) _transport!.Close(() => Done(null)); else _transport!.Send(copy, Done); }
                catch { Done(new RaftException(18)); }
            }
            if (packetFound < 0) Check(-packetFound);
            while (Read(1).Length != 0) { }
            while (_machine.ObserverErrors.TryDequeue(out var observerError)) errors.Add(observerError);
            int found;
            while ((found = Native.PeekCompletion(_handle, out var value)) == 1)
            {
                var result = Native.Copy(value.Data, value.Size);
                Check(Native.PopCompletion(_handle));
                if (_pending.Remove(value.RequestId, out var callback))
                {
                    try { callback(value.Status == 0 ? null : new RaftException(value.Status), result); }
                    catch (Exception error) { errors.Add(error); }
                }
            }
            if (found < 0) Check(-found);
            Check(Native.GetStatus(_handle, out var status));
            if (_closing && _pending.Count == 0 && status.Closed != 0)
            {
                Closed = true;
                var callbacks = _closeCallbacks.ToArray(); _closeCallbacks.Clear();
                foreach (var callback in callbacks)
                {
                    try { callback(); } catch (Exception error) { errors.Add(error); }
                }
            }
        }
        finally { _polling = false; }
        if (errors.Count != 0) throw new AggregateException(errors);
    }
    public void Close(Action? callback = null)
    {
        if (Closed) { callback?.Invoke(); return; }
        CheckAccess();
        // Reserve before native admission to keep OOM from losing notification.
        if (callback != null) _closeCallbacks.Add(callback);
        try { Check(Native.Close(_handle)); _closing = true; }
        catch { if (callback != null) _closeCallbacks.RemoveAt(_closeCallbacks.Count - 1); throw; }
    }
    public static IReadOnlyDictionary<string, bool> Features => new[] { "uv", "asio", "sqlite", "rocksdb", "lz4", "zstd", "zlib", "asyncpp", "mvfst", "msquic" }
        .Select((name, bit) => (name, enabled: (Native.Features() & (1UL << bit)) != 0)).ToDictionary(pair => pair.name, pair => pair.enabled);
    public static void LoadBackendPlugin(string path) { ValidateText(path); Check(Native.LoadPlugin(path)); }
    public void Receive(Packet packet)
    {
        CheckAccess(); ValidateId(packet.PeerId); ValidateText(packet.Address); ArgumentNullException.ThrowIfNull(packet.Data);
        Check(Native.Receive(_handle, packet.PeerId, packet.Address, packet.Data, (nuint)packet.Data.Length));
    }
    private byte[] Read(int kind)
    {
        nuint size = 0; Check(Native.Read(_handle, kind, null, ref size)); var bytes = new byte[checked((int)size)];
        if (size != 0) Check(Native.Read(_handle, kind, bytes, ref size)); return bytes;
    }
    private Native.Details Details { get { CheckAccess(); Check(Native.GetDetails(_handle, out var value)); return value; } }
    private static string Text(byte[] bytes) { var end = Array.IndexOf(bytes, (byte)0); return Encoding.UTF8.GetString(bytes, 0, end < 0 ? bytes.Length : end); }
    public NodeState State => Status.State;
    public ulong CurrentTerm => Status.Term;
    public ulong VotedFor => Details.VotedFor;
    public ulong LastIndex => Status.LastIndex;
    public ulong CommitIndex => Status.CommitIndex;
    public ulong LastApplied => Status.LastApplied;
    public ulong Timeout => Details.Timeout;
    public ulong Transferee => Details.Transferee;
    public ulong DroppedEvents => Details.DroppedEvents;
    public ServerRole? LocalRole => Details.LocalRole < 0 ? null : (ServerRole)Details.LocalRole;
    public LeaderInfo? Leader => Status.LeaderId == 0 ? null : new(Status.LeaderId, Encoding.UTF8.GetString(Read(2)));
    public ErrorDetails ErrorDetails { get { var d = Details; return new(d.ErrorCode, Text(d.ErrorCategory), d.CauseCode, Text(d.CauseCategory), Text(d.Message)); } }
    public int Error => Status.ErrorCode;
    public string ErrorMessage => ErrorDetails.Message;
    public IReadOnlyList<Server> Configuration
    {
        get {
            CheckAccess(); var bytes = Read(0); List<Server> servers = []; if (bytes.Length == 0) return servers;
            var count = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(1)); int offset = 9;
            for (ulong i = 0; i < count; ++i) {
                var id = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(offset)); offset += 8;
                var end = Array.IndexOf(bytes, (byte)0, offset); if (end < 0) throw new InvalidDataException();
                var address = Encoding.UTF8.GetString(bytes, offset, end - offset); offset = end + 1;
                servers.Add(new(id, address, (ServerRole)bytes[offset++]));
            }
            return servers;
        }
    }
    public PeerStatus GetPeerStatus(ulong id) { CheckAccess(); ValidateId(id); Check(Native.Peer(_handle, id, out var index, out var status)); return new(index, (CatchUpStatus)status); }
    public ulong MatchIndex(ulong id) => GetPeerStatus(id).MatchIndex;
    public CatchUpStatus CatchUp(ulong id) => GetPeerStatus(id).CatchUpStatus;
    private void Set(int key, uint value) { CheckAccess(); Check(Native.Configure(_handle, key, value)); }
    public void SetElectionTimeout(uint value) => Set(0, value);
    public void SetHeartbeatTimeout(uint value) => Set(1, value);
    public void SetInstallSnapshotTimeout(uint value) => Set(2, value);
    public void SetPreVote(bool value) => Set(3, value ? 1u : 0u);
    public void SetSnapshotThreshold(uint value) => Set(4, value);
    public void SetSnapshotTrailing(uint value) => Set(5, value);
    public void SetMaxCatchUpRounds(uint value) => Set(6, value);
    public void SetMaxCatchUpRoundDuration(uint value) => Set(7, value);
    public void SetMaxInflightEntries(uint value) => Set(8, value);
    public void SetCapacityThreshold(ushort value) => Set(9, value);
    public void Seed(uint value) => Set(10, value);
    public void SetStepCallback(Action? callback) { Set(11, callback == null ? 0u : 1u); _machine.Step = callback; }
    public void SetTracer(Action<TraceEvent>? callback) { Set(12, callback == null ? 0u : 1u); _machine.Tracer = callback; }
    public void Dispose()
    {
        if (_disposed) return;
        if (_threadId != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Use the creating thread");
        if (!Closed) throw new InvalidOperationException("Call Close and Poll until Closed before Dispose");
        _handle.Dispose(); _disposed = true;
    }
}
