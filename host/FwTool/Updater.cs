using System.Buffers.Binary;
using System.Diagnostics;
using System.Text;

namespace FwTool;

/// <summary>The device refused a message or stopped answering.</summary>
public sealed class UpdateException : Exception
{
    public byte? ErrorCode { get; }
    public UpdateException(string message, byte? code = null) : base(message) => ErrorCode = code;
}

/// <summary>Host side of the bootloader's update protocol (see README, "UART protocol").
/// Behaves like tools/fwtool.py.</summary>
public sealed class Updater
{
    public const byte MsgPing = 0x01, MsgStart = 0x10, MsgData = 0x11, MsgEnd = 0x12;
    public const byte MsgAck = 0x81, MsgNack = 0x82;
    public const int Chunk = 252;

    // NACKs meaning the frame was damaged on the wire and not acted on: resend.
    private static readonly byte[] Retryable = { 0x02, 0x03 };

    private readonly ITransport _link;
    private readonly FrameReader _reader = new();
    private readonly byte[] _rx = new byte[512];
    private readonly Action<string> _log;

    public Updater(ITransport link, Action<string> log)
    {
        _link = link;
        _log = log;
    }

    private (byte Type, byte[] Payload)? ReadFrame(int timeoutMs)
    {
        var clock = Stopwatch.StartNew();
        while (true)
        {
            var frame = _reader.Next();
            if (frame != null) return frame;
            int left = timeoutMs - (int)clock.ElapsedMilliseconds;
            if (left <= 0) return null;
            int n = _link.Read(_rx, Math.Min(left, 50));
            if (n > 0) _reader.Add(_rx.AsSpan(0, n));
        }
    }

    /// <summary>Pings until the bootloader answers during its listen window after reset.</summary>
    public byte Connect(int timeoutMs = 15000)
    {
        var clock = Stopwatch.StartNew();
        while (clock.ElapsedMilliseconds < timeoutMs)
        {
            _link.Write(Frame.Encode(MsgPing, ReadOnlySpan<byte>.Empty));
            var frame = ReadFrame(200);
            if (frame is { Type: MsgAck } f && f.Payload.Length == 1)
            {
                Thread.Sleep(100); // let replies to the other pings arrive, then drop them
                _link.DiscardInput();
                _reader.Clear();
                return f.Payload[0];
            }
        }
        throw new UpdateException($"no answer from the bootloader within {timeoutMs / 1000} s");
    }

    /// <summary>Sends one message and returns the 32-bit value from its ACK.</summary>
    public uint Request(byte type, byte[] payload, int timeoutMs, int retries = 3)
    {
        for (int attempt = 0; attempt < retries; attempt++)
        {
            _link.Write(Frame.Encode(type, payload));
            var clock = Stopwatch.StartNew();
            while (clock.ElapsedMilliseconds < timeoutMs)
            {
                var frame = ReadFrame(timeoutMs - (int)clock.ElapsedMilliseconds);
                if (frame == null) break;
                var (replyType, reply) = frame.Value;
                if (replyType == MsgAck && reply.Length == 4)
                    return BinaryPrimitives.ReadUInt32LittleEndian(reply);
                if (replyType == MsgNack && reply.Length == 1)
                {
                    if (Array.IndexOf(Retryable, reply[0]) >= 0) break;
                    throw new UpdateException(
                        $"device refused message 0x{type:x2}: error 0x{reply[0]:x2} ({ErrorName(reply[0])})", reply[0]);
                }
            }
        }
        throw new UpdateException($"no reply to message 0x{type:x2} after {retries} attempts");
    }

    public void Send(byte[] image)
    {
        uint crc = Crc32.Compute(image);
        _log($"image: {image.Length} bytes, CRC-32 0x{crc:x8}");
        var clock = Stopwatch.StartNew();

        var start = new byte[8];
        BinaryPrimitives.WriteUInt32LittleEndian(start, (uint)image.Length);
        BinaryPrimitives.WriteUInt32LittleEndian(start.AsSpan(4), crc);
        Request(MsgStart, start, 10000);
        _log("slot B erased");

        int offset = 0;
        while (offset < image.Length)
        {
            int n = Math.Min(Chunk, image.Length - offset);
            var data = new byte[4 + n];
            BinaryPrimitives.WriteUInt32LittleEndian(data, (uint)offset);
            Array.Copy(image, offset, data, 4, n);
            uint next = Request(MsgData, data, 1000);
            if (next != offset + n)
                throw new UpdateException($"device is at offset {next}, expected {offset + n}");
            offset = (int)next;
        }
        _log($"sent {offset} / {image.Length} bytes");

        Request(MsgEnd, Array.Empty<byte>(), 30000);
        _log("image accepted: CRC and signature verified on the device");
        _log($"transfer took {clock.Elapsed.TotalSeconds:F2} s");
    }

    /// <summary>Prints the device's log text for a while (install, trial, confirm).</summary>
    public void Watch(int seconds, Action<string> output)
    {
        var clock = Stopwatch.StartNew();
        var line = new StringBuilder();
        while (clock.Elapsed.TotalSeconds < seconds)
        {
            int n = _link.Read(_rx, 100);
            for (int i = 0; i < n; i++)
            {
                char c = (char)_rx[i];
                if (c == '\n') { output(line.ToString().TrimEnd('\r')); line.Clear(); }
                else if (c >= ' ' && c < 127 || c == '\r') line.Append(c);
            }
        }
        if (line.Length > 0) output(line.ToString());
    }

    public static string ErrorName(byte code) => code switch
    {
        0x02 => "frame length too large",
        0x03 => "frame CRC mismatch",
        0x10 => "unknown message type",
        0x20 => "bad payload length",
        0x21 => "image too large",
        0x22 => "flash erase or write failed",
        0x23 => "message not valid now",
        0x24 => "unexpected data offset",
        0x25 => "image CRC mismatch",
        0x26 => "image header or hash invalid",
        0x27 => "signature invalid",
        0x28 => "update already in progress",
        _ => "unknown error",
    };
}
