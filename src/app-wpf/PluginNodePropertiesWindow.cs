using System.Windows;
using System.Windows.Controls;

namespace Elka.VoiceMeeterFxHost.App;

internal sealed class PluginNodePropertiesWindow : Window
{
    private readonly ComboBox _mainInputPinsCombo = new();
    private readonly ComboBox _sidechainPinsCombo = new();
    private readonly ComboBox _outputPinsCombo = new();

    public PluginNodePropertiesWindow(PluginNodeSnapshot node)
    {
        Title = $"{node.Name} Properties";
        Width = 360;
        Height = 300;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ResizeMode = ResizeMode.NoResize;

        MainInputPins = Math.Max(1, node.MainInputPins);
        SidechainInputPins = Math.Max(0, node.SidechainInputPins);
        OutputPins = Math.Max(1, node.OutputPins);

        var root = new Grid
        {
            Margin = new Thickness(18)
        };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(135) });
        root.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        AddPinRow(root, 0, "Main input", _mainInputPinsCombo, MainInputPins, [1, 2, 4, 6, 8]);
        AddPinRow(root, 1, "Sidechain input", _sidechainPinsCombo, SidechainInputPins, [0, 1, 2, 4]);
        AddPinRow(root, 2, "Output", _outputPinsCombo, OutputPins, [1, 2, 4, 6, 8]);

        var note = new TextBlock
        {
            Text = "Sidechain pins appear above the main inputs as SL/SR and are mapped to the plugin sidechain bus when the VST3 exposes one.",
            TextWrapping = TextWrapping.Wrap,
            Style = TryFindResource("MutedText") as Style,
            Margin = new Thickness(0, 14, 0, 16)
        };
        Grid.SetRow(note, 3);
        Grid.SetColumnSpan(note, 2);
        root.Children.Add(note);

        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right
        };
        var cancel = new Button
        {
            Content = "Cancel",
            MinWidth = 82,
            Margin = new Thickness(0, 0, 8, 0),
            IsCancel = true
        };
        var apply = new Button
        {
            Content = "Apply",
            MinWidth = 82,
            IsDefault = true,
            Style = TryFindResource("RouteButton") as Style
        };
        apply.Click += (_, _) =>
        {
            MainInputPins = SelectedPinCount(_mainInputPinsCombo, MainInputPins);
            SidechainInputPins = SelectedPinCount(_sidechainPinsCombo, SidechainInputPins);
            OutputPins = SelectedPinCount(_outputPinsCombo, OutputPins);
            DialogResult = true;
        };

        buttons.Children.Add(cancel);
        buttons.Children.Add(apply);
        Grid.SetRow(buttons, 4);
        Grid.SetColumnSpan(buttons, 2);
        root.Children.Add(buttons);

        Content = root;
    }

    public int MainInputPins { get; private set; }
    public int SidechainInputPins { get; private set; }
    public int OutputPins { get; private set; }

    private static void AddPinRow(Grid root, int row, string label, ComboBox combo, int selectedValue, int[] values)
    {
        var text = new TextBlock
        {
            Text = label,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(0, 0, 12, 10)
        };
        Grid.SetRow(text, row);
        root.Children.Add(text);

        foreach (var value in values)
        {
            combo.Items.Add(value);
        }

        combo.SelectedItem = values.Contains(selectedValue) ? selectedValue : values.OrderBy(value => Math.Abs(value - selectedValue)).First();
        combo.Margin = new Thickness(0, 0, 0, 10);
        Grid.SetRow(combo, row);
        Grid.SetColumn(combo, 1);
        root.Children.Add(combo);
    }

    private static int SelectedPinCount(ComboBox combo, int fallback)
    {
        return combo.SelectedItem is int value ? value : fallback;
    }
}
