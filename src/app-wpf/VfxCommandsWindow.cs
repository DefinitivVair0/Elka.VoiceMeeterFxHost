using System.Windows;
using System.Windows.Controls;

namespace Elka.VoiceMeeterFxHost.App;

internal sealed class VfxCommandsWindow : Window
{
    private readonly TextBox _commandsTextBox;

    public VfxCommandsWindow()
    {
        Title = "VFX Text Commands";
        Width = 820;
        Height = 680;
        MinWidth = 640;
        MinHeight = 480;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;

        var root = new Grid
        {
            Margin = new Thickness(18)
        };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        root.Children.Add(new TextBlock
        {
            Text = "VFX Text Command Reference",
            FontSize = 22,
            FontWeight = FontWeights.SemiBold,
            Margin = new Thickness(0, 0, 0, 12)
        });

        _commandsTextBox = new TextBox
        {
            Text = CommandReference,
            IsReadOnly = true,
            AcceptsReturn = true,
            AcceptsTab = true,
            TextWrapping = TextWrapping.Wrap,
            FontFamily = new System.Windows.Media.FontFamily("Consolas"),
            FontSize = 13,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Auto
        };
        Grid.SetRow(_commandsTextBox, 1);
        root.Children.Add(_commandsTextBox);

        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 12, 0, 0)
        };
        var copyButton = new Button
        {
            Content = "Copy",
            Width = 88,
            Margin = new Thickness(0, 0, 8, 0)
        };
        copyButton.Click += (_, _) => Clipboard.SetText(_commandsTextBox.Text);
        buttons.Children.Add(copyButton);

        var closeButton = new Button
        {
            Content = "Close",
            Width = 88
        };
        closeButton.Click += (_, _) => Close();
        buttons.Children.Add(closeButton);

        Grid.SetRow(buttons, 2);
        root.Children.Add(buttons);

        Content = root;
    }

    private const string CommandReference = """
Elka VoiceMeeter FX Host VFX Text Commands

MacroButtons sends these over VBAN-TEXT:

SendText("vban1", VFX.Strip(0).Ch(1).Delay=25;);

The MacroButtons VBAN-TEXT slot must use the same port and stream name as the app.
Default app settings are port 6981 and stream Command1.

Targets

Strip(...) = input endpoint. Numbers are zero-based.
Bus(...)   = output bus endpoint. Numbers are zero-based, or use A1-A5 / B1-B3.

For Potato input strips:
Strip(0) = Hardware In 1
Strip(1) = Hardware In 2
Strip(2) = Hardware In 3
Strip(3) = Hardware In 4
Strip(4) = Hardware In 5
Strip(5) = VAIO
Strip(6) = AUX
Strip(7) = VAIO3

Channel selection is one-based:
Ch(1)
Ch(1-2)
Ch(1,3,5)
Ch(All)
Ch(*)

Enable delay/volume processing:
SendText("vban1", VFX.Strip(0).Ch(1).Enable=1;);
SendText("vban1", VFX.Strip(0).Ch(1).Enable=0;);
SendText("vban1", VFX.Bus(B1).Ch(1).Enable=1;);

Delay:
SendText("vban1", VFX.Strip(0).Ch(1).Delay=25;);
SendText("vban1", VFX.Strip(0).Ch(1).Delay+=10;);
SendText("vban1", VFX.Strip(0).Ch(1).Delay-=10;);
SendText("vban1", VFX.Bus(B1).Ch(1).Delay=25;);

Volume:
SendText("vban1", VFX.Strip(0).Ch(1).Volume=100;);
SendText("vban1", VFX.Strip(0).Ch(1).Volume+=5;);
SendText("vban1", VFX.Strip(0).Ch(1).Volume-=5;);
SendText("vban1", VFX.Bus(B1).Ch(1).Volume=100;);

Direct routing, input strips only:
SendText("vban1", VFX.Strip(0).Ch(1).Route=Bus(B1).Ch(3););
SendText("vban1", VFX.Strip(0).Ch(1).Route+=Bus(B2).Ch(4););
SendText("vban1", VFX.Strip(0).Ch(1).Route-=Bus(B1).Ch(3););

Enable saved route destinations:
SendText("vban1", VFX.Strip(0).Ch(1).RouteEnable=1;);
SendText("vban1", VFX.Strip(0).Ch(1).RouteEnable=0;);

Mute standard routing for a routed input channel:
SendText("vban1", VFX.Strip(0).Ch(1).MuteNormal=1;);
SendText("vban1", VFX.Strip(0).Ch(1).MuteNormal=0;);

Combined commands:
SendText("vban1", VFX.Strip(5).Ch(1).Route=Bus(B1).Ch(1); VFX.Strip(5).Ch(1).MuteNormal=1; VFX.Strip(5).Ch(1).Delay=20;);

Aliases

Enable: Enable, Enabled
Delay: Delay, DelayMs, Ms
Volume: Volume, Vol, Gain
Route enable: RouteEnable, RouteEnabled
Mute normal: MuteNormal, RouteMute, MuteRoute, RouteMuteNormal
Boolean values: 1, 0, true, false, on, off, yes, no

This V1 command surface controls delay, volume, direct routing, and mute-standard routing.
It intentionally does not control VST plugins or VST node wiring.
""";
}
