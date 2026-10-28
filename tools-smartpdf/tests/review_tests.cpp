// Portable tests for the smartpdf review model (src/Review.cpp) and Excel writer.
// Built and run on Linux by tools-smartpdf/tests/run-review-tests.sh

#include "base/Base.h"
#include "base/File.h"

#include "Review.h"
#include "ReviewXlsx.h"

static int gFailed = 0;
static int gChecks = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        gChecks++;                                                      \
        if (!(cond)) {                                                  \
            gFailed++;                                                  \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

static void AddWord(Vec<ReviewWord>& words, const char* s, float x, float y, float dx = 0, float dy = 8) {
    ReviewWord w;
    w.text = str::Dup(Str(s));
    if (dx == 0) {
        dx = 5.f * (float)strlen(s);
    }
    w.rect = RectF(x, y, dx, dy);
    words.Append(w);
}

static void TestJsonRoundTrip() {
    ReviewEventBuilder eb(StrL("create"), StrL("alice"), 1000);
    eb.Add(StrL("text"), StrL("Pull-up \"R12\" missing,\nsee U5\\"));
    eb.Add(StrL("n"), 3);
    eb.Add(StrL("x"), 12.5f);
    eb.AddBool(StrL("on"), true);
    TempStr line = eb.FinishTemp();
    CHECK(str::EndsWith(line, "}\n"));
    Vec<ReviewEvent*> events;
    ReviewParseLog(line, events);
    CHECK(len(events) == 1);
    ReviewModel m;
    ReviewReplay(&m, events);
    CHECK(len(m.comments) == 1);
    if (len(m.comments) == 1) {
        ReviewComment* c = m.comments[0];
        CHECK(str::Eq(c->text, "Pull-up \"R12\" missing,\nsee U5\\"));
        CHECK(c->n == 3);
        CHECK(str::Eq(c->key, "alice#3"));
        CHECK(fabsf(c->rect.x - 12.5f) < 0.01f);
    }
    ReviewDeleteEvents(events);
}

static TempStr Ev(const char* type, const char* by, i64 ts, std::initializer_list<const char*> kv) {
    ReviewEventBuilder eb(Str(type), Str(by), ts);
    auto it = kv.begin();
    while (it != kv.end()) {
        const char* k = *it++;
        const char* v = *it++;
        eb.Add(Str(k), Str(v));
    }
    return eb.FinishTemp();
}

static void TestReplay() {
    str::Builder alice;
    alice.Append(Ev("profile", "alice", 1, {"name", "Alice Andrews", "initials", "AA"}));
    alice.Append(Ev("create", "alice", 10, {"n", "1", "sev", "Critical", "text", "first", "page", "2", "rev", "a.pdf"}));
    alice.Append(Ev("create", "alice", 20, {"n", "2", "sev", "Minor", "text", "second", "page", "1"}));
    alice.Append(Ev("edit", "alice", 30, {"id", "alice#2", "sev", "Major"}));
    alice.Append(Ev("page", "alice", 31, {"page", "2", "done", "true"}));
    alice.Append(Ev("check", "alice", 32, {"item", "Decoupling", "done", "true"}));
    alice.Append(Ev("signoff", "alice", 33, {"done", "true", "note", "all good"}));
    alice.Append("{this line is broken\n"); // partially synced line
    str::Builder bob;
    bob.Append(Ev("profile", "bob", 2, {"name", "Amr Adel"})); // initials collide: AA
    bob.Append(Ev("status", "bob", 40, {"id", "alice#1", "status", "Fixed"}));
    bob.Append(Ev("reply", "bob", 41, {"id", "alice#1", "rid", "bob-r1", "text", "done in rev B @alice"}));
    bob.Append(Ev("agree", "bob", 42, {"id", "alice#2", "on", "true"}));
    bob.Append(Ev("create", "bob", 43, {"n", "1", "sev", "Comment", "text", "bob's", "page", "3"}));
    bob.Append(Ev("delete", "bob", 44, {"id", "bob#1"}));
    bob.Append(Ev("config", "bob", 45, {"key", "designer", "val", "bob"}));
    bob.Append(Ev("config", "bob", 46, {"key", "round", "val", "2"}));
    bob.Append(Ev("config", "bob", 47, {"key", "assign:alice", "val", "1-20, 33"}));
    // reply timestamped before the create it answers (clock skew) must still apply
    bob.Append(Ev("reply", "bob", 5, {"id", "alice#2", "rid", "bob-r0", "text", "early"}));

    Vec<ReviewEvent*> events;
    ReviewParseLog(Str(alice.els, (int)alice.len), events);
    ReviewParseLog(Str(bob.els, (int)bob.len), events);
    ReviewModel m;
    ReviewReplay(&m, events);

    CHECK(len(m.comments) == 3);
    ReviewComment* c1 = m.FindComment(StrL("alice#1"));
    ReviewComment* c2 = m.FindComment(StrL("alice#2"));
    ReviewComment* b1 = m.FindComment(StrL("bob#1"));
    CHECK(c1 && c2 && b1);
    if (c1 && c2 && b1) {
        CHECK(c1->status == ReviewStatus::Fixed);
        CHECK(str::Eq(c1->statusBy, "bob"));
        CHECK(len(c1->replies) == 1);
        CHECK(len(c1->history) == 1);
        CHECK(c2->severity == ReviewSeverity::Major);
        CHECK(len(c2->agrees) == 1);
        CHECK(len(c2->replies) == 1);
        CHECK(b1->deleted);
        CHECK(str::Eq(m.DisplayIdTemp(c1), "AA-001"));
        CHECK(str::Eq(m.DisplayIdTemp(b1), "AA2-001"));
    }
    CHECK(str::Eq(m.designer, "bob"));
    CHECK(m.round == 2);
    CHECK(str::Eq(m.AssignedPages(StrL("alice")), "1-20, 33"));
    ReviewUser* ua = m.FindUser(StrL("alice"));
    CHECK(ua && ua->signedOff && len(ua->reviewedPages) == 1 && len(ua->checkedItems) == 1);
    CHECK(ReviewNextNumber(&m, StrL("alice")) == 3);
    CHECK(ReviewNextNumber(&m, StrL("carol")) == 1);
    CHECK(m.OpenCount(ReviewSeverity::Major) == 1);
    CHECK(m.OpenCount(ReviewSeverity::Critical) == 0);
    ReviewDeleteEvents(events);
}

static void TestHelpers() {
    CHECK(str::Eq(ReviewInitialsTemp(StrL("Ahmed Al Saadi")), "AAS"));
    CHECK(str::Eq(ReviewInitialsTemp(StrL("ahmed")), "AH"));
    CHECK(str::Eq(ReviewInitialsTemp(StrL("a.alsaadi")), "AA"));
    CHECK(str::Eq(ReviewSanitizeUserIdTemp(StrL("Ahmed Al/Saadi")), "ahmed_al_saadi"));
    CHECK(ReviewPageInRanges(StrL("1-20, 33"), 1));
    CHECK(ReviewPageInRanges(StrL("1-20, 33"), 20));
    CHECK(ReviewPageInRanges(StrL("1-20, 33"), 33));
    CHECK(!ReviewPageInRanges(StrL("1-20, 33"), 21));
    CHECK(ReviewPageInRanges(StrL("5 - 7"), 6));
    CHECK(ReviewIsDesignator(StrL("R12")));
    CHECK(ReviewIsDesignator(StrL("U5A")));
    CHECK(ReviewIsDesignator(StrL("TP103")));
    CHECK(!ReviewIsDesignator(StrL("GND")));
    CHECK(!ReviewIsDesignator(StrL("VREG_S4A_1P8")));
    CHECK(ReviewSeverityFromName(StrL("minor")) == ReviewSeverity::Minor);
    CHECK(ReviewStatusFromName(StrL("Rejected")) == ReviewStatus::Rejected);
}

static void TestAnchoring() {
    Vec<ReviewWord> v1;
    AddWord(v1, "U12", 100, 100);
    AddWord(v1, "VDD_3V3", 140, 100);
    AddWord(v1, "C45", 100, 130);
    AddWord(v1, "GND", 100, 160);
    AddWord(v1, "U13", 400, 400);
    ReviewComment c;
    c.rect = RectF(110, 95, 0, 0);
    PointF pt{110, 95};
    ReviewComputeFingerprint(&c, pt, v1);
    CHECK(str::Eq(c.anchorWord, "U12"));
    CHECK(len(c.nearWords) >= 2);

    // rev B: everything shifted by (+30, +12), plus an unrelated U12 decoy far away
    Vec<ReviewWord> v2;
    AddWord(v2, "U12", 130, 112);
    AddWord(v2, "VDD_3V3", 170, 112);
    AddWord(v2, "C45", 130, 142);
    AddWord(v2, "GND", 130, 172);
    PointF newPt{};
    bool good = false;
    bool found = ReviewLocateOnPage(&c, v2, &newPt, &good);
    CHECK(found && good);
    CHECK(fabsf(newPt.x - 140) < 0.5f && fabsf(newPt.y - 107) < 0.5f);

    // component removed from the page -> not found
    Vec<ReviewWord> v3;
    AddWord(v3, "VDD_3V3", 170, 112);
    found = ReviewLocateOnPage(&c, v3, &newPt, &good);
    CHECK(!found);

    // words near point
    TempStr des = {};
    TempStr clicked = {};
    ReviewWordsNearPoint(v1, PointF{142, 103}, &des, &clicked);
    CHECK(str::Eq(clicked, "VDD_3V3"));
    CHECK(str::Eq(des, "U12"));

    // comment in an empty area: anchored by voting of nearby words
    ReviewComment c2;
    c2.rect = RectF(200, 200, 0, 0);
    Vec<ReviewWord> v4;
    AddWord(v4, "NET_A", 180, 180);
    AddWord(v4, "NET_B", 220, 230);
    AddWord(v4, "NET_C", 160, 220);
    ReviewComputeFingerprint(&c2, PointF{200, 200}, v4);
    Vec<ReviewWord> v5;
    AddWord(v5, "NET_A", 190, 185);
    AddWord(v5, "NET_B", 230, 235);
    AddWord(v5, "NET_C", 170, 225);
    // NET_* aren't designators but are within the word radius: one becomes the anchor
    found = ReviewLocateOnPage(&c2, v5, &newPt, &good);
    CHECK(found);
    CHECK(fabsf(newPt.x - 210) < 0.5f && fabsf(newPt.y - 205) < 0.5f);

    ReviewFreeWords(v1);
    ReviewFreeWords(v2);
    ReviewFreeWords(v3);
    ReviewFreeWords(v4);
    ReviewFreeWords(v5);
}

static void TestSheetTitle() {
    Vec<ReviewWord> w;
    RectF mb(0, 0, 1584, 1224);
    AddWord(w, "THIS", 1000, 1150, 20, 6);
    AddWord(w, "DOCUMENT", 1022, 1150, 40, 6);
    AddWord(w, "CONTAINS", 1064, 1150, 40, 6);
    AddWord(w, "PROPRIETARY", 1106, 1150, 50, 6);
    AddWord(w, "INFORMATION", 1160, 1150, 50, 6);
    AddWord(w, "POWER", 1200, 1100, 40, 12);
    AddWord(w, "SUPPLY", 1245, 1100, 45, 12);
    AddWord(w, "U12", 300, 300);
    TempStr t = ReviewSheetTitleTemp(w, mb);
    CHECK(str::Eq(t, "POWER SUPPLY"));
    AddWord(w, "TITLE:", 1000, 1080, 30, 8);
    AddWord(w, "AUDIO", 1040, 1080, 30, 8);
    AddWord(w, "CODEC", 1075, 1080, 30, 8);
    t = ReviewSheetTitleTemp(w, mb);
    CHECK(str::Eq(t, "AUDIO CODEC"));
    ReviewFreeWords(w);
}

static void TestWordsFromText() {
    Str text = StrL("R12 (VDD),\nGND");
    int n = text.len;
    Rect* coords = new Rect[n];
    for (int i = 0; i < n; i++) {
        coords[i] = Rect(i * 10, 0, 8, 10);
    }
    Vec<ReviewWord> words;
    ReviewWordsFromPageText(text, coords, n, words);
    CHECK(len(words) == 3);
    if (len(words) == 3) {
        CHECK(str::Eq(words[0].text, "R12"));
        CHECK(str::Eq(words[1].text, "VDD"));
        CHECK(str::Eq(words[2].text, "GND"));
        CHECK(words[2].rect.x == 110);
    }
    ReviewFreeWords(words);
    delete[] coords;
}

static void TestCsv() {
    Vec<StrVec*> rows;
    ReviewParseCsv(StrL("a,b,c\r\n1,\"x, \"\"y\"\"\nz\",\r\n"), rows);
    CHECK(len(rows) == 2);
    if (len(rows) == 2) {
        CHECK(len(*rows[1]) == 3);
        CHECK(str::Eq(rows[1]->At(1), "x, \"y\"\nz"));
        CHECK(str::Eq(rows[1]->At(2), ""));
    }
    DeleteVecMembers(rows);
}

void ReviewXlsxTests(Str outDir); // in this file, below

int main(int argc, char** argv) {
    TestJsonRoundTrip();
    TestReplay();
    TestHelpers();
    TestAnchoring();
    TestSheetTitle();
    TestWordsFromText();
    TestCsv();
    Str outDir = argc > 1 ? Str(argv[1]) : StrL(".");
    ReviewXlsxTests(outDir);
    printf("review tests: %d checks, %d failed\n", gChecks, gFailed);
    return gFailed == 0 ? 0 : 1;
}

// --- xlsx ---

void ReviewXlsxTests(Str outDir) {
    str::Builder log;
    log.Append(Ev("profile", "alice", 1, {"name", "Alice Andrews", "initials", "AA"}));
    log.Append(Ev("profile", "bob", 1, {"name", "Bob Brown"}));
    log.Append(Ev("create", "alice", 10,
                  {"n", "1", "sev", "Critical", "text", "Missing pull-up on SDA, see \"I2C\" spec & <datasheet>", "page", "2",
                   "rev", "board_revA.pdf", "comp", "R12", "sheet", "POWER SUPPLY", "round", "1"}));
    log.Append(Ev("create", "alice", 20,
                  {"n", "2", "sev", "Minor", "text", "Label typo", "page", "5", "rev", "board_revA.pdf", "sheet", "AUDIO"}));
    log.Append(Ev("create", "bob", 25,
                  {"n", "1", "sev", "Comment", "text", "Consider TVS diode", "page", "5", "rev", "board_revB.pdf",
                   "comp", "J3", "round", "2"}));
    log.Append(Ev("status", "bob", 40, {"id", "alice#2", "status", "Fixed"}));
    log.Append(Ev("reply", "bob", 41, {"id", "alice#1", "rid", "r1", "text", "Will add 4.7k"}));
    log.Append(Ev("agree", "bob", 42, {"id", "alice#1", "on", "true"}));
    log.Append(Ev("page", "alice", 43, {"page", "2", "done", "true"}));
    log.Append(Ev("signoff", "alice", 44, {"done", "true"}));
    log.Append(Ev("config", "bob", 45, {"key", "checklist", "val", "Decoupling caps\nPull-ups\nESD"}));
    log.Append(Ev("check", "alice", 46, {"item", "Pull-ups", "done", "true"}));
    log.Append(Ev("config", "bob", 47, {"key", "assign:alice", "val", "1-3"}));
    Vec<ReviewEvent*> events;
    ReviewParseLog(Str(log.els, (int)log.len), events);
    ReviewModel m;
    ReviewReplay(&m, events);
    ReviewDeleteEvents(events);

    ReviewXlsxArgs args;
    args.model = &m;
    args.title = StrL("board_revB.pdf");
    args.currentRev = StrL("board_revB.pdf");
    args.pageCount = 6;
    args.generatedTime = 1730000000;
    TempStr path = path::JoinTemp(outDir, StrL("Review-Report.xlsx"));
    Str data = ReviewWriteXlsx(&args);
    CHECK(len(data) > 1000);
    CHECK(file::WriteFile(path, data));
    str::Free(data);
    printf("wrote %s\n", path.s);
}

// logging is not needed by the tests
void log(Str s) {
    (void)s;
}
