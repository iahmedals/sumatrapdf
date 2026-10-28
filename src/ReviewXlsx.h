/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// smartpdf: writes the review state as a formatted Excel workbook (.xlsx).
// Portable (no Windows APIs): builds the OOXML parts and a stored zip in memory.

struct ReviewModel;

struct ReviewXlsxImage {
    Str commentKey; // ReviewComment::key
    Str png;        // PNG file bytes (not owned)
    int dx = 0;     // pixel size
    int dy = 0;
};

struct ReviewXlsxArgs {
    ReviewModel* model = nullptr;
    Str title;      // shown on the dashboard (PDF file name)
    Str currentRev; // file name of the newest PDF revision
    Str pdfLink;    // relative link to the PDF (optional)
    int pageCount = 0;
    StrVec pageTitles; // optional sheet title per page (index = page - 1)
    i64 generatedTime = 0;
    Vec<ReviewXlsxImage> images; // optional snapshots
};

// returns the .xlsx file bytes (caller frees)
Str ReviewWriteXlsx(ReviewXlsxArgs* args);

// minimal zip writer (stored entries), exposed for tests
struct ZipWriterStored {
    str::Builder data;
    str::Builder centralDir;
    int count = 0;
    void Add(Str name, Str content);
    Str Finish(); // caller frees
};
u32 Crc32(u32 crc, const u8* data, size_t len);
