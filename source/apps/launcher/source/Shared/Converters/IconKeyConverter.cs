using System;
using System.Globalization;
using Avalonia;
using Avalonia.Data.Converters;
using Avalonia.Media;

namespace ECS3DLauncher.Shared.Converters;

// Resolves an icon resource key (e.g. "IconFolder") to its StreamGeometry so
// data-bound items can pick an icon by name.
public sealed class IconKeyConverter : IValueConverter
{
    public static readonly IconKeyConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        // Use TryGetResource (not Resources.TryGetValue) so lookup traverses
        // merged dictionaries and theme scopes — the icon geometries live in a
        // merged ResourceInclude.
        if (value is string key &&
            Application.Current!.TryGetResource(key, null, out var res) &&
            res is Geometry geometry)
        {
            return geometry;
        }

        return null;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotSupportedException();
}
