#pragma once

// The three overlays that stop the game to ask something -- the toilet choice,
// the key sheet and the need controls -- are all the same shape: a centred
// translucent panel, a title, some rows of (key, what it does), and a footer.
//
// This exists because of a bug worth keeping out: the toilet choice and the key
// sheet used to be panels, and when the old interface was deleted the panels went
// with it while the *state* that opens them stayed. Pressing E or H then put the
// game into a mode that swallowed every click and every movement key, with
// nothing on screen to explain why. So an overlay that is open now always has
// something to draw, and rows are laid out once and hit-tested from the same
// numbers the drawing used.

#include "client/raster.h"

namespace gtc::native {

struct OverlayRow {
    const char* key = "";
    const char* label = "";
};

struct OverlayPanelLayout {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    int textHeight = 0;
    int titleHeight = 0;
    int titleTop = 0;
    int footerTop = 0;
    int firstRowTop = 0;
    int rowGap = 0;
    int columns = 1;
    int rowsPerColumn = 0;
    int columnLeft[4] = {0, 0, 0, 0};
    int columnKeyWidth = 0;
    int columnWidth = 0;
    int clickableTop = 0;     // the band the rows occupy, for hit testing
    int clickableHeight = 0;
};

// `columns` splits the list into that many columns, filled top to bottom then
// left to right. Titles and footers are optional: pass nullptr for none.
OverlayPanelLayout layoutOverlayPanel(int frameWidth, int frameHeight, const OverlayRow* rows, int rowCount,
                                      int columns, const char* title, const char* footer);

// Draws what the layout describes: panel, title, rows, footer.
void drawOverlayPanel(NativeFrame& frame, const OverlayPanelLayout& layout, const OverlayRow* rows, int rowCount,
                      const char* title, const char* footer);

// Which row is under this point, or -1 for none. The same layout that was drawn:
// a click and a label cannot disagree about where a row is.
int overlayRowAt(const OverlayPanelLayout& layout, int rowCount, int x, int y);

// The two panels, drawn from one definition so the window loop, the headless
// capture and the check cannot drift apart.
OverlayPanelLayout toiletChoiceLayout(int frameWidth, int frameHeight);
OverlayPanelLayout manualLayout(int frameWidth, int frameHeight);
void drawToiletChoice(NativeFrame& frame);
void drawManual(NativeFrame& frame);
// 0, 1, 2 for sit, shit, piss; -1 when the point is not on a row or the panel is
// not where the frame says it is.
int toiletChoiceRowAt(int frameWidth, int frameHeight, int x, int y);

}  // namespace gtc::native
