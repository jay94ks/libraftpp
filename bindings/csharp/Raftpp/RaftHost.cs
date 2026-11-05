using System.Collections.Concurrent;
namespace Raftpp;

// Owns the creating thread and event-loop progress. All posted work, FSM hooks,
// request completions and shutdown callbacks execute on that same thread.
public sealed class RaftHost : IDisposable
{
    private readonly object _gate = new();
    private readonly ConcurrentQueue<Action<RaftNode>> _work = new();
    private readonly List<Action> _closedCallbacks = [];
    private readonly AutoResetEvent _wake = new(false);
    private readonly Thread _thread;
    private readonly Action<Exception>? _onError;
    private bool _closing, _closed;
    public bool Closed { get { lock (_gate) return _closed; } }
    public RaftHost(NodeOptions options, IStateMachine machine, IReadOnlyList<Server>? bootstrap = null,
        Action<RaftNode>? started = null, Action<Exception>? onError = null)
    {
        ArgumentNullException.ThrowIfNull(options); ArgumentNullException.ThrowIfNull(machine);
        var configuration = bootstrap?.ToArray(); _onError = onError;
        _thread = new Thread(() => Run(options, machine, configuration, started)) { Name = "raftpp" };
        _thread.Start();
    }
    public void Post(Action<RaftNode> action)
    {
        ArgumentNullException.ThrowIfNull(action);
        lock (_gate)
        {
            if (_closing || _closed) throw new ObjectDisposedException(nameof(RaftHost));
            _work.Enqueue(action); _wake.Set();
        }
    }
    public void Close(Action? completed = null)
    {
        bool immediate;
        lock (_gate)
        {
            immediate = _closed;
            if (!immediate)
            {
                if (completed != null) _closedCallbacks.Add(completed);
                if (!_closing) { _closing = true; _work.Enqueue(node => node.Close()); }
                _wake.Set();
            }
        }
        if (immediate) completed?.Invoke();
    }
    public void Dispose() => Close();
    private void Report(Exception error) { try { _onError?.Invoke(error); } catch { } }
    private void Run(NodeOptions options, IStateMachine machine, Server[]? bootstrap, Action<RaftNode>? started)
    {
        RaftNode? node = null;
        try
        {
            node = new(options, machine);
            if (bootstrap != null)
            {
                try { node.Bootstrap(bootstrap); } catch (RaftException error) when (error.Code == 10) { }
            }
            node.Start(); started?.Invoke(node);
            while (!node.Closed)
            {
                while (_work.TryDequeue(out var action)) { try { action(node); } catch (Exception error) { Report(error); } }
                if (!node.Closed) { try { node.Poll(); } catch (Exception error) { Report(error); } }
                if (!node.Closed) _wake.WaitOne(5);
            }
        }
        catch (Exception error) { Report(error); }
        finally
        {
            if (node != null)
            {
                if (!node.Closed)
                {
                    try { node.Close(); while (!node.Closed) { node.Poll(); _wake.WaitOne(1); } }
                    catch (Exception error) { Report(error); }
                }
                if (node.Closed) node.Dispose();
            }
            Action[] callbacks;
            lock (_gate) { _closed = true; _closing = true; callbacks = _closedCallbacks.ToArray(); _closedCallbacks.Clear(); _wake.Dispose(); }
            foreach (var callback in callbacks) { try { callback(); } catch (Exception error) { Report(error); } }
        }
    }
}
