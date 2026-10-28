/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// smartpdf review model: see Review.h. Portable (also built by the Linux tests).

#include "base/Base.h"
#include "base/JsonParser.h"

#include "Review.h"

static SeqStrings gSeverityNames = "Critical\0Major\0Minor\0Comment\0";
static SeqStrings gStatusNames = "Open\0Fixed\0Rejected\0";

Str ReviewSeverityName(ReviewSeverity sev) {
    return SeqStrByIndex(gSeverityNames, (int)sev);
}

Str ReviewStatusName(ReviewStatus st) {
    return SeqStrByIndex(gStatusNames, (int)st);
}

ReviewSeverity ReviewSeverityFromName(Str s) {
    int idx = SeqStrIndexIS(gSeverityNames, s);
    if (idx < 0) {
        return ReviewSeverity::Major;
    }
    return (ReviewSeverity)idx;
}

ReviewStatus ReviewStatusFromName(Str s) {
    int idx = SeqStrIndexIS(gStatusNames, s);
    if (idx < 0) {
        return ReviewStatus::Open;
    }
    return (ReviewStatus)idx;
}

ReviewReply::~ReviewReply() {
    str::Free(id);
    str::Free(by);
    str::Free(text);
}

ReviewHistoryItem::~ReviewHistoryItem() {
    str::Free(by);
    str::Free(what);
}

ReviewComment::~ReviewComment() {
    str::Free(key);
    str::Free(userId);
    str::Free(text);
    str::Free(rev);
    str::Free(shape);
    str::Free(component);
    str::Free(clicked);
    str::Free(sheet);
    str::Free(net);
    str::Free(statusBy);
    str::Free(snapshot);
    str::Free(anchorWord);
    DeleteVecMembers(replies);
    DeleteVecMembers(history);
}

PointF ReviewComment::Point() const {
    return PointF{rect.x, rect.y};
}

bool ReviewComment::IsBox() const {
    return rect.dx > 0 && rect.dy > 0;
}

ReviewUser::~ReviewUser() {
    str::Free(userId);
    str::Free(name);
    str::Free(initials);
    str::Free(idPrefix);
    str::Free(signOffNote);
}

static void ReplaceStr(Str* dst, Str src) {
    str::Free(*dst);
    *dst = len(src) > 0 ? str::Dup(src) : Str{};
}

ReviewModel::~ReviewModel() {
    Reset();
}

void ReviewModel::Reset() {
    DeleteVecMembers(comments);
    DeleteVecMembers(users);
    str::FreePtr(&designer);
    str::FreePtr(&templates);
    str::FreePtr(&checklist);
    round = 1;
    assignUsers.Reset();
    assignPages.Reset();
}

ReviewComment* ReviewModel::FindComment(Str key) const {
    for (ReviewComment* c : comments) {
        if (str::Eq(c->key, key)) {
            return c;
        }
    }
    return nullptr;
}

ReviewUser* ReviewModel::FindUser(Str userId) const {
    for (ReviewUser* u : users) {
        if (str::EqI(u->userId, userId)) {
            return u;
        }
    }
    return nullptr;
}

ReviewUser* ReviewModel::GetOrCreateUser(Str userId) {
    ReviewUser* u = FindUser(userId);
    if (!u) {
        u = new ReviewUser();
        u->userId = str::Dup(userId);
        users.Append(u);
    }
    return u;
}

Str ReviewModel::UserName(Str userId) const {
    ReviewUser* u = FindUser(userId);
    if (u && len(u->name) > 0) {
        return u->name;
    }
    return userId;
}

TempStr ReviewModel::DisplayIdTemp(const ReviewComment* c) const {
    ReviewUser* u = FindUser(c->userId);
    Str prefix = (u && len(u->idPrefix) > 0) ? u->idPrefix : Str("R");
    return fmt("%s-%03d", prefix, c->n);
}

Str ReviewModel::AssignedPages(Str userId) const {
    int n = len(assignUsers);
    for (int i = 0; i < n; i++) {
        if (str::EqI(assignUsers.At(i), userId)) {
            return assignPages.At(i);
        }
    }
    return {};
}

int ReviewModel::OpenCount(ReviewSeverity sev) const {
    int n = 0;
    for (ReviewComment* c : comments) {
        if (!c->deleted && c->status == ReviewStatus::Open && c->severity == sev) {
            n++;
        }
    }
    return n;
}

// --- JSON writing ---

void AppendJsonString(str::Builder& b, Str s) {
    b.AppendChar('"');
    for (int i = 0; i < s.len; i++) {
        u8 c = (u8)s.s[i];
        switch (c) {
            case '"':
                b.Append(StrL("\\\""));
                break;
            case '\\':
                b.Append(StrL("\\\\"));
                break;
            case '\n':
                b.Append(StrL("\\n"));
                break;
            case '\r':
                b.Append(StrL("\\r"));
                break;
            case '\t':
                b.Append(StrL("\\t"));
                break;
            default:
                if (c < 0x20) {
                    b.Append(fmt("\\u%04x", (int)c));
                } else {
                    b.AppendChar((char)c);
                }
        }
    }
    b.AppendChar('"');
}

ReviewEventBuilder::ReviewEventBuilder(Str type, Str by, i64 time) {
    b.AppendChar('{');
    Add(StrL("t"), type);
    Add(StrL("by"), by);
    b.Append(fmt(",\"ts\":%d", (i64)time));
}

void ReviewEventBuilder::Add(Str key, Str val) {
    if (!first) {
        b.AppendChar(',');
    }
    first = false;
    AppendJsonString(b, key);
    b.AppendChar(':');
    AppendJsonString(b, val);
}

void ReviewEventBuilder::Add(Str key, int val) {
    b.AppendChar(',');
    AppendJsonString(b, key);
    b.Append(fmt(":%d", val));
}

void ReviewEventBuilder::Add(Str key, float val) {
    b.AppendChar(',');
    AppendJsonString(b, key);
    b.Append(fmt(":%.2f", val));
}

void ReviewEventBuilder::AddBool(Str key, bool val) {
    b.AppendChar(',');
    AppendJsonString(b, key);
    b.Append(val ? StrL(":true") : StrL(":false"));
}

TempStr ReviewEventBuilder::FinishTemp() {
    b.Append(StrL("}\n"));
    return str::DupTemp(Str(b.els, (int)b.len));
}

// --- events ---

struct ReviewEvent {
    StrVec keys;
    StrVec vals;
    i64 ts = 0;
    int seq = 0;

    Str Get(Str key) const {
        int n = len(keys);
        for (int i = 0; i < n; i++) {
            if (str::Eq(keys.At(i), key)) {
                return vals.At(i);
            }
        }
        return {};
    }
    bool Has(Str key) const {
        int n = len(keys);
        for (int i = 0; i < n; i++) {
            if (str::Eq(keys.At(i), key)) {
                return true;
            }
        }
        return false;
    }
    int GetInt(Str key, int def = 0) const {
        Str v = Get(key);
        if (len(v) == 0) {
            return def;
        }
        return atoi(CStrTemp(v));
    }
    float GetFloat(Str key) const {
        Str v = Get(key);
        if (len(v) == 0) {
            return 0;
        }
        return (float)atof(CStrTemp(v));
    }
    bool GetBool(Str key) const {
        Str v = Get(key);
        return str::Eq(v, "true") || str::Eq(v, "1");
    }
};

struct EventVisitor : json::ValueVisitor {
    ReviewEvent* ev = nullptr;
    bool Visit(Str path, Str value, json::Type) override {
        // only flat objects: "/key"
        if (len(path) < 2 || path.s[0] != '/') {
            return true;
        }
        Str key(path.s + 1, path.len - 1);
        if (str::IndexOfChar(key, '/') >= 0 || str::IndexOfChar(key, '[') >= 0) {
            return true;
        }
        ev->keys.Append(key);
        ev->vals.Append(len(value) > 0 ? value : StrL(""));
        return true;
    }
};

void ReviewParseLog(Str data, Vec<ReviewEvent*>& events) {
    int start = 0;
    int n = data.len;
    for (int i = 0; i <= n; i++) {
        if (i < n && data.s[i] != '\n') {
            continue;
        }
        Str line(data.s + start, i - start);
        start = i + 1;
        // trim
        while (line.len > 0 && (line.s[line.len - 1] == '\r' || line.s[line.len - 1] == ' ')) {
            line.len--;
        }
        if (line.len < 2 || line.s[0] != '{' || line.s[line.len - 1] != '}') {
            continue; // empty or partially synced line
        }
        auto ev = new ReviewEvent();
        EventVisitor v;
        v.ev = ev;
        TempStr lineZ = str::DupTemp(line);
        bool ok = json::Parse(lineZ, &v);
        Str t = ev->Get(StrL("t"));
        Str by = ev->Get(StrL("by"));
        if (!ok || len(t) == 0 || len(by) == 0) {
            delete ev;
            continue;
        }
        Str tsStr = ev->Get(StrL("ts"));
        ev->ts = len(tsStr) > 0 ? (i64)atoll(CStrTemp(tsStr)) : 0;
        ev->seq = len(events);
        events.Append(ev);
    }
}

void ReviewDeleteEvents(Vec<ReviewEvent*>& events) {
    DeleteVecMembers(events);
}

static void AddHistory(ReviewComment* c, Str by, Str what, i64 time) {
    auto h = new ReviewHistoryItem();
    h->by = str::Dup(by);
    h->what = str::Dup(what);
    h->time = time;
    c->history.Append(h);
}

static void SetFromList(StrVec& v, Str item, bool on) {
    int idx = v.Find(item);
    if (on && idx < 0) {
        v.Append(item);
    } else if (!on && idx >= 0) {
        v.RemoveAt(idx);
    }
}

static void ApplyEvent(ReviewModel* m, ReviewEvent* ev) {
    Str t = ev->Get(StrL("t"));
    Str by = ev->Get(StrL("by"));
    ReviewUser* user = m->GetOrCreateUser(by);
    if (ev->ts > user->lastSeen) {
        user->lastSeen = ev->ts;
    }

    if (str::Eq(t, "profile")) {
        ReplaceStr(&user->name, ev->Get(StrL("name")));
        ReplaceStr(&user->initials, ev->Get(StrL("initials")));
        return;
    }
    if (str::Eq(t, "create")) {
        int n = ev->GetInt(StrL("n"));
        TempStr key = fmt("%s#%d", by, n);
        if (n <= 0 || m->FindComment(key)) {
            return;
        }
        auto c = new ReviewComment();
        c->key = str::Dup(key);
        c->userId = str::Dup(by);
        c->n = n;
        c->severity = ReviewSeverityFromName(ev->Get(StrL("sev")));
        ReplaceStr(&c->text, ev->Get(StrL("text")));
        ReplaceStr(&c->rev, ev->Get(StrL("rev")));
        c->page = ev->GetInt(StrL("page"), 1);
        c->rect = RectF(ev->GetFloat(StrL("x")), ev->GetFloat(StrL("y")), ev->GetFloat(StrL("w")),
                        ev->GetFloat(StrL("h")));
        ReplaceStr(&c->shape, ev->Get(StrL("shape")));
        ReplaceStr(&c->component, ev->Get(StrL("comp")));
        ReplaceStr(&c->clicked, ev->Get(StrL("clicked")));
        ReplaceStr(&c->sheet, ev->Get(StrL("sheet")));
        ReplaceStr(&c->net, ev->Get(StrL("net")));
        ReplaceStr(&c->snapshot, ev->Get(StrL("snap")));
        c->round = ev->GetInt(StrL("round"), 1);
        ReplaceStr(&c->anchorWord, ev->Get(StrL("aw")));
        c->anchorDx = ev->GetFloat(StrL("ax"));
        c->anchorDy = ev->GetFloat(StrL("ay"));
        Str nearStr = ev->Get(StrL("near"));
        if (len(nearStr) > 0) {
            Split(&c->nearWords, nearStr, StrL("\n"), true);
        }
        c->created = ev->ts;
        c->updated = ev->ts;
        m->comments.Append(c);
        return;
    }
    if (str::Eq(t, "config")) {
        Str key = ev->Get(StrL("key"));
        Str val = ev->Get(StrL("val"));
        if (str::Eq(key, "designer")) {
            ReplaceStr(&m->designer, val);
        } else if (str::Eq(key, "round")) {
            int r = atoi(CStrTemp(val));
            m->round = r > 0 ? r : 1;
        } else if (str::Eq(key, "templates")) {
            ReplaceStr(&m->templates, val);
        } else if (str::Eq(key, "checklist")) {
            ReplaceStr(&m->checklist, val);
        } else if (str::StartsWith(key, "assign:")) {
            Str uid(key.s + 7, key.len - 7);
            int idx = m->assignUsers.FindI(uid);
            if (idx >= 0) {
                m->assignUsers.RemoveAt(idx);
                m->assignPages.RemoveAt(idx);
            }
            if (len(val) > 0) {
                m->assignUsers.Append(uid);
                m->assignPages.Append(val);
            }
        }
        return;
    }
    if (str::Eq(t, "page")) {
        TempStr pg = fmt("%d", ev->GetInt(StrL("page")));
        SetFromList(user->reviewedPages, pg, ev->GetBool(StrL("done")));
        return;
    }
    if (str::Eq(t, "check")) {
        Str item = ev->Get(StrL("item"));
        if (len(item) > 0) {
            SetFromList(user->checkedItems, item, ev->GetBool(StrL("done")));
        }
        return;
    }
    if (str::Eq(t, "signoff")) {
        user->signedOff = ev->GetBool(StrL("done"));
        user->signedOffTime = ev->ts;
        ReplaceStr(&user->signOffNote, ev->Get(StrL("note")));
        return;
    }

    // remaining events refer to a comment
    ReviewComment* c = m->FindComment(ev->Get(StrL("id")));
    if (!c) {
        return;
    }
    if (str::Eq(t, "edit")) {
        if (ev->Has(StrL("text"))) {
            ReplaceStr(&c->text, ev->Get(StrL("text")));
            AddHistory(c, by, StrL("edited text"), ev->ts);
        }
        if (ev->Has(StrL("sev"))) {
            c->severity = ReviewSeverityFromName(ev->Get(StrL("sev")));
            AddHistory(c, by, str::JoinTemp(StrL("severity: "), ReviewSeverityName(c->severity)), ev->ts);
        }
        c->updated = ev->ts;
    } else if (str::Eq(t, "delete")) {
        c->deleted = true;
        c->updated = ev->ts;
    } else if (str::Eq(t, "status")) {
        c->status = ReviewStatusFromName(ev->Get(StrL("status")));
        ReplaceStr(&c->statusBy, by);
        c->statusTime = ev->ts;
        c->updated = ev->ts;
        AddHistory(c, by, str::JoinTemp(StrL("status: "), ReviewStatusName(c->status)), ev->ts);
    } else if (str::Eq(t, "reply")) {
        auto r = new ReviewReply();
        r->id = str::Dup(ev->Get(StrL("rid")));
        r->by = str::Dup(by);
        r->text = str::Dup(ev->Get(StrL("text")));
        r->time = ev->ts;
        c->replies.Append(r);
        c->updated = ev->ts;
    } else if (str::Eq(t, "agree")) {
        SetFromList(c->agrees, by, ev->GetBool(StrL("on")));
    } else if (str::Eq(t, "move")) {
        // manual re-anchoring on another revision
        ReplaceStr(&c->rev, ev->Get(StrL("rev")));
        c->page = ev->GetInt(StrL("page"), c->page);
        c->rect.x = ev->GetFloat(StrL("x"));
        c->rect.y = ev->GetFloat(StrL("y"));
        if (ev->Has(StrL("aw"))) {
            ReplaceStr(&c->anchorWord, ev->Get(StrL("aw")));
            c->anchorDx = ev->GetFloat(StrL("ax"));
            c->anchorDy = ev->GetFloat(StrL("ay"));
            c->nearWords.Reset();
            Str nearStr = ev->Get(StrL("near"));
            if (len(nearStr) > 0) {
                Split(&c->nearWords, nearStr, StrL("\n"), true);
            }
        }
        if (ev->Has(StrL("sheet"))) {
            ReplaceStr(&c->sheet, ev->Get(StrL("sheet")));
        }
        AddHistory(c, by, StrL("moved"), ev->ts);
        c->updated = ev->ts;
    }
}

static void ComputeIdPrefixes(ReviewModel* m) {
    // sort users so that prefix disambiguation is stable everywhere
    Vec<ReviewUser*> sorted;
    for (ReviewUser* u : m->users) {
        sorted.Append(u);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](ReviewUser* a, ReviewUser* b) { return strcmp(CStrTemp(a->userId), CStrTemp(b->userId)) < 0; });
    StrVec used;
    for (ReviewUser* u : sorted) {
        TempStr base = len(u->initials) > 0 ? str::DupTemp(u->initials)
                                            : ReviewInitialsTemp(len(u->name) > 0 ? u->name : u->userId);
        str::ToUpperInPlace(base);
        TempStr prefix = base;
        for (int i = 2; used.Contains(prefix); i++) {
            prefix = fmt("%s%d", base, i);
        }
        used.Append(prefix);
        ReplaceStr(&u->idPrefix, prefix);
    }
}

void ReviewReplay(ReviewModel* model, Vec<ReviewEvent*>& events) {
    model->Reset();
    std::stable_sort(events.begin(), events.end(), [](ReviewEvent* a, ReviewEvent* b) {
        if (a->ts != b->ts) {
            return a->ts < b->ts;
        }
        return a->seq < b->seq;
    });
    // creates must be applied before anything referring to them; with clock skew
    // a reply could be timestamped before the create it answers, so do creates first
    for (ReviewEvent* ev : events) {
        if (str::Eq(ev->Get(StrL("t")), "create") || str::Eq(ev->Get(StrL("t")), "profile")) {
            ApplyEvent(model, ev);
        }
    }
    for (ReviewEvent* ev : events) {
        if (!str::Eq(ev->Get(StrL("t")), "create") && !str::Eq(ev->Get(StrL("t")), "profile")) {
            ApplyEvent(model, ev);
        }
    }
    ComputeIdPrefixes(model);
}

int ReviewNextNumber(const ReviewModel* model, Str userId) {
    int maxN = 0;
    for (ReviewComment* c : model->comments) {
        if (str::EqI(c->userId, userId) && c->n > maxN) {
            maxN = c->n;
        }
    }
    return maxN + 1;
}

static bool IsAsciiAlnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

TempStr ReviewInitialsTemp(Str name) {
    str::Builder b;
    int words = 0;
    bool inWord = false;
    for (int i = 0; i < name.len; i++) {
        char c = name.s[i];
        if (IsAsciiAlnum(c)) {
            if (!inWord && words < 3) {
                b.AppendChar((char)toupper((u8)c));
                words++;
            }
            inWord = true;
        } else {
            inWord = false;
        }
    }
    if (words == 1) {
        // single word: use its first two letters
        for (int i = 0, seen = 0; i < name.len; i++) {
            if (IsAsciiAlnum(name.s[i])) {
                seen++;
                if (seen == 2) {
                    b.AppendChar((char)toupper((u8)name.s[i]));
                    break;
                }
            }
        }
    }
    if (b.IsEmpty()) {
        b.Append(StrL("RV"));
    }
    return str::DupTemp(Str(b.els, (int)b.len));
}

static bool TimeToTm(i64 t, bool utc, struct tm* out) {
    time_t tt = (time_t)t;
#if defined(_WIN32)
    return (utc ? gmtime_s(out, &tt) : localtime_s(out, &tt)) == 0;
#else
    return (utc ? gmtime_r(&tt, out) : localtime_r(&tt, out)) != nullptr;
#endif
}

TempStr ReviewFormatTimeTemp(i64 t) {
    struct tm tm {};
    if (t <= 0 || !TimeToTm(t, false, &tm)) {
        return {};
    }
    char buf[32]{};
    strftime(buf, sizeof(buf) - 1, "%Y-%m-%d %H:%M", &tm);
    return str::DupTemp(Str(buf));
}

TempStr ReviewFormatTimeIsoUtcTemp(i64 t) {
    struct tm tm {};
    if (!TimeToTm(t, true, &tm)) {
        return str::DupTemp(StrL("2026-01-01T00:00:00Z"));
    }
    char buf[32]{};
    strftime(buf, sizeof(buf) - 1, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return str::DupTemp(Str(buf));
}

Str ReviewAnchorStateLabel(ReviewAnchorState st) {
    switch (st) {
        case ReviewAnchorState::Same:
            return StrL("\xe2\x9c\x93 original");
        case ReviewAnchorState::Found:
            return StrL("\xe2\x9c\x93 found");
        case ReviewAnchorState::Moved:
            return StrL("\xe2\x9a\xa0 check position");
        case ReviewAnchorState::Gone:
            return StrL("\xe2\x9c\x97 component gone");
        default:
            return {};
    }
}

TempStr ReviewSanitizeUserIdTemp(Str userName) {
    str::Builder b;
    for (int i = 0; i < userName.len; i++) {
        char c = userName.s[i];
        if (IsAsciiAlnum(c) || c == '.' || c == '-' || c == '_') {
            b.AppendChar((char)tolower((u8)c));
        } else {
            b.AppendChar('_');
        }
    }
    if (b.IsEmpty()) {
        b.Append(StrL("user"));
    }
    return str::DupTemp(Str(b.els, (int)b.len));
}

bool ReviewPageInRanges(Str ranges, int page) {
    // "1-20, 33,40 - 42"
    int i = 0;
    int n = ranges.len;
    while (i < n) {
        while (i < n && !(ranges.s[i] >= '0' && ranges.s[i] <= '9')) {
            i++;
        }
        if (i >= n) {
            break;
        }
        int a = 0;
        while (i < n && ranges.s[i] >= '0' && ranges.s[i] <= '9') {
            a = a * 10 + (ranges.s[i] - '0');
            i++;
        }
        int b = a;
        int j = i;
        while (j < n && ranges.s[j] == ' ') {
            j++;
        }
        if (j < n && ranges.s[j] == '-') {
            j++;
            while (j < n && ranges.s[j] == ' ') {
                j++;
            }
            if (j < n && ranges.s[j] >= '0' && ranges.s[j] <= '9') {
                b = 0;
                while (j < n && ranges.s[j] >= '0' && ranges.s[j] <= '9') {
                    b = b * 10 + (ranges.s[j] - '0');
                    j++;
                }
                i = j;
            }
        }
        if (page >= a && page <= b) {
            return true;
        }
    }
    return false;
}

// --- anchoring ---

// common reference designator prefixes with 2-3 letters (any 1-letter prefix counts)
static SeqStrings gDesignatorPrefixes =
    "TP\0SW\0FB\0IC\0CN\0BT\0RN\0RP\0RV\0CR\0VR\0DS\0JP\0FL\0MH\0PS\0TR\0LED\0ANT\0BZ\0SP\0LS\0TH\0"
    "NTC\0RLY\0OSC\0TVS\0MK\0FD\0FID\0HS\0XT\0ZD\0PTC\0PT\0ST\0TC\0CON\0SPK\0MIC\0BAT\0XTL\0REG\0";

int ReviewDesignatorScore(Str w) {
    // 1-3 uppercase letters, 1-4 digits, optional trailing unit letter (U12A)
    int i = 0, nLetters = 0, nDigits = 0;
    while (i < w.len && w.s[i] >= 'A' && w.s[i] <= 'Z') {
        i++;
        nLetters++;
    }
    while (i < w.len && w.s[i] >= '0' && w.s[i] <= '9') {
        i++;
        nDigits++;
    }
    if (i < w.len && w.s[i] >= 'A' && w.s[i] <= 'Z' && i == w.len - 1) {
        i++;
    }
    if (i != w.len || nLetters < 1 || nLetters > 3 || nDigits < 1 || nDigits > 4) {
        return 0;
    }
    if (nLetters == 1 || SeqStrIndex(gDesignatorPrefixes, Str(w.s, nLetters)) >= 0) {
        return 2;
    }
    // e.g. pin names (PA9); part numbers like SI2301 or BC847 are not designators
    return nDigits <= 2 ? 1 : 0;
}

bool ReviewIsDesignator(Str w) {
    return ReviewDesignatorScore(w) > 0;
}

static PointF Center(RectF r) {
    return PointF{r.x + r.dx / 2, r.y + r.dy / 2};
}

static float DistSq(PointF a, PointF b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// distance from point to rectangle (0 if inside), squared
static float DistToRectSq(PointF p, RectF r) {
    float dx = 0, dy = 0;
    if (p.x < r.x) {
        dx = r.x - p.x;
    } else if (p.x > r.x + r.dx) {
        dx = p.x - (r.x + r.dx);
    }
    if (p.y < r.y) {
        dy = r.y - p.y;
    } else if (p.y > r.y + r.dy) {
        dy = p.y - (r.y + r.dy);
    }
    return dx * dx + dy * dy;
}

static bool IsSignificantWord(Str w) {
    if (w.len < 2) {
        return false;
    }
    for (int i = 0; i < w.len; i++) {
        if (IsAsciiAlnum(w.s[i])) {
            return true;
        }
    }
    return false;
}

constexpr float kAnchorDesignatorRadius = 60.f;
constexpr float kAnchorWordRadius = 120.f;
constexpr float kNearWordsRadius = 150.f;
constexpr int kMaxNearWords = 8;
constexpr float kNearTolerance = 6.f;

void ReviewComputeFingerprint(ReviewComment* c, PointF pt, const Vec<ReviewWord>& words) {
    int best = -1;
    float bestD = 0;
    int bestScore = 0;
    int n = len(words);
    for (int i = 0; i < n; i++) {
        const ReviewWord& w = words[i];
        if (!IsSignificantWord(w.text)) {
            continue;
        }
        float d = DistToRectSq(pt, w.rect);
        int score = ReviewDesignatorScore(w.text);
        float limit = score > 0 ? kAnchorDesignatorRadius : kAnchorWordRadius;
        if (d > limit * limit) {
            continue;
        }
        // designators win over plain words
        bool better = best < 0 || score > bestScore || (score == bestScore && d < bestD);
        if (better) {
            best = i;
            bestD = d;
            bestScore = score;
        }
    }
    str::FreePtr(&c->anchorWord);
    c->anchorDx = 0;
    c->anchorDy = 0;
    if (best >= 0) {
        const ReviewWord& w = words[best];
        c->anchorWord = str::Dup(w.text);
        PointF ctr = Center(w.rect);
        c->anchorDx = pt.x - ctr.x;
        c->anchorDy = pt.y - ctr.y;
    }

    // nearest other words, stored relative to the comment point
    c->nearWords.Reset();
    for (int k = 0; k < kMaxNearWords; k++) {
        int pick = -1;
        float pickD = 0;
        for (int i = 0; i < n; i++) {
            const ReviewWord& w = words[i];
            if (i == best || !IsSignificantWord(w.text)) {
                continue;
            }
            float d = DistSq(pt, Center(w.rect));
            if (d > kNearWordsRadius * kNearWordsRadius) {
                continue;
            }
            // skip already picked (compare by text + position)
            bool dup = false;
            PointF ctr = Center(w.rect);
            TempStr entry = fmt("%s %.1f %.1f", w.text, ctr.x - pt.x, ctr.y - pt.y);
            if (c->nearWords.Contains(entry)) {
                dup = true;
            }
            if (dup) {
                continue;
            }
            if (pick < 0 || d < pickD) {
                pick = i;
                pickD = d;
            }
        }
        if (pick < 0) {
            break;
        }
        PointF ctr = Center(words[pick].rect);
        c->nearWords.Append(fmt("%s %.1f %.1f", words[pick].text, ctr.x - pt.x, ctr.y - pt.y));
    }
}

struct NearWord {
    TempStr text;
    float dx = 0;
    float dy = 0;
};

static bool ParseNearWord(Str entry, NearWord* out) {
    // "text dx dy"; text has no spaces
    int sp1 = str::IndexOfChar(entry, ' ');
    if (sp1 <= 0) {
        return false;
    }
    Str rest(entry.s + sp1 + 1, entry.len - sp1 - 1);
    int sp2 = str::IndexOfChar(rest, ' ');
    if (sp2 <= 0) {
        return false;
    }
    out->text = str::DupTemp(Str(entry.s, sp1));
    out->dx = (float)atof(CStrTemp(Str(rest.s, sp2)));
    out->dy = (float)atof(CStrTemp(Str(rest.s + sp2 + 1, rest.len - sp2 - 1)));
    return true;
}

// how many near words are where we expect them if the comment point is at pt
static int CountNearMatches(const ReviewComment* c, const Vec<ReviewWord>& words, PointF pt, int* totalOut) {
    int total = 0;
    int matches = 0;
    for (Str entry : c->nearWords) {
        NearWord nw;
        if (!ParseNearWord(entry, &nw)) {
            continue;
        }
        total++;
        PointF expected{pt.x + nw.dx, pt.y + nw.dy};
        for (const ReviewWord& w : words) {
            if (!str::Eq(w.text, nw.text)) {
                continue;
            }
            if (DistSq(Center(w.rect), expected) <= kNearTolerance * kNearTolerance) {
                matches++;
                break;
            }
        }
    }
    *totalOut = total;
    return matches;
}

bool ReviewLocateOnPage(const ReviewComment* c, const Vec<ReviewWord>& words, PointF* ptOut, bool* goodOut) {
    *goodOut = false;
    PointF orig = c->Point();
    if (len(c->anchorWord) > 0) {
        PointF oldAnchor{orig.x - c->anchorDx, orig.y - c->anchorDy};
        int nCandidates = 0;
        int bestMatches = -1;
        float bestD = 0;
        PointF bestPt{};
        int total = 0;
        for (const ReviewWord& w : words) {
            if (!str::Eq(w.text, c->anchorWord)) {
                continue;
            }
            nCandidates++;
            PointF ctr = Center(w.rect);
            PointF pt{ctr.x + c->anchorDx, ctr.y + c->anchorDy};
            int matches = CountNearMatches(c, words, pt, &total);
            float d = DistSq(ctr, oldAnchor);
            if (matches > bestMatches || (matches == bestMatches && d < bestD)) {
                bestMatches = matches;
                bestD = d;
                bestPt = pt;
            }
        }
        if (nCandidates == 0) {
            return false;
        }
        *ptOut = bestPt;
        if (total == 0) {
            *goodOut = nCandidates == 1 || bestD < 50.f * 50.f;
        } else {
            *goodOut = bestMatches * 2 >= total;
        }
        return true;
    }

    // no anchor word: vote with the near words for a common translation
    Vec<PointF> shifts;
    for (Str entry : c->nearWords) {
        NearWord nw;
        if (!ParseNearWord(entry, &nw)) {
            continue;
        }
        PointF expected{orig.x + nw.dx, orig.y + nw.dy};
        float bestD = -1;
        PointF found{};
        for (const ReviewWord& w : words) {
            if (!str::Eq(w.text, nw.text)) {
                continue;
            }
            PointF ctr = Center(w.rect);
            float d = DistSq(ctr, expected);
            if (bestD < 0 || d < bestD) {
                bestD = d;
                found = ctr;
            }
        }
        if (bestD >= 0) {
            shifts.Append(PointF{found.x - expected.x, found.y - expected.y});
        }
    }
    int ns = len(shifts);
    if (ns == 0) {
        return false;
    }
    // pick the shift most other shifts agree with
    int bestVotes = -1;
    PointF best{};
    for (int i = 0; i < ns; i++) {
        int votes = 0;
        for (int j = 0; j < ns; j++) {
            if (DistSq(shifts[i], shifts[j]) <= kNearTolerance * kNearTolerance) {
                votes++;
            }
        }
        if (votes > bestVotes) {
            bestVotes = votes;
            best = shifts[i];
        }
    }
    *ptOut = PointF{orig.x + best.x, orig.y + best.y};
    *goodOut = bestVotes >= 2 && bestVotes * 2 >= len(c->nearWords);
    return true;
}

void ReviewWordsNearPoint(const Vec<ReviewWord>& words, PointF pt, TempStr* designatorOut, TempStr* clickedOut) {
    float bestDes = -1;
    int bestScore = 0;
    float bestClick = -1;
    for (const ReviewWord& w : words) {
        float d = DistToRectSq(pt, w.rect);
        if (clickedOut && d <= 4.f * 4.f && (bestClick < 0 || d < bestClick) && IsSignificantWord(w.text)) {
            bestClick = d;
            *clickedOut = str::DupTemp(w.text);
        }
        int score = designatorOut ? ReviewDesignatorScore(w.text) : 0;
        if (score > 0) {
            // a sure designator (U5) beats a closer maybe-designator (pin PA9)
            float dc = DistSq(pt, Center(w.rect));
            bool better = bestDes < 0 || score > bestScore || (score == bestScore && dc < bestDes);
            if (dc <= 250.f * 250.f && better) {
                bestDes = dc;
                bestScore = score;
                *designatorOut = str::DupTemp(w.text);
            }
        }
    }
}

static SeqStrings gTitleBoilerplate =
    "COPYRIGHT\0PROPRIETARY\0CONFIDENTIAL\0PERMISSION\0INFORMATION\0RESERVED\0DISCLOSURE\0"
    "REPRODUCED\0WITHOUT\0DOCUMENT\0WRITTEN\0EXPRESSLY\0";

// a line of words in the title block
struct TitleLine {
    float y = 0;
    float h = 0;
    float minX = 0;
    str::Builder text;
    bool boilerplate = false;
    bool afterTitleLabel = false;
};

TempStr ReviewSheetTitleTemp(const Vec<ReviewWord>& words, RectF mb) {
    RectF region{mb.x + mb.dx * 0.55f, mb.y + mb.dy * 0.82f, mb.dx * 0.45f, mb.dy * 0.18f};
    // collect words in region sorted by y then x
    Vec<int> idx;
    int n = len(words);
    for (int i = 0; i < n; i++) {
        PointF ctr = Center(words[i].rect);
        if (region.Contains(ctr)) {
            idx.Append(i);
        }
    }
    std::sort(idx.begin(), idx.end(), [&words](int a, int b) {
        RectF ra = words[a].rect;
        RectF rb = words[b].rect;
        float ya = ra.y + ra.dy / 2;
        float yb = rb.y + rb.dy / 2;
        if (fabsf(ya - yb) > std::max(ra.dy, rb.dy) / 2) {
            return ya < yb;
        }
        return ra.x < rb.x;
    });

    // explicit "TITLE" label: the text right of it on the same line wins
    int ni = len(idx);
    for (int k = 0; k < ni; k++) {
        const ReviewWord& w = words[idx[k]];
        TempStr up = str::DupTemp(w.text);
        str::ToUpperInPlace(up);
        if (!str::StartsWith(up, "TITLE")) {
            continue;
        }
        float cy = w.rect.y + w.rect.dy / 2;
        str::Builder line;
        for (int j = k + 1; j < ni; j++) {
            const ReviewWord& w2 = words[idx[j]];
            float cy2 = w2.rect.y + w2.rect.dy / 2;
            if (fabsf(cy2 - cy) > w.rect.dy / 2 || w2.rect.x < w.rect.x) {
                break;
            }
            if (!line.IsEmpty()) {
                line.AppendChar(' ');
            }
            line.Append(w2.text);
        }
        if (line.len > 1) {
            return str::DupTemp(Str(line.els, (int)line.len));
        }
    }

    // otherwise the tallest (largest font) non-boilerplate line, longest on ties
    TempStr best = {};
    float bestH = 0;
    int k = 0;
    while (k < ni) {
        const ReviewWord& w0 = words[idx[k]];
        float cy = w0.rect.y + w0.rect.dy / 2;
        float h = w0.rect.dy;
        str::Builder line;
        bool boiler = false;
        int j = k;
        while (j < ni) {
            const ReviewWord& w = words[idx[j]];
            float cyj = w.rect.y + w.rect.dy / 2;
            if (fabsf(cyj - cy) > std::max(h, w.rect.dy) / 2) {
                break;
            }
            if (!line.IsEmpty()) {
                line.AppendChar(' ');
            }
            line.Append(w.text);
            h = std::max(h, w.rect.dy);
            TempStr up = str::DupTemp(w.text);
            str::ToUpperInPlace(up);
            if (SeqStrIndexIS(gTitleBoilerplate, up) >= 0) {
                boiler = true;
            }
            j++;
        }
        k = j;
        if (boiler || line.len < 3) {
            continue;
        }
        bool better = len(best) == 0 || h > bestH + 0.5f || (fabsf(h - bestH) <= 0.5f && (int)line.len > len(best));
        if (better) {
            best = str::DupTemp(Str(line.els, (int)line.len));
            bestH = h;
        }
    }
    return best;
}

static bool IsWordSeparator(int c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == 0xA0;
}

static Str StripPunct(Str s) {
    auto isPunct = [](char c) {
        return c == ',' || c == ';' || c == ':' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' ||
               c == '}' || c == '"' || c == '\'';
    };
    while (s.len > 0 && isPunct(s.s[0])) {
        s.s++;
        s.len--;
    }
    while (s.len > 0 && isPunct(s.s[s.len - 1])) {
        s.len--;
    }
    return s;
}

void ReviewWordsFromPageText(Str text, const Rect* coords, int textLen, Vec<ReviewWord>& words) {
    int byteIdx = 0;
    int wordStart = -1;
    RectF wordRect{};
    for (int i = 0; i <= textLen; i++) {
        int charStart = byteIdx;
        int c = ' ';
        if (i < textLen) {
            c = Utf8CodepointNext(text, byteIdx);
        }
        if (IsWordSeparator(c)) {
            if (wordStart >= 0) {
                Str w = StripPunct(Str(text.s + wordStart, charStart - wordStart));
                if (w.len > 0) {
                    ReviewWord rw;
                    rw.text = str::Dup(w);
                    rw.rect = wordRect;
                    words.Append(rw);
                }
                wordStart = -1;
            }
            continue;
        }
        RectF gr = ToRectF(coords[i]);
        if (wordStart < 0) {
            wordStart = charStart;
            wordRect = gr;
        } else {
            wordRect = wordRect.Union(gr);
        }
    }
}

void ReviewFreeWords(Vec<ReviewWord>& words) {
    for (ReviewWord& w : words) {
        str::Free(w.text);
    }
    words.Reset();
}

// --- csv ---

void ReviewParseCsv(Str data, Vec<StrVec*>& rows) {
    StrVec* row = nullptr;
    str::Builder field;
    bool inQuotes = false;
    bool any = false;
    auto endField = [&]() {
        if (!row) {
            row = new StrVec();
        }
        row->Append(field.IsEmpty() ? StrL("") : Str(field.els, (int)field.len));
        field.Reset();
        any = false;
    };
    auto endRow = [&]() {
        endField();
        rows.Append(row);
        row = nullptr;
    };
    int i = 0;
    while (i < data.len) {
        char c = data.s[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < data.len && data.s[i + 1] == '"') {
                    field.AppendChar('"');
                    i += 2;
                } else {
                    inQuotes = false;
                    i++;
                }
            } else {
                field.AppendChar(c);
                i++;
            }
            continue;
        }
        if (c == '"') {
            inQuotes = true;
            any = true;
            i++;
        } else if (c == ',') {
            endField();
            any = true;
            i++;
        } else if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < data.len && data.s[i + 1] == '\n') {
                i++;
            }
            i++;
            endRow();
        } else {
            field.AppendChar(c);
            any = true;
            i++;
        }
    }
    if (row || any || !field.IsEmpty()) {
        endRow();
    }
}
