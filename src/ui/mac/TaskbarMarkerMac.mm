#import <AppKit/AppKit.h>

namespace TaskbarMarker::detail {

// A badge on the Dock's icon (red, as macOS draws badges) while recording.
void dockBadge(bool on)
{
    NSApp.dockTile.badgeLabel = on ? @"●" : nil;
}

} // namespace TaskbarMarker::detail
