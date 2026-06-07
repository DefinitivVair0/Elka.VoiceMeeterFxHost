using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Elka.VoiceMeeterFxHost.App;

internal sealed record VbanTextMessage(
    string StreamName,
    string Text,
    IPAddress RemoteAddress);

internal sealed class VbanTextListener : IDisposable
{
    private const int HeaderLength = 28;
    private const byte ProtocolMask = 0xE0;
    private const byte TextProtocol = 0x40;

    private readonly Action<VbanTextMessage> _messageReceived;
    private readonly Action<string>? _diagnostic;
    private readonly UdpClient _udpClient;
    private readonly CancellationTokenSource _cancellation = new();
    private readonly Task _receiveTask;
    private DateTime _lastDiagnosticUtc;
    private bool _disposed;

    public VbanTextListener(
        int port,
        string streamName,
        bool localOnly,
        Action<VbanTextMessage> messageReceived,
        Action<string>? diagnostic = null)
    {
        Port = port;
        StreamName = streamName.Trim();
        LocalOnly = localOnly;
        _messageReceived = messageReceived;
        _diagnostic = diagnostic;
        _udpClient = new UdpClient(new IPEndPoint(IPAddress.Any, port));
        _receiveTask = Task.Run(ReceiveLoop);
    }

    public int Port { get; }

    public string StreamName { get; }

    public bool LocalOnly { get; }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _cancellation.Cancel();
        _udpClient.Dispose();
        _cancellation.Dispose();
    }

    private async Task ReceiveLoop()
    {
        while (!_cancellation.IsCancellationRequested)
        {
            UdpReceiveResult result;
            try
            {
                result = await _udpClient.ReceiveAsync(_cancellation.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch
            {
                continue;
            }

            if (TryParseMessage(result.Buffer, result.RemoteEndPoint.Address, out var message))
            {
                _messageReceived(message);
            }
        }
    }

    private bool TryParseMessage(byte[] packet, IPAddress remoteAddress, out VbanTextMessage message)
    {
        message = new VbanTextMessage(string.Empty, string.Empty, remoteAddress);

        if (LocalOnly && !IPAddress.IsLoopback(remoteAddress))
        {
            ReportDiagnostic($"VBAN packet ignored from {remoteAddress}: Local only is enabled.");
            return false;
        }

        if (packet.Length <= HeaderLength
            || packet[0] != (byte)'V'
            || packet[1] != (byte)'B'
            || packet[2] != (byte)'A'
            || packet[3] != (byte)'N'
            || (packet[4] & ProtocolMask) != TextProtocol)
        {
            return false;
        }

        var packetStreamName = ReadNullTerminatedAscii(packet, start: 8, length: 16);
        if (!string.IsNullOrWhiteSpace(StreamName)
            && !string.Equals(packetStreamName, StreamName, StringComparison.OrdinalIgnoreCase))
        {
            ReportDiagnostic($"VBAN text ignored from {remoteAddress}: stream '{packetStreamName}' does not match '{StreamName}'.");
            return false;
        }

        var textLength = packet.Length - HeaderLength;
        while (textLength > 0 && packet[HeaderLength + textLength - 1] == 0)
        {
            textLength--;
        }

        if (textLength <= 0)
        {
            return false;
        }

        var text = Encoding.UTF8.GetString(packet, HeaderLength, textLength).Trim();
        if (text.Length == 0)
        {
            return false;
        }

        message = new VbanTextMessage(packetStreamName, text, remoteAddress);
        return true;
    }

    private void ReportDiagnostic(string message)
    {
        if (_diagnostic is null)
        {
            return;
        }

        var now = DateTime.UtcNow;
        if (now - _lastDiagnosticUtc < TimeSpan.FromSeconds(1))
        {
            return;
        }

        _lastDiagnosticUtc = now;
        _diagnostic(message);
    }

    private static string ReadNullTerminatedAscii(byte[] bytes, int start, int length)
    {
        var end = start;
        var maxEnd = Math.Min(bytes.Length, start + length);
        while (end < maxEnd && bytes[end] != 0)
        {
            end++;
        }

        return Encoding.ASCII.GetString(bytes, start, end - start).Trim();
    }
}
