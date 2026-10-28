# smartpdf changes

This fork of SumatraPDF contains one focused change on top of the official code, plus a
small Python tool (`tools-smartpdf/`) that prepares schematic PDFs for it.

## What was changed (in plain words)

Our schematic PDFs get processed by `tools-smartpdf/smartify.py`, which makes three kinds
of text clickable. The forked reader understands the special links the tool writes:

1. **Net names** (e.g. `CLK`, `VREG_S4A_1P8`) carry a hidden `search:NAME?z=200` link.
   Clicking one does the same as Ctrl+F: the Find box fills with the name, the view jumps
   to the next place it appears, highlights it — and zooms in (the `z=200` part; the tool's
   `--zoom` option controls it, `0` turns it off). F3 / Shift+F3 move between matches.
   "Whole word" and "Match case" are forced on, so clicking `CLK` never lands on `CLK_EN`.
2. **Manufacturer part numbers** (e.g. `DRV2604YZFR`) carry an ordinary web link that
   opens the browser with a Google "…datasheet" search. This part works in ANY PDF viewer.
3. **Company part numbers** (`yyy-xxxx`, e.g. `214-0034`) carry a hidden `folder:` link.
   Clicking one opens File Explorer at the company parts library
   (`\\datastore\groups\Engineering\Approved Component\yyy\yyy-xxxx\Data Sheets`).
   If the exact folder doesn't exist, the reader walks up to the nearest parent that does.

The tool also adds a **bookmarks sidebar** (the PDF outline panel) listing the part numbers
(`--bookmarks all` adds nets too) — works in any viewer.

## Design review (v0.4.0)

The reader is a multi-reviewer schematic review tool. No server: a SharePoint folder,
synced to every PC by OneDrive, is the hub.

### Set up (once per design)

1. Keep the schematic PDFs in a SharePoint folder and click **Sync** in SharePoint so
   OneDrive keeps a copy on every reviewer's PC. Put every new revision in the same folder.
2. The first comment creates a `review-comments` folder next to the PDF. Everyone's
   comments live there, one file per person (`<username>.jsonl`), so nobody ever
   overwrites anybody else. Snapshots go to `review-comments\snapshots`.
3. The designer opens the review panel (Ctrl+Shift+R) → **Team** → **I'm the designer**.
   From then on the designer's PC keeps `Review-Report.xlsx` (next to the PDF) up to date.
   Everyone else only opens it (Excel desktop, or Excel Online in SharePoint).

### Reviewing

- **Add a comment:** right-click the spot → **Add Review Comment**, or press
  **Ctrl+Shift+M** with the mouse over it, or click **+ Comment** in the panel and then
  the spot. Pick the severity (**Critical / Major / Minor / Comment**, Ctrl+1…4), type,
  **Enter** saves (Shift+Enter = new line, Esc = cancel, Ctrl+T = templates).
  Page, sheet title and the nearest part (R45, U12…) are filled in automatically.
  Nothing typed gets lost: with text in the box, Esc asks for a second Esc, and if the
  window closes while the box is open the text goes to
  `%LOCALAPPDATA%\SumatraPDF\review-drafts.txt`.
- **Already reported?** If someone commented on the same spot or part, the pop-up shows it
  with a **+1 instead** button.
- **Area comments:** hold Ctrl and drag a rectangle first, then Add Review Comment and
  choose box, ellipse or highlight.
- **Net comments:** comment on a net name and tick "apply to the whole net": the net gets
  marked everywhere it appears.
- Every comment gets an ID like `AAS-014` (your initials + a number): the same on the
  pin, the card and in Excel.
- **No Publish button any more:** saving is publishing. Others see your comment within
  seconds (as fast as OneDrive syncs) and get a pop-up when you @mention them.
- **Snapshot:** a small picture of the spot is saved with every comment (card + Excel).
- **Pins** on the page: colored by severity, numbered with the ID, same size at any zoom.
  Hover shows the comment. ✓ = fixed, ✗ = rejected, amber dot = moved in this
  revision (check it), dashed = the part is gone. Click a pin to open it in the panel.
- **Keyboard review mode (F7):** `C` comment at the mouse, `1`–`4` severity of the
  selected comment, `N` / `Shift+N` next / previous, `D` page reviewed, `Enter` edit,
  `Esc` exit.

### The review panel (Ctrl+Shift+R)

- Severity tiles with the open counts (click to filter), search, **This page** (follows
  the page you're looking at), **@ Mentions**, **My sheets**, status, reviewer and round
  filters, several sort orders.
- Cards colored by severity. Click a card to jump to the exact spot. An open card shows
  the snapshot and details, **Status Open / Fixed / Rejected** (anyone can set it; who and
  when is kept in the history), severity, +1, edit / delete (your own), **Move pin**, and
  replies with @mentions.
- **Sheets:** tick sheets you finished, see progress and who reviewed what.
- **Checklist:** the team checklist and your **sign-off**.
- **Team:** your name and initials, the designer, review rounds, sheet assignments,
  comment templates and checklist items (shared with everyone).
- Without WebView2 a simple built-in list is shown instead.

### New PDF revisions

- Export the new revision into the same folder, under a new name or over the old file
  (both work: revisions are told apart by their content). Comments follow it: each comment
  remembers its part and the words around it and is found again even when things moved a
  bit. Card badges: ✓ found, ⚠ moved (please check), ✕ gone (part removed, maybe fixed).
  **Move pin** corrects a spot.
- **Compare:** panel ⋯ menu or **Review → Compare with Another Revision**: what changed
  against another PDF in the folder is highlighted in pink.
- **Rounds:** Team → Start round N+1. New comments are tagged with the round; filter
  "New in round N".

### Excel report (`Review-Report.xlsx`)

- **Comments** sheet: a filterable table with severity and status colors, IDs, parts,
  sheets, who/when, replies, +1s, revision found in, and snapshot pictures.
- **Dashboard:** open counts, a chart per severity, comments per reviewer and per sheet.
- **Progress:** reviewers, sheets reviewed, checklist and sign-offs.
- Only the designer's PC writes it (Excel can't take many writers). If it's open in
  Excel it tries again later. Others: **Open Excel report** opens the shared file, or a
  personal copy when there is none yet.

### Export PDF with comments

**Review → Export PDF with Comments** saves a copy of the PDF with every comment as a
sticky note colored by severity, for people who use Acrobat. The original is untouched.

### Speed

PDFs without a review folder pay nothing. Pins are drawn for visible pages only; page
text is read one page at a time in idle moments; the panel is created the first time it's
opened (the log shows "review: panel ready in N ms"); the Excel report, snapshots and
compare run in the background.

### Try it: the review demo

`tools-smartpdf/review-demo` holds a tiny generated 3-sheet design in two revisions
(`Demo-Board_RevA.pdf`, `Demo-Board_RevB.pdf`) and comments from two demo reviewers made on
revision A. Copy the folder somewhere (e.g. into a OneDrive folder), open
`Demo-Board_RevB.pdf` and press Ctrl+Shift+R: the comments are found again on revision B
(Q3 was removed: "gone"; the crystal area changed: "moved"), Review → Compare shows what
changed, and Team → "I'm the designer" writes the Excel report. Regenerate it with
`python3 tools-smartpdf/make_review_demo.py`.

### From v0.3

Your old `review-comments\<you>.csv` is moved into your new log automatically (the CSV
is renamed to `.csv.migrated`). Other people's old CSVs keep showing until they open the
new version.

## Smarter linking with design data (optional)

`smartify.py` accepts two files any EDA tool can export, replacing guesswork with truth:
- `--nets nets.txt` (one net name per line): ONLY these names become net links.
- `--bom bom.csv` (reference designator + manufacturer part number columns, header
  auto-detected): part-number links come from the BOM, and reference designators (R45,
  U12) become clickable search links plus a "Components" bookmarks tree.

The `search:` and `folder:` links are handled entirely inside the reader and are never
passed to Windows — no protocol handlers, no registry entries. Every other kind of link
(web addresses, page jumps, table of contents) behaves exactly like the official reader.

## Where the changes live

- Links: `src/MainWindow.cpp`, function `LinkHandler::LaunchURL` — the single door every
  clicked link walks through. Two `if` blocks marked with `smartpdf` comments: one for
  `search:` (+optional zoom), one for `folder:`.
- Review: `src/Review.cpp` (data: comments, logs, IDs, re-finding comments on new
  revisions), `src/ReviewXlsx.cpp` (the Excel writer), `src/ReviewUI.cpp` (pins, pop-up,
  panel, live sync, report, snapshots, compare, PDF export), `src/review/panel.html` (the
  panel, built into the exe). Small hooks marked `smartpdf` / `Review` in `Canvas.cpp`,
  `SumatraPDF.cpp`, `Menu.cpp`, `Accelerators.cpp`, `CommandAvailability.cpp`,
  `SumatraStartup.cpp`, `MainWindow.*`, `WindowTab.*`, `AIChatCommon.cpp`.
- Tests (Linux): `tools-smartpdf/tests/run-review-tests.sh` (data model + Excel file) and
  `tools-smartpdf/tests/run-demo-anchor-test.sh` (comments re-found on a new revision).
- Tool: `tools-smartpdf/smartify.py` (adds the links to a PDF),
  `tools-smartpdf/make_test_pdf.py` + `smartpdf-test.pdf` (test sheet covering every
  behavior — instructions printed on page 1).
- CI: `.github/workflows/smartpdf-build.yml` builds `SumatraPDF.exe` on GitHub's Windows
  servers on every push to the smartpdf branch; download it from the run's Artifacts.

## Versions

- **v0.1.0** — commit `789a6f5`: click-to-search for nets (no zoom), smartify v1, CI, test PDF.
- **v0.2.0** — commit `9e2a616` (+ feedback fixes `e7cd62a`): zoom on net click, browser
  search for manufacturer part numbers, File Explorer for company part numbers, bookmarks
  sidebar, smarter token classification.
- **v0.3.0** — commit `69fc241`: design review mode: click-to-comment with auto-opening
  comment box, Publish/Import review comments over a shared folder, merged
  ALL-comments.csv central sheet, Review Comments panel with clickable cards,
  revision-proof review folder; smartify --nets/--bom knowledge files.

(Tags exist in the local development clone; pushing tags is blocked in the build
environment, so use the commit hashes above to check out a version.)

## How to carry this change onto a newer SumatraPDF release

The viewer change is small and self-contained, so updating is easy:

1. Fetch the new official release tag from the upstream repository
   (https://github.com/sumatrapdfreader/sumatrapdf).
2. Rebase or cherry-pick this fork's commits onto the new tag, e.g.
   `git rebase <new-tag>` on this branch.
3. If `LaunchURL` moved or was renamed, search the code for `SumatraLaunchBrowser` —
   the `search:`/`folder:` checks belong immediately before the code path that reaches it.
4. Rebuild on Windows (Visual Studio 2022, Release | x64, `vs2022\SumatraPDF.sln`,
   project `SumatraPDF`) — or just push and let the CI workflow build it.
