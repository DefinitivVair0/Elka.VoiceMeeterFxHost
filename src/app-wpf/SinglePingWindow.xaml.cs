using System.Windows;
using System.Windows.Controls;

namespace Elka.VoiceMeeterFxHost.App;

public partial class SinglePingWindow : Window
{
    private readonly VoicemeeterKind _kind;

    public SinglePingWindow(VoicemeeterKind kind)
    {
        InitializeComponent();
        _kind = kind;
        PopulateEndpoints();
        UpdateSelectionText();
    }

    private void PopulateEndpoints()
    {
        foreach (var endpoint in VoicemeeterIoLayout.GetEndpoints(CallbackMode.Input, _kind))
        {
            PingInputComboBox.Items.Add(endpoint);
        }

        foreach (var endpoint in VoicemeeterIoLayout.GetEndpoints(CallbackMode.Output, _kind))
        {
            ReturnOutputComboBox.Items.Add(endpoint);
        }

        PingInputComboBox.SelectedIndex = PingInputComboBox.Items.Count > 0 ? 0 : -1;
        ReturnOutputComboBox.SelectedIndex = ReturnOutputComboBox.Items.Count > 0 ? 0 : -1;
    }

    private void EndpointComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        UpdateSelectionText();
    }

    private void PingButton_Click(object sender, RoutedEventArgs e)
    {
        if (PingInputComboBox.SelectedItem is not IoEndpoint input ||
            ReturnOutputComboBox.SelectedItem is not IoEndpoint output)
        {
            ResultTextBlock.Text = "Pick an input and return output first.";
            return;
        }

        ResultTextBlock.Text =
            $"Single-ping setup staged: {input.DisplayName} -> {output.DisplayName}.\n" +
            "The WPF shell is ready; the native engine bridge will run the actual ping generator/listener in the next slice.";
    }

    private void CloseButton_Click(object sender, RoutedEventArgs e)
    {
        Close();
    }

    private void UpdateSelectionText()
    {
        var input = PingInputComboBox.SelectedItem as IoEndpoint;
        var output = ReturnOutputComboBox.SelectedItem as IoEndpoint;
        SelectionTextBlock.Text = input is null || output is null
            ? string.Empty
            : $"{input.Name} -> {output.Name}";
    }
}
