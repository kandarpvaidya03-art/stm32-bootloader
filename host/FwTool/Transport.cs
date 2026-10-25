using System.Net.Sockets;

namespace FwTool;

/// <summary>A byte pipe to the board: a serial port, or the simulator over TCP.</summary>
public interface ITransport : IDisposable
{
    void Write(byte[] data);

    /// <summary>Reads what is available, waiting up to timeoutMs. Returns 0 on timeout.</summary>
    int Read(byte[] buffer, int timeoutMs);

    void DiscardInput();

    static ITransport Open(string port)
    {
        if (port.StartsWith("socket://", StringComparison.OrdinalIgnoreCase))
        {
            var hostPort = port["socket://".Length..].Split(':');
            return new TcpTransport(hostPort[0], int.Parse(hostPort[1]));
        }
#if NO_SERIAL
        throw new NotSupportedException("built without serial support; use socket://host:port");
#else
        return new SerialTransport(port);
#endif
    }
}

public sealed class TcpTransport : ITransport
{
    private readonly TcpClient _client;
    private readonly NetworkStream _stream;

    public TcpTransport(string host, int port)
    {
        _client = new TcpClient(host, port) { NoDelay = true };
        _stream = _client.GetStream();
    }

    public void Write(byte[] data) => _stream.Write(data);

    public int Read(byte[] buffer, int timeoutMs)
    {
        _stream.ReadTimeout = Math.Max(1, timeoutMs);
        try
        {
            return _stream.Read(buffer, 0, buffer.Length);
        }
        catch (IOException e) when (e.InnerException is SocketException { SocketErrorCode: SocketError.TimedOut })
        {
            return 0;
        }
    }

    public void DiscardInput()
    {
        var scratch = new byte[4096];
        while (_client.Available > 0)
            _stream.Read(scratch, 0, Math.Min(scratch.Length, _client.Available));
    }

    public void Dispose() => _client.Dispose();
}
