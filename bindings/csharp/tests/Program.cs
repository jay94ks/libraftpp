using System.Net;
using System.Net.Sockets;
using System.Buffers.Binary;
using Raftpp;

static class Program
{
    private static void Require(bool value, string message)
    {
        if (!value) throw new Exception(message);
    }
    private static string Address()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port; listener.Stop();
        return $"127.0.0.1:{port}";
    }
    private static void Until(RaftNode node, Func<bool> condition)
    {
        var end = Environment.TickCount64 + 12000;
        while (!condition())
        {
            if (Environment.TickCount64 > end) throw new TimeoutException();
            node.Poll(); Thread.Sleep(2);
        }
    }
    private static void Shutdown(RaftNode node)
    {
        if (!node.Closed) { node.Close(); Until(node, () => node.Closed); }
        node.Dispose();
    }
    private static byte[] Command(uint value)
    {
        var bytes = new byte[4]; BinaryPrimitives.WriteUInt32LittleEndian(bytes, value); return bytes;
    }
    private static void Apply(RaftNode node, uint value)
    {
        bool done = false;
        node.Apply(Command(value), (error, output) => { Require(error == null, "apply failure"); done = true; });
        Until(node, () => done);
    }
    private static void Main()
    {
        if (RaftNode.Features["uv"]) Legacy();
        Composition(); Host(); Plugin(); ReentrantClose();
    }
    private static void Composition()
    {
        if (!RaftNode.Features["sqlite"] && !RaftNode.Features["uv"]) return;
        var root = Path.Combine(Path.GetTempPath(), "raftpp-composed-csharp-" + Guid.NewGuid());
        Directory.CreateDirectory(root);
        var storage = RaftNode.Features["sqlite"] ? "sqlite" : "uv";
        var timer = RaftNode.Features["asio"] ? "asio" : "poll";
        var codec = RaftNode.Features["zstd"] ? "zstd" : "none";
        var bus = new System.Collections.Concurrent.ConcurrentDictionary<ulong, (Action<Packet> Receive, Server Local)>();
        var config = Enumerable.Range(1,3).Select(i => new Server((ulong)i, $"node-{i}-" + new string('x', 2500))).ToArray();
        var machines = config.Select(_ => new Counter()).ToArray();
        var transports = config.Select(_ => new BusTransport(bus)).ToArray();
        var nodes = config.Select((server, i) => {
            var directory = Path.Combine(root, i.ToString()); Directory.CreateDirectory(directory);
            if (storage == "sqlite") directory = Path.Combine(directory, "state.sqlite");
            return new RaftNode(new(server.Id, server.Address, directory) {
                ElectionTimeout = 180, HeartbeatTimeout = 30, PreVote = true, SnapshotThreshold = 2,
                SnapshotTrailing = 0,
                IO = new() { Storage = storage, Timer = timer, Codec = codec, Transport = transports[i] }
            }, machines[i]);
        }).ToArray();
        int steps = 0, traces = 0;
        var deadline = Environment.TickCount64 + 12000;
        void Wait(Func<bool> ready) {
            while (!ready()) {
                Require(Environment.TickCount64 < deadline, "cluster timeout");
                foreach (var node in nodes) if (!node.Closed) node.Poll(); Thread.Sleep(2);
            }
        }
        try {
            foreach (var node in nodes) {
                Require(node.Configuration.Count == 0, "initial configuration");
                node.SetStepCallback(() => ++steps); node.SetTracer(trace => { Require(trace.File.Length != 0, "trace source"); ++traces; });
                node.SetElectionTimeout(180); node.SetHeartbeatTimeout(30); node.SetInstallSnapshotTimeout(2000);
                node.SetPreVote(true); node.SetMaxCatchUpRounds(8); node.SetMaxCatchUpRoundDuration(1000);
                node.SetMaxInflightEntries(16); node.SetCapacityThreshold(0); node.Seed((uint)(steps + 6));
                node.Bootstrap(config); node.Start();
            }
            Wait(() => nodes.Count(node => node.State == NodeState.Leader) == 1);
            var leader = nodes.Single(node => node.State == NodeState.Leader);
            Require(leader.Configuration.SequenceEqual(config) && leader.LocalRole == ServerRole.Voter, "configuration/role");
            var leaderInfo = leader.Leader; Require(leaderInfo != null && leader.Configuration.Any(server => server.Id == leaderInfo.Id && server.Address == leaderInfo.Address), "leader");
            Require(leader.CurrentTerm > 0 && leader.Timeout > 0 && leader.Transferee == 0 && leader.ErrorDetails.Code == 0, "queries");
            for (int i = 0; i < 6; ++i) {
                bool done = false; leader.Apply(Command(1), (error, result) => { Require(error == null, "custom send/apply"); done = true; });
                Wait(() => done);
            }
            Wait(() => machines.All(machine => machine.Value == 6));
            var lagging = Array.FindIndex(nodes, node => node != leader); transports[lagging].Pause();
            for (int i = 0; i < 6; ++i) {
                bool done = false; leader.Apply(Command(1), (error, output) => { Require(error == null, "partition apply"); done = true; });
                Wait(() => done);
            }
            Wait(() => machines.Where((machine, i) => i != lagging).All(machine => machine.Value == 12));
            transports[lagging].Resume(); Wait(() => machines[lagging].Value == 12 && machines[lagging].Restores > 0);
            var peer = config.First(server => server.Id != leader.Leader!.Id);
            Require(leader.MatchIndex(peer.Id) > 0 && leader.CatchUp(peer.Id) >= CatchUpStatus.None, "peer progress");
            Require(steps > 0 && traces > 0 && machines.Sum(machine => machine.Finalized) > 0, "events/FSM phases");
            foreach (var node in nodes) { node.SetStepCallback(null); node.SetTracer(null); }
            foreach (var node in nodes) node.Close(); Wait(() => nodes.All(node => node.Closed));
            Console.WriteLine("C# runtime composition/custom asynchronous transport/queries/settings/events passed");
        }
        finally {
            foreach (var node in nodes) if (!node.Closed) node.Close();
            Wait(() => nodes.All(node => node.Closed)); foreach (var node in nodes) node.Dispose();
            Directory.Delete(root, true);
        }
    }
    private static void Host()
    {
        if (!RaftNode.Features["uv"] && !RaftNode.Features["sqlite"]) return;
        var root = Path.Combine(Path.GetTempPath(), "raftpp-host-" + Guid.NewGuid()); Directory.CreateDirectory(root);
        using var applied = new ManualResetEventSlim(); using var closed = new ManualResetEventSlim();
        Exception? failure = null; bool sent = false;
        var address = Address(); failure = null;
        var io = RaftNode.Features["sqlite"] ? new IOOptions { Storage = "sqlite", Timer = "poll",
            Transport = new BusTransport(new()) } : new IOOptions();
        var directory = io.Storage == "sqlite" ? Path.Combine(root, "host.sqlite") : root;
        using var running = new RaftHost(new(1, address, directory) { ElectionTimeout = 150, HeartbeatTimeout = 30, IO = io },
            new Counter(), [new Server(1, address)], onError: error => { failure = error; applied.Set(); });
        var deadline = Environment.TickCount64 + 10000;
        while (!applied.Wait(5)) {
            Require(Environment.TickCount64 < deadline, "host progress without Poll");
            running.Post(node => {
                if (sent || node.State != NodeState.Leader) return; sent = true;
                node.Apply(Command(3), (error, output) => { if (error != null) failure = error; applied.Set(); });
            });
        }
        Require(failure == null, "host error");
        closed.Reset(); running.Close(closed.Set); Require(closed.Wait(10000), "host drain");
        Directory.Delete(root, true); Console.WriteLine("C# automatic RaftHost passed");
    }
    private static void ReentrantClose()
    {
        if (Environment.GetEnvironmentVariable("RAFTPP_BINDING_PLUGIN") == null) return;
        RaftNode? node = null; bool queried = false; List<string> order = [];
        var machine = new ClosingMachine(() => {
            Require(node!.State == NodeState.Leader, "FSM query"); queried = true;
            try { node.Poll(); throw new Exception("recursive Poll accepted"); }
            catch (RaftException error) when (error.Code == 17) { }
            catch (InvalidOperationException) { }
            node.Close(() => order.Add("close"));
        });
        node = new(new(1, "reentrant", "") { IO = new() { Factory = "test-memory" } }, machine);
        try {
            node.Bootstrap([new Server(1, "reentrant")]); node.Start(); Until(node, () => node.State == NodeState.Leader);
            node.Apply(Command(1), (error, output) => order.Add("apply")); Until(node, () => node.Closed);
            Require(queried && order.SequenceEqual(["apply", "close"]), "FSM reentry close ordering");
        } finally { Shutdown(node); }
        Console.WriteLine("C# FSM query/close reentry passed");
    }
    private static void Plugin()
    {
        var path = Environment.GetEnvironmentVariable("RAFTPP_BINDING_PLUGIN"); if (path == null) return;
        RaftNode.LoadBackendPlugin(path);
        var node = new RaftNode(new(1, "plugin", "") { ElectionTimeout = 150, HeartbeatTimeout = 30,
            SnapshotTrailing = 0, IO = new() { Factory = "test-memory" } }, new Counter());
        try {
            node.Bootstrap([new Server(1, "plugin")]); node.Start(); Until(node, () => node.State == NodeState.Leader);
            Apply(node, 4); Require(node.LastApplied > 0, "plugin apply");
        } finally { Shutdown(node); }
        Console.WriteLine("C# plugin/core-only runtime passed");
    }
    private static void Legacy()
    {
        var directory = Path.Combine(Path.GetTempPath(), "raftpp-csharp-" + Guid.NewGuid());
        Directory.CreateDirectory(directory);
        var address = Address();
        var options = new NodeOptions(9007199254740993, address, directory)
        { ElectionTimeout = 150, HeartbeatTimeout = 30, SnapshotThreshold = 2, SnapshotTrailing = 0 };
        RaftNode? node = null;
        try
        {
            var machine = new Counter(); node = new(options, machine);
            node.Bootstrap([new Server(options.Id, address)]); node.Start();
            Until(node, () => node.Status.State == NodeState.Leader);
            Require(node.Status.LeaderId == options.Id, "64-bit ID changed");
            bool applied = false;
            var bytes = Command(3);
            node.Apply(bytes, (error, output) =>
            {
                Require(error == null && BinaryPrimitives.ReadUInt32LittleEndian(output) == 3, "result/copy");
                applied = true;
            });
            Array.Fill(bytes, (byte)255); GC.Collect(); GC.WaitForPendingFinalizers();
            Until(node, () => applied);
            for (var i = 0; i < 5; ++i) Apply(node, 1);
            Until(node, () => machine.Snapshots > 0 && Directory.GetFiles(directory, "snapshot-*").Length > 0);
            Exception? wrongThread = null;
            var thread = new Thread(() => { try { node.Poll(); } catch (Exception error) { wrongThread = error; } });
            thread.Start(); thread.Join(); Require(wrongThread is InvalidOperationException, "thread confinement");
            List<string> order = [];
            node.Barrier((error, output) => order.Add("barrier"));
            node.Close(() => order.Add("close")); Until(node, () => node.Closed);
            Require(order.SequenceEqual(["barrier", "close"]), "completion/close order"); node.Dispose();
            machine = new Counter(); node = new(options, machine); node.Start();
            Until(node, () => machine.Value == 8 && node.Status.State == NodeState.Leader);
            Require(machine.Restores > 0, "snapshot not restored");
            machine.Fail = true;
            bool failed = false;
            node.Apply(Command(1), (error, output) => { Require(error != null, "machine exception status"); failed = true; });
            Until(node, () => failed); Require(node.LastMachineError?.Message == "machine failure", "exception lost");
            Shutdown(node);
            Console.WriteLine("C# bindings: copy, 64-bit IDs, persistence, snapshots, callbacks, errors and lifetime passed");
        }
        finally
        {
            if (node != null) Shutdown(node);
            Directory.Delete(directory, true);
        }
    }
}
sealed class Counter : IStateMachine
{
    internal uint Value;
    internal int Snapshots, Restores, Finalized;
    internal bool Fail;
    public byte[] Apply(byte[] command)
    {
        if (Fail) throw new InvalidOperationException("machine failure");
        Value += BinaryPrimitives.ReadUInt32LittleEndian(command); return Bytes();
    }
    private byte[] Bytes() { var bytes = new byte[4]; BinaryPrimitives.WriteUInt32LittleEndian(bytes, Value); return bytes; }
    public byte[] Snapshot() { ++Snapshots; return Bytes(); }
    public byte[] SnapshotAsync(byte[] snapshot) => snapshot;
    public void SnapshotFinalize() => ++Finalized;
    public void Restore(byte[] snapshot) { ++Restores; Value = BinaryPrimitives.ReadUInt32LittleEndian(snapshot); }
}

sealed class BusTransport(System.Collections.Concurrent.ConcurrentDictionary<ulong, (Action<Packet> Receive, Server Local)> members) : IPacketTransport
{
    private Server? _local;
    private Action<Packet>? _receive;
    public void Start(Action<Packet> receive, Server local) { _local = local; _receive = receive; Resume(); }
    internal void Pause() { if (_local != null) members.TryRemove(_local.Id, out _); }
    internal void Resume() { members[_local!.Id] = (_receive!, _local); }
    public void Send(Packet packet, Action<RaftException?> completed)
    {
        var source = _local!;
        ThreadPool.QueueUserWorkItem(_ => {
            if (!members.ContainsKey(source.Id) || !members.TryGetValue(packet.PeerId, out var peer)) { completed(new RaftException(16)); return; }
            peer.Receive(new(source.Id, source.Address, packet.Data)); completed(null);
        });
    }
    public void Close(Action completed) { if (_local != null) members.TryRemove(_local.Id, out _); ThreadPool.QueueUserWorkItem(_ => completed()); }
}

sealed class ClosingMachine(Action onApply) : IStateMachine
{
    public byte[] Apply(byte[] command) { onApply(); return command; }
    public byte[] Snapshot() => [];
    public void Restore(byte[] snapshot) { }
}
