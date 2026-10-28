/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

/*
smartpdf design review UI. The data model lives in Review.cpp; this file
connects it to the viewer:

- ReviewSession (one per tab): paths of the shared review folder, the replayed
  ReviewModel, cached page words for anchoring, pins hit-testing data
- pins painted on top of pages (Canvas.cpp calls ReviewPaint)
- the comment composer pop-up
- the review panel (WebView2 web app, with a native list as fallback)
- keyboard review mode, live sync (folder change notifications), Excel report
*/

#include "base/Base.h"
#include "base/Dpi.h"
#include "base/File.h"
#include "base/DirIter.h"
#include "base/GuessFileType.h"
#include "base/UITask.h"
#include "base/Win.h"
#include "base/WinDynCalls.h"
#include "base/GdiPlus.h"
#include "base/Pixmap.h"
#include "base/ScopedWin.h"
#include "base/Timer.h"
#include "base/JsonParser.h"

#include "wingui/UIModels.h"
#include "wingui/Layout.h"
#include "wingui/WinGui.h"
#include "wingui/WebView.h"
#include "wingui/LabelWithCloseWnd.h"

#include "Settings.h"
#include "GlobalPrefs.h"
#include "AppSettings.h"
#include "DocController.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "Annotation.h"
#include "DisplayModel.h"
#include "TextSelection.h"
#include "MainWindow.h"
#include "WindowTab.h"
#include "Selection.h"
#include "SumatraPDF.h"
#include "AppTools.h"
#include "AIChatCommon.h"
#include "Notifications.h"
#include "Translations.h"
#include "Theme.h"
#include "resource.h"

#include "Review.h"
#include "ReviewXlsx.h"
#include "ReviewUI.h"

#define SECURITY_WIN32
#include <security.h>
#if defined(_MSC_VER)
#pragma comment(lib, "secur32.lib")
#endif

constexpr const char* kReviewFolderName = "review-comments";
constexpr const char* kReviewReportName = "Review-Report.xlsx";
constexpr const char* kPanelHost = "https://smartpdf.review/";
// placingKey value while waiting for the click that places a new comment
constexpr const char* kPlaceNewComment = "#new";

// severity colors: Critical, Major, Minor, Comment
static const COLORREF kSevColor[kReviewSeverityCount] = {RGB(0xF0, 0x44, 0x38), RGB(0xF9, 0x73, 0x16),
                                                         RGB(0xEA, 0xB3, 0x08), RGB(0x3B, 0x82, 0xF6)};
static const char* kSevHex[kReviewSeverityCount] = {"#F04438", "#F97316", "#EAB308", "#3B82F6"};
constexpr COLORREF kResolvedColor = RGB(0x9C, 0xA3, 0xAF);

// clang-format off
static const char* kDefaultTemplates =
    "Missing decoupling capacitor\n"
    "Check pull-up / pull-down value\n"
    "Wrong footprint or package?\n"
    "Net name mismatch between sheets\n"
    "Add a test point\n"
    "Add ESD / TVS protection\n"
    "Check voltage / power rating\n"
    "DNP / assembly option unclear\n"
    "Connector pinout to verify\n"
    "Typo in label";

static const char* kDefaultChecklist =
    "Power: decoupling and bulk capacitors\n"
    "Power: regulator ratings and dissipation\n"
    "Pull-ups / pull-downs on open-drain and strap pins\n"
    "ESD / TVS protection on external connectors\n"
    "Reset and boot / strap configuration\n"
    "Clocks: sources and crystal load capacitors\n"
    "Test points on key nets\n"
    "Connector pinouts verified\n"
    "Net names consistent across sheets\n"
    "Part numbers and DNP options correct";
// clang-format on

struct ReviewPageWords {
    Vec<ReviewWord> words;
    Str title;
    bool loaded = false;
    ~ReviewPageWords() {
        ReviewFreeWords(words);
        str::Free(title);
    }
};

struct ReviewResolved {
    Str key;
    Str rev;
    int page = 0;
    RectF rect;
    ReviewAnchorState state = ReviewAnchorState::Unresolved;
    ~ReviewResolved() {
        str::Free(key);
        str::Free(rev);
    }
};

struct ReviewResolveJob {
    Str key;
    Vec<int> pages;
    int idx = 0;
    bool active = false;
    bool designatorAnchor = false;
    void Reset() {
        str::FreePtr(&key);
        pages.Reset();
        idx = 0;
        active = false;
        designatorAnchor = false;
    }
    ~ReviewResolveJob() {
        str::Free(key);
    }
};

// rectangles on one page that changed between two revisions
struct ReviewDiffPage {
    int page = 0;
    Vec<RectF> rects; // page coordinates
    bool done = false;
};

struct ReviewCompare {
    Str otherPath;               // revision to compare with
    EngineBase* other = nullptr; // opened by the first diff job
    Vec<ReviewDiffPage*> pages;
    bool busy = false;
    int generation = 0;
    ~ReviewCompare() {
        str::Free(otherPath);
        DeleteVecMembers(pages);
        SafeEngineRelease(&other);
    }
};

struct ReviewSession {
    WindowTab* tab = nullptr;
    bool active = false; // false: the PDF has no review folder, nothing else is set
    EngineBase* engine = nullptr; // not owned, used to detect reloads
    int pageCount = 0;
    Str pdfPath;
    Str pdfName;
    Str pdfDir;
    Str reviewDir;
    Str myLogPath;
    Str reportPath;
    Str myId;
    Str myName;
    bool profileKnown = false;
    bool localFallback = false;

    ReviewModel model;
    Vec<ReviewEvent*> events;
    Vec<ReviewPageWords*> pages; // index: page - 1
    Vec<ReviewResolved*> resolved;
    ReviewResolveJob job;

    // pins painted in the last paint, for hit testing
    Vec<Rect> pinRects;
    StrVec pinKeys;

    Str selectedKey;
    Str placingKey; // comment whose pin is being re-placed with the next click
    bool showResolved = true;

    HANDLE changeHandle = nullptr;
    i64 signature = 0;
    int ticks = 0;
    i64 lastMentionCheck = 0;

    int titlesFetched = 0;

    // logs are loaded in the background (LoadThread)
    bool loading = false;
    bool reloadPending = false; // something changed while loading
    bool loadedOnce = false;
    int appendCount = 0; // my writes; a load started before a write is outdated

    bool reportDirty = false;
    bool reportBusy = false;
    i64 reportNextTry = 0;

    ReviewCompare* compare = nullptr;

    ~ReviewSession();
};

ReviewSession::~ReviewSession() {
    if (changeHandle && changeHandle != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(changeHandle);
    }
    ReviewDeleteEvents(events);
    DeleteVecMembers(pages);
    DeleteVecMembers(resolved);
    str::Free(pdfPath);
    str::Free(pdfName);
    str::Free(pdfDir);
    str::Free(reviewDir);
    str::Free(myLogPath);
    str::Free(reportPath);
    str::Free(myId);
    str::Free(myName);
    str::Free(selectedKey);
    str::Free(placingKey);
    delete compare;
}

static void SetStr(Str* dst, Str src) {
    str::Free(*dst);
    *dst = len(src) > 0 ? str::Dup(src) : Str{};
}

static i64 NowTime() {
    return (i64)time(nullptr);
}

static void ShowReviewToast(MainWindow* win, Str msg, bool warning = false, int timeoutMs = 3500) {
    if (!win || !win->hwndCanvas) {
        return;
    }
    NotificationCreateArgs nargs;
    nargs.hwndParent = win->hwndCanvas;
    nargs.timeoutMs = timeoutMs;
    nargs.warning = warning;
    nargs.msg = msg;
    ShowNotification(nargs);
}

// --- identity ---

static TempStr GetWindowsUserNameTemp() {
    Str u = Str(getenv("USERNAME"));
    if (!u) {
        u = Str(getenv("USER"));
    }
    if (!u) {
        return str::DupTemp(StrL("user"));
    }
    return str::DupTemp(u);
}

static TempStr GetUserDisplayNameTemp() {
    Str pref = gGlobalPrefs->annotations.defaultAuthor;
    if (len(pref) > 0 && !str::Eq(pref, "(none)") && !str::IsEmptyOrWhiteSpace(pref)) {
        return str::DupTemp(pref);
    }
    WCHAR buf[256]{};
    ULONG n = dimof(buf);
    if (GetUserNameExW(NameDisplay, buf, &n) && n > 0 && buf[0]) {
        return ToUtf8Temp(WStr(buf));
    }
    return GetWindowsUserNameTemp();
}

static bool IsDesigner(ReviewSession* s) {
    return len(s->model.designer) > 0 && str::EqI(s->model.designer, s->myId);
}

// --- paths ---

static TempStr LocalReviewDirTemp(Str pdfPath) {
    Str base = Str(getenv("LOCALAPPDATA"));
    if (!base) {
        base = Str(getenv("TEMP"));
    }
    TempStr name = ReviewSanitizeUserIdTemp(path::GetBaseNameTemp(pdfPath));
    return path::JoinTemp(base, StrL("SumatraPDF\\review-local"), name);
}

static void SetReviewDir(ReviewSession* s, Str dir, bool local) {
    SetStr(&s->reviewDir, dir);
    s->localFallback = local;
    SetStr(&s->myLogPath, path::JoinTemp(dir, fmt("%s.jsonl", s->myId)));
    if (local) {
        SetStr(&s->reportPath, path::JoinTemp(dir, StrL(kReviewReportName)));
    } else {
        SetStr(&s->reportPath, path::JoinTemp(s->pdfDir, StrL(kReviewReportName)));
    }
    if (s->changeHandle && s->changeHandle != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(s->changeHandle);
    }
    s->changeHandle = nullptr;
}

static bool EnsureReviewDir(MainWindow* win, ReviewSession* s) {
    if (dir::Exists(s->reviewDir)) {
        return true;
    }
    if (dir::CreateAll(s->reviewDir)) {
        return true;
    }
    if (s->localFallback) {
        return false;
    }
    // e.g. a read-only location: keep the comments locally
    TempStr local = LocalReviewDirTemp(s->pdfPath);
    SetReviewDir(s, local, true);
    if (!dir::CreateAll(local)) {
        ShowReviewToast(win, _TRA("Couldn't create a folder for review comments"), true);
        return false;
    }
    ShowReviewToast(win, _TRA("Can't write next to the PDF: review comments are saved on this PC only"), true, 6000);
    return true;
}

// --- loading logs ---
// Logs are read on a background thread: with OneDrive "files on demand" a log
// that another reviewer just wrote may have to be downloaded first.

static void ApplyResolutions(ReviewSession* s, bool* needsWorkOut);

static i64 ComputeSignature(Str reviewDir) {
    i64 sig = 0;
    if (!dir::Exists(reviewDir)) {
        return 0;
    }
    DirIter di{reviewDir};
    for (DirIterEntry* de : di) {
        if (!str::EndsWithI(de->name, StrL(".jsonl")) && !str::EndsWithI(de->name, StrL(".csv"))) {
            continue;
        }
        WIN32_FIND_DATAW* fd = de->fd;
        if (fd) {
            sig = sig * 31 + (i64)fd->nFileSizeLow + ((i64)fd->ftLastWriteTime.dwLowDateTime << 8);
        }
    }
    return sig;
}

// v0.3 stored one csv per reviewer: Rev,Page,Sheet,Component,ClickedText,Comment,Reviewer,Date,X,Y
// myLogPath: if the csv is mine, it's moved into my log once (empty: only read it)
static void ImportLegacyCsv(Str filePath, Str fileName, Str myId, Str myLogPath, Vec<ReviewEvent*>& events) {
    TempStr owner = ReviewSanitizeUserIdTemp(Str(fileName.s, fileName.len - 4));
    Str data = file::ReadFile(filePath);
    if (!data) {
        return;
    }
    Vec<StrVec*> rows;
    ReviewParseCsv(data, rows);
    str::Free(data);
    str::Builder lines;
    int n = len(rows);
    Str reviewerName;
    for (int i = 1; i < n; i++) {
        StrVec* row = rows[i];
        if (len(*row) < 10) {
            continue;
        }
        if (!reviewerName) {
            reviewerName = row->At(6);
        }
        ReviewEventBuilder eb(StrL("create"), owner, 1000 + i);
        eb.Add(StrL("n"), fmt("%d", i));
        eb.Add(StrL("sev"), StrL("Comment"));
        eb.Add(StrL("rev"), row->At(0));
        eb.Add(StrL("page"), row->At(1));
        eb.Add(StrL("sheet"), row->At(2));
        eb.Add(StrL("comp"), row->At(3));
        eb.Add(StrL("clicked"), row->At(4));
        eb.Add(StrL("text"), row->At(5));
        eb.Add(StrL("x"), row->At(8));
        eb.Add(StrL("y"), row->At(9));
        lines.Append(eb.FinishTemp());
    }
    if (len(reviewerName) > 0) {
        ReviewEventBuilder eb(StrL("profile"), owner, 999);
        eb.Add(StrL("name"), reviewerName);
        lines.Append(eb.FinishTemp());
    }
    DeleteVecMembers(rows);
    if (lines.IsEmpty()) {
        return;
    }
    Str ls(lines.els, (int)lines.len);
    if (str::EqI(owner, myId) && len(myLogPath) > 0) {
        // my own legacy file: move it into my log once
        FILE* f = _wfopen(CWStrTemp(myLogPath), L"ab");
        if (f) {
            fwrite(ls.s, 1, (size_t)ls.len, f);
            fclose(f);
            TempStr done = str::JoinTemp(filePath, StrL(".migrated"));
            MoveFileExW(CWStrTemp(filePath), CWStrTemp(done), MOVEFILE_REPLACE_EXISTING);
        }
    }
    ReviewParseLog(ls, events);
}

// reads and parses every reviewer's log in reviewDir
static void ReadAllLogs(Str reviewDir, Str myId, Str myLogPath, Vec<ReviewEvent*>& events) {
    if (!dir::Exists(reviewDir)) {
        return;
    }
    StrVec csvFiles;
    StrVec csvNames;
    DirIter di{reviewDir};
    for (DirIterEntry* de : di) {
        if (str::EndsWithI(de->name, StrL(".jsonl"))) {
            Str data = file::ReadFile(de->filePath);
            if (data) {
                ReviewParseLog(data, events);
                str::Free(data);
            }
        } else if (str::EndsWithI(de->name, StrL(".csv")) && !str::EqI(de->name, StrL("ALL-comments.csv"))) {
            csvFiles.Append(de->filePath);
            csvNames.Append(de->name);
        }
    }
    int n = len(csvFiles);
    for (int i = 0; i < n; i++) {
        // a legacy reviewer who already has a log was migrated
        TempStr owner = ReviewSanitizeUserIdTemp(Str(csvNames.At(i).s, csvNames.At(i).len - 4));
        TempStr logPath = path::JoinTemp(reviewDir, fmt("%s.jsonl", owner));
        if (file::Exists(logPath)) {
            continue;
        }
        ImportLegacyCsv(csvFiles.At(i), csvNames.At(i), myId, myLogPath, events);
    }
}

// rebuilds the model from s->events and everything that depends on it.
// Returns true if comments must be located in this revision (idle work)
static bool ApplyEvents(ReviewSession* s) {
    ReviewReplay(&s->model, s->events);
    ReviewUser* me = s->model.FindUser(s->myId);
    s->profileKnown = me && len(me->name) > 0;
    if (s->profileKnown) {
        SetStr(&s->myName, me->name);
    }
    if (len(s->model.templates) == 0) {
        SetStr(&s->model.templates, Str(kDefaultTemplates));
    }
    if (len(s->model.checklist) == 0) {
        SetStr(&s->model.checklist, Str(kDefaultChecklist));
    }
    bool needsWork = false;
    ApplyResolutions(s, &needsWork);
    if (IsDesigner(s)) {
        s->reportDirty = true;
    }
    return needsWork;
}

static TempStr MyLogPathForMigrationTemp(ReviewSession* s) {
    return s->localFallback ? TempStr{} : str::DupTemp(s->myLogPath);
}

// synchronous load, when the complete model is needed right now (e.g. to number
// a new comment) and the background load hasn't finished yet
static void EnsureLoaded(ReviewSession* s) {
    if (s->loadedOnce) {
        return;
    }
    ReviewDeleteEvents(s->events);
    s->signature = ComputeSignature(s->reviewDir);
    ReadAllLogs(s->reviewDir, s->myId, MyLogPathForMigrationTemp(s), s->events);
    ApplyEvents(s);
    s->loadedOnce = true;
    s->appendCount++; // a background load still running is outdated now
}

struct ReviewLoadJob {
    MainWindow* win = nullptr;
    Str pdfPath; // identifies the session
    Str reviewDir;
    Str myId;
    Str myLogPath;
    int appendCount = 0;
    i64 signature = 0;
    Vec<ReviewEvent*> events;
    ~ReviewLoadJob() {
        str::Free(pdfPath);
        str::Free(reviewDir);
        str::Free(myId);
        str::Free(myLogPath);
        ReviewDeleteEvents(events);
    }
};

static void LoadDone(ReviewLoadJob* job);

static void LoadThread(ReviewLoadJob* job) {
    // signature first: files that change while being read are noticed next time
    job->signature = ComputeSignature(job->reviewDir);
    ReadAllLogs(job->reviewDir, job->myId, job->myLogPath, job->events);
    uitask::Post(MkFunc0(LoadDone, job), "ReviewLoadDone");
}

static void StartLoad(MainWindow* win, ReviewSession* s) {
    if (s->loading) {
        s->reloadPending = true;
        return;
    }
    auto job = new ReviewLoadJob();
    job->win = win;
    job->pdfPath = str::Dup(s->pdfPath);
    job->reviewDir = str::Dup(s->reviewDir);
    job->myId = str::Dup(s->myId);
    job->myLogPath = str::Dup(MyLogPathForMigrationTemp(s));
    job->appendCount = s->appendCount;
    s->loading = true;
    RunAsync(MkFunc0(LoadThread, job), "ReviewLoad");
}

static void ClearPageCaches(ReviewSession* s) {
    DeleteVecMembers(s->pages);
    for (int i = 0; i < s->pageCount; i++) {
        s->pages.Append(new ReviewPageWords());
    }
    DeleteVecMembers(s->resolved);
    s->job.Reset();
    delete s->compare;
    s->compare = nullptr;
}

static void FreeSessionPtr(WindowTab* tab) {
    if (tab && tab->review) {
        delete tab->review;
        tab->review = nullptr;
    }
}

void ReviewFreeSession(WindowTab* tab) {
    FreeSessionPtr(tab);
}

static void StartReviewTimers(MainWindow* win);

// create: also create a session when the PDF has no review folder yet
static ReviewSession* GetSession(WindowTab* tab, bool create) {
    if (!tab || !tab->IsDocLoaded()) {
        return nullptr;
    }
    DisplayModel* dm = tab->AsFixed();
    if (!dm) {
        return nullptr;
    }
    EngineBase* engine = dm->GetEngine();
    if (!engine || len(tab->filePath) == 0) {
        return nullptr;
    }
    ReviewSession* s = tab->review;
    if (s && !str::EqI(s->pdfPath, tab->filePath)) {
        FreeSessionPtr(tab);
        s = nullptr;
    }
    if (s && !s->active && !create) {
        // already checked: this PDF has no review folder (re-checked on tab change)
        return nullptr;
    }
    if (!s || !s->active) {
        TempStr pdfDir = path::GetDirTemp(tab->filePath);
        TempStr shared = path::JoinTemp(pdfDir, StrL(kReviewFolderName));
        TempStr local = LocalReviewDirTemp(tab->filePath);
        bool hasShared = dir::Exists(shared);
        bool hasLocal = !hasShared && dir::Exists(local);
        if (!s) {
            s = new ReviewSession();
            s->tab = tab;
            SetStr(&s->pdfPath, tab->filePath);
            tab->review = s;
        }
        if (!create && !hasShared && !hasLocal) {
            return nullptr;
        }
        s->active = true;
        SetStr(&s->pdfName, path::GetBaseNameTemp(tab->filePath));
        SetStr(&s->pdfDir, pdfDir);
        SetStr(&s->myId, ReviewSanitizeUserIdTemp(GetWindowsUserNameTemp()));
        SetStr(&s->myName, GetUserDisplayNameTemp());
        SetReviewDir(s, hasLocal ? local : shared, hasLocal);
        s->engine = engine;
        s->pageCount = engine->PageCount();
        ClearPageCaches(s);
        s->lastMentionCheck = NowTime();
        StartLoad(tab->win, s);
        StartReviewTimers(tab->win);
    }
    if (s->engine != engine) {
        // document was reloaded: positions and words must be recomputed
        s->engine = engine;
        s->pageCount = engine->PageCount();
        ClearPageCaches(s);
        bool needsWork = false;
        ApplyResolutions(s, &needsWork);
    }
    return s;
}

static ReviewSession* CurrentSession(MainWindow* win, bool create = false) {
    if (!win || !IsMainWindowValid(win)) {
        return nullptr;
    }
    return GetSession(win->CurrentTab(), create);
}

static int ClampPage(ReviewSession* s, int page) {
    if (page < 1) {
        return 1;
    }
    if (page > s->pageCount) {
        return s->pageCount;
    }
    return page;
}

static ReviewPageWords* PageWords(ReviewSession* s, int page) {
    if (page < 1 || page > len(s->pages)) {
        return nullptr;
    }
    ReviewPageWords* pw = s->pages[page - 1];
    if (!pw->loaded) {
        pw->loaded = true;
        int textLen = 0;
        Rect* coords = nullptr;
        Str text = s->engine->GetTextForPage(page, &textLen, &coords);
        if (text && coords && textLen > 0) {
            ReviewWordsFromPageText(text, coords, textLen, pw->words);
        }
        RectF mb = s->engine->PageMediabox(page);
        SetStr(&pw->title, ReviewSheetTitleTemp(pw->words, mb));
    }
    return pw;
}

// --- anchors ---

static ReviewResolved* FindResolved(ReviewSession* s, Str key, Str rev) {
    for (ReviewResolved* r : s->resolved) {
        if (str::Eq(r->key, key) && str::EqI(r->rev, rev)) {
            return r;
        }
    }
    return nullptr;
}

static void ApplyResolutions(ReviewSession* s, bool* needsWorkOut) {
    for (ReviewComment* c : s->model.comments) {
        if (c->deleted) {
            continue;
        }
        if (len(c->rev) == 0 || str::EqI(c->rev, s->pdfName)) {
            c->curPage = ClampPage(s, c->page);
            c->curRect = c->rect;
            c->anchorState = ReviewAnchorState::Same;
            continue;
        }
        ReviewResolved* r = FindResolved(s, c->key, c->rev);
        if (r) {
            c->curPage = r->page;
            c->curRect = r->rect;
            c->anchorState = r->state;
            continue;
        }
        c->curPage = ClampPage(s, c->page);
        c->curRect = c->rect;
        c->anchorState = ReviewAnchorState::Unresolved;
        *needsWorkOut = true;
    }
}

static void SetResolved(ReviewSession* s, ReviewComment* c, int page, RectF rect, ReviewAnchorState state) {
    auto r = new ReviewResolved();
    r->key = str::Dup(c->key);
    r->rev = str::Dup(c->rev);
    r->page = page;
    r->rect = rect;
    r->state = state;
    s->resolved.Append(r);
    c->curPage = page;
    c->curRect = rect;
    c->anchorState = state;
}

// one step of locating comments made on other revisions; extracts at most one
// page of text per call so the UI stays responsive. Returns true if more to do
static bool ResolveStep(ReviewSession* s, bool* didExtract) {
    ReviewResolveJob& job = s->job;
    if (!job.active) {
        ReviewComment* next = nullptr;
        for (ReviewComment* c : s->model.comments) {
            if (!c->deleted && c->anchorState == ReviewAnchorState::Unresolved) {
                next = c;
                break;
            }
        }
        if (!next) {
            return false;
        }
        job.Reset();
        job.key = str::Dup(next->key);
        job.active = true;
        job.designatorAnchor = ReviewDesignatorScore(next->anchorWord) == 2;
        int target = ClampPage(s, next->page);
        job.pages.Append(target);
        if (job.designatorAnchor) {
            // component designators are unique: look on all pages, nearest first
            for (int d = 1; d < s->pageCount; d++) {
                if (target - d >= 1) {
                    job.pages.Append(target - d);
                }
                if (target + d <= s->pageCount) {
                    job.pages.Append(target + d);
                }
            }
        } else {
            if (target - 1 >= 1) {
                job.pages.Append(target - 1);
            }
            if (target + 1 <= s->pageCount) {
                job.pages.Append(target + 1);
            }
        }
    }
    ReviewComment* c = s->model.FindComment(job.key);
    if (!c || c->deleted || c->anchorState != ReviewAnchorState::Unresolved) {
        job.Reset();
        return true;
    }
    int target = ClampPage(s, c->page);
    while (job.idx < len(job.pages)) {
        int page = job.pages[job.idx];
        ReviewPageWords* pw = s->pages[page - 1];
        if (!pw->loaded) {
            if (*didExtract) {
                return true; // continue in the next step
            }
            *didExtract = true;
            pw = PageWords(s, page);
        }
        PointF pt{};
        bool good = false;
        if (ReviewLocateOnPage(c, pw->words, &pt, &good)) {
            ReviewAnchorState st = ReviewAnchorState::Found;
            if (!good || page != target) {
                st = ReviewAnchorState::Moved;
            }
            if (len(c->sheet) > 0 && len(pw->title) > 0 && !str::EqI(c->sheet, pw->title) && page == target) {
                st = ReviewAnchorState::Moved;
            }
            SetResolved(s, c, page, RectF(pt.x, pt.y, c->rect.dx, c->rect.dy), st);
            job.Reset();
            return true;
        }
        job.idx++;
    }
    ReviewAnchorState st = job.designatorAnchor ? ReviewAnchorState::Gone : ReviewAnchorState::Moved;
    if (len(c->anchorWord) == 0 && len(c->nearWords) == 0) {
        st = ReviewAnchorState::Moved;
    }
    SetResolved(s, c, target, c->rect, st);
    job.Reset();
    return true;
}


// --- writing events ---

static void PanelUpdate(MainWindow* win);
static void RefreshReviewUI(MainWindow* win) {
    if (!win || !IsMainWindowValid(win)) {
        return;
    }
    ScheduleRepaint(win, 0);
    PanelUpdate(win);
}

static void StartIdleWork(MainWindow* win) {
    if (win && win->hwndCanvas) {
        SetTimer(win->hwndCanvas, kReviewIdleTimerId, 15, nullptr);
    }
}

static void StartReviewTimers(MainWindow* win) {
    if (win && win->hwndCanvas) {
        SetTimer(win->hwndCanvas, kReviewSyncTimerId, 1500, nullptr);
        StartIdleWork(win);
    }
}

// appends JSON lines to my log and updates the model
static bool AppendEvents(MainWindow* win, ReviewSession* s, Str lines) {
    EnsureLoaded(s); // e.g. my profile must be known before writing
    if (!EnsureReviewDir(win, s)) {
        return false;
    }
    str::Builder all;
    if (!s->profileKnown) {
        ReviewEventBuilder pe(StrL("profile"), s->myId, NowTime());
        pe.Add(StrL("name"), s->myName);
        pe.Add(StrL("initials"), ReviewInitialsTemp(s->myName));
        all.Append(pe.FinishTemp());
    }
    all.Append(lines);
    Str data(all.els, (int)all.len);
    FILE* f = _wfopen(CWStrTemp(s->myLogPath), L"ab");
    if (!f) {
        ShowReviewToast(win, fmt(_TRA("Couldn't save the review comment to %s").s, s->myLogPath), true, 6000);
        return false;
    }
    size_t written = fwrite(data.s, 1, (size_t)data.len, f);
    fclose(f);
    if (written != (size_t)data.len) {
        ShowReviewToast(win, _TRA("Couldn't save the review comment"), true);
        return false;
    }
    s->appendCount++; // a background load that started before this is outdated
    ReviewParseLog(data, s->events);
    if (ApplyEvents(s)) {
        StartIdleWork(win);
    }
    s->signature = ComputeSignature(s->reviewDir);
    RefreshReviewUI(win);
    return true;
}

static bool CommitEvent(MainWindow* win, ReviewSession* s, ReviewEventBuilder& eb) {
    return AppendEvents(win, s, eb.FinishTemp());
}

static void SelectComment(MainWindow* win, ReviewSession* s, Str key, bool scroll);

// --- report (designer) ---

struct ReviewReportJob {
    MainWindow* win = nullptr;
    Str pdfPath;
    Str reviewDir;
    Str reportPath;
    Str pdfName;
    int pageCount = 0;
    StrVec pageTitles;
    StrVec anchorKeys;
    Vec<int> anchorPages;
    Vec<int> anchorStates;
    bool openAfter = false;
    bool ok = false;
    ~ReviewReportJob() {
        str::Free(pdfPath);
        str::Free(reviewDir);
        str::Free(reportPath);
        str::Free(pdfName);
    }
};

static bool PngSize(Str png, int* dx, int* dy) {
    // PNG signature (8) + IHDR length (4) + "IHDR" (4) + width (4) + height (4), big endian
    if (png.len < 24 || (u8)png.s[0] != 0x89 || png.s[1] != 'P') {
        return false;
    }
    auto be32 = [&](int off) {
        const u8* p = (const u8*)png.s + off;
        return (int)(((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]);
    };
    *dx = be32(16);
    *dy = be32(20);
    return *dx > 0 && *dy > 0;
}

static void ReportDone(ReviewReportJob* job);

static void ReportThread(ReviewReportJob* job) {
    Vec<ReviewEvent*> events;
    if (dir::Exists(job->reviewDir)) {
        DirIter di{job->reviewDir};
        for (DirIterEntry* de : di) {
            if (str::EndsWithI(de->name, StrL(".jsonl"))) {
                Str data = file::ReadFile(de->filePath);
                if (data) {
                    ReviewParseLog(data, events);
                    str::Free(data);
                }
            }
        }
    }
    ReviewModel model;
    ReviewReplay(&model, events);
    ReviewDeleteEvents(events);
    int na = len(job->anchorKeys);
    for (int i = 0; i < na; i++) {
        ReviewComment* c = model.FindComment(job->anchorKeys.At(i));
        if (c) {
            c->curPage = job->anchorPages[i];
            c->anchorState = (ReviewAnchorState)job->anchorStates[i];
        }
    }
    ReviewXlsxArgs args;
    args.model = &model;
    args.title = job->pdfName;
    args.currentRev = job->pdfName;
    args.pageCount = job->pageCount;
    args.generatedTime = NowTime();
    for (Str t : job->pageTitles) {
        args.pageTitles.Append(t);
    }
    TempStr link = path::JoinTemp(StrL("."), job->pdfName);
    args.pdfLink = link;
    Vec<Str> pngs;
    for (ReviewComment* c : model.comments) {
        if (c->deleted || len(c->snapshot) == 0) {
            continue;
        }
        TempStr p = path::JoinTemp(job->reviewDir, StrL("snapshots"), c->snapshot);
        Str png = file::ReadFile(p);
        int dx = 0, dy = 0;
        if (png && PngSize(png, &dx, &dy)) {
            pngs.Append(png);
            ReviewXlsxImage img;
            img.commentKey = c->key;
            img.png = png;
            img.dx = dx;
            img.dy = dy;
            args.images.Append(img);
        } else {
            str::Free(png);
        }
    }
    Str data = ReviewWriteXlsx(&args);
    for (Str png : pngs) {
        str::Free(png);
    }
    TempStr tmp = str::JoinTemp(job->reportPath, StrL(".tmp"));
    bool ok = file::WriteFile(tmp, data);
    str::Free(data);
    if (ok) {
        ok = MoveFileExW(CWStrTemp(tmp), CWStrTemp(job->reportPath), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) {
            file::Delete(tmp);
        }
    }
    job->ok = ok;
    uitask::Post(MkFunc0(ReportDone, job), "ReviewReportDone");
}

static ReviewSession* FindSessionByPath(MainWindow* win, Str pdfPath) {
    if (!win || !IsMainWindowValid(win)) {
        return nullptr;
    }
    for (WindowTab* tab : win->Tabs()) {
        if (tab->review && tab->review->active && str::EqI(tab->review->pdfPath, pdfPath)) {
            return tab->review;
        }
    }
    return nullptr;
}

static void ReportDone(ReviewReportJob* job) {
    AutoDelete del(job);
    ReviewSession* s = FindSessionByPath(job->win, job->pdfPath);
    if (s) {
        s->reportBusy = false;
        if (job->ok) {
            s->reportDirty = false;
        } else {
            // probably open in Excel: try again later
            s->reportNextTry = NowTime() + 30;
        }
    }
    if (job->openAfter) {
        if (job->ok) {
            LaunchFileShell(job->reportPath);
        } else if (IsMainWindowValid(job->win)) {
            ShowReviewToast(job->win, _TRA("Couldn't write the Excel report (is it open in Excel?)"), true, 6000);
        }
    }
}

static void StartReport(MainWindow* win, ReviewSession* s, Str outPath, bool openAfter) {
    auto job = new ReviewReportJob();
    job->win = win;
    job->pdfPath = str::Dup(s->pdfPath);
    job->reviewDir = str::Dup(s->reviewDir);
    job->reportPath = str::Dup(outPath);
    job->pdfName = str::Dup(s->pdfName);
    job->pageCount = s->pageCount;
    job->openAfter = openAfter;
    for (int i = 0; i < s->pageCount; i++) {
        ReviewPageWords* pw = s->pages[i];
        job->pageTitles.Append(pw->loaded ? pw->title : StrL(""));
    }
    for (ReviewComment* c : s->model.comments) {
        job->anchorKeys.Append(c->key);
        job->anchorPages.Append(c->curPage);
        job->anchorStates.Append((int)c->anchorState);
    }
    if (str::EqI(outPath, s->reportPath)) {
        s->reportBusy = true;
    }
    RunAsync(MkFunc0(ReportThread, job), "ReviewReport");
}

void ReviewOpenReport(MainWindow* win) {
    ReviewSession* s = CurrentSession(win, true);
    if (!s) {
        return;
    }
    if (IsDesigner(s)) {
        StartReport(win, s, s->reportPath, true);
        return;
    }
    if (file::Exists(s->reportPath)) {
        LaunchFileShell(s->reportPath);
        return;
    }
    // no shared report yet: make a personal copy
    TempStr tmpDir = GetTempDirTemp();
    TempStr name = fmt("SmartPDF-Review-%s.xlsx", path::GetPathNoExtTemp(s->pdfName));
    TempStr out = path::JoinTemp(tmpDir, name);
    StartReport(win, s, out, true);
}

// --- snapshots ---

struct ReviewSnapJob {
    MainWindow* win = nullptr;
    EngineBase* engine = nullptr; // AddRef'd
    int page = 0;
    RectF region;
    float zoom = 1;
    RectF mark; // comment point (dx == 0) or box, page coordinates
    COLORREF color = 0;
    Str outPath;
    ~ReviewSnapJob() {
        str::Free(outPath);
    }
};

static void SnapDone(ReviewSnapJob* job) {
    AutoDelete del(job);
    if (IsMainWindowValid(job->win)) {
        PanelUpdate(job->win);
    }
}

static void SnapThread(ReviewSnapJob* job) {
    RenderPageArgs args(job->page, job->zoom, 0, &job->region, RenderTarget::Export);
    Pixmap* px = job->engine->RenderPage(args);
    if (px && px->hbmp) {
        Gdiplus::Bitmap src(px->hbmp, nullptr);
        int w = (int)src.GetWidth();
        int h = (int)src.GetHeight();
        Gdiplus::Bitmap dst(w, h, PixelFormat32bppARGB);
        {
            Gdiplus::Graphics g(&dst);
            g.Clear(Gdiplus::Color(255, 255, 255, 255));
            g.DrawImage(&src, 0, 0, w, h);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::Color col(230, GetRValue(job->color), GetGValue(job->color), GetBValue(job->color));
            Gdiplus::Pen pen(col, 3.f);
            float x = (job->mark.x - job->region.x) * job->zoom;
            float y = (job->mark.y - job->region.y) * job->zoom;
            if (job->mark.dx > 0 && job->mark.dy > 0) {
                g.DrawRectangle(&pen, x, y, job->mark.dx * job->zoom, job->mark.dy * job->zoom);
            } else {
                float r = 10.f;
                Gdiplus::Pen white(Gdiplus::Color(255, 255, 255, 255), 5.f);
                g.DrawEllipse(&white, x - r, y - r, 2 * r, 2 * r);
                g.DrawEllipse(&pen, x - r, y - r, 2 * r, 2 * r);
            }
        }
        CLSID clsid = GetGdiPlusEncoderClsid(L"image/png");
        dst.Save(CWStrTemp(job->outPath), &clsid, nullptr);
    }
    if (px) {
        FreePixmap(px);
    }
    job->engine->Release();
    job->engine = nullptr;
    uitask::Post(MkFunc0(SnapDone, job), "ReviewSnapDone");
}

// renders the area around a new comment to review-comments/snapshots/<name>
static void StartSnapshot(MainWindow* win, ReviewSession* s, int page, RectF mark, ReviewSeverity sev, Str fileName) {
    TempStr dirPath = path::JoinTemp(s->reviewDir, StrL("snapshots"));
    if (!dir::Exists(dirPath) && !dir::CreateAll(dirPath)) {
        return;
    }
    RectF mb = s->engine->PageMediabox(page);
    RectF region;
    if (mark.dx > 0 && mark.dy > 0) {
        region = mark;
        region.Inflate(30, 30);
    } else {
        region = RectF(mark.x - 130, mark.y - 80, 260, 160);
    }
    // keep inside the page
    region = region.Intersect(mb);
    if (region.IsEmpty()) {
        return;
    }
    auto job = new ReviewSnapJob();
    job->win = win;
    job->engine = s->engine;
    job->engine->AddRef();
    job->page = page;
    job->region = region;
    job->zoom = std::min(3.f, std::max(1.f, 560.f / std::max(region.dx, 1.f)));
    job->mark = mark;
    job->color = kSevColor[(int)sev];
    job->outPath = str::Dup(path::JoinTemp(dirPath, fileName));
    RunAsync(MkFunc0(SnapThread, job), "ReviewSnapshot");
}

// --- timers ---

static void NotifyMentions(MainWindow* win, ReviewSession* s) {
    i64 since = s->lastMentionCheck;
    s->lastMentionCheck = NowTime();
    TempStr at1 = str::JoinTemp(StrL("@"), s->myName);
    TempStr at2 = str::JoinTemp(StrL("@"), s->myId);
    auto mentions = [&](Str text) {
        return str::IndexOfI(text, at1) >= 0 || str::IndexOfI(text, at2) >= 0;
    };
    for (ReviewComment* c : s->model.comments) {
        if (c->deleted) {
            continue;
        }
        if (c->created > since && !str::EqI(c->userId, s->myId) && mentions(c->text)) {
            ShowReviewToast(win, fmt(_TRA("%s mentioned you in %s").s, s->model.UserName(c->userId),
                                     s->model.DisplayIdTemp(c)));
            return;
        }
        for (ReviewReply* r : c->replies) {
            if (r->time > since && !str::EqI(r->by, s->myId) && mentions(r->text)) {
                ShowReviewToast(win, fmt(_TRA("%s mentioned you in %s").s, s->model.UserName(r->by),
                                         s->model.DisplayIdTemp(c)));
                return;
            }
        }
    }
}

static void LoadDone(ReviewLoadJob* job) {
    AutoDelete del(job);
    ReviewSession* s = FindSessionByPath(job->win, job->pdfPath);
    if (!s) {
        return; // the tab was closed
    }
    s->loading = false;
    bool outdated = job->appendCount != s->appendCount || !str::EqI(job->reviewDir, s->reviewDir);
    if (outdated || s->reloadPending) {
        s->reloadPending = false;
        StartLoad(job->win, s);
        return;
    }
    ReviewDeleteEvents(s->events);
    for (ReviewEvent* e : job->events) {
        s->events.Append(e);
    }
    job->events.Reset();
    bool firstLoad = !s->loadedOnce;
    s->loadedOnce = true;
    s->signature = job->signature;
    if (ApplyEvents(s)) {
        StartIdleWork(job->win);
    }
    if (!firstLoad) {
        NotifyMentions(job->win, s);
    }
    RefreshReviewUI(job->win);
}

static void SyncTick(MainWindow* win) {
    ReviewSession* s = CurrentSession(win);
    if (!s) {
        return;
    }
    bool maybeChanged = false;
    if (dir::Exists(s->reviewDir)) {
        if (!s->changeHandle) {
            DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
            s->changeHandle = FindFirstChangeNotificationW(CWStrTemp(s->reviewDir), FALSE, filter);
        }
        if (s->changeHandle && s->changeHandle != INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(s->changeHandle, 0) == WAIT_OBJECT_0) {
                FindNextChangeNotification(s->changeHandle);
                maybeChanged = true;
            }
        }
        // safety net for file systems without change notifications
        s->ticks++;
        if (s->ticks % 10 == 0) {
            maybeChanged = true;
        }
    }
    if (maybeChanged) {
        i64 sig = ComputeSignature(s->reviewDir);
        if (sig != s->signature) {
            s->signature = sig; // start this reload only once
            StartLoad(win, s);
        }
    }
    for (ReviewComment* c : s->model.comments) {
        if (!c->deleted && len(c->net) > 0 && c->status == ReviewStatus::Open) {
            StartIdleWork(win); // net markers on pages that scrolled into view
            break;
        }
    }
    if (IsDesigner(s) && s->reportDirty && !s->reportBusy && NowTime() >= s->reportNextTry && !s->localFallback) {
        s->reportNextTry = NowTime() + 10; // at most every 10 seconds
        StartReport(win, s, s->reportPath, false);
    }
}

static int CountVisibleNetWorkPending(MainWindow* win, ReviewSession* s, bool* didExtract);

// true if the user pressed a key or moved / clicked the mouse in the last ms milliseconds
static bool UserInputWithinMs(DWORD ms) {
    LASTINPUTINFO li{};
    li.cbSize = sizeof(li);
    return GetLastInputInfo(&li) && GetTickCount() - li.dwTime < ms;
}

// sheet titles for the panel (Sheets tab, report): reads the text of the remaining
// pages, one page at a time and only while the user isn't doing anything.
// Returns true while there are pages left
static bool PrefetchTitlesStep(MainWindow* win, ReviewSession* s, bool* didExtract) {
    if (!win->reviewVisible) {
        return false;
    }
    int next = 0;
    int n = len(s->pages);
    for (int i = 0; i < n; i++) {
        if (!s->pages[i]->loaded) {
            next = i + 1;
            break;
        }
    }
    if (next == 0) {
        return false;
    }
    if (UserInputWithinMs(1000)) {
        return true; // the user is scrolling or typing: later
    }
    PageWords(s, next);
    *didExtract = true;
    return true;
}

static void IdleTick(MainWindow* win) {
    ReviewSession* s = CurrentSession(win);
    if (!s) {
        KillTimer(win->hwndCanvas, kReviewIdleTimerId);
        return;
    }
    DWORD start = GetTickCount();
    int resolvedBefore = len(s->resolved);
    // reading a page's text can take tens of ms: not while the user scrolls or zooms
    // (pretending a page was already read this tick stops the steps before the next read)
    bool userBusy = UserInputWithinMs(300);
    bool didExtract = userBusy;
    bool more = true;
    while (more && (GetTickCount() - start) < 12) {
        more = ResolveStep(s, &didExtract);
        if (didExtract) {
            break;
        }
    }
    if (!more) {
        more = CountVisibleNetWorkPending(win, s, &didExtract) > 0;
    }
    bool extracted = didExtract && !userBusy;
    if (len(s->resolved) != resolvedBefore || extracted) {
        // pins moved or net markers became available; the panel gets one update at the end
        ScheduleRepaint(win, 0);
    }
    if (more) {
        return;
    }
    bool fetchedTitle = false;
    if (PrefetchTitlesStep(win, s, &fetchedTitle)) {
        // low priority work: tick slower
        SetTimer(win->hwndCanvas, kReviewIdleTimerId, 150, nullptr);
        if (fetchedTitle && ++s->titlesFetched % 10 == 0) {
            PanelUpdate(win);
        }
        return;
    }
    KillTimer(win->hwndCanvas, kReviewIdleTimerId);
    PanelUpdate(win);
}

static void CompareTick(MainWindow* win);

void ReviewOnTimer(MainWindow* win, UINT_PTR timerId) {
    if (!win || !IsMainWindowValid(win)) {
        return;
    }
    if (timerId == kReviewSyncTimerId) {
        SyncTick(win);
        CompareTick(win);
    } else if (timerId == kReviewIdleTimerId) {
        IdleTick(win);
    }
}


// --- pins on the canvas ---

static Gdiplus::Color GdipColor(COLORREF c, u8 alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void AddRoundRect(Gdiplus::GraphicsPath& path, Gdiplus::RectF r, float radius) {
    float d = radius * 2;
    if (d > r.Height) {
        d = r.Height;
    }
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

static bool IsVisibleOnCanvas(ReviewSession* s, ReviewComment* c) {
    if (c->deleted || c->curPage < 1) {
        return false;
    }
    if (!s->showResolved && c->status != ReviewStatus::Open) {
        return false;
    }
    return true;
}

// rings around every occurrence of a net-level comment's net on visible pages
static void PaintNetMarkers(Gdiplus::Graphics& g, DisplayModel* dm, ReviewSession* s, ReviewComment* c, float scale) {
    if (len(c->net) == 0 || c->status != ReviewStatus::Open) {
        return;
    }
    Gdiplus::Pen pen(GdipColor(kSevColor[(int)c->severity], 200), 2.f * scale);
    int nPages = dm->PageCount();
    for (int page = 1; page <= nPages && page <= len(s->pages); page++) {
        if (!dm->PageVisible(page)) {
            continue;
        }
        ReviewPageWords* pw = s->pages[page - 1];
        if (!pw->loaded) {
            continue; // the idle worker loads it and repaints
        }
        for (const ReviewWord& w : pw->words) {
            if (!str::Eq(w.text, c->net)) {
                continue;
            }
            Rect rc = dm->CvtToScreen(page, w.rect);
            float pad = 3.f * scale;
            Gdiplus::GraphicsPath path;
            AddRoundRect(path,
                         Gdiplus::RectF((float)rc.x - pad, (float)rc.y - pad, (float)rc.dx + 2 * pad, (float)rc.dy + 2 * pad),
                         4.f * scale);
            g.DrawPath(&pen, &path);
        }
    }
}

static int CountVisibleNetWorkPending(MainWindow* win, ReviewSession* s, bool* didExtract) {
    bool anyNet = false;
    for (ReviewComment* c : s->model.comments) {
        if (!c->deleted && len(c->net) > 0 && c->status == ReviewStatus::Open) {
            anyNet = true;
            break;
        }
    }
    if (!anyNet) {
        return 0;
    }
    DisplayModel* dm = win->AsFixed();
    if (!dm) {
        return 0;
    }
    int pending = 0;
    int nPages = std::min(dm->PageCount(), len(s->pages));
    for (int page = 1; page <= nPages; page++) {
        if (!dm->PageVisible(page) || s->pages[page - 1]->loaded) {
            continue;
        }
        if (*didExtract) {
            pending++;
            continue;
        }
        PageWords(s, page);
        *didExtract = true;
    }
    return pending;
}

static void PaintCompareOverlay(Gdiplus::Graphics& g, DisplayModel* dm, ReviewSession* s);

void ReviewPaint(MainWindow* win, HDC hdc) {
    ReviewSession* s = CurrentSession(win);
    if (!s) {
        return;
    }
    s->pinRects.Reset();
    s->pinKeys.Reset();
    DisplayModel* dm = win->AsFixed();
    if (!dm) {
        return;
    }
    bool anything = len(s->model.comments) > 0 || (s->compare != nullptr);
    if (!anything) {
        return;
    }
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.SetPageUnit(Gdiplus::UnitPixel);
    float scale = (float)DpiScale(win->hwndCanvas, 100) / 100.f;

    if (s->compare) {
        PaintCompareOverlay(g, dm, s);
    }

    HFONT hfont = GetDefaultGuiFont(true, false);
    Gdiplus::Font font(hdc, hfont);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    // paint the selected pin last so it's on top
    Vec<ReviewComment*> order;
    ReviewComment* selected = nullptr;
    for (ReviewComment* c : s->model.comments) {
        if (!IsVisibleOnCanvas(s, c) || !dm->PageVisible(c->curPage)) {
            continue;
        }
        if (len(s->selectedKey) > 0 && str::Eq(c->key, s->selectedKey)) {
            selected = c;
            continue;
        }
        order.Append(c);
    }
    if (selected) {
        order.Append(selected);
    }
    // zoomed far out with many pins: just dots (labels would overlap anyway, and
    // measuring text for hundreds of labels would slow down scrolling)
    bool compact = len(order) > 150;

    for (ReviewComment* c : order) {
        bool isOpen = c->status == ReviewStatus::Open;
        bool isSel = c == selected;
        COLORREF col = isOpen ? kSevColor[(int)c->severity] : kResolvedColor;
        u8 alpha = c->status == ReviewStatus::Rejected ? 150 : 255;
        if (c->anchorState == ReviewAnchorState::Gone) {
            alpha = 150;
        }
        PaintNetMarkers(g, dm, s, c, scale);

        Point anchor = dm->CvtToScreen(c->curPage, PointF{c->curRect.x, c->curRect.y});
        if (compact && c != selected) {
            float r = 4.5f * scale;
            Gdiplus::SolidBrush dot(GdipColor(col, alpha));
            Gdiplus::Pen ring(Gdiplus::Color(alpha, 255, 255, 255), 1.f * scale);
            g.FillEllipse(&dot, (float)anchor.x - r, (float)anchor.y - r, 2 * r, 2 * r);
            g.DrawEllipse(&ring, (float)anchor.x - r, (float)anchor.y - r, 2 * r, 2 * r);
            int hr = (int)r + 3;
            s->pinRects.Append(Rect(anchor.x - hr, anchor.y - hr, 2 * hr, 2 * hr));
            s->pinKeys.Append(c->key);
            continue;
        }
        if (c->IsBox()) {
            Rect rc = dm->CvtToScreen(c->curPage, c->curRect);
            Gdiplus::RectF r((float)rc.x, (float)rc.y, (float)rc.dx, (float)rc.dy);
            Gdiplus::Pen pen(GdipColor(col, alpha), (isSel ? 3.f : 2.f) * scale);
            if (str::Eq(c->shape, "ellipse")) {
                g.DrawEllipse(&pen, r);
            } else if (str::Eq(c->shape, "highlight")) {
                Gdiplus::SolidBrush fill(GdipColor(col, 50));
                g.FillRectangle(&fill, r);
            } else {
                g.DrawRectangle(&pen, r);
            }
        }

        // label: "AA-014" (fixed: "✓ AA-014")
        TempStr label = s->model.DisplayIdTemp(c);
        if (c->status == ReviewStatus::Fixed) {
            label = str::JoinTemp(StrL("\xe2\x9c\x93 "), label);
        } else if (c->status == ReviewStatus::Rejected) {
            label = str::JoinTemp(StrL("\xe2\x9c\x97 "), label);
        }
        int cch = 0;
        WCHAR* labelW = CWStrTemp(label, cch);
        Gdiplus::RectF measured;
        g.MeasureString(labelW, cch, &font, Gdiplus::PointF(0, 0), &sf, &measured);
        float padX = 7.f * scale;
        float h = std::max(measured.Height + 4.f * scale, 18.f * scale);
        float w = measured.Width + 2 * padX;
        float dotR = 4.f * scale;
        float x = (float)anchor.x + 5.f * scale;
        float y = (float)anchor.y - h - 5.f * scale;
        Gdiplus::RectF pill(x, y, w, h);

        // connector + dot at the exact comment point
        if (!c->IsBox()) {
            Gdiplus::Pen line(GdipColor(col, alpha), 1.5f * scale);
            g.DrawLine(&line, (float)anchor.x, (float)anchor.y, x + 3.f * scale, y + h - 2.f * scale);
            Gdiplus::SolidBrush dot(GdipColor(col, alpha));
            Gdiplus::Pen dotRing(Gdiplus::Color(alpha, 255, 255, 255), 1.5f * scale);
            g.FillEllipse(&dot, (float)anchor.x - dotR, (float)anchor.y - dotR, 2 * dotR, 2 * dotR);
            g.DrawEllipse(&dotRing, (float)anchor.x - dotR, (float)anchor.y - dotR, 2 * dotR, 2 * dotR);
        }

        // soft shadow, then the pill
        {
            Gdiplus::GraphicsPath shadow;
            AddRoundRect(shadow, Gdiplus::RectF(x + 1.f * scale, y + 2.f * scale, w, h), h / 2);
            Gdiplus::SolidBrush sb(Gdiplus::Color(isSel ? 90 : 50, 0, 0, 0));
            g.FillPath(&sb, &shadow);
        }
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, pill, h / 2);
        Gdiplus::SolidBrush fill(GdipColor(col, alpha));
        g.FillPath(&fill, &path);
        if (isSel) {
            Gdiplus::Pen ring(GdipColor(col, 120), 4.f * scale);
            Gdiplus::GraphicsPath outer;
            AddRoundRect(outer, Gdiplus::RectF(x - 3.f * scale, y - 3.f * scale, w + 6.f * scale, h + 6.f * scale),
                         h / 2 + 3.f * scale);
            g.DrawPath(&ring, &outer);
        }
        Gdiplus::Pen border(Gdiplus::Color(alpha, 255, 255, 255), 1.5f * scale);
        if (c->anchorState == ReviewAnchorState::Gone) {
            border.SetDashStyle(Gdiplus::DashStyleDash);
        }
        g.DrawPath(&border, &path);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
        g.DrawString(labelW, cch, &font, pill, &sf, &textBrush);

        // amber badge: position needs checking on this revision
        if (c->anchorState == ReviewAnchorState::Moved) {
            float br = 4.5f * scale;
            Gdiplus::SolidBrush amber(Gdiplus::Color(255, 0xF5, 0x9E, 0x0B));
            Gdiplus::Pen ring(Gdiplus::Color(255, 255, 255, 255), 1.5f * scale);
            g.FillEllipse(&amber, x + w - br, y - br, 2 * br, 2 * br);
            g.DrawEllipse(&ring, x + w - br, y - br, 2 * br, 2 * br);
        }
        // reply count bubble
        int nReplies = len(c->replies);
        if (nReplies > 0) {
            TempStr cnt = fmt("%d", nReplies);
            int ccn = 0;
            WCHAR* cntW = CWStrTemp(cnt, ccn);
            float bh = h * 0.8f;
            float bw = std::max(bh, 10.f * scale + 6.f * scale * (float)(ccn - 1));
            Gdiplus::RectF bub(x + w + 3.f * scale, y + (h - bh) / 2, bw, bh);
            Gdiplus::GraphicsPath bp;
            AddRoundRect(bp, bub, bh / 2);
            Gdiplus::SolidBrush bubFill(Gdiplus::Color(alpha, 0x37, 0x41, 0x51));
            g.FillPath(&bubFill, &bp);
            g.DrawString(cntW, ccn, &font, bub, &sf, &textBrush);
            pill.Width += bw + 3.f * scale;
        }

        Rect hit((int)pill.X, (int)pill.Y, (int)pill.Width + 1, (int)pill.Height + 1);
        if (!c->IsBox()) {
            Rect dotRc(anchor.x - (int)dotR - 2, anchor.y - (int)dotR - 2, (int)(2 * dotR) + 4, (int)(2 * dotR) + 4);
            hit = hit.Union(dotRc);
        }
        s->pinRects.Append(hit);
        s->pinKeys.Append(c->key);
    }
}

static int HitTestPin(ReviewSession* s, Point pt) {
    // last painted is on top
    for (int i = len(s->pinRects) - 1; i >= 0; i--) {
        if (s->pinRects[i].Contains(pt)) {
            return i;
        }
    }
    return -1;
}

static TempStr PinTooltipTemp(ReviewSession* s, ReviewComment* c) {
    str::Builder b;
    b.Append(fmt("%s  \xc2\xb7  %s  \xc2\xb7  %s", s->model.DisplayIdTemp(c), ReviewSeverityName(c->severity),
                 ReviewStatusName(c->status)));
    b.AppendChar('\n');
    if (len(c->component) > 0) {
        b.Append(c->component);
        b.Append(StrL("  \xc2\xb7  "));
    }
    b.Append(fmt("p.%d", c->curPage));
    if (len(c->sheet) > 0) {
        b.Append(StrL("  \xc2\xb7  "));
        b.Append(c->sheet);
    }
    b.Append(StrL("\n"));
    b.Append(s->model.UserName(c->userId));
    b.Append(StrL(": "));
    TempStr text = str::DupTemp(c->text);
    if (text.len > 240) {
        text.len = 240;
        text = str::JoinTemp(text, StrL("\xe2\x80\xa6"));
    }
    b.Append(text);
    int nr = len(c->replies);
    if (nr > 0 || len(c->agrees) > 0) {
        b.Append(fmt("\n\xf0\x9f\x92\xac %d   +1 \xc3\x97%d", nr, len(c->agrees)));
    }
    if (c->anchorState == ReviewAnchorState::Moved) {
        b.Append(StrL("\n\xe2\x9a\xa0 position moved in this revision, please check"));
    } else if (c->anchorState == ReviewAnchorState::Gone) {
        b.Append(StrL("\n\xe2\x9c\x97 component no longer found (fixed?)"));
    }
    return str::DupTemp(Str(b.els, (int)b.len));
}

bool ReviewOnSetCursor(MainWindow* win, Point pt) {
    ReviewSession* s = CurrentSession(win);
    if (!s) {
        return false;
    }
    if (len(s->placingKey) > 0 && win->AsFixed() && win->AsFixed()->GetPageNoByPoint(pt) > 0) {
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
        return true;
    }
    int idx = HitTestPin(s, pt);
    if (idx < 0) {
        return false;
    }
    ReviewComment* c = s->model.FindComment(s->pinKeys.At(idx));
    if (!c) {
        return false;
    }
    SetCursor(LoadCursor(nullptr, IDC_HAND));
    Rect rc = s->pinRects[idx];
    win->ShowToolTip(PinTooltipTemp(s, c), rc, true);
    return true;
}

static void WriteMoveEvent(MainWindow* win, ReviewSession* s, ReviewComment* c, int page, PointF pt);

bool ReviewOnLeftButtonDown(MainWindow* win, int x, int y) {
    ReviewSession* s = CurrentSession(win);
    if (!s) {
        return false;
    }
    Point pt{x, y};
    DisplayModel* dm = win->AsFixed();
    if (len(s->placingKey) > 0 && dm) {
        int page = dm->GetPageNoByPoint(pt);
        if (page < 1) {
            return true; // keep waiting for a click on a page
        }
        bool isNew = str::Eq(s->placingKey, kPlaceNewComment);
        ReviewComment* c = isNew ? nullptr : s->model.FindComment(s->placingKey);
        str::FreePtr(&s->placingKey);
        PanelUpdate(win);
        if (isNew) {
            ReviewAddComment(win, pt, true);
        } else if (c) {
            WriteMoveEvent(win, s, c, page, dm->CvtFromScreen(pt, page));
        }
        return true;
    }
    int idx = HitTestPin(s, pt);
    if (idx < 0) {
        return false;
    }
    TempStr key = str::DupTemp(s->pinKeys.At(idx));
    SelectComment(win, s, key, false);
    if (!win->reviewVisible) {
        ReviewTogglePanel(win);
    }
    return true;
}

// --- selection and navigation ---

static void ScrollToComment(MainWindow* win, ReviewComment* c) {
    if (!win->ctrl || c->curPage < 1) {
        return;
    }
    RectF r = c->curRect;
    if (!c->IsBox()) {
        r = RectF(r.x - 60, r.y - 60, 120, 120);
    } else {
        r.Inflate(30, 30);
    }
    win->ctrl->ScrollTo(c->curPage, r, 0);
}

static void SelectComment(MainWindow* win, ReviewSession* s, Str key, bool scroll) {
    SetStr(&s->selectedKey, key);
    ReviewComment* c = s->model.FindComment(key);
    if (c && scroll) {
        ScrollToComment(win, c);
    }
    RefreshReviewUI(win);
}

// comments in reading order: page, then top to bottom, left to right
static void CommentsInOrder(ReviewSession* s, Vec<ReviewComment*>& out) {
    for (ReviewComment* c : s->model.comments) {
        if (IsVisibleOnCanvas(s, c)) {
            out.Append(c);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](ReviewComment* a, ReviewComment* b) {
        if (a->curPage != b->curPage) {
            return a->curPage < b->curPage;
        }
        if (fabsf(a->curRect.y - b->curRect.y) > 4) {
            return a->curRect.y < b->curRect.y;
        }
        return a->curRect.x < b->curRect.x;
    });
}

void ReviewSelectNext(MainWindow* win, bool forward) {
    ReviewSession* s = CurrentSession(win);
    if (!s) {
        return;
    }
    Vec<ReviewComment*> v;
    CommentsInOrder(s, v);
    int n = len(v);
    if (n == 0) {
        ShowReviewToast(win, _TRA("No review comments yet"));
        return;
    }
    int cur = -1;
    for (int i = 0; i < n; i++) {
        if (len(s->selectedKey) > 0 && str::Eq(v[i]->key, s->selectedKey)) {
            cur = i;
            break;
        }
    }
    int next;
    if (cur < 0) {
        next = forward ? 0 : n - 1;
    } else {
        next = forward ? (cur + 1) % n : (cur - 1 + n) % n;
    }
    SelectComment(win, s, v[next]->key, true);
}

// --- simple status / edit actions ---

static ReviewComment* SelectedComment(ReviewSession* s) {
    if (!s || len(s->selectedKey) == 0) {
        return nullptr;
    }
    ReviewComment* c = s->model.FindComment(s->selectedKey);
    if (c && c->deleted) {
        return nullptr;
    }
    return c;
}

static void SetCommentSeverity(MainWindow* win, ReviewSession* s, ReviewComment* c, ReviewSeverity sev) {
    if (!c || c->severity == sev) {
        return;
    }
    ReviewEventBuilder eb(StrL("edit"), s->myId, NowTime());
    eb.Add(StrL("id"), c->key);
    eb.Add(StrL("sev"), ReviewSeverityName(sev));
    CommitEvent(win, s, eb);
}

static void SetCommentStatus(MainWindow* win, ReviewSession* s, ReviewComment* c, ReviewStatus st) {
    if (!c || c->status == st) {
        return;
    }
    ReviewEventBuilder eb(StrL("status"), s->myId, NowTime());
    eb.Add(StrL("id"), c->key);
    eb.Add(StrL("status"), ReviewStatusName(st));
    CommitEvent(win, s, eb);
}

void ReviewMarkPageReviewed(MainWindow* win) {
    ReviewSession* s = CurrentSession(win, true);
    DisplayModel* dm = win ? win->AsFixed() : nullptr;
    if (!s || !dm) {
        return;
    }
    EnsureLoaded(s);
    int page = dm->CurrentPageNo();
    ReviewUser* me = s->model.FindUser(s->myId);
    TempStr pageStr = fmt("%d", page);
    bool done = !(me && me->reviewedPages.Contains(pageStr));
    ReviewEventBuilder eb(StrL("page"), s->myId, NowTime());
    eb.Add(StrL("page"), page);
    eb.AddBool(StrL("done"), done);
    ReviewPageWords* pw = PageWords(s, page);
    if (pw && len(pw->title) > 0) {
        eb.Add(StrL("sheet"), pw->title);
    }
    if (CommitEvent(win, s, eb)) {
        ShowReviewToast(win, done ? fmt(_TRA("Page %d marked as reviewed").s, page)
                                  : fmt(_TRA("Page %d marked as not reviewed").s, page));
    }
}


// --- composer: the pop-up where a comment is written ---

enum ComposerHit {
    kHitNone = -1,
    kHitSev0 = 0, // .. kHitSev0 + 3
    kHitShape0 = 10,
    kHitDupAgree = 20,
    kHitNet = 21,
    kHitTemplates = 30,
    kHitCancel = 31,
    kHitSave = 32,
};

static SeqStrings gShapeNames = "box\0ellipse\0highlight\0";
static SeqStrings gShapeLabels = "\xe2\x96\xad Box\0\xe2\x97\xaf Ellipse\0\xe2\x96\xae Highlight\0";

struct ReviewComposer : Wnd {
    MainWindow* win = nullptr;
    Str pdfPath;
    int page = 0;
    PointF pt;
    RectF box;
    Str editKey; // editing an existing comment
    ReviewSeverity sev = ReviewSeverity::Major;
    int shapeIdx = 0;
    Str component;
    Str clicked;
    Str sheet;
    Str netCandidate;
    bool applyToNet = false;
    Str dupKey;
    Str dupLabel;
    Str anchorWord;
    float ax = 0;
    float ay = 0;
    Str nearJoined;
    Str title;
    Str context;

    Edit* edit = nullptr;
    Rect rcSev[kReviewSeverityCount];
    Rect rcShape[3];
    Rect rcEditFrame;
    Rect rcHint;
    Rect rcDup;
    Rect rcDupBtn;
    Rect rcNet;
    Rect rcTemplates;
    Rect rcCancel;
    Rect rcSave;
    int hot = kHitNone;
    bool closing = false;

    ReviewComposer() = default;
    ~ReviewComposer() override;

    bool IsBox() const {
        return box.dx > 0 && box.dy > 0;
    }
    void DoLayout();
    int HitTest(Point p);
    void Click(int hit);
    void Save();
    void Cancel();
    void ShowTemplatesMenu();
    void Paint(HDC hdc);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) override;
    bool PreTranslateMessage(MSG& msg) override;
};

static ReviewComposer* gComposer = nullptr;

ReviewComposer::~ReviewComposer() {
    delete edit;
    str::Free(pdfPath);
    str::Free(editKey);
    str::Free(component);
    str::Free(clicked);
    str::Free(sheet);
    str::Free(netCandidate);
    str::Free(dupKey);
    str::Free(dupLabel);
    str::Free(anchorWord);
    str::Free(nearJoined);
    str::Free(title);
    str::Free(context);
}

static void DestroyComposerTask(ReviewComposer* c) {
    delete c;
}

static void CloseComposer(ReviewComposer* c) {
    if (!c || c->closing) {
        return;
    }
    c->closing = true;
    if (gComposer == c) {
        gComposer = nullptr; // from now on only the posted task owns it
    }
    if (c->hwnd) {
        ShowWindow(c->hwnd, SW_HIDE);
    }
    if (c->win && IsMainWindowValid(c->win)) {
        HwndSetFocus(c->win->hwndFrame);
    }
    // can't delete the window while it's processing a message
    uitask::Post(MkFunc0(DestroyComposerTask, c), "ReviewComposerDestroy");
}

static int Px(HWND hwnd, int n) {
    return DpiScale(hwnd, n);
}

void ReviewComposer::DoLayout() {
    Rect rc = ClientRect(hwnd);
    int pad = Px(hwnd, 14);
    int gap = Px(hwnd, 8);
    int w = rc.dx - 2 * pad;
    int y = pad + Px(hwnd, 22) + gap; // title row
    int sevGap = Px(hwnd, 6);
    int sevW = (w - 3 * sevGap) / kReviewSeverityCount;
    int sevH = Px(hwnd, 28);
    for (int i = 0; i < kReviewSeverityCount; i++) {
        rcSev[i] = Rect(pad + i * (sevW + sevGap), y, sevW, sevH);
    }
    y += sevH + gap;
    if (IsBox()) {
        int shW = (w - 2 * sevGap) / 3;
        int shH = Px(hwnd, 24);
        for (int i = 0; i < 3; i++) {
            rcShape[i] = Rect(pad + i * (shW + sevGap), y, shW, shH);
        }
        y += shH + gap;
    } else {
        for (int i = 0; i < 3; i++) {
            rcShape[i] = Rect();
        }
    }
    int editH = Px(hwnd, 86);
    rcEditFrame = Rect(pad, y, w, editH);
    if (edit && edit->hwnd) {
        int inset = Px(hwnd, 7);
        MoveWindow(edit->hwnd, rcEditFrame.x + inset, rcEditFrame.y + inset - Px(hwnd, 2), rcEditFrame.dx - 2 * inset,
                   rcEditFrame.dy - 2 * inset + Px(hwnd, 2), TRUE);
    }
    y += editH + Px(hwnd, 4);
    rcHint = Rect(pad, y, w, Px(hwnd, 16));
    y += rcHint.dy + gap;
    if (len(dupKey) > 0) {
        rcDup = Rect(pad, y, w, Px(hwnd, 34));
        int bw = Px(hwnd, 92);
        rcDupBtn = Rect(rcDup.x + rcDup.dx - bw - Px(hwnd, 6), rcDup.y + Px(hwnd, 5), bw, rcDup.dy - Px(hwnd, 10));
        y += rcDup.dy + gap;
    } else {
        rcDup = Rect();
        rcDupBtn = Rect();
    }
    if (len(netCandidate) > 0 && len(editKey) == 0) {
        rcNet = Rect(pad, y, w, Px(hwnd, 22));
        y += rcNet.dy + gap;
    } else {
        rcNet = Rect();
    }
    int btnH = Px(hwnd, 30);
    int btnW = Px(hwnd, 86);
    rcSave = Rect(pad + w - btnW, y, btnW, btnH);
    rcCancel = Rect(rcSave.x - Px(hwnd, 8) - btnW, y, btnW, btnH);
    rcTemplates = Rect(pad, y, Px(hwnd, 110), btnH);
    y += btnH + pad;
    if (rc.dy != y) {
        Rect wr = WindowRect(hwnd);
        int extra = wr.dy - rc.dy;
        SetWindowPos(hwnd, nullptr, 0, 0, wr.dx, y + extra, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

int ReviewComposer::HitTest(Point p) {
    for (int i = 0; i < kReviewSeverityCount; i++) {
        if (rcSev[i].Contains(p)) {
            return kHitSev0 + i;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (!rcShape[i].IsEmpty() && rcShape[i].Contains(p)) {
            return kHitShape0 + i;
        }
    }
    if (!rcDupBtn.IsEmpty() && rcDupBtn.Contains(p)) {
        return kHitDupAgree;
    }
    if (!rcNet.IsEmpty() && rcNet.Contains(p)) {
        return kHitNet;
    }
    if (rcTemplates.Contains(p) && len(editKey) == 0) {
        return kHitTemplates;
    }
    if (rcCancel.Contains(p)) {
        return kHitCancel;
    }
    if (rcSave.Contains(p)) {
        return kHitSave;
    }
    return kHitNone;
}

void ReviewComposer::Cancel() {
    CloseComposer(this);
}

void ReviewComposer::ShowTemplatesMenu() {
    ReviewSession* s = FindSessionByPath(win, pdfPath);
    if (!s) {
        return;
    }
    StrVec items;
    Split(&items, len(s->model.templates) > 0 ? s->model.templates : Str(kDefaultTemplates), StrL("\n"), true);
    HMENU m = CreatePopupMenu();
    int n = len(items);
    for (int i = 0; i < n; i++) {
        AppendMenuW(m, MF_STRING, (UINT_PTR)(i + 1), CWStrTemp(items.At(i)));
    }
    POINT p{rcTemplates.x, rcTemplates.y + rcTemplates.dy};
    ClientToScreen(hwnd, &p);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    if (cmd < 1 || cmd > n) {
        HwndSetFocus(edit->hwnd);
        return;
    }
    Str t = items.At(cmd - 1);
    TempStr cur = edit->GetTextTemp();
    if (str::IsEmptyOrWhiteSpace(cur)) {
        edit->SetText(t);
    } else {
        edit->SetText(str::JoinTemp(cur, StrL("\r\n"), t));
    }
    HwndSetFocus(edit->hwnd);
    edit->SetCursorPositionAtEnd();
}

void ReviewComposer::Click(int hit) {
    if (hit >= kHitSev0 && hit < kHitSev0 + kReviewSeverityCount) {
        sev = (ReviewSeverity)(hit - kHitSev0);
        InvalidateRect(hwnd, nullptr, FALSE);
        HwndSetFocus(edit->hwnd);
        return;
    }
    if (hit >= kHitShape0 && hit < kHitShape0 + 3) {
        shapeIdx = hit - kHitShape0;
        InvalidateRect(hwnd, nullptr, FALSE);
        HwndSetFocus(edit->hwnd);
        return;
    }
    switch (hit) {
        case kHitDupAgree: {
            ReviewSession* s = FindSessionByPath(win, pdfPath);
            ReviewComment* d = s ? s->model.FindComment(dupKey) : nullptr;
            if (s && d) {
                bool already = d->agrees.Contains(s->myId);
                if (!already) {
                    ReviewEventBuilder eb(StrL("agree"), s->myId, NowTime());
                    eb.Add(StrL("id"), d->key);
                    eb.AddBool(StrL("on"), true);
                    CommitEvent(win, s, eb);
                }
                TempStr key = str::DupTemp(d->key);
                SelectComment(win, s, key, false);
                ShowReviewToast(win, fmt(_TRA("Added your +1 to %s").s, s->model.DisplayIdTemp(d)));
            }
            Cancel();
            return;
        }
        case kHitNet:
            applyToNet = !applyToNet;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kHitTemplates:
            ShowTemplatesMenu();
            return;
        case kHitCancel:
            Cancel();
            return;
        case kHitSave:
            Save();
            return;
    }
}

void ReviewComposer::Save() {
    if (closing) {
        return;
    }
    ReviewSession* s = FindSessionByPath(win, pdfPath);
    if (!s) {
        Cancel();
        return;
    }
    EnsureLoaded(s); // the comment number must not clash with one not loaded yet
    TempStr text = edit->GetTextTemp();
    str::TrimWSInPlace(text, str::TrimOpt::Both);
    text = Str(text.s); // trimming shortened the buffer
    if (len(text) == 0) {
        MessageBeep(MB_ICONWARNING);
        HwndSetFocus(edit->hwnd);
        return;
    }
    // edit control uses \r\n
    str::Builder clean;
    for (int i = 0; i < text.len; i++) {
        if (text.s[i] != '\r') {
            clean.AppendChar(text.s[i]);
        }
    }
    Str t(clean.els, (int)clean.len);

    if (len(editKey) > 0) {
        ReviewComment* c = s->model.FindComment(editKey);
        if (c) {
            ReviewEventBuilder eb(StrL("edit"), s->myId, NowTime());
            eb.Add(StrL("id"), c->key);
            if (!str::Eq(c->text, t)) {
                eb.Add(StrL("text"), t);
            }
            if (c->severity != sev) {
                eb.Add(StrL("sev"), ReviewSeverityName(sev));
            }
            CommitEvent(win, s, eb);
        }
        CloseComposer(this);
        return;
    }

    int n = ReviewNextNumber(&s->model, s->myId);
    TempStr snap = fmt("%s-%d.png", s->myId, n);
    ReviewEventBuilder eb(StrL("create"), s->myId, NowTime());
    eb.Add(StrL("n"), n);
    eb.Add(StrL("sev"), ReviewSeverityName(sev));
    eb.Add(StrL("text"), t);
    eb.Add(StrL("rev"), s->pdfName);
    eb.Add(StrL("page"), page);
    if (IsBox()) {
        eb.Add(StrL("x"), box.x);
        eb.Add(StrL("y"), box.y);
        eb.Add(StrL("w"), box.dx);
        eb.Add(StrL("h"), box.dy);
        eb.Add(StrL("shape"), SeqStrByIndex(gShapeNames, shapeIdx));
    } else {
        eb.Add(StrL("x"), pt.x);
        eb.Add(StrL("y"), pt.y);
        eb.Add(StrL("shape"), StrL("pin"));
    }
    if (len(component) > 0) {
        eb.Add(StrL("comp"), component);
    }
    if (len(clicked) > 0) {
        eb.Add(StrL("clicked"), clicked);
    }
    if (len(sheet) > 0) {
        eb.Add(StrL("sheet"), sheet);
    }
    if (applyToNet && len(netCandidate) > 0) {
        eb.Add(StrL("net"), netCandidate);
    }
    eb.Add(StrL("round"), s->model.round);
    if (len(anchorWord) > 0) {
        eb.Add(StrL("aw"), anchorWord);
        eb.Add(StrL("ax"), ax);
        eb.Add(StrL("ay"), ay);
    }
    if (len(nearJoined) > 0) {
        eb.Add(StrL("near"), nearJoined);
    }
    eb.Add(StrL("snap"), snap);
    if (CommitEvent(win, s, eb)) {
        RectF mark = IsBox() ? box : RectF(pt.x, pt.y, 0, 0);
        StartSnapshot(win, s, page, mark, sev, snap);
        TempStr key = fmt("%s#%d", s->myId, n);
        SelectComment(win, s, key, false);
    }
    CloseComposer(this);
}

static void FillRoundRect(Gdiplus::Graphics& g, Rect r, float radius, Gdiplus::Color fill) {
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, Gdiplus::RectF((float)r.x, (float)r.y, (float)r.dx, (float)r.dy), radius);
    Gdiplus::SolidBrush b(fill);
    g.FillPath(&b, &path);
}

static void StrokeRoundRect(Gdiplus::Graphics& g, Rect r, float radius, Gdiplus::Color col, float width) {
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, Gdiplus::RectF((float)r.x + 0.5f, (float)r.y + 0.5f, (float)r.dx - 1, (float)r.dy - 1), radius);
    Gdiplus::Pen p(col, width);
    g.DrawPath(&p, &path);
}

static void GdipDrawText(Gdiplus::Graphics& g, Str s, Gdiplus::Font* font, Rect r, Gdiplus::Color col,
                     Gdiplus::StringAlignment align = Gdiplus::StringAlignmentNear) {
    int cch = 0;
    WCHAR* ws = CWStrTemp(s, cch);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(align);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::SolidBrush b(col);
    g.DrawString(ws, cch, font, Gdiplus::RectF((float)r.x, (float)r.y, (float)r.dx, (float)r.dy), &sf, &b);
}

static bool IsDarkColor(COLORREF c) {
    int lum = (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000;
    return lum < 128;
}

void ReviewComposer::Paint(HDC hdcWnd) {
    Rect rc = ClientRect(hwnd);
    DoubleBuffer buffer(hwnd, rc);
    HDC hdc = buffer.GetDC();
    COLORREF bg = ThemeWindowBackgroundColor();
    COLORREF fg = ThemeWindowTextColor();
    bool dark = IsDarkColor(bg);
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    g.Clear(GdipColor(bg));
    Gdiplus::Font fntNormal(hdc, GetDefaultGuiFont());
    Gdiplus::Font bold(hdc, GetDefaultGuiFont(true, false));
    Gdiplus::Color fgc = GdipColor(fg);
    Gdiplus::Color dim = dark ? Gdiplus::Color(255, 0x9C, 0xA3, 0xAF) : Gdiplus::Color(255, 0x6B, 0x72, 0x80);
    int pad = Px(hwnd, 14);

    // title + context
    Rect rTitle(pad, pad, rc.dx - 2 * pad, Px(hwnd, 22));
    GdipDrawText(g, title, &bold, rTitle, fgc);
    if (len(context) > 0) {
        Rect rCtx(rTitle.x + rTitle.dx / 2, rTitle.y, rTitle.dx / 2, rTitle.dy);
        GdipDrawText(g, context, &fntNormal, rCtx, dim, Gdiplus::StringAlignmentFar);
    }

    // severity pills
    for (int i = 0; i < kReviewSeverityCount; i++) {
        Rect r = rcSev[i];
        COLORREF sc = kSevColor[i];
        bool sel = (int)sev == i;
        float radius = (float)r.dy / 2;
        if (sel) {
            FillRoundRect(g, r, radius, GdipColor(sc));
            GdipDrawText(g, ReviewSeverityName((ReviewSeverity)i), &bold, r, Gdiplus::Color(255, 255, 255, 255),
                     Gdiplus::StringAlignmentCenter);
        } else {
            if (hot == kHitSev0 + i) {
                FillRoundRect(g, r, radius, GdipColor(sc, 40));
            }
            StrokeRoundRect(g, r, radius, GdipColor(sc, 170), 1.2f);
            GdipDrawText(g, ReviewSeverityName((ReviewSeverity)i), &fntNormal, r, GdipColor(sc), Gdiplus::StringAlignmentCenter);
        }
    }

    // box shapes
    if (IsBox()) {
        for (int i = 0; i < 3; i++) {
            Rect r = rcShape[i];
            bool sel = shapeIdx == i;
            float radius = (float)r.dy / 2;
            Gdiplus::Color accent(255, 0x63, 0x66, 0xF1);
            if (sel) {
                FillRoundRect(g, r, radius, Gdiplus::Color(dark ? 90 : 40, 0x63, 0x66, 0xF1));
            } else if (hot == kHitShape0 + i) {
                FillRoundRect(g, r, radius, Gdiplus::Color(25, 0x63, 0x66, 0xF1));
            }
            StrokeRoundRect(g, r, radius, sel ? accent : dim, 1.f);
            GdipDrawText(g, SeqStrByIndex(gShapeLabels, i), &fntNormal, r, sel ? fgc : dim, Gdiplus::StringAlignmentCenter);
        }
    }

    // edit frame
    COLORREF editBg = ThemeWindowControlBackgroundColor();
    FillRoundRect(g, rcEditFrame, (float)Px(hwnd, 6), GdipColor(editBg));
    bool editFocused = edit && GetFocus() == edit->hwnd;
    StrokeRoundRect(g, rcEditFrame, (float)Px(hwnd, 6),
                    editFocused ? GdipColor(kSevColor[(int)sev]) : Gdiplus::Color(255, 0xD1, 0xD5, 0xDB),
                    editFocused ? 1.6f : 1.f);
    GdipDrawText(g, StrL("Enter to save  \xc2\xb7  Shift+Enter new line  \xc2\xb7  Ctrl+1-4 severity  \xc2\xb7  Esc cancel"),
             &fntNormal, rcHint, dim);

    // duplicate hint
    if (!rcDup.IsEmpty()) {
        Gdiplus::Color amberBg = dark ? Gdiplus::Color(255, 0x42, 0x36, 0x12) : Gdiplus::Color(255, 0xFE, 0xF3, 0xC7);
        FillRoundRect(g, rcDup, (float)Px(hwnd, 6), amberBg);
        Rect rText(rcDup.x + Px(hwnd, 10), rcDup.y, rcDup.dx - rcDupBtn.dx - Px(hwnd, 24), rcDup.dy);
        GdipDrawText(g, dupLabel, &fntNormal, rText, dark ? Gdiplus::Color(255, 0xFD, 0xE6, 0x8A) : Gdiplus::Color(255, 0x92, 0x40, 0x0E));
        FillRoundRect(g, rcDupBtn, (float)rcDupBtn.dy / 2,
                      hot == kHitDupAgree ? Gdiplus::Color(255, 0xD9, 0x77, 0x06) : Gdiplus::Color(255, 0xF5, 0x9E, 0x0B));
        GdipDrawText(g, StrL("+1 instead"), &bold, rcDupBtn, Gdiplus::Color(255, 255, 255, 255), Gdiplus::StringAlignmentCenter);
    }

    // net option
    if (!rcNet.IsEmpty()) {
        int boxDx = Px(hwnd, 14);
        Rect rb(rcNet.x, rcNet.y + (rcNet.dy - boxDx) / 2, boxDx, boxDx);
        if (applyToNet) {
            FillRoundRect(g, rb, 3.f, Gdiplus::Color(255, 0x63, 0x66, 0xF1));
            GdipDrawText(g, StrL("\xe2\x9c\x93"), &bold, rb, Gdiplus::Color(255, 255, 255, 255), Gdiplus::StringAlignmentCenter);
        } else {
            StrokeRoundRect(g, rb, 3.f, dim, 1.2f);
        }
        Rect rt(rb.x + boxDx + Px(hwnd, 8), rcNet.y, rcNet.dx - boxDx - Px(hwnd, 8), rcNet.dy);
        GdipDrawText(g, fmt("Apply to the whole net %s (marked everywhere it appears)", netCandidate), &fntNormal, rt,
                 hot == kHitNet ? fgc : dim);
    }

    // buttons
    if (len(editKey) == 0) {
        GdipDrawText(g, StrL("\xe2\x9c\x8e Templates \xe2\x96\xbe"), &fntNormal, rcTemplates,
                 hot == kHitTemplates ? Gdiplus::Color(255, 0x63, 0x66, 0xF1) : dim);
    }
    if (hot == kHitCancel) {
        FillRoundRect(g, rcCancel, (float)rcCancel.dy / 2, dark ? Gdiplus::Color(255, 0x37, 0x41, 0x51) : Gdiplus::Color(255, 0xF3, 0xF4, 0xF6));
    }
    StrokeRoundRect(g, rcCancel, (float)rcCancel.dy / 2, dim, 1.f);
    GdipDrawText(g, StrL("Cancel"), &fntNormal, rcCancel, fgc, Gdiplus::StringAlignmentCenter);
    Gdiplus::Color saveCol = hot == kHitSave ? Gdiplus::Color(255, 0x1D, 0x4E, 0xD8) : Gdiplus::Color(255, 0x25, 0x63, 0xEB);
    FillRoundRect(g, rcSave, (float)rcSave.dy / 2, saveCol);
    GdipDrawText(g, len(editKey) > 0 ? StrL("Update") : StrL("Save"), &bold, rcSave, Gdiplus::Color(255, 255, 255, 255),
             Gdiplus::StringAlignmentCenter);

    buffer.Flush(hdcWnd);
}

bool ReviewComposer::PreTranslateMessage(MSG& msg) {
    if (closing) {
        return false;
    }
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) {
        return false;
    }
    WPARAM vk = msg.wParam;
    bool ctrl = IsCtrlPressed();
    bool alt = IsAltPressed();
    if (vk == VK_ESCAPE) {
        Cancel();
        return true;
    }
    if (vk == VK_RETURN && !IsShiftPressed()) {
        Save();
        return true;
    }
    if ((ctrl || alt) && vk >= '1' && vk <= '4') {
        sev = (ReviewSeverity)(vk - '1');
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (ctrl && vk == 'T' && len(editKey) == 0) {
        ShowTemplatesMenu();
        return true;
    }
    return false;
}

LRESULT ReviewComposer::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            Paint(hdc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_SIZE:
            if (edit) {
                DoLayout();
            }
            break;
        case WM_LBUTTONDOWN: {
            Point p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int hit = HitTest(p);
            if (hit != kHitNone) {
                Click(hit);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            Point p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int hit = HitTest(p);
            if (hit != hot) {
                hot = hit;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = h;
                TrackMouseEvent(&tme);
            }
            break;
        }
        case WM_MOUSELEAVE:
            if (hot != kHitNone) {
                hot = kHitNone;
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        case WM_SETCURSOR: {
            Point p = HwndGetCursorPos(h);
            if (HitTest(p) != kHitNone) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            // edit focus changes repaint the frame around it
            if (edit && (HWND)lp == edit->hwnd && (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS)) {
                RECT r = ToRECT(rcEditFrame);
                InvalidateRect(h, &r, FALSE);
            }
            break;
        case WM_CLOSE:
            Cancel();
            return 0;
    }
    return WndProcDefault(h, msg, wp, lp);
}

static bool LooksLikeNet(Str w) {
    if (len(w) < 2 || ReviewIsDesignator(w)) {
        return false;
    }
    bool hasUpper = false;
    for (int i = 0; i < w.len; i++) {
        char c = w.s[i];
        if (c >= 'A' && c <= 'Z') {
            hasUpper = true;
            continue;
        }
        if ((c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-' || c == '/' || c == '.' || c == '#') {
            continue;
        }
        return false;
    }
    return hasUpper;
}

static void OpenComposer(MainWindow* win, ReviewSession* s, int page, PointF pt, RectF box, Str editKey) {
    if (gComposer && !gComposer->closing) {
        // don't throw away a comment that is being written
        TempStr cur = gComposer->edit ? gComposer->edit->GetTextTemp() : TempStr{};
        if (!str::IsEmptyOrWhiteSpace(cur)) {
            ShowWindow(gComposer->hwnd, SW_SHOW);
            HwndSetFocus(gComposer->edit->hwnd);
            MessageBeep(MB_ICONWARNING);
            ShowReviewToast(win, _TRA("Save or cancel the open comment first"), true);
            return;
        }
        CloseComposer(gComposer);
    }
    auto c = new ReviewComposer();
    c->win = win;
    c->pdfPath = str::Dup(s->pdfPath);
    c->page = page;
    c->pt = pt;
    c->box = box;
    ReviewComment* existing = len(editKey) > 0 ? s->model.FindComment(editKey) : nullptr;
    if (existing) {
        c->editKey = str::Dup(editKey);
        c->sev = existing->severity;
        c->page = existing->curPage;
        c->pt = PointF{existing->curRect.x, existing->curRect.y};
        c->title = str::Dup(fmt(_TRA("Edit %s").s, s->model.DisplayIdTemp(existing)));
        c->context = len(existing->component) > 0 ? str::Dup(fmt("%s  \xc2\xb7  p.%d", existing->component, existing->curPage))
                                                   : str::Dup(fmt("p.%d", existing->curPage));
    } else {
        ReviewPageWords* pw = PageWords(s, page);
        PointF anchorPt = c->IsBox() ? PointF{box.x + box.dx / 2, box.y + box.dy / 2} : pt;
        TempStr des = {};
        TempStr clicked = {};
        if (pw) {
            ReviewWordsNearPoint(pw->words, anchorPt, &des, &clicked);
            ReviewComment tmp;
            tmp.rect = RectF(pt.x, pt.y, 0, 0);
            ReviewComputeFingerprint(&tmp, pt, pw->words);
            c->anchorWord = str::Dup(tmp.anchorWord);
            c->ax = tmp.anchorDx;
            c->ay = tmp.anchorDy;
            str::Builder nb;
            for (Str e : tmp.nearWords) {
                if (!nb.IsEmpty()) {
                    nb.AppendChar('\n');
                }
                nb.Append(e);
            }
            c->nearJoined = str::Dup(Str(nb.els, (int)nb.len));
            c->sheet = str::Dup(pw->title);
        }
        c->component = str::Dup(des);
        c->clicked = str::Dup(clicked);
        if (LooksLikeNet(clicked)) {
            c->netCandidate = str::Dup(clicked);
        }
        c->title = str::Dup(_TRA("New review comment"));
        str::Builder ctx;
        if (len(des) > 0) {
            ctx.Append(des);
            ctx.Append(StrL("  \xc2\xb7  "));
        }
        ctx.Append(fmt("p.%d", page));
        c->context = str::Dup(Str(ctx.els, (int)ctx.len));

        // similar open comment nearby or on the same component
        float bestD = -1;
        for (ReviewComment* o : s->model.comments) {
            if (o->deleted || o->status != ReviewStatus::Open || o->curPage != page) {
                continue;
            }
            float dx = o->curRect.x - anchorPt.x;
            float dy = o->curRect.y - anchorPt.y;
            float d = dx * dx + dy * dy;
            bool isNear = d < 45.f * 45.f;
            bool sameComp = len(des) > 0 && str::Eq(o->component, des);
            if (!isNear && !sameComp) {
                continue;
            }
            if (bestD < 0 || d < bestD) {
                bestD = d;
                str::FreePtr(&c->dupKey);
                c->dupKey = str::Dup(o->key);
            }
        }
        if (len(c->dupKey) > 0) {
            ReviewComment* d = s->model.FindComment(c->dupKey);
            TempStr t = str::DupTemp(d->text);
            if (t.len > 60) {
                t.len = 60;
                t = str::JoinTemp(t, StrL("\xe2\x80\xa6"));
            }
            c->dupLabel =
                str::Dup(fmt("\xe2\x9a\xa0 Similar: %s by %s \xe2\x80\x94 \xe2\x80\x9c%s\xe2\x80\x9d", s->model.DisplayIdTemp(d),
                             s->model.UserName(d->userId), t));
        }
    }

    CreateCustomArgs args;
    args.visible = false;
    args.title = c->title;
    args.style = WS_POPUP | WS_BORDER | WS_CLIPCHILDREN;
    args.exStyle = WS_EX_TOOLWINDOW;
    args.isRtl = IsUIRtl();
    c->CreateCustom(args);
    if (!c->hwnd) {
        delete c;
        return;
    }
    SetWindowLongPtrW(c->hwnd, GWLP_HWNDPARENT, (LONG_PTR)win->hwndFrame);
    dwm::SetWindowRoundedCorners(c->hwnd, true);

    {
        Edit::CreateArgs ea;
        ea.parent = c->hwnd;
        ea.isMultiLine = true;
        ea.withBorder = false;
        ea.idealSizeLines = 4;
        ea.cueText = _TRA("Describe the issue...");
        ea.isRtl = IsUIRtl();
        c->edit = new Edit();
        c->edit->SetColors(ThemeWindowTextColor(), ThemeWindowControlBackgroundColor());
        c->edit->Create(ea);
        if (existing) {
            c->edit->SetText(existing->text);
        }
    }

    // size and position next to the comment, inside the monitor
    int dx = Px(c->hwnd, 430);
    int dy = Px(c->hwnd, 300);
    DisplayModel* dm = win->AsFixed();
    Point sp = dm ? dm->CvtToScreen(c->page, c->IsBox() ? PointF{box.x + box.dx, box.y} : c->pt) : Point{0, 0};
    POINT p{sp.x, sp.y};
    ClientToScreen(win->hwndCanvas, &p);
    Rect work = GetWorkAreaRect(WindowRect(win->hwndFrame), win->hwndFrame);
    int x = p.x + Px(c->hwnd, 24);
    int y = p.y - Px(c->hwnd, 40);
    if (x + dx > work.x + work.dx) {
        x = p.x - dx - Px(c->hwnd, 24);
    }
    x = std::max(work.x, std::min(x, work.x + work.dx - dx));
    y = std::max(work.y, std::min(y, work.y + work.dy - dy));
    SetWindowPos(c->hwnd, nullptr, x, y, dx, dy, SWP_NOZORDER | SWP_NOACTIVATE);
    c->DoLayout();
    ShowWindow(c->hwnd, SW_SHOW);
    HwndSetFocus(c->edit->hwnd);
    if (existing) {
        c->edit->SetCursorPositionAtEnd();
    }
    gComposer = c;
}

void ReviewAddComment(MainWindow* win, Point canvasPt, bool hasPt) {
    DisplayModel* dm = win ? win->AsFixed() : nullptr;
    WindowTab* tab = win ? win->CurrentTab() : nullptr;
    if (!dm || !tab) {
        return;
    }
    ReviewSession* s = GetSession(tab, true);
    if (!s) {
        return;
    }
    int page = -1;
    PointF pt{};
    RectF box{};
    // a rectangle selection (Ctrl+drag) becomes a box comment
    bool hasRectSel = win->showSelection && tab->selectionOnPage && len(*tab->selectionOnPage) > 0 &&
                      dm->textSelection && dm->textSelection->result.len == 0;
    if (hasRectSel) {
        SelectionOnPage& sel = (*tab->selectionOnPage)[0];
        page = sel.pageNo;
        box = sel.rect;
        pt = PointF{box.x, box.y};
        DeleteOldSelectionInfo(win, true);
        ScheduleRepaint(win, 0);
    } else {
        Point p = hasPt ? canvasPt : HwndGetCursorPos(win->hwndCanvas);
        page = dm->GetPageNoByPoint(p);
        if (page < 1) {
            page = dm->CurrentPageNo();
            RectF mb = s->engine->PageMediabox(page);
            pt = PointF{mb.x + mb.dx / 2, mb.y + mb.dy / 2};
        } else {
            pt = dm->CvtFromScreen(p, page);
        }
    }
    if (page < 1) {
        return;
    }
    OpenComposer(win, s, page, pt, box, Str());
}

static void EditComment(MainWindow* win, ReviewSession* s, ReviewComment* c) {
    if (!c || !str::EqI(c->userId, s->myId)) {
        ShowReviewToast(win, _TRA("You can only edit your own comments"), true);
        return;
    }
    OpenComposer(win, s, c->curPage, PointF{c->curRect.x, c->curRect.y}, RectF(), c->key);
}

static void WriteMoveEvent(MainWindow* win, ReviewSession* s, ReviewComment* c, int page, PointF pt) {
    ReviewPageWords* pw = PageWords(s, page);
    ReviewComment tmp;
    tmp.rect = RectF(pt.x, pt.y, 0, 0);
    if (pw) {
        ReviewComputeFingerprint(&tmp, pt, pw->words);
    }
    ReviewEventBuilder eb(StrL("move"), s->myId, NowTime());
    eb.Add(StrL("id"), c->key);
    eb.Add(StrL("rev"), s->pdfName);
    eb.Add(StrL("page"), page);
    eb.Add(StrL("x"), pt.x);
    eb.Add(StrL("y"), pt.y);
    eb.Add(StrL("aw"), tmp.anchorWord ? tmp.anchorWord : StrL(""));
    eb.Add(StrL("ax"), tmp.anchorDx);
    eb.Add(StrL("ay"), tmp.anchorDy);
    str::Builder nb;
    for (Str e : tmp.nearWords) {
        if (!nb.IsEmpty()) {
            nb.AppendChar('\n');
        }
        nb.Append(e);
    }
    eb.Add(StrL("near"), Str(nb.els, (int)nb.len));
    if (pw && len(pw->title) > 0) {
        eb.Add(StrL("sheet"), pw->title);
    }
    if (CommitEvent(win, s, eb)) {
        ShowReviewToast(win, fmt(_TRA("%s moved").s, s->model.DisplayIdTemp(c)));
    }
}

// --- revision compare: highlight what changed against another PDF revision ---

static int gCompareGeneration = 0;

// other PDFs in the same folder, newest first
static void OtherRevisions(ReviewSession* s, StrVec& out) {
    struct Rev {
        Str name;
        i64 time;
    };
    Vec<Rev> revs;
    StrVec names; // owns the strings
    DirIter di{s->pdfDir};
    for (DirIterEntry* de : di) {
        if (!str::EndsWithI(de->name, StrL(".pdf")) || str::EqI(de->name, s->pdfName)) {
            continue;
        }
        i64 t = 0;
        if (de->fd) {
            t = ((i64)de->fd->ftLastWriteTime.dwHighDateTime << 32) | (i64)de->fd->ftLastWriteTime.dwLowDateTime;
        }
        names.Append(de->name);
        revs.Append(Rev{Str(), t});
    }
    int n = len(revs);
    for (int i = 0; i < n; i++) {
        revs[i].name = names.At(i);
    }
    std::stable_sort(revs.begin(), revs.end(), [](const Rev& a, const Rev& b) { return a.time > b.time; });
    for (const Rev& r : revs) {
        out.Append(r.name);
    }
}

struct ReviewDiffJob {
    MainWindow* win = nullptr;
    Str pdfPath; // identifies the session
    Str otherPath;
    EngineBase* cur = nullptr;   // AddRef'd
    EngineBase* other = nullptr; // AddRef'd; opened by the job if the compare doesn't have it yet
    bool openedOther = false;
    int page = 0;
    int generation = 0;
    Vec<RectF> rects;
    bool failed = false;
    ~ReviewDiffJob() {
        str::Free(pdfPath);
        str::Free(otherPath);
        SafeEngineRelease(&other);
    }
};

static int PixelGray(const Pixmap* px, int x, int y) {
    const u8* p = px->data + (size_t)y * (size_t)px->stride + (size_t)x * (size_t)PixmapBytesPerPixel(px->format);
    int r = p[2], g = p[1], b = p[0];
    if (px->format == PixmapFormat::RGBA8) {
        r = p[0];
        b = p[2];
    }
    return (r * 3 + g * 6 + b) / 10;
}

// compares two renderings of a page on a grid of cells and returns the changed
// areas in page coordinates
static void DiffPixmaps(const Pixmap* a, const Pixmap* b, float zoom, RectF mb, Vec<RectF>& out) {
    constexpr int kCell = 8;
    if (std::abs(a->width - b->width) > 2 || std::abs(a->height - b->height) > 2) {
        out.Append(mb); // page size changed: the whole page is different
        return;
    }
    int w = std::min(a->width, b->width);
    int h = std::min(a->height, b->height);
    int gw = (w + kCell - 1) / kCell;
    int gh = (h + kCell - 1) / kCell;
    if (gw <= 0 || gh <= 0) {
        return;
    }
    u8* changed = AllocArray<u8>(gw * gh);
    int nChanged = 0;
    for (int gy = 0; gy < gh; gy++) {
        for (int gx = 0; gx < gw; gx++) {
            int diffs = 0;
            int y1 = std::min(h, (gy + 1) * kCell);
            int x1 = std::min(w, (gx + 1) * kCell);
            for (int y = gy * kCell; y < y1 && diffs < 3; y++) {
                for (int x = gx * kCell; x < x1; x++) {
                    if (std::abs(PixelGray(a, x, y) - PixelGray(b, x, y)) > 40) {
                        diffs++;
                    }
                }
            }
            if (diffs >= 3) {
                changed[gy * gw + gx] = 1;
                nChanged++;
            }
        }
    }
    if (nChanged == 0) {
        free(changed);
        return;
    }
    if (nChanged * 100 > gw * gh * 35) {
        // most of the page differs (e.g. everything shifted): flag the whole page
        free(changed);
        out.Append(mb);
        return;
    }
    // group changed cells that are close to each other (within 2 cells)
    int* label = AllocArray<int>(gw * gh);
    Vec<int> stack;
    int nextLabel = 0;
    constexpr int kReach = 2;
    for (int start = 0; start < gw * gh; start++) {
        if (!changed[start] || label[start] != 0) {
            continue;
        }
        nextLabel++;
        int minX = gw, minY = gh, maxX = -1, maxY = -1;
        label[start] = nextLabel;
        stack.Append(start);
        while (len(stack) > 0) {
            int cell = stack.Pop();
            int cx = cell % gw;
            int cy = cell / gw;
            minX = std::min(minX, cx);
            minY = std::min(minY, cy);
            maxX = std::max(maxX, cx);
            maxY = std::max(maxY, cy);
            for (int ny = std::max(0, cy - kReach); ny <= std::min(gh - 1, cy + kReach); ny++) {
                for (int nx = std::max(0, cx - kReach); nx <= std::min(gw - 1, cx + kReach); nx++) {
                    int idx = ny * gw + nx;
                    if (changed[idx] && label[idx] == 0) {
                        label[idx] = nextLabel;
                        stack.Append(idx);
                    }
                }
            }
        }
        float pad = 2.f / zoom;
        float x0 = mb.x + (float)(minX * kCell) / zoom - pad;
        float y0 = mb.y + (float)(minY * kCell) / zoom - pad;
        float x1 = mb.x + (float)std::min(w, (maxX + 1) * kCell) / zoom + pad;
        float y1 = mb.y + (float)std::min(h, (maxY + 1) * kCell) / zoom + pad;
        out.Append(RectF(x0, y0, x1 - x0, y1 - y0));
        if (len(out) >= 300) {
            break;
        }
    }
    free(label);
    free(changed);
}

static void DiffDone(ReviewDiffJob* job);

static void DiffThread(ReviewDiffJob* job) {
    if (!job->other) {
        job->other = CreateEngineMupdfFromFile(job->otherPath, kindFilePDF, 96);
        job->openedOther = job->other != nullptr;
    }
    if (!job->other) {
        job->failed = true;
    } else if (job->page > job->other->PageCount()) {
        // a new page: everything on it is new
        job->rects.Append(job->cur->PageMediabox(job->page));
    } else {
        RectF mb = job->cur->PageMediabox(job->page);
        float zoom = std::min(1.5f, 1400.f / std::max(1.f, std::max(mb.dx, mb.dy)));
        RenderPageArgs argsCur(job->page, zoom, 0, nullptr, RenderTarget::Export);
        Pixmap* a = job->cur->RenderPage(argsCur);
        RenderPageArgs argsOther(job->page, zoom, 0, nullptr, RenderTarget::Export);
        Pixmap* b = job->other->RenderPage(argsOther);
        if (a && b && a->data && b->data) {
            DiffPixmaps(a, b, zoom, mb, job->rects);
        }
        if (a) {
            FreePixmap(a);
        }
        if (b) {
            FreePixmap(b);
        }
    }
    job->cur->Release();
    job->cur = nullptr;
    uitask::Post(MkFunc0(DiffDone, job), "ReviewDiffDone");
}

static ReviewDiffPage* FindDiffPage(ReviewCompare* cmp, int page) {
    for (ReviewDiffPage* dp : cmp->pages) {
        if (dp->page == page) {
            return dp;
        }
    }
    return nullptr;
}

static void CompareTick(MainWindow* win) {
    ReviewSession* s = CurrentSession(win);
    if (!s || !s->compare || s->compare->busy) {
        return;
    }
    DisplayModel* dm = win->AsFixed();
    if (!dm) {
        return;
    }
    ReviewCompare* cmp = s->compare;
    int n = dm->PageCount();
    for (int page = 1; page <= n; page++) {
        if (!dm->PageVisible(page) || FindDiffPage(cmp, page)) {
            continue;
        }
        auto job = new ReviewDiffJob();
        job->win = win;
        job->pdfPath = str::Dup(s->pdfPath);
        job->otherPath = str::Dup(cmp->otherPath);
        job->cur = s->engine;
        job->cur->AddRef();
        if (cmp->other) {
            job->other = cmp->other;
            job->other->AddRef();
        }
        job->page = page;
        job->generation = cmp->generation;
        cmp->busy = true;
        RunAsync(MkFunc0(DiffThread, job), "ReviewDiff");
        return;
    }
}

static void DiffDone(ReviewDiffJob* job) {
    AutoDelete del(job);
    if (!IsMainWindowValid(job->win)) {
        return;
    }
    ReviewSession* s = FindSessionByPath(job->win, job->pdfPath);
    ReviewCompare* cmp = s ? s->compare : nullptr;
    if (!cmp || cmp->generation != job->generation) {
        return;
    }
    cmp->busy = false;
    if (job->failed) {
        ShowReviewToast(job->win, fmt(_TRA("Couldn't open %s").s, path::GetBaseNameTemp(job->otherPath)), true);
        delete s->compare;
        s->compare = nullptr;
        RefreshReviewUI(job->win);
        return;
    }
    if (job->openedOther && !cmp->other) {
        cmp->other = job->other; // keep it open for the next pages
        job->other = nullptr;
    }
    auto dp = new ReviewDiffPage();
    dp->page = job->page;
    for (RectF r : job->rects) {
        dp->rects.Append(r);
    }
    dp->done = true;
    cmp->pages.Append(dp);
    if (len(dp->rects) > 0) {
        ScheduleRepaint(job->win, 0);
    }
    if (s == CurrentSession(job->win)) {
        CompareTick(job->win); // next visible page
        if (!s->compare->busy) {
            PanelUpdate(job->win);
        }
    }
}

static void StartCompare(MainWindow* win, ReviewSession* s, Str otherPath) {
    delete s->compare;
    s->compare = new ReviewCompare();
    s->compare->otherPath = str::Dup(otherPath);
    s->compare->generation = ++gCompareGeneration;
    ShowReviewToast(win, fmt(_TRA("Comparing with %s: changes are highlighted in pink").s, path::GetBaseNameTemp(otherPath)),
                    false, 5000);
    CompareTick(win);
    RefreshReviewUI(win);
}

static void StopCompare(MainWindow* win, ReviewSession* s) {
    if (!s->compare) {
        return;
    }
    delete s->compare;
    s->compare = nullptr;
    RefreshReviewUI(win);
}

// name: file name in the PDF's folder (from the panel)
static void StartCompareWithName(MainWindow* win, ReviewSession* s, Str name) {
    if (len(name) == 0 || str::IndexOfChar(name, '/') >= 0 || str::IndexOfChar(name, '\\') >= 0 ||
        !str::EndsWithI(name, StrL(".pdf"))) {
        return;
    }
    TempStr p = path::JoinTemp(s->pdfDir, name);
    if (!file::Exists(p)) {
        ShowReviewToast(win, fmt(_TRA("Couldn't open %s").s, name), true);
        return;
    }
    StartCompare(win, s, p);
}

void ReviewToggleCompare(MainWindow* win) {
    ReviewSession* s = CurrentSession(win, true);
    if (!s) {
        return;
    }
    if (s->compare) {
        StopCompare(win, s);
        ShowReviewToast(win, _TRA("Compare off"));
        return;
    }
    StrVec revs;
    OtherRevisions(s, revs);
    int n = len(revs);
    if (n == 0) {
        ShowReviewToast(win, _TRA("No other PDF revision in this folder to compare with"), true, 5000);
        return;
    }
    if (n == 1) {
        StartCompareWithName(win, s, revs.At(0));
        return;
    }
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_DISABLED | MF_GRAYED, 0, L"Compare with...");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < n && i < 30; i++) {
        AppendMenuW(m, MF_STRING, (UINT_PTR)(i + 1), CWStrTemp(revs.At(i)));
    }
    POINT p;
    GetCursorPos(&p);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, win->hwndFrame, nullptr);
    DestroyMenu(m);
    if (cmd >= 1 && cmd <= n) {
        StartCompareWithName(win, s, revs.At(cmd - 1));
    }
}

static void PaintCompareOverlay(Gdiplus::Graphics& g, DisplayModel* dm, ReviewSession* s) {
    ReviewCompare* cmp = s->compare;
    float scale = (float)DpiScale(s->tab->win->hwndCanvas, 100) / 100.f;
    Gdiplus::SolidBrush fill(Gdiplus::Color(48, 0xEC, 0x48, 0x99));
    Gdiplus::Pen pen(Gdiplus::Color(190, 0xDB, 0x27, 0x77), 1.5f * scale);
    for (ReviewDiffPage* dp : cmp->pages) {
        if (!dm->PageVisible(dp->page)) {
            continue;
        }
        for (RectF r : dp->rects) {
            Rect rc = dm->CvtToScreen(dp->page, r);
            Gdiplus::RectF gr((float)rc.x, (float)rc.y, (float)rc.dx, (float)rc.dy);
            g.FillRectangle(&fill, gr);
            g.DrawRectangle(&pen, gr);
        }
    }
}


// --- review panel: state as JSON for the web app ---

struct JsonObj {
    str::Builder& b;
    bool first = true;
    explicit JsonObj(str::Builder& builder) : b(builder) {
        b.AppendChar('{');
    }
    void Key(const char* k) {
        if (!first) {
            b.AppendChar(',');
        }
        first = false;
        AppendJsonString(b, Str(k));
        b.AppendChar(':');
    }
    void S(const char* k, Str v) {
        Key(k);
        AppendJsonString(b, v);
    }
    void I(const char* k, i64 v) {
        Key(k);
        b.Append(fmt("%d", v));
    }
    void B(const char* k, bool v) {
        Key(k);
        b.Append(v ? StrL("true") : StrL("false"));
    }
    void End() {
        b.AppendChar('}');
    }
};

static void AppendStrArray(str::Builder& b, const StrVec& v) {
    b.AppendChar('[');
    int n = len(v);
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            b.AppendChar(',');
        }
        AppendJsonString(b, v.At(i));
    }
    b.AppendChar(']');
}

static SeqStrings gAnchorNames = "pending\0same\0found\0moved\0gone\0";

static TempStr ColorHexTemp(COLORREF c) {
    return fmt("#%02X%02X%02X", (int)GetRValue(c), (int)GetGValue(c), (int)GetBValue(c));
}

static TempStr PanelStateJsonTemp(MainWindow* win) {
    str::Builder b(64 * 1024);
    ReviewSession* s = CurrentSession(win);
    if (!s) {
        JsonObj o(b);
        o.B("none", true);
        o.B("hasDoc", win->AsFixed() != nullptr);
        o.End();
        return str::DupTemp(Str(b.els, (int)b.len));
    }
    ReviewModel& m = s->model;
    DisplayModel* dm = win->AsFixed();
    JsonObj o(b);
    o.Key("me");
    {
        JsonObj me(b);
        me.S("id", s->myId);
        me.S("name", s->myName);
        ReviewUser* u = m.FindUser(s->myId);
        me.S("initials", u && len(u->initials) > 0 ? u->initials : ReviewInitialsTemp(s->myName));
        me.End();
    }
    o.S("designer", m.designer);
    o.S("designerName", len(m.designer) > 0 ? m.UserName(m.designer) : Str());
    o.B("isDesigner", IsDesigner(s));
    o.I("round", m.round);
    o.Key("pdf");
    {
        JsonObj p(b);
        p.S("name", s->pdfName);
        p.I("pages", s->pageCount);
        p.I("page", dm ? dm->CurrentPageNo() : 1);
        p.B("local", s->localFallback);
        p.S("folder", s->reviewDir);
        p.B("hasReport", file::Exists(s->reportPath));
        p.End();
    }
    o.B("reviewMode", win->reviewMode);
    o.B("showResolved", s->showResolved);
    o.S("selected", s->selectedKey);
    o.S("placing", s->placingKey);
    o.Key("compare");
    {
        JsonObj cmp(b);
        cmp.B("on", s->compare != nullptr);
        cmp.S("other", s->compare ? path::GetBaseNameTemp(s->compare->otherPath) : Str());
        cmp.B("busy", s->compare && s->compare->busy);
        cmp.Key("choices");
        StrVec revs;
        OtherRevisions(s, revs);
        AppendStrArray(b, revs);
        cmp.End();
    }
    StrVec templates;
    Split(&templates, m.templates, StrL("\n"), true);
    o.Key("templates");
    AppendStrArray(b, templates);
    StrVec checklist;
    Split(&checklist, m.checklist, StrL("\n"), true);
    o.Key("checklist");
    AppendStrArray(b, checklist);

    o.Key("sheets");
    b.AppendChar('[');
    for (int i = 0; i < s->pageCount; i++) {
        if (i > 0) {
            b.AppendChar(',');
        }
        ReviewPageWords* pw = s->pages[i];
        AppendJsonString(b, pw->loaded ? pw->title : Str());
    }
    b.AppendChar(']');

    o.Key("users");
    b.AppendChar('[');
    bool firstUser = true;
    for (ReviewUser* u : m.users) {
        if (!firstUser) {
            b.AppendChar(',');
        }
        firstUser = false;
        JsonObj ju(b);
        ju.S("id", u->userId);
        ju.S("name", len(u->name) > 0 ? u->name : u->userId);
        ju.S("prefix", u->idPrefix);
        ju.B("signedOff", u->signedOff);
        ju.I("signedOffTime", u->signedOffTime);
        ju.S("note", u->signOffNote);
        ju.S("assigned", m.AssignedPages(u->userId));
        ju.I("lastSeen", u->lastSeen);
        ju.Key("pages");
        AppendStrArray(b, u->reviewedPages);
        ju.Key("checked");
        AppendStrArray(b, u->checkedItems);
        ju.End();
    }
    b.AppendChar(']');

    o.Key("comments");
    b.AppendChar('[');
    bool firstC = true;
    for (ReviewComment* c : m.comments) {
        if (c->deleted) {
            continue;
        }
        if (!firstC) {
            b.AppendChar(',');
        }
        firstC = false;
        JsonObj jc(b);
        jc.S("key", c->key);
        jc.S("id", m.DisplayIdTemp(c));
        jc.S("sev", ReviewSeverityName(c->severity));
        jc.S("status", ReviewStatusName(c->status));
        jc.S("text", c->text);
        jc.I("page", c->curPage);
        jc.I("origPage", c->page);
        jc.I("x", (i64)c->curRect.x); // reading order within a page
        jc.I("y", (i64)c->curRect.y);
        jc.S("rev", c->rev);
        jc.S("sheet", c->sheet);
        jc.S("comp", c->component);
        jc.S("clicked", c->clicked);
        jc.S("net", c->net);
        jc.S("shape", c->shape);
        jc.I("round", c->round);
        jc.S("by", c->userId);
        jc.S("byName", m.UserName(c->userId));
        jc.B("mine", str::EqI(c->userId, s->myId));
        jc.I("created", c->created);
        jc.I("updated", c->updated);
        jc.S("statusByName", len(c->statusBy) > 0 ? m.UserName(c->statusBy) : Str());
        jc.I("statusTime", c->statusTime);
        jc.S("anchor", SeqStrByIndex(gAnchorNames, (int)c->anchorState));
        bool hasSnap = false;
        if (len(c->snapshot) > 0) {
            TempStr sp = path::JoinTemp(s->reviewDir, StrL("snapshots"), c->snapshot);
            hasSnap = file::Exists(sp);
        }
        jc.S("snap", hasSnap ? str::JoinTemp(StrL("snap/"), c->snapshot) : Str());
        jc.B("agreed", c->agrees.Contains(s->myId));
        jc.Key("agrees");
        b.AppendChar('[');
        int na = len(c->agrees);
        for (int i = 0; i < na; i++) {
            if (i > 0) {
                b.AppendChar(',');
            }
            AppendJsonString(b, m.UserName(c->agrees.At(i)));
        }
        b.AppendChar(']');
        jc.Key("replies");
        b.AppendChar('[');
        bool firstR = true;
        for (ReviewReply* r : c->replies) {
            if (!firstR) {
                b.AppendChar(',');
            }
            firstR = false;
            JsonObj jr(b);
            jr.S("byName", m.UserName(r->by));
            jr.S("text", r->text);
            jr.I("time", r->time);
            jr.B("mine", str::EqI(r->by, s->myId));
            jr.End();
        }
        b.AppendChar(']');
        jc.Key("history");
        b.AppendChar('[');
        bool firstH = true;
        for (ReviewHistoryItem* h : c->history) {
            if (!firstH) {
                b.AppendChar(',');
            }
            firstH = false;
            JsonObj jh(b);
            jh.S("byName", m.UserName(h->by));
            jh.S("what", h->what);
            jh.I("time", h->time);
            jh.End();
        }
        b.AppendChar(']');
        jc.End();
    }
    b.AppendChar(']');
    o.End();
    return str::DupTemp(Str(b.els, (int)b.len));
}

static TempStr ThemeJsonTemp() {
    str::Builder b;
    JsonObj o(b);
    COLORREF bg = ThemeWindowBackgroundColor();
    o.S("bg", ColorHexTemp(bg));
    o.S("fg", ColorHexTemp(ThemeWindowTextColor()));
    o.S("link", ColorHexTemp(ThemeWindowLinkColor()));
    o.S("control", ColorHexTemp(ThemeWindowControlBackgroundColor()));
    o.B("dark", IsDarkColor(bg));
    o.Key("sev");
    b.AppendChar('[');
    for (int i = 0; i < kReviewSeverityCount; i++) {
        if (i > 0) {
            b.AppendChar(',');
        }
        AppendJsonString(b, Str(kSevHex[i]));
    }
    b.AppendChar(']');
    o.End();
    return str::DupTemp(Str(b.els, (int)b.len));
}

// --- review panel: the web view ---

// when the web panel was created, to log how long it takes to come up
static LARGE_INTEGER gPanelCreateTime{};

struct ReviewWebView : WebviewWnd {
    MainWindow* win = nullptr;
    void OnBrowserMessage(Str msg) override;
};

static void HandlePanelMessage(MainWindow* win, Str msg);

void ReviewWebView::OnBrowserMessage(Str msg) {
    if (win && IsMainWindowValid(win)) {
        HandlePanelMessage(win, msg);
    }
}

static bool GetPanelResource(void* ctx, Str path, WebViewResourceResult* res) {
    MainWindow* win = (MainWindow*)ctx;
    if (str::EqI(path, "panel.html") || len(path) == 0) {
        LoadedDataResource lr;
        if (!LockDataResource(IDR_REVIEW_PANEL_HTML, &lr)) {
            return false;
        }
        res->data = lr.data;
        res->dataLen = (size_t)lr.dataSize;
        res->contentType = str::Dup("text/html; charset=utf-8");
        res->ownsData = false;
        return true;
    }
    if (str::StartsWith(path, "snap/")) {
        Str name(path.s + 5, path.len - 5);
        if (len(name) == 0 || str::IndexOfChar(name, '/') >= 0 || str::IndexOfChar(name, '\\') >= 0 ||
            str::IndexOf(name, StrL("..")) >= 0) {
            return false;
        }
        ReviewSession* s = CurrentSession(win);
        if (!s) {
            return false;
        }
        TempStr p = path::JoinTemp(s->reviewDir, StrL("snapshots"), name);
        Str data = file::ReadFile(p);
        if (!data) {
            return false;
        }
        res->data = (const u8*)data.s;
        res->dataLen = (size_t)data.len;
        res->contentType = str::Dup("image/png");
        res->ownsData = true;
        return true;
    }
    return false;
}

static void PanelEval(MainWindow* win, Str js) {
    if (win->reviewWebView && win->reviewPanelReady) {
        win->reviewWebView->Eval(js);
    }
}

static void RebuildFallbackList(MainWindow* win);

static void PanelUpdate(MainWindow* win) {
    if (!win || !win->reviewVisible) {
        return;
    }
    if (win->reviewWebView) {
        if (win->reviewPanelReady) {
            auto t = TimeGet();
            TempStr json = PanelStateJsonTemp(win);
            PanelEval(win, fmt("window.smartpdf && smartpdf.update(%s)", json));
            double ms = TimeSinceInMs(t);
            if (ms > 20) {
                logf("review: panel update took %.0f ms (%d bytes)\n", ms, json.len);
            }
        }
        return;
    }
    RebuildFallbackList(win);
}

void ReviewOnPageChanged(MainWindow* win, int pageNo) {
    if (!win || !IsMainWindowValid(win)) {
        return;
    }
    if (win->reviewVisible && win->reviewWebView && win->reviewPanelReady) {
        PanelEval(win, fmt("window.smartpdf && smartpdf.setPage(%d)", pageNo));
    }
    ReviewSession* s = CurrentSession(win);
    if (s) {
        // new pages came into view: net markers and revision compare
        StartIdleWork(win);
        CompareTick(win);
    }
}

void ReviewOnThemeChanged(MainWindow* win) {
    if (!win) {
        return;
    }
    if (win->reviewWebView && win->reviewPanelReady) {
        PanelEval(win, fmt("window.smartpdf && smartpdf.setTheme(%s)", ThemeJsonTemp()));
    }
    if (win->hwndReviewBox) {
        InvalidateRect(win->hwndReviewBox, nullptr, TRUE);
    }
    ScheduleRepaint(win, 0);
}

void ReviewOnTabChanged(MainWindow* win) {
    if (!win || !IsMainWindowValid(win)) {
        return;
    }
    WindowTab* tab = win->CurrentTab();
    if (tab && tab->review && !tab->review->active) {
        // someone may have started a review of this PDF in the meantime
        FreeSessionPtr(tab);
    }
    ReviewSession* s = CurrentSession(win, false);
    if (s) {
        StartReviewTimers(win);
    }
    PanelUpdate(win);
}

// flat {"cmd":"...","key":"..."} messages from the panel
struct PanelMsg : json::ValueVisitor {
    StrVec keys;
    StrVec vals;
    bool Visit(Str path, Str value, json::Type) override {
        if (len(path) < 2 || path.s[0] != '/') {
            return true;
        }
        keys.Append(Str(path.s + 1, path.len - 1));
        vals.Append(len(value) > 0 ? value : StrL(""));
        return true;
    }
    Str Get(const char* k) const {
        int n = len(keys);
        for (int i = 0; i < n; i++) {
            if (str::Eq(keys.At(i), k)) {
                return vals.At(i);
            }
        }
        return {};
    }
    bool GetBool(const char* k) const {
        return str::Eq(Get(k), "true");
    }
    int GetInt(const char* k) const {
        Str v = Get(k);
        return len(v) > 0 ? atoi(CStrTemp(v)) : 0;
    }
};

void ReviewToggleMode(MainWindow* win) {
    if (!win || !win->AsFixed()) {
        return;
    }
    win->reviewMode = !win->reviewMode;
    if (win->reviewMode) {
        CurrentSession(win, true);
        ShowReviewToast(win,
                        _TRA("Review mode: C comment \xc2\xb7 1-4 severity \xc2\xb7 N / Shift+N next / previous \xc2\xb7 "
                             "D page reviewed \xc2\xb7 Enter edit \xc2\xb7 Esc exit"),
                        false, 7000);
    } else {
        ShowReviewToast(win, _TRA("Review mode off"));
    }
    PanelUpdate(win);
}

static void HandlePanelMessage(MainWindow* win, Str msg) {
    PanelMsg m;
    TempStr data = str::DupTemp(msg);
    if (!json::Parse(data, &m)) {
        return;
    }
    Str cmd = m.Get("cmd");
    if (str::Eq(cmd, "ready")) {
        logf("review: panel ready in %.0f ms\n", TimeSinceInMs(gPanelCreateTime));
        win->reviewPanelReady = true;
        PanelEval(win, fmt("window.smartpdf && smartpdf.setTheme(%s)", ThemeJsonTemp()));
        PanelEval(win, fmt("window.smartpdf && smartpdf.update(%s)", PanelStateJsonTemp(win)));
        return;
    }
    if (str::Eq(cmd, "close")) {
        if (win->reviewVisible) {
            ReviewTogglePanel(win);
        }
        return;
    }
    if (str::Eq(cmd, "addComment")) {
        // the next click on the page says where the comment goes
        ReviewSession* ns = CurrentSession(win, true);
        if (ns) {
            SetStr(&ns->placingKey, StrL(kPlaceNewComment));
            ShowReviewToast(win, _TRA("Click on the schematic where the comment goes (Esc cancels)"), false, 6000);
            PanelUpdate(win);
        }
        return;
    }
    if (str::Eq(cmd, "reviewMode")) {
        if (m.GetBool("on") != win->reviewMode) {
            ReviewToggleMode(win);
        }
        return;
    }
    if (str::Eq(cmd, "openReport")) {
        ReviewOpenReport(win);
        return;
    }
    if (str::Eq(cmd, "exportPdf")) {
        ReviewExportPdf(win);
        return;
    }
    if (str::Eq(cmd, "openFolder")) {
        ReviewSession* s = CurrentSession(win, true);
        if (s && EnsureReviewDir(win, s)) {
            LaunchFileShell(s->reviewDir);
        }
        return;
    }
    if (str::Eq(cmd, "gotoPage")) {
        int page = m.GetInt("page");
        if (win->ctrl && page >= 1 && page <= win->ctrl->PageCount()) {
            win->ctrl->GoToPage(page, true);
        }
        return;
    }
    ReviewSession* s = CurrentSession(win, true);
    if (!s) {
        return;
    }
    i64 now = NowTime();
    if (str::Eq(cmd, "select")) {
        SelectComment(win, s, m.Get("key"), true);
        return;
    }
    if (str::Eq(cmd, "showResolved")) {
        s->showResolved = m.GetBool("on");
        RefreshReviewUI(win);
        return;
    }
    if (str::Eq(cmd, "cancelPlacing")) {
        if (len(s->placingKey) > 0) {
            str::FreePtr(&s->placingKey);
            PanelUpdate(win);
        }
        return;
    }
    if (str::Eq(cmd, "move")) {
        ReviewComment* c = s->model.FindComment(m.Get("key"));
        if (c) {
            SetStr(&s->placingKey, c->key);
            ShowReviewToast(win, fmt(_TRA("Click on the page where %s should be (Esc cancels)").s, s->model.DisplayIdTemp(c)),
                            false, 6000);
            PanelUpdate(win);
        }
        return;
    }
    if (str::Eq(cmd, "compare")) {
        Str other = m.Get("path");
        if (len(other) == 0) {
            StopCompare(win, s);
        } else {
            StartCompareWithName(win, s, other);
        }
        return;
    }
    if (str::Eq(cmd, "edit")) {
        EditComment(win, s, s->model.FindComment(m.Get("key")));
        return;
    }
    ReviewComment* c = s->model.FindComment(m.Get("key"));
    if (str::Eq(cmd, "status") && c) {
        SetCommentStatus(win, s, c, ReviewStatusFromName(m.Get("status")));
    } else if (str::Eq(cmd, "severity") && c) {
        SetCommentSeverity(win, s, c, ReviewSeverityFromName(m.Get("sev")));
    } else if (str::Eq(cmd, "delete") && c) {
        if (!str::EqI(c->userId, s->myId)) {
            ShowReviewToast(win, _TRA("You can only delete your own comments"), true);
            return;
        }
        ReviewEventBuilder eb(StrL("delete"), s->myId, now);
        eb.Add(StrL("id"), c->key);
        CommitEvent(win, s, eb);
    } else if (str::Eq(cmd, "reply") && c) {
        Str text = m.Get("text");
        if (str::IsEmptyOrWhiteSpace(text)) {
            return;
        }
        ReviewEventBuilder eb(StrL("reply"), s->myId, now);
        eb.Add(StrL("id"), c->key);
        eb.Add(StrL("rid"), fmt("%s-%d", s->myId, now));
        eb.Add(StrL("text"), text);
        CommitEvent(win, s, eb);
    } else if (str::Eq(cmd, "agree") && c) {
        ReviewEventBuilder eb(StrL("agree"), s->myId, now);
        eb.Add(StrL("id"), c->key);
        eb.AddBool(StrL("on"), m.GetBool("on"));
        CommitEvent(win, s, eb);
    } else if (str::Eq(cmd, "pageReviewed")) {
        int page = m.GetInt("page");
        if (page < 1) {
            return;
        }
        ReviewEventBuilder eb(StrL("page"), s->myId, now);
        eb.Add(StrL("page"), page);
        eb.AddBool(StrL("done"), m.GetBool("done"));
        CommitEvent(win, s, eb);
    } else if (str::Eq(cmd, "check")) {
        ReviewEventBuilder eb(StrL("check"), s->myId, now);
        eb.Add(StrL("item"), m.Get("item"));
        eb.AddBool(StrL("done"), m.GetBool("done"));
        CommitEvent(win, s, eb);
    } else if (str::Eq(cmd, "signoff")) {
        ReviewEventBuilder eb(StrL("signoff"), s->myId, now);
        eb.AddBool(StrL("done"), m.GetBool("done"));
        eb.Add(StrL("note"), m.Get("note"));
        CommitEvent(win, s, eb);
    } else if (str::Eq(cmd, "config")) {
        Str key = m.Get("key");
        if (!str::Eq(key, "designer") && !str::Eq(key, "round") && !str::Eq(key, "templates") &&
            !str::Eq(key, "checklist") && !str::StartsWith(key, "assign:")) {
            return;
        }
        ReviewEventBuilder eb(StrL("config"), s->myId, now);
        eb.Add(StrL("key"), key);
        eb.Add(StrL("val"), m.Get("val"));
        CommitEvent(win, s, eb);
        if (str::Eq(key, "designer") && IsDesigner(s)) {
            s->reportDirty = true;
            ShowReviewToast(win, _TRA("You're the designer: this PC now keeps Review-Report.xlsx up to date"));
        }
    } else if (str::Eq(cmd, "profile")) {
        Str name = m.Get("name");
        if (str::IsEmptyOrWhiteSpace(name)) {
            return;
        }
        ReviewEventBuilder eb(StrL("profile"), s->myId, now);
        eb.Add(StrL("name"), name);
        Str initials = m.Get("initials");
        eb.Add(StrL("initials"), len(initials) > 0 ? initials : ReviewInitialsTemp(name));
        SetStr(&s->myName, name);
        s->profileKnown = true;
        CommitEvent(win, s, eb);
    }
}

// --- review panel: native fallback when WebView2 isn't available ---

static void DrawFallbackCard(MainWindow* win, ListBox::DrawItemEvent* ev);
static void OnFallbackSelected(MainWindow* win);

static void RebuildFallbackList(MainWindow* win) {
    if (!win->reviewListBox) {
        return;
    }
    win->reviewItems->Reset();
    ReviewSession* s = CurrentSession(win);
    auto model = new ListBoxModelStrings();
    if (s) {
        Vec<ReviewComment*> v;
        CommentsInOrder(s, v);
        for (ReviewComment* c : v) {
            win->reviewItems->Append(c->key);
            model->strings.Append(c->text);
        }
    }
    win->reviewListBox->SetModel(model);
    if (win->reviewLabelWithClose) {
        win->reviewLabelWithClose->SetLabel(fmt(_TRA("Review Comments (%d)").s, len(*win->reviewItems)));
    }
    int sel = s ? win->reviewItems->Find(s->selectedKey) : -1;
    if (sel >= 0) {
        win->reviewListBox->SetCurrentSelection(sel);
    }
    if (win->reviewDetails) {
        ReviewComment* c = s ? SelectedComment(s) : nullptr;
        if (!c) {
            win->reviewDetails->SetText(StrL(""));
        } else {
            str::Builder b;
            b.Append(fmt("%s  %s  %s\r\n", s->model.DisplayIdTemp(c), ReviewSeverityName(c->severity),
                         ReviewStatusName(c->status)));
            b.Append(fmt("%s, %s\r\n", s->model.UserName(c->userId), ReviewFormatTimeTemp(c->created)));
            b.Append(fmt("Page %d", c->curPage));
            if (len(c->sheet) > 0) {
                b.Append(fmt(" \xc2\xb7 %s", c->sheet));
            }
            if (len(c->component) > 0) {
                b.Append(fmt(" \xc2\xb7 %s", c->component));
            }
            b.Append(StrL("\r\n\r\n"));
            b.Append(c->text);
            for (ReviewReply* r : c->replies) {
                b.Append(fmt("\r\n\r\n%s: %s", s->model.UserName(r->by), r->text));
            }
            win->reviewDetails->SetText(Str(b.els, (int)b.len));
        }
    }
}

static void DrawFallbackCard(MainWindow* win, ListBox::DrawItemEvent* ev) {
    ReviewSession* s = CurrentSession(win);
    if (!s || ev->itemIndex < 0 || ev->itemIndex >= len(*win->reviewItems)) {
        return;
    }
    ReviewComment* c = s->model.FindComment(win->reviewItems->At(ev->itemIndex));
    if (!c) {
        return;
    }
    ListBox* lb = ev->listBox;
    HDC hdc = ev->hdc;
    RECT rc = ev->itemRect;
    COLORREF colBg = IsSpecialColor(lb->bgColor) ? GetSysColor(COLOR_WINDOW) : lb->bgColor;
    COLORREF colText = IsSpecialColor(lb->textColor) ? GetSysColor(COLOR_WINDOWTEXT) : lb->textColor;
    if (ev->selected) {
        colBg = AccentColor(colBg, 30);
    }
    SetBkColor(hdc, colBg);
    ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
    SetBkMode(hdc, TRANSPARENT);
    RECT stripe = rc;
    stripe.right = stripe.left + DpiScale(lb->hwnd, 4);
    COLORREF sc = c->status == ReviewStatus::Open ? kSevColor[(int)c->severity] : kResolvedColor;
    HBRUSH br = CreateSolidBrush(sc);
    FillRect(hdc, &stripe, br);
    DeleteObject(br);
    HFONT oldFont = lb->font ? SelectFont(hdc, lb->font) : nullptr;
    int pad = DpiScale(lb->hwnd, 8);
    int lineDy = (rc.bottom - rc.top) / 3;
    RECT r = rc;
    r.left += pad;
    r.right -= pad;
    uint fmtFlags = DT_SINGLELINE | DT_NOPREFIX | DT_LEFT | DT_END_ELLIPSIS | DT_VCENTER;
    TempStr l1 = fmt("%s \xc2\xb7 %s \xc2\xb7 p.%d \xc2\xb7 %s", s->model.DisplayIdTemp(c), ReviewSeverityName(c->severity),
                     c->curPage, s->model.UserName(c->userId));
    r.top = rc.top;
    r.bottom = rc.top + lineDy;
    SetTextColor(hdc, sc);
    int cch = 0;
    WCHAR* ws = CWStrTemp(l1, cch);
    DrawTextW(hdc, ws, cch, &r, fmtFlags);
    r.top = r.bottom;
    r.bottom = r.top + lineDy;
    SetTextColor(hdc, colText);
    ws = CWStrTemp(c->text, cch);
    DrawTextW(hdc, ws, cch, &r, fmtFlags);
    r.top = r.bottom;
    r.bottom = rc.bottom;
    SetTextColor(hdc, AccentColor(colText, 80));
    TempStr l3 = fmt("%s \xc2\xb7 %s \xc2\xb7 %s", ReviewStatusName(c->status), c->sheet, c->component);
    ws = CWStrTemp(l3, cch);
    DrawTextW(hdc, ws, cch, &r, fmtFlags);
    if (oldFont) {
        SelectFont(hdc, oldFont);
    }
}

static void OnFallbackSelected(MainWindow* win) {
    ReviewSession* s = CurrentSession(win);
    int idx = win->reviewListBox ? win->reviewListBox->GetCurrentSelection() : -1;
    if (!s || idx < 0 || idx >= len(*win->reviewItems)) {
        return;
    }
    SelectComment(win, s, win->reviewItems->At(idx), true);
}

static void FallbackContextMenu(MainWindow* win) {
    ReviewSession* s = CurrentSession(win);
    ReviewComment* c = SelectedComment(s);
    if (!c) {
        return;
    }
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Mark Open");
    AppendMenuW(m, MF_STRING, 2, L"Mark Fixed");
    AppendMenuW(m, MF_STRING, 3, L"Mark Rejected");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 4, L"+1");
    if (str::EqI(c->userId, s->myId)) {
        AppendMenuW(m, MF_STRING, 5, L"Edit...");
    }
    POINT p;
    GetCursorPos(&p);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD, p.x, p.y, 0, win->hwndFrame, nullptr);
    DestroyMenu(m);
    if (cmd >= 1 && cmd <= 3) {
        SetCommentStatus(win, s, c, (ReviewStatus)(cmd - 1));
    } else if (cmd == 4) {
        ReviewEventBuilder eb(StrL("agree"), s->myId, NowTime());
        eb.Add(StrL("id"), c->key);
        eb.AddBool(StrL("on"), !c->agrees.Contains(s->myId));
        CommitEvent(win, s, eb);
    } else if (cmd == 5) {
        EditComment(win, s, c);
    }
}

// --- review panel: container, layout, toggling ---

#define IDC_REVIEW_LABEL_WITH_CLOSE 1180

static void LayoutReviewBox(MainWindow* win) {
    if (!win || !win->hwndReviewBox) {
        return;
    }
    Rect rc = ClientRect(win->hwndReviewBox);
    if (rc.dx <= 0 || rc.dy <= 0) {
        return;
    }
    if (win->reviewWebView && win->reviewWebView->hwnd) {
        MoveWindow(win->reviewWebView->hwnd, 0, 0, rc.dx, rc.dy, TRUE);
        win->reviewWebView->UpdateWebviewSize();
        return;
    }
    if (!win->reviewLabelWithClose) {
        return;
    }
    int labelDy = win->reviewLabelWithClose->GetIdealSize().dy;
    int detailsDy = std::max(DpiScale(win->hwndReviewBox, 120), std::min(rc.dy / 3, DpiScale(win->hwndReviewBox, 260)));
    int listDy = std::max(0, rc.dy - labelDy - detailsDy);
    MoveWindow(win->reviewLabelWithClose->hwnd, 0, 0, rc.dx, labelDy, TRUE);
    if (win->reviewListBox) {
        MoveWindow(win->reviewListBox->hwnd, 0, labelDy, rc.dx, listDy, TRUE);
    }
    if (win->reviewDetails) {
        MoveWindow(win->reviewDetails->hwnd, 0, labelDy + listDy, rc.dx, detailsDy, TRUE);
    }
}

void ReviewRelayoutPanel(MainWindow* win) {
    if (!win || !win->hwndReviewBox || !win->reviewVisible) {
        return;
    }
    LayoutReviewBox(win);
    RedrawWindow(win->hwndReviewBox, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
    if (win->reviewSplitter && win->reviewSplitter->hwnd) {
        InvalidateRect(win->reviewSplitter->hwnd, nullptr, TRUE);
    }
}

static void OnReviewSplitterMove(Splitter::MoveEvent* ev) {
    Splitter* splitter = ev->w;
    MainWindow* win = FindMainWindowByHwnd(splitter->hwnd);
    if (!win) {
        return;
    }
    Point pcur = HwndGetCursorPos(win->hwndFrame);
    Rect rFrame = ClientRect(win->hwndFrame);
    int dx = rFrame.dx - pcur.x;
    if (dx < DpiScale(win->hwndFrame, 260) || dx > rFrame.dx * 2 / 3) {
        ev->resizeAllowed = false;
        return;
    }
    AIChatUpdateSidebarDx(win, dx, ev->finishedDragging);
    if (ev->finishedDragging) {
        win->lastLayoutState = {};
        RelayoutWindow(win);
    }
}

static LRESULT CALLBACK WndProcReviewBox(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    MainWindow* win = (MainWindow*)data;
    if (!win) {
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    LRESULT res = TryReflectMessages(hwnd, msg, wp, lp);
    if (res) {
        return res;
    }
    switch (msg) {
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wp;
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH br = CreateSolidBrush(ThemeWindowBackgroundColor());
            FillRect(hdc, &rc, br);
            DeleteObject(br);
            return TRUE;
        }
        case WM_SIZE:
            LayoutReviewBox(win);
            break;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_REVIEW_LABEL_WITH_CLOSE) {
                ReviewTogglePanel(win);
            }
            break;
        case WM_CONTEXTMENU:
            if (win->reviewListBox && (HWND)wp == win->reviewListBox->hwnd) {
                FallbackContextMenu(win);
                return 0;
            }
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void CreateFallbackControls(MainWindow* win) {
    {
        LabelWithCloseWnd::CreateArgs args;
        args.parent = win->hwndReviewBox;
        args.cmdId = IDC_REVIEW_LABEL_WITH_CLOSE;
        args.isRtl = IsUIRtl();
        args.font = GetDefaultGuiFont(true, false);
        auto label = new LabelWithCloseWnd();
        label->Create(args);
        label->SetPaddingXY(2, 2);
        label->SetLabel(_TRA("Review Comments"));
        win->reviewLabelWithClose = label;
    }
    {
        ListBox::CreateArgs args;
        args.parent = win->hwndReviewBox;
        args.font = GetDefaultGuiFont();
        auto lb = new ListBox();
        lb->onDrawItem = MkFunc1(DrawFallbackCard, win);
        lb->onSelectionChanged = MkFunc0(OnFallbackSelected, win);
        lb->Create(args);
        Size sz = HwndMeasureText(lb->hwnd, StrL("Ag"), args.font);
        SendMessageW(lb->hwnd, LB_SETITEMHEIGHT, 0, 3 * (sz.dy + DpiScale(lb->hwnd, 2)) + DpiScale(lb->hwnd, 6));
        win->reviewListBox = lb;
    }
    {
        Edit::CreateArgs args;
        args.parent = win->hwndReviewBox;
        args.isMultiLine = true;
        args.font = GetDefaultGuiFont();
        auto ed = new Edit();
        ed->Create(args);
        SendMessageW(ed->hwnd, EM_SETREADONLY, TRUE, 0);
        win->reviewDetails = ed;
    }
}

static void CreateReviewPanel(MainWindow* win) {
    if (win->hwndReviewBox) {
        return;
    }
    gPanelCreateTime = TimeGet();
    HMODULE hmod = GetModuleHandle(nullptr);
    DWORD style = WS_CHILD | WS_CLIPCHILDREN;
    win->hwndReviewBox = CreateWindowExW(0, WC_STATIC, L"", style, 0, 0, 300, 0, win->hwndFrame, nullptr, hmod, nullptr);
    if (!win->hwndReviewBox) {
        return;
    }
    win->reviewItems = new StrVec();
    {
        Splitter::CreateArgs args;
        args.parent = win->hwndFrame;
        args.type = SplitterType::Vert;
        args.isLive = false;
        win->reviewSplitter = new Splitter();
        win->reviewSplitter->onMove = MkFunc1Void(OnReviewSplitterMove);
        win->reviewSplitter->Create(args);
    }
    win->reviewBoxSubclassId = NextSubclassId();
    SetWindowSubclass(win->hwndReviewBox, WndProcReviewBox, win->reviewBoxSubclassId, (DWORD_PTR)win);

    bool useWeb = HasWebView();
    if (useWeb) {
        Rect rc = ClientRect(win->hwndReviewBox);
        auto wv = new ReviewWebView();
        wv->win = win;
        wv->dataDir = str::Dup(GetWebViewDataDirTemp());
        wv->resourceUriPrefix = wstr::Dup(WStr(L"https://smartpdf.review/"));
        wv->resourceProvider.ctx = win;
        wv->resourceProvider.getResource = GetPanelResource;
        wv->allowExternalDrop = false;
        CreateWebViewArgs a;
        a.parent = win->hwndReviewBox;
        a.pos = Rect(0, 0, std::max(rc.dx, 100), std::max(rc.dy, 100));
        wv->Create(a);
        if (wv->hwnd) {
            win->reviewWebView = wv;
            win->reviewPanelReady = false;
            wv->Navigate(str::JoinTemp(Str(kPanelHost), StrL("panel.html")));
        } else {
            delete wv;
            useWeb = false;
        }
    }
    if (!useWeb) {
        CreateFallbackControls(win);
    }
}

void ReviewTogglePanel(MainWindow* win) {
    if (!win) {
        return;
    }
    if (!win->hwndReviewBox) {
        CreateReviewPanel(win);
    }
    if (!win->hwndReviewBox) {
        return;
    }
    win->reviewVisible = !win->reviewVisible;
    if (win->reviewVisible) {
        CurrentSession(win, true);
        StartIdleWork(win); // sheet titles for the panel
        if (win->aiChatDx <= 0) {
            win->aiChatDx = DpiScale(win->hwndFrame, 380);
        }
    }
    HwndSetVisibility(win->hwndReviewBox, win->reviewVisible);
    if (win->reviewSplitter && win->reviewSplitter->hwnd) {
        HwndSetVisibility(win->reviewSplitter->hwnd, win->reviewVisible);
    }
    // the review panel shares the right-side slot with the AI chat panels
    AIChatSyncPanelsToCurrentTab(win);
    win->lastLayoutState = {};
    RelayoutWindow(win);
    if (win->reviewVisible) {
        PanelUpdate(win);
        if (win->reviewWebView) {
            win->reviewWebView->Focus();
        }
    } else {
        HwndSetFocus(win->hwndFrame);
    }
}

void ReviewDestroyPanel(MainWindow* win) {
    if (!win) {
        return;
    }
    if (gComposer && gComposer->win == win) {
        // not inside one of its messages: can be deleted right away
        ReviewComposer* c = gComposer;
        gComposer = nullptr;
        c->closing = true;
        delete c;
    }
    if (win->hwndCanvas) {
        KillTimer(win->hwndCanvas, kReviewSyncTimerId);
        KillTimer(win->hwndCanvas, kReviewIdleTimerId);
    }
    if (!win->hwndReviewBox) {
        return;
    }
    RemoveWindowSubclass(win->hwndReviewBox, WndProcReviewBox, win->reviewBoxSubclassId);
    delete win->reviewWebView;
    win->reviewWebView = nullptr;
    win->reviewPanelReady = false;
    delete win->reviewListBox; // ~ListBox deletes its model
    win->reviewListBox = nullptr;
    delete win->reviewDetails;
    win->reviewDetails = nullptr;
    delete win->reviewLabelWithClose;
    win->reviewLabelWithClose = nullptr;
    delete win->reviewSplitter;
    win->reviewSplitter = nullptr;
    delete win->reviewItems;
    win->reviewItems = nullptr;
    DestroyWindow(win->hwndReviewBox);
    win->hwndReviewBox = nullptr;
}

// --- keyboard review mode ---

bool ReviewPreTranslateKey(MSG& msg) {
    if (msg.message != WM_KEYDOWN) {
        return false;
    }
    MainWindow* win = FindMainWindowByHwnd(msg.hwnd);
    if (!win) {
        return false;
    }
    // called for every key press: only look at already loaded state
    WindowTab* tab = win->CurrentTab();
    ReviewSession* s = tab && tab->review && tab->review->active ? tab->review : nullptr;
    bool placing = s && len(s->placingKey) > 0;
    if (!win->reviewMode && !placing) {
        return false;
    }
    if (!s) {
        s = CurrentSession(win, true);
    }
    if (msg.hwnd != win->hwndFrame && msg.hwnd != win->hwndCanvas) {
        return false;
    }
    if (IsCtrlPressed() || IsAltPressed() || (gComposer && !gComposer->closing)) {
        return false;
    }
    WPARAM vk = msg.wParam;
    if (vk == VK_ESCAPE) {
        if (placing) {
            str::FreePtr(&s->placingKey);
            PanelUpdate(win);
            ShowReviewToast(win, _TRA("Move cancelled"));
            return true;
        }
        ReviewToggleMode(win);
        return true;
    }
    if (!win->reviewMode) {
        return false;
    }
    if (vk == 'C') {
        ReviewAddComment(win, Point{}, false);
        return true;
    }
    if (vk == 'N') {
        ReviewSelectNext(win, !IsShiftPressed());
        return true;
    }
    if (vk == 'D') {
        ReviewMarkPageReviewed(win);
        return true;
    }
    int sevKey = -1;
    if (vk >= '1' && vk <= '4') {
        sevKey = (int)(vk - '1');
    } else if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD4) {
        sevKey = (int)(vk - VK_NUMPAD1);
    }
    if (sevKey >= 0) {
        ReviewComment* c = SelectedComment(s);
        if (c) {
            SetCommentSeverity(win, s, c, (ReviewSeverity)sevKey);
        }
        return true;
    }
    if (vk == VK_RETURN) {
        ReviewComment* c = SelectedComment(s);
        if (c) {
            EditComment(win, s, c);
            return true;
        }
    }
    return false;
}

bool ReviewCanUse(MainWindow* win) {
    return win && win->AsFixed() != nullptr;
}

// --- export a PDF with the comments as sticky notes (for Acrobat users) ---

void ReviewExportPdf(MainWindow* win) {
    ReviewSession* s = CurrentSession(win);
    if (!s || len(s->model.comments) == 0) {
        ShowReviewToast(win, _TRA("No review comments to export"), true);
        return;
    }
    WCHAR dstFileName[MAX_PATH + 1]{};
    TempStr defName = fmt("%s-review.pdf", path::GetPathNoExtTemp(s->pdfName));
    TempWStr defNameW = ToWStrTemp(defName);
    wstr::BufSet(WStr(dstFileName, dimof(dstFileName)), defNameW);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwndFrame;
    ofn.lpstrFile = dstFileName;
    ofn.nMaxFile = dimof(dstFileName);
    ofn.lpstrFilter = L"PDF documents\0*.pdf\0\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"pdf";
    TempWStr initDir = ToWStrTemp(s->pdfDir);
    ofn.lpstrInitialDir = initDir.s;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) {
        return;
    }
    TempStr dstPath = ToUtf8Temp(WStr(dstFileName));
    if (str::EqI(dstPath, s->pdfPath)) {
        ShowReviewToast(win, _TRA("Choose a different file name than the original PDF"), true);
        return;
    }
    // work on a separate copy of the document so the open one stays untouched
    EngineBase* engine = CreateEngineMupdfFromFile(s->pdfPath, kindFilePDF, 96);
    if (!engine || !EngineSupportsAnnotations(engine)) {
        SafeEngineRelease(&engine);
        ShowReviewToast(win, _TRA("Only PDF documents can be exported with comments"), true);
        return;
    }
    int n = 0;
    for (ReviewComment* c : s->model.comments) {
        if (c->deleted || c->curPage < 1 || c->curPage > engine->PageCount()) {
            continue;
        }
        AnnotCreateArgs args{AnnotationType::Text};
        Annotation* a = EngineMupdfCreateAnnotation(engine, c->curPage, PointF{c->curRect.x, c->curRect.y}, &args);
        if (!a) {
            continue;
        }
        str::Builder text;
        text.Append(fmt("[%s] %s \xc2\xb7 %s\n", s->model.DisplayIdTemp(c), ReviewSeverityName(c->severity),
                        ReviewStatusName(c->status)));
        text.Append(c->text);
        for (ReviewReply* r : c->replies) {
            text.Append(fmt("\n\n%s: %s", s->model.UserName(r->by), r->text));
        }
        SetContents(a, Str(text.els, (int)text.len));
        SetAuthor(a, s->model.UserName(c->userId));
        COLORREF col = c->status == ReviewStatus::Open ? kSevColor[(int)c->severity] : kResolvedColor;
        SetColor(a, MkPdfColor(GetRValue(col), GetGValue(col), GetBValue(col)));
        n++;
    }
    ShowErrorCb noErr;
    bool ok = EngineMupdfSaveUpdated(engine, dstPath, noErr);
    SafeEngineRelease(&engine);
    if (ok) {
        ShowReviewToast(win, fmt(_TRA("Exported %d comments to %s").s, n, dstPath), false, 6000);
    } else {
        ShowReviewToast(win, fmt(_TRA("Couldn't save %s").s, dstPath), true, 6000);
    }
}
