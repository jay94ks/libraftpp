using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Raftpp;

internal sealed class MachineContext(IStateMachine machine)
{
    internal readonly IStateMachine Machine = machine;
    internal Exception? LastError;
    internal Action? Step;
    internal Action<TraceEvent>? Tracer;
    internal readonly System.Collections.Concurrent.ConcurrentQueue<Exception> ObserverErrors = new();
}

internal sealed class NodeHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    private GCHandle _context;
    internal NodeHandle(IntPtr handle, GCHandle context) : base(true)
    {
        SetHandle(handle);
        _context = context;
    }
    protected override bool ReleaseHandle()
    {
        Native.Release(handle); // Disables callbacks before returning; close drains off-thread.
        if (_context.IsAllocated) _context.Free();
        return true;
    }
}

internal static class Native
{
    private const string Library = "raftpp-bindings";
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int MachineCallback(IntPtr context, int operation, IntPtr data, nuint size, IntPtr output);
    // The delegate remains rooted for the entire native callback lifetime.
    internal static readonly MachineCallback Callback = InvokeMachine;

    [StructLayout(LayoutKind.Sequential)]
    internal struct Options
    {
        internal uint ElectionTimeout, HeartbeatTimeout, SnapshotThreshold, SnapshotTrailing;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct RuntimeOptions
    {
        internal Options Node;
        internal uint InstallSnapshotTimeout, MaxCatchUpRounds, MaxCatchUpRoundDuration,
            MaxInflightEntries, CapacityThreshold, PreVote, ForceBarrier, RawPayloads, SnapshotAsync;
        [MarshalAs(UnmanagedType.LPUTF8Str)] internal string Factory;
        [MarshalAs(UnmanagedType.LPUTF8Str)] internal string Storage;
        [MarshalAs(UnmanagedType.LPUTF8Str)] internal string Timer;
        [MarshalAs(UnmanagedType.LPUTF8Str)] internal string Network;
        [MarshalAs(UnmanagedType.LPUTF8Str)] internal string Codec;
        internal IntPtr Configuration;
        internal nuint ConfigurationSize;
        internal ulong MaxSnapshotSize, MaxEncodedSnapshotSize;
        internal uint RandomSeed;
        internal ulong SegmentSize, BlockSize, MaxEntrySize, MaxEntries;
        internal uint DiskRetry, ConnectRetry, AutoRecovery;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Details
    {
        internal ulong VotedFor, Timeout, Transferee, DroppedEvents;
        internal int LocalRole, ErrorCode, CauseCode;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)] internal byte[] ErrorCategory;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)] internal byte[] CauseCategory;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)] internal byte[] Message;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2048)] internal byte[] LeaderAddress;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Packet
    {
        internal ulong RequestId, ServerId;
        internal IntPtr Address, Data;
        internal nuint Size;
        internal int Closing;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Server
    {
        internal ulong Id;
        internal IntPtr Address;
        internal int Role;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Status
    {
        internal ulong Term, LastIndex, CommitIndex, LastApplied, LeaderId;
        internal int State, Closed, ErrorCode;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Completion
    {
        internal ulong RequestId;
        internal int Status;
        internal IntPtr Data;
        internal nuint Size;
    }
    internal static byte[] Copy(IntPtr data, nuint size)
    {
        var result = new byte[checked((int)size)];
        if (result.Length != 0) Marshal.Copy(data, result, 0, result.Length);
        return result;
    }
    private static unsafe int InvokeMachine(IntPtr context, int operation, IntPtr data, nuint size, IntPtr output)
    {
        MachineContext? state = null;
        try
        {
            state = (MachineContext)GCHandle.FromIntPtr(context).Target!;
            if (operation >= 5)
            {
                try {
                    if (operation == 5) state.Step?.Invoke();
                    else {
                        var bytes = Copy(data, size);
                        var parts = System.Text.Encoding.UTF8.GetString(bytes, 9, bytes.Length - 9).Split('\0');
                        state.Tracer?.Invoke(new(System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(1)),
                            System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(5)), parts[0],
                            parts.Length > 1 ? parts[1] : "", parts.Length > 2 ? parts[2] : "",
                            parts.Length > 3 && uint.TryParse(parts[3], out var line) ? line : 0));
                    }
                } catch (Exception error) { state.ObserverErrors.Enqueue(error); }
                return 0;
            }
            if (operation == 2)
            {
                state.Machine.Restore(Copy(data, size));
                return 0;
            }
            if (operation == 4) { state.Machine.SnapshotFinalize(); return 0; }
            var result = operation == 0 ? state.Machine.Apply(Copy(data, size)) :
                operation == 3 ? state.Machine.SnapshotAsync(Copy(data, size)) : state.Machine.Snapshot();
            ArgumentNullException.ThrowIfNull(result);
            fixed (byte* bytes = result) return SetOutput(output, (IntPtr)bytes, (nuint)result.Length);
        }
        catch (Exception error)
        {
            if (state != null) state.LastError = error;
            return 20; // errc::invalid; exceptions never cross the native boundary.
        }
    }
    [DllImport(Library, EntryPoint = "raftpp_binding_abi_version", CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint AbiVersion();
    [DllImport(Library, EntryPoint = "raftpp_binding_set_output", CallingConvention = CallingConvention.Cdecl)]
    private static extern int SetOutput(IntPtr output, IntPtr data, nuint size);
    [DllImport(Library, EntryPoint = "raftpp_binding_create", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int Create(ulong id, [MarshalAs(UnmanagedType.LPUTF8Str)] string address,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string directory, in Options options,
        MachineCallback callback, IntPtr context, out IntPtr node);
    [DllImport(Library, EntryPoint = "raftpp_binding_bootstrap", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int Bootstrap(NodeHandle node, [In] Server[] servers, nuint size, int recover);
    [DllImport(Library, EntryPoint = "raftpp_binding_start", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int Start(NodeHandle node);
    [DllImport(Library, EntryPoint = "raftpp_binding_poll", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int Poll(NodeHandle node);
    [DllImport(Library, EntryPoint = "raftpp_binding_get_status", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int GetStatus(NodeHandle node, out Status status);
    [DllImport(Library, EntryPoint = "raftpp_binding_submit", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int Submit(NodeHandle node, int operation, ulong requestId,
        byte[] data, nuint size, ulong serverId, [MarshalAs(UnmanagedType.LPUTF8Str)] string address, int role);
    [DllImport(Library, EntryPoint = "raftpp_binding_peek_completion", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int PeekCompletion(NodeHandle node, out Completion completion);
    [DllImport(Library, EntryPoint = "raftpp_binding_pop_completion", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int PopCompletion(NodeHandle node);
    [DllImport(Library, EntryPoint = "raftpp_binding_close", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int Close(NodeHandle node);
    [DllImport(Library, EntryPoint = "raftpp_binding_release", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void Release(IntPtr node);
    [DllImport(Library, EntryPoint = "raftpp_binding_create_runtime", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int CreateRuntime(ulong id, [MarshalAs(UnmanagedType.LPUTF8Str)] string address,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string directory, in RuntimeOptions options,
        MachineCallback callback, IntPtr context, out IntPtr node);
    [DllImport(Library, EntryPoint = "raftpp_binding_features", CallingConvention = CallingConvention.Cdecl)]
    internal static extern ulong Features();
    [DllImport(Library, EntryPoint = "raftpp_binding_load_plugin", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int LoadPlugin([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [DllImport(Library, EntryPoint = "raftpp_binding_configure", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int Configure(NodeHandle node, int key, ulong value);
    [DllImport(Library, EntryPoint = "raftpp_binding_details_get", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int GetDetails(NodeHandle node, out Details value);
    [DllImport(Library, EntryPoint = "raftpp_binding_peer", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int Peer(NodeHandle node, ulong id, out ulong index, out int catchUp);
    [DllImport(Library, EntryPoint = "raftpp_binding_read", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int Read(NodeHandle node, int kind, [Out] byte[]? data, ref nuint size);
    [DllImport(Library, EntryPoint = "raftpp_binding_transport_peek", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int TransportPeek(NodeHandle node, out Packet packet);
    [DllImport(Library, EntryPoint = "raftpp_binding_transport_complete", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int TransportComplete(NodeHandle node, ulong id, int status);
    [DllImport(Library, EntryPoint = "raftpp_binding_receive", CallingConvention = CallingConvention.Cdecl)]
    internal static extern int Receive(NodeHandle node, ulong id, [MarshalAs(UnmanagedType.LPUTF8Str)] string address, byte[] data, nuint size);

}
