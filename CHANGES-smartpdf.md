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

## Design review mode (v0.3.0)

The reader doubles as a multi-reviewer schematic review tool. No server — a shared
network folder is the hub.

**For reviewers:**
1. Open the review PDF (it lives in a shared folder, e.g. `\\share\reviews\boardX\`).
2. Right-click a spot → Create Annotation Under Cursor → Text. The comment box opens
   by itself with the cursor ready — type and close. Repeat for every finding.
3. When done: right-click → **Publish Review Comments**. Your comments are written to
   `review-comments\<your-username>.csv` next to the PDF, with page number, sheet title,
   nearest component (R45, U12…), the clicked word, and date filled in automatically.

**The central sheet:** `review-comments\ALL-comments.csv` is rebuilt on every publish and
merges everyone's comments — open it in Excel directly. For a permanent workbook that
refreshes itself: Excel → Data → Get Data → From File → From Folder → pick the
`review-comments` folder → Combine & Load; from then on "Refresh All" pulls in the latest
comments.

**For the designer:**
- Right-click → **Import Review Comments** → every reviewer's notes appear as sticky
  notes at the spots they clicked, named by reviewer. Re-running syncs (no duplicates).
  The notes are an overlay: the PDF file is NOT modified unless you explicitly use
  "Save Annotations".
- **Review Comments panel** (search "Toggle Review Comments Panel" in the command
  palette, Ctrl+Shift+P): every comment as a card — component, page, reviewer (with a
  per-reviewer color), comment, sheet/revision/date. Click a card to jump to that exact
  spot (Alt+Left goes back). The pane below the list shows every detail of the selected
  comment in full.
- **Revisions:** the `review-comments` folder has a fixed name, so when a new revision
  PDF is exported into the same folder, Import on the new PDF still finds all comments.
  Each CSV row records which revision file the comment was made on.

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

- File: `src/MainWindow.cpp`, function `LinkHandler::LaunchURL` — the single door every
  clicked link walks through, for every document type. Two `if` blocks marked with
  `smartpdf` comments (about 55 lines total): one for `search:` (+optional zoom), one for
  `folder:`.
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
