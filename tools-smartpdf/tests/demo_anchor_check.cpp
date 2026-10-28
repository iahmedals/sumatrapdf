/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// Locates the review demo's revision A comments on revision B the way the viewer
// does (src/ReviewUI.cpp ResolveStep) and prints found / moved / gone for each.
//   demo_anchor_check <words.tsv> <log.jsonl>...
// words.tsv: page<TAB>word<TAB>x0<TAB>y0<TAB>x1<TAB>y1 (see run-demo-anchor-test.sh)

#include "base/Base.h"
#include "base/File.h"
#include "Review.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    // words of revision B, per page
    Vec<ReviewWord> pages[4];
    FILE* f = fopen(argv[1], "r");
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int page; char text[256]; float x0, y0, x1, y1;
        if (sscanf(line, "%d\t%255[^\t]\t%f\t%f\t%f\t%f", &page, text, &x0, &y0, &x1, &y1) != 6) continue;
        ReviewWord w;
        w.text = str::Dup(Str(text));
        w.rect = RectF(x0, y0, x1 - x0, y1 - y0);
        pages[page].Append(w);
    }
    fclose(f);
    Vec<ReviewEvent*> events;
    for (int i = 2; i < argc; i++) {
        Str data = file::ReadFile(Str(argv[i]));
        ReviewParseLog(data, events);
    }
    ReviewModel m;
    ReviewReplay(&m, events);
    for (ReviewComment* c : m.comments) {
        bool des = ReviewDesignatorScore(c->anchorWord) == 2;
        Vec<int> order;
        order.Append(c->page);
        for (int d = 1; d < 3; d++) {
            if (des || d == 1) {
                if (c->page - d >= 1) order.Append(c->page - d);
                if (c->page + d <= 3) order.Append(c->page + d);
            }
        }
        const char* state = des ? "gone" : "moved(no anchor)";
        PointF pt{};
        int foundPage = 0;
        for (int p : order) {
            bool good = false;
            if (ReviewLocateOnPage(c, pages[p], &pt, &good)) {
                state = (!good || p != c->page) ? "moved" : "found";
                foundPage = p;
                break;
            }
        }
        printf("%s %s %s %.0f %.0f\n", CStrTemp(c->key), CStrTemp(c->anchorWord), state, pt.x, pt.y);
    }
    return 0;
}
void log(Str s) { fwrite(s.s, 1, (size_t)s.len, stderr); }
