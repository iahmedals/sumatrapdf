/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

/*
smartpdf review model (portable, no UI).

Every reviewer appends events to their own log file in a shared folder next to
the reviewed PDF (synced via SharePoint/OneDrive):

    review-comments/<userId>.jsonl     one JSON object per line, append-only

Nobody ever writes someone else's file, so there are no write conflicts. The
current review state is the replay of all events from all logs, ordered by time.
*/

enum class ReviewSeverity {
    Critical = 0,
    Major = 1,
    Minor = 2,
    Comment = 3,
};
constexpr int kReviewSeverityCount = 4;

enum class ReviewStatus {
    Open = 0,
    Fixed = 1,
    Rejected = 2,
};
constexpr int kReviewStatusCount = 3;

// how well a comment was located in the currently open PDF revision
enum class ReviewAnchorState {
    Unresolved = 0, // not computed yet
    Same = 1,       // comment made on this very file
    Found = 2,      // re-located with high confidence
    Moved = 3,      // re-located with low confidence, please check
    Gone = 4,       // anchor component no longer in the document
};

Str ReviewSeverityName(ReviewSeverity);
Str ReviewStatusName(ReviewStatus);
ReviewSeverity ReviewSeverityFromName(Str);
ReviewStatus ReviewStatusFromName(Str);

struct ReviewReply {
    Str id;
    Str by;
    Str text;
    i64 time = 0;
    ~ReviewReply();
};

struct ReviewHistoryItem {
    Str by;
    Str what; // e.g. "status: Fixed", "severity: Major", "edited"
    i64 time = 0;
    ~ReviewHistoryItem();
};

// a word on a page with its position, used for anchoring
struct ReviewWord {
    Str text;
    RectF rect;
};

struct ReviewComment {
    Str key;    // "<userId>#<n>", unique
    Str userId; // author
    int n = 0;  // per-author sequence number
    ReviewSeverity severity = ReviewSeverity::Major;
    ReviewStatus status = ReviewStatus::Open;
    Str text;
    Str rev;   // file name of the PDF revision the comment was made on
    int page = 0;
    RectF rect;      // point comment: dx == dy == 0, else box (page coordinates)
    Str shape;       // "pin", "box", "ellipse", "highlight"
    Str component;   // nearest component designator (R12, U5)
    Str clicked;     // word under the click
    Str sheet;       // sheet title
    Str net;         // non-empty for net-level comments
    int round = 1;
    i64 created = 0;
    i64 updated = 0;
    Str statusBy;
    i64 statusTime = 0;
    Str snapshot; // file name in review-comments/snapshots/
    bool deleted = false;

    // anchor fingerprint
    Str anchorWord;     // word the comment is attached to
    float anchorDx = 0; // comment point minus anchor word center
    float anchorDy = 0;
    StrVec nearWords;   // "text dx dy" of nearby words (relative to the comment point)

    Vec<ReviewReply*> replies;
    StrVec agrees; // userIds who +1'd
    Vec<ReviewHistoryItem*> history;

    // resolved position in the currently open document (runtime only)
    int curPage = 0;
    RectF curRect;
    ReviewAnchorState anchorState = ReviewAnchorState::Unresolved;

    ~ReviewComment();
    PointF Point() const; // the pin location in page coordinates (top-left for boxes)
    bool IsBox() const;
};

struct ReviewUser {
    Str userId;
    Str name;
    Str initials;     // as chosen by the user
    Str idPrefix;     // initials, disambiguated across users (AA, AA2)
    i64 lastSeen = 0; // time of the user's last event
    bool signedOff = false;
    i64 signedOffTime = 0;
    Str signOffNote;
    StrVec reviewedPages; // page numbers as strings
    StrVec checkedItems;  // checklist items this user ticked
    ~ReviewUser();
};

struct ReviewEvent;

struct ReviewModel {
    Vec<ReviewComment*> comments; // in creation order
    Vec<ReviewUser*> users;
    // shared settings, latest value wins
    Str designer; // userId of the designer (owner of the Excel report)
    int round = 1;
    Str templates; // newline separated comment templates
    Str checklist; // newline separated checklist items
    StrVec assignUsers; // parallel arrays: userId -> page ranges ("1-20,33")
    StrVec assignPages;

    ~ReviewModel();
    void Reset();
    ReviewComment* FindComment(Str key) const;
    ReviewUser* FindUser(Str userId) const;
    ReviewUser* GetOrCreateUser(Str userId);
    Str UserName(Str userId) const;
    TempStr DisplayIdTemp(const ReviewComment*) const; // "AA-014"
    Str AssignedPages(Str userId) const;
    int OpenCount(ReviewSeverity) const;
};

// --- events and logs ---

// a single event to append, built by the UI and serialized as one JSON line
struct ReviewEventBuilder {
    str::Builder b;
    bool first = true;
    ReviewEventBuilder(Str type, Str by, i64 time);
    void Add(Str key, Str val);
    void Add(Str key, int val);
    void Add(Str key, float val);
    void AddBool(Str key, bool val);
    TempStr FinishTemp(); // JSON object + '\n'
};

void AppendJsonString(str::Builder& b, Str s);

// parses all lines of a log file and appends events (in file order)
void ReviewParseLog(Str data, Vec<ReviewEvent*>& events);
// rebuilds model from events of all logs; events are sorted by time (stable)
void ReviewReplay(ReviewModel* model, Vec<ReviewEvent*>& events);
void ReviewDeleteEvents(Vec<ReviewEvent*>& events);

// the next per-author sequence number for userId
int ReviewNextNumber(const ReviewModel* model, Str userId);

TempStr ReviewInitialsTemp(Str name); // "Ahmed Al Saadi" -> "AAS" (max 3)
TempStr ReviewFormatTimeTemp(i64 t);     // local time "2026-10-28 14:05"
TempStr ReviewFormatTimeIsoUtcTemp(i64 t); // "2026-10-28T14:05:00Z"
Str ReviewAnchorStateLabel(ReviewAnchorState);
TempStr ReviewSanitizeUserIdTemp(Str userName);
bool ReviewPageInRanges(Str ranges, int page); // "1-20, 33"

// --- anchoring across PDF revisions ---

bool ReviewIsDesignator(Str w);
// 0: not a reference designator, 1: maybe (pin names like PA9), 2: surely (R12, U5A, TP103)
int ReviewDesignatorScore(Str w);
// picks the anchor word near pt and fills c->anchorWord/anchorDx/anchorDy/nearWords
void ReviewComputeFingerprint(ReviewComment* c, PointF pt, const Vec<ReviewWord>& words);
// locates comment c on a page given its words; returns true if the anchor word was
// found and sets *ptOut to the new comment point; *goodOut = near words agree
bool ReviewLocateOnPage(const ReviewComment* c, const Vec<ReviewWord>& words, PointF* ptOut, bool* goodOut);
// nearest component designator to pt, and the word containing pt
void ReviewWordsNearPoint(const Vec<ReviewWord>& words, PointF pt, TempStr* designatorOut, TempStr* clickedOut);
// title block heuristic: longest line in the bottom-right region
TempStr ReviewSheetTitleTemp(const Vec<ReviewWord>& words, RectF mediabox);
// splits a page's text + glyph coords into words
void ReviewWordsFromPageText(Str text, const Rect* coords, int textLen, Vec<ReviewWord>& words);
void ReviewFreeWords(Vec<ReviewWord>& words);

// --- csv (legacy v0.3 migration) ---
void ReviewParseCsv(Str data, Vec<StrVec*>& rows);
