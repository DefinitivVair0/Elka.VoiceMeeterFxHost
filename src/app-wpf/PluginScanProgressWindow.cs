using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Elka.VoiceMeeterFxHost.App;

internal sealed class PluginScanProgressWindow : Window
{
    private readonly TextBlock _elapsedTextBlock;
    private readonly TextBlock _countTextBlock;
    private readonly TextBlock _stageTextBlock;
    private readonly TextBlock _pathTextBlock;
    private readonly ProgressBar _progressBar;

    public PluginScanProgressWindow()
    {
        Title = "VST Scan Progress";
        Width = 760;
        Height = 360;
        MinWidth = 560;
        MinHeight = 280;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = Brush("WindowBackgroundBrush", "#111417");
        Foreground = Brush("TextBrush", "#EDF3F6");

        var root = new Grid
        {
            Margin = new Thickness(18)
        };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var titlePanel = new DockPanel
        {
            Margin = new Thickness(0, 0, 0, 12)
        };
        _elapsedTextBlock = new TextBlock
        {
            Text = "0s",
            Style = TryFindResource("MutedText") as Style,
            VerticalAlignment = VerticalAlignment.Bottom
        };
        DockPanel.SetDock(_elapsedTextBlock, Dock.Right);
        titlePanel.Children.Add(_elapsedTextBlock);
        titlePanel.Children.Add(new TextBlock
        {
            Text = "VST Scan",
            FontSize = 22,
            FontWeight = FontWeights.SemiBold
        });
        root.Children.Add(titlePanel);

        var card = new Border
        {
            Background = Brush("PanelBrush", "#1A2025"),
            BorderBrush = Brush("StrokeBrush", "#33414A"),
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(8),
            Padding = new Thickness(16)
        };
        Grid.SetRow(card, 1);
        root.Children.Add(card);

        var stack = new StackPanel();
        card.Child = stack;

        _countTextBlock = new TextBlock
        {
            Text = "Preparing",
            FontWeight = FontWeights.SemiBold,
            Margin = new Thickness(0, 0, 0, 8),
            TextTrimming = TextTrimming.None
        };
        stack.Children.Add(_countTextBlock);

        _progressBar = new ProgressBar
        {
            Height = 12,
            Minimum = 0,
            Maximum = 1,
            IsIndeterminate = true,
            Margin = new Thickness(0, 0, 0, 14)
        };
        stack.Children.Add(_progressBar);

        _stageTextBlock = new TextBlock
        {
            Text = "Starting scanner...",
            FontWeight = FontWeights.SemiBold,
            Margin = new Thickness(0, 0, 0, 8),
            TextTrimming = TextTrimming.None,
            TextWrapping = TextWrapping.Wrap
        };
        stack.Children.Add(_stageTextBlock);

        _pathTextBlock = new TextBlock
        {
            Text = string.Empty,
            FontFamily = new FontFamily("Consolas"),
            FontSize = 12,
            Foreground = Brush("MutedTextBrush", "#9AA8B2"),
            TextTrimming = TextTrimming.None,
            TextWrapping = TextWrapping.Wrap
        };
        stack.Children.Add(_pathTextBlock);

        var closeButton = new Button
        {
            Content = "Close",
            Width = 96,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 14, 0, 0)
        };
        closeButton.Click += (_, _) => Close();
        Grid.SetRow(closeButton, 2);
        root.Children.Add(closeButton);

        Content = root;
    }

    public void UpdateProgress(string rawProgress, TimeSpan elapsed, bool finished = false)
    {
        var snapshot = ParseProgress(rawProgress);
        var seconds = Math.Max(0, elapsed.TotalSeconds);
        _elapsedTextBlock.Text = $"{seconds:0}s";

        var stage = string.IsNullOrWhiteSpace(snapshot.Stage)
            ? finished ? "Plugin scan finished." : "Waiting for scanner..."
            : snapshot.Stage;
        _stageTextBlock.Text = stage;
        _pathTextBlock.Text = snapshot.Path;

        if (snapshot.Total > 0)
        {
            _countTextBlock.Text = $"{snapshot.Current:n0} / {snapshot.Total:n0}";
            _progressBar.IsIndeterminate = false;
            _progressBar.Maximum = snapshot.Total;
            _progressBar.Value = Math.Clamp(snapshot.Current, 0, snapshot.Total);
        }
        else if (snapshot.Current > 0)
        {
            _countTextBlock.Text = $"{snapshot.Current:n0} file system item(s) checked";
            _progressBar.IsIndeterminate = !finished && snapshot.Running;
        }
        else
        {
            _countTextBlock.Text = finished ? "Finished" : "Preparing";
            _progressBar.IsIndeterminate = !finished;
        }

        if (finished)
        {
            _progressBar.IsIndeterminate = false;
            if (snapshot.Total > 0)
            {
                _progressBar.Value = Math.Clamp(snapshot.Current, 0, snapshot.Total);
            }
            else
            {
                _progressBar.Maximum = 1;
                _progressBar.Value = 1;
            }
        }
    }

    public static string CompactStatus(string rawProgress)
    {
        var snapshot = ParseProgress(rawProgress);
        if (string.IsNullOrWhiteSpace(snapshot.Stage) && string.IsNullOrWhiteSpace(snapshot.Path))
        {
            return string.Empty;
        }

        var path = ShortPath(snapshot.Path);
        if (snapshot.Total > 0)
        {
            return $"{snapshot.Stage} {snapshot.Current}/{snapshot.Total}: {path}";
        }

        if (snapshot.Current > 0)
        {
            return $"{snapshot.Stage} {snapshot.Current}: {path}";
        }

        return string.IsNullOrWhiteSpace(path) ? snapshot.Stage : $"{snapshot.Stage}: {path}";
    }

    private Brush Brush(string resourceKey, string fallback)
    {
        return TryFindResource(resourceKey) as Brush
            ?? new SolidColorBrush((Color)ColorConverter.ConvertFromString(fallback));
    }

    private static ScanProgressSnapshot ParseProgress(string rawProgress)
    {
        var running = false;
        var current = 0;
        var total = 0;
        var stage = string.Empty;
        var path = string.Empty;

        foreach (var rawLine in rawProgress.Split(["\r\n", "\n"], StringSplitOptions.RemoveEmptyEntries))
        {
            var separator = rawLine.IndexOf('=');
            if (separator <= 0)
            {
                continue;
            }

            var key = rawLine[..separator].Trim();
            var value = rawLine[(separator + 1)..].Trim();
            switch (key.ToLowerInvariant())
            {
                case "running":
                    running = value == "1" || value.Equals("true", StringComparison.OrdinalIgnoreCase);
                    break;
                case "current":
                    int.TryParse(value, out current);
                    break;
                case "total":
                    int.TryParse(value, out total);
                    break;
                case "stage":
                    stage = value;
                    break;
                case "path":
                    path = value;
                    break;
            }
        }

        return new ScanProgressSnapshot(running, Math.Max(0, current), Math.Max(0, total), stage, path);
    }

    private static string ShortPath(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return string.Empty;
        }

        try
        {
            var fileName = Path.GetFileName(path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            return string.IsNullOrWhiteSpace(fileName) ? path : fileName;
        }
        catch
        {
            return path;
        }
    }

    private readonly record struct ScanProgressSnapshot(bool Running, int Current, int Total, string Stage, string Path);
}
