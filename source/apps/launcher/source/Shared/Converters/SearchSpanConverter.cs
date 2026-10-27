using System;
using System.Globalization;
using Avalonia.Data.Converters;

namespace ECS3DLauncher.Shared.Converters;

// Column span for the header title: one column beside the search box, two when the search box is hidden.
public sealed class SearchSpanConverter : IValueConverter
{
    public static readonly SearchSpanConverter Instance = new();

    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is true ? 1 : 2;

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
