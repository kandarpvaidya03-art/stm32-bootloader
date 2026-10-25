using FwTool;

const string Usage = """
    usage: FwTool PORT ping
           FwTool PORT send IMAGE.bin [--watch SECONDS]

    PORT is a serial port (COM24, /dev/ttyACM0) or socket://HOST:PORT for the simulator.
    """;

if (args.Length < 2 || (args[1] == "send" && args.Length < 3))
{
    Console.Error.WriteLine(Usage);
    return 64;
}

int watch = 40;
int w = Array.IndexOf(args, "--watch");
if (w >= 0 && w + 1 < args.Length) watch = int.Parse(args[w + 1]);

try
{
    using var link = ITransport.Open(args[0]);
    var updater = new Updater(link, Console.WriteLine);
    Console.WriteLine("sending PING - press the reset button on the board now");
    byte version = updater.Connect();

    switch (args[1])
    {
        case "ping":
            Console.WriteLine($"bootloader answered, protocol version {version}");
            return 0;
        case "send":
            updater.Send(File.ReadAllBytes(args[2]));
            Console.WriteLine($"--- device output for {watch} s ---");
            updater.Watch(watch, Console.WriteLine);
            return 0;
        default:
            Console.Error.WriteLine(Usage);
            return 64;
    }
}
catch (UpdateException e)
{
    Console.Error.WriteLine(e.Message);
    return e.ErrorCode.HasValue ? 1 : 2;
}
