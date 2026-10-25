using System.Buffers.Binary;

namespace FwTool;

/// <summary>The bootloader's UART frame:
/// 0xA5, type, 2-byte length (LE), payload, CRC-32 (LE) over type+length+payload.</summary>
public static class Frame
{
    public const byte Sof = 0xA5;
    public const int MaxPayload = 256;

    public static byte[] Encode(byte type, ReadOnlySpan<byte> payload)
    {
        if (payload.Length > MaxPayload)
            throw new ArgumentException("payload too long", nameof(payload));
        var frame = new byte[8 + payload.Length];
        frame[0] = Sof;
        frame[1] = type;
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(2), (ushort)payload.Length);
        payload.CopyTo(frame.AsSpan(4));
        uint crc = Crc32.Compute(frame.AsSpan(1, 3 + payload.Length));
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(4 + payload.Length), crc);
        return frame;
    }
}

/// <summary>Finds valid frames in a byte stream that also carries log text.</summary>
public sealed class FrameReader
{
    private readonly List<byte> _buf = new();

    public void Add(ReadOnlySpan<byte> data)
    {
        foreach (byte b in data) _buf.Add(b);
    }

    public void Clear() => _buf.Clear();

    /// <summary>Returns the next valid frame, or null if none is complete yet.</summary>
    public (byte Type, byte[] Payload)? Next()
    {
        while (true)
        {
            int start = _buf.IndexOf(Frame.Sof);
            if (start < 0) { _buf.Clear(); return null; }
            _buf.RemoveRange(0, start);
            if (_buf.Count < 4) return null;

            int length = _buf[2] | (_buf[3] << 8);
            if (length > Frame.MaxPayload) { _buf.RemoveAt(0); continue; }
            int total = 8 + length;
            if (_buf.Count < total) return null;

            byte[] frame = _buf.GetRange(0, total).ToArray();
            uint crc = BinaryPrimitives.ReadUInt32LittleEndian(frame.AsSpan(4 + length));
            if (Crc32.Compute(frame.AsSpan(1, 3 + length)) == crc)
            {
                _buf.RemoveRange(0, total);
                return (frame[1], frame.AsSpan(4, length).ToArray());
            }
            _buf.RemoveAt(0); // not a real frame start: resynchronise
        }
    }
}
