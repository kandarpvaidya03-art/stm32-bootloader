using System.IO.Ports;

namespace FwTool;

/// <summary>The ST-Link virtual COM port: 115200 baud, 8N1.</summary>
public sealed class SerialTransport : ITransport
{
    private readonly SerialPort _port;

    public SerialTransport(string name)
    {
        _port = new SerialPort(name, 115200, Parity.None, 8, StopBits.One);
        _port.Open();
    }

    public void Write(byte[] data) => _port.Write(data, 0, data.Length);

    public int Read(byte[] buffer, int timeoutMs)
    {
        _port.ReadTimeout = Math.Max(1, timeoutMs);
        try
        {
            return _port.Read(buffer, 0, buffer.Length);
        }
        catch (TimeoutException)
        {
            return 0;
        }
    }

    public void DiscardInput() => _port.DiscardInBuffer();

    public void Dispose() => _port.Dispose();
}
