/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// smartpdf design review: pins on the canvas, comment pop-up, review panel,
// keyboard review mode, live sync with the shared review folder, Excel report.

struct MainWindow;
struct WindowTab;
struct ReviewSession;

constexpr UINT_PTR kReviewSyncTimerId = 11;
constexpr UINT_PTR kReviewIdleTimerId = 12;

// lifecycle hooks
void ReviewOnTabChanged(MainWindow* win); // current tab changed or a document was (re)loaded
void ReviewFreeSession(WindowTab* tab);   // tab is being destroyed
void ReviewOnPageChanged(MainWindow* win, int pageNo);
void ReviewOnThemeChanged(MainWindow* win);
void ReviewOnTimer(MainWindow* win, UINT_PTR timerId);

// canvas hooks
void ReviewPaint(MainWindow* win, HDC hdc);
bool ReviewOnLeftButtonDown(MainWindow* win, int x, int y); // true if handled
bool ReviewOnSetCursor(MainWindow* win, Point pt);          // true if handled
bool ReviewPreTranslateKey(MSG& msg);                       // keyboard review mode

// commands
void ReviewAddComment(MainWindow* win, Point canvasPt, bool hasPt);
void ReviewToggleMode(MainWindow* win);
void ReviewTogglePanel(MainWindow* win);
void ReviewMarkPageReviewed(MainWindow* win);
void ReviewOpenReport(MainWindow* win);
void ReviewExportPdf(MainWindow* win);
void ReviewSelectNext(MainWindow* win, bool forward);
void ReviewToggleCompare(MainWindow* win);
bool ReviewCanUse(MainWindow* win); // a fixed-layout document is loaded

// right-side panel plumbing (layout in SumatraPDF.cpp RelayoutFrame)
void ReviewRelayoutPanel(MainWindow* win);
void ReviewDestroyPanel(MainWindow* win);
