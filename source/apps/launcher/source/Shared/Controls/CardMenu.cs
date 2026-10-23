using Avalonia.Controls;
using Avalonia.VisualTree;

namespace ECS3DLauncher.Shared.Controls;

public static class CardMenu
{
    // Opens the context menu of the nearest ancestor that owns one, anchored below the given control.
    public static bool OpenFrom(Control anchor)
    {
        foreach (var ancestor in anchor.GetVisualAncestors())
        {
            if (ancestor is Control { ContextMenu: { } menu } owner)
            {
                // ContextMenu.Open throws when given a control other than one the menu is attached to.
                menu.PlacementTarget = anchor;
                menu.Placement = PlacementMode.BottomEdgeAlignedRight;
                menu.Open(owner);
                menu.ClearValue(ContextMenu.PlacementTargetProperty);
                menu.ClearValue(ContextMenu.PlacementProperty);
                return true;
            }
        }

        return false;
    }
}
