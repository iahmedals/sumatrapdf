/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// smartpdf: the review report as a formatted Excel workbook. See ReviewXlsx.h.
// Portable: generates the OOXML parts and packs them in a stored (uncompressed) zip.

#include "base/Base.h"

#include "Review.h"
#include "ReviewXlsx.h"

// clang-format off
// standard Office theme (needed for table styles to render with the expected colors)
static const char* kThemeXmlParts[] = {
R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Office Theme"><a:themeElements><a:clrScheme name="Office"><a:dk1><a:sysClr val="windowText" lastClr="000000"/></a:dk1><a:lt1><a:sysClr val="window" lastClr="FFFFFF"/></a:lt1><a:dk2><a:srgbClr val="1F497D"/></a:dk2><a:lt2><a:srgbClr val="EEECE1"/></a:lt2><a:accent1><a:srgbClr val="4F81BD"/></a:accent1><a:accent2><a:srgbClr val="C0504D"/></a:accent2><a:accent3><a:srgbClr val="9BBB59"/></a:accent3><a:accent4><a:srgbClr val="8064A2"/></a:accent4><a:accent5><a:srgbClr val="4BACC6"/></a:accent5><a:accent6><a:srgbClr val="F79646"/></a:accent6><a:hlink><a:srgbClr val="0000FF"/></a:hlink><a:folHlink><a:srgbClr val="800080"/></a:folHlink></a:clrScheme><a:fontScheme name="Office"><a:majorFont><a:latin typeface="Cambria"/><a:ea typeface=""/><a:cs typeface=""/><a:font script="Jpan" typeface="&#xFF2D;&#xFF33; &#xFF30;&#x30B4;&#x30B7;&#x30C3;&#x30AF;"/><a:font script="Hang" typeface="&#xB9D1;&#xC740; &#xACE0;&#xB515;"/><a:font script="Hans" typeface="&#x5B8B;&#x4F53;"/><a:font script="Hant" typeface="&#x65B0;&#x7D30;&#x660E;&#x9AD4;"/><a:font script="Arab" typeface="Times New Roman"/><a:font script="Hebr" typeface="Times New Roman"/><a:font script="Thai" typeface="Tahoma"/><a:font script="Ethi" typeface="Nyala"/><a:font script="Beng" typeface="Vrinda"/><a:font script="Gujr" typeface="Shruti"/><a:font script="Khmr" typeface="MoolBoran"/><a:font script="Knda" typeface="Tunga"/><a:font script="Guru" typeface="Raavi"/><a:font script="Cans" typeface="Euphemia"/><a:font script="Cher" typeface="Plantagenet Cherokee"/><a:font script="Yiii" typeface="Microsoft Yi Baiti"/><a:font script="Tibt" typeface="Microsoft Himalaya"/><a:font script="Thaa" typeface="MV Boli"/><a:font script="Deva" typeface="Mangal"/><a:font script="Telu" typeface="Gautami"/><a:font script="Taml" typeface="Latha"/><a:font script="Syrc" typeface="Estrangelo Edessa"/><a:font script="Orya" typeface="Kalinga"/><a:font script="Mlym" typeface="Kartika"/><a:font script="Laoo" typeface="DokChampa"/><a:font script="Sinh" typeface="Iskoola Pota"/><a:font script="Mong" typeface="Mongolian Baiti"/><a:font script="Viet" typeface="Times New Roman"/><a:font script="Uigh" typeface="Microsoft Uighur"/></a:majorFont><a:minorFont><a:latin typeface="Calibri"/><a:ea typeface=""/><a:cs typeface=""/><a:font script="Jpan" typeface="&#xFF2D;&#xFF33; &#xFF30;&#x30B4;&#x30B7;&#x30C3;&#x30AF;"/><a:font script="Hang" typeface="&#xB9D1;&#xC740; &#xACE0;&#xB515;"/><a:font script="Hans" typeface="&#x5B8B;&#x4F53;"/><a:font script="Hant" typeface="&#x65B0;&#x7D30;&#x660E;&#x9AD4;"/><a:font script="Arab" typeface="Arial"/><a:font script="Hebr" typeface="Arial"/><a:font script="Thai" typeface="Tahoma"/><a:font script="Ethi" typeface="Nyala"/><a:font script="Beng" typeface="Vrinda"/><a:font script="Gujr" typeface="Shruti"/><a:font script="Khmr" typeface="DaunPenh"/><a:font script="Knda" typeface="Tunga"/><a:font script="Guru" typeface="Raavi"/><a:font script="Cans" typeface="Euphemia"/><a:font script="Cher" typeface="Plantagenet Cherokee"/><a:font script="Yiii" typeface="Microsoft Yi Baiti"/><a:font script="Tibt" typeface="Microsoft Himalaya"/><a:font script="Thaa" typeface="MV Boli"/><a:font script="Deva" typeface="Mangal"/><a:font script="Telu" typeface="Gautami"/><a:font script="Taml" typeface="Latha"/><a:font script="Syrc" typeface="Estrangelo Edessa"/><a:font script="Orya" typeface="Kalinga"/><a:font script="Mlym" typeface="Kartika"/><a:font script="Laoo" typeface="DokChampa"/><a:font script="Sinh" typeface="Iskoola Pota"/><a:font script="Mong" typeface="Mongolian Baiti"/><a:font script="Viet" typeface="Arial"/><a:font script="Uigh" typeface="Microsoft Uighur"/></a:minorFont></a:fontScheme><a:fmtScheme name="Office"><a:fillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:gradFill rotWithShape="1"><a:gsLst><a:gs pos="0"><a:schemeClr val="phClr"><a:tint val="50000)",
R"("/><a:satMod val="300000"/></a:schemeClr></a:gs><a:gs pos="35000"><a:schemeClr val="phClr"><a:tint val="37000"/><a:satMod val="300000"/></a:schemeClr></a:gs><a:gs pos="100000"><a:schemeClr val="phClr"><a:tint val="15000"/><a:satMod val="350000"/></a:schemeClr></a:gs></a:gsLst><a:lin ang="16200000" scaled="1"/></a:gradFill><a:gradFill rotWithShape="1"><a:gsLst><a:gs pos="0"><a:schemeClr val="phClr"><a:shade val="51000"/><a:satMod val="130000"/></a:schemeClr></a:gs><a:gs pos="80000"><a:schemeClr val="phClr"><a:shade val="93000"/><a:satMod val="130000"/></a:schemeClr></a:gs><a:gs pos="100000"><a:schemeClr val="phClr"><a:shade val="94000"/><a:satMod val="135000"/></a:schemeClr></a:gs></a:gsLst><a:lin ang="16200000" scaled="0"/></a:gradFill></a:fillStyleLst><a:lnStyleLst><a:ln w="9525" cap="flat" cmpd="sng" algn="ctr"><a:solidFill><a:schemeClr val="phClr"><a:shade val="95000"/><a:satMod val="105000"/></a:schemeClr></a:solidFill><a:prstDash val="solid"/></a:ln><a:ln w="25400" cap="flat" cmpd="sng" algn="ctr"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:prstDash val="solid"/></a:ln><a:ln w="38100" cap="flat" cmpd="sng" algn="ctr"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:prstDash val="solid"/></a:ln></a:lnStyleLst><a:effectStyleLst><a:effectStyle><a:effectLst><a:outerShdw blurRad="40000" dist="20000" dir="5400000" rotWithShape="0"><a:srgbClr val="000000"><a:alpha val="38000"/></a:srgbClr></a:outerShdw></a:effectLst></a:effectStyle><a:effectStyle><a:effectLst><a:outerShdw blurRad="40000" dist="23000" dir="5400000" rotWithShape="0"><a:srgbClr val="000000"><a:alpha val="35000"/></a:srgbClr></a:outerShdw></a:effectLst></a:effectStyle><a:effectStyle><a:effectLst><a:outerShdw blurRad="40000" dist="23000" dir="5400000" rotWithShape="0"><a:srgbClr val="000000"><a:alpha val="35000"/></a:srgbClr></a:outerShdw></a:effectLst><a:scene3d><a:camera prst="orthographicFront"><a:rot lat="0" lon="0" rev="0"/></a:camera><a:lightRig rig="threePt" dir="t"><a:rot lat="0" lon="0" rev="1200000"/></a:lightRig></a:scene3d><a:sp3d><a:bevelT w="63500" h="25400"/></a:sp3d></a:effectStyle></a:effectStyleLst><a:bgFillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:gradFill rotWithShape="1"><a:gsLst><a:gs pos="0"><a:schemeClr val="phClr"><a:tint val="40000"/><a:satMod val="350000"/></a:schemeClr></a:gs><a:gs pos="40000"><a:schemeClr val="phClr"><a:tint val="45000"/><a:shade val="99000"/><a:satMod val="350000"/></a:schemeClr></a:gs><a:gs pos="100000"><a:schemeClr val="phClr"><a:shade val="20000"/><a:satMod val="255000"/></a:schemeClr></a:gs></a:gsLst><a:path path="circle"><a:fillToRect l="50000" t="-80000" r="50000" b="180000"/></a:path></a:gradFill><a:gradFill rotWithShape="1"><a:gsLst><a:gs pos="0"><a:schemeClr val="phClr"><a:tint val="80000"/><a:satMod val="300000"/></a:schemeClr></a:gs><a:gs pos="100000"><a:schemeClr val="phClr"><a:shade val="30000"/><a:satMod val="200000"/></a:schemeClr></a:gs></a:gsLst><a:path path="circle"><a:fillToRect l="50000" t="50000" r="50000" b="50000"/></a:path></a:gradFill></a:bgFillStyleLst></a:fmtScheme></a:themeElements><a:objectDefaults/><a:extraClrSchemeLst/></a:theme>)",
};
// clang-format on


// --- zip (stored entries) ---

u32 Crc32(u32 crc, const u8* data, size_t n) {
    static u32 table[256];
    static bool didInit = false;
    if (!didInit) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        didInit = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

static void Put16(str::Builder& b, u32 v) {
    b.AppendChar((char)(v & 0xff));
    b.AppendChar((char)((v >> 8) & 0xff));
}

static void Put32(str::Builder& b, u32 v) {
    Put16(b, v & 0xffff);
    Put16(b, (v >> 16) & 0xffff);
}

void ZipWriterStored::Add(Str name, Str content) {
    u32 crc = Crc32(0, (const u8*)content.s, (size_t)content.len);
    u32 offset = data.len;
    u32 size = (u32)content.len;
    u32 dosDate = ((2026 - 1980) << 9) | (1 << 5) | 1;
    u32 dosTime = 0;
    Put32(data, 0x04034b50);
    Put16(data, 20);
    Put16(data, 0);
    Put16(data, 0); // stored
    Put16(data, dosTime);
    Put16(data, dosDate);
    Put32(data, crc);
    Put32(data, size);
    Put32(data, size);
    Put16(data, (u32)name.len);
    Put16(data, 0);
    data.Append(name);
    if (size > 0) {
        data.Append(content);
    }

    Put32(centralDir, 0x02014b50);
    Put16(centralDir, 20);
    Put16(centralDir, 20);
    Put16(centralDir, 0);
    Put16(centralDir, 0);
    Put16(centralDir, dosTime);
    Put16(centralDir, dosDate);
    Put32(centralDir, crc);
    Put32(centralDir, size);
    Put32(centralDir, size);
    Put16(centralDir, (u32)name.len);
    Put16(centralDir, 0);
    Put16(centralDir, 0);
    Put16(centralDir, 0);
    Put16(centralDir, 0);
    Put32(centralDir, 0);
    Put32(centralDir, offset);
    centralDir.Append(name);
    count++;
}

Str ZipWriterStored::Finish() {
    u32 cdOffset = data.len;
    u32 cdSize = centralDir.len;
    if (cdSize > 0) {
        data.Append(Str(centralDir.els, (int)cdSize));
    }
    Put32(data, 0x06054b50);
    Put16(data, 0);
    Put16(data, 0);
    Put16(data, (u32)count);
    Put16(data, (u32)count);
    Put32(data, cdSize);
    Put32(data, cdOffset);
    Put16(data, 0);
    return data.TakeStr();
}

// --- xml helpers ---

static void XmlEsc(str::Builder& b, Str s) {
    for (int i = 0; i < s.len; i++) {
        char c = s.s[i];
        switch (c) {
            case '&':
                b.Append(StrL("&amp;"));
                break;
            case '<':
                b.Append(StrL("&lt;"));
                break;
            case '>':
                b.Append(StrL("&gt;"));
                break;
            case '"':
                b.Append(StrL("&quot;"));
                break;
            default:
                // XML 1.0 forbids most control characters
                if ((u8)c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
                    break;
                }
                b.AppendChar(c);
        }
    }
}

static TempStr ColNameTemp(int col) {
    // 0 -> A, 25 -> Z, 26 -> AA
    char buf[8]{};
    int n = 0;
    col++;
    while (col > 0 && n < 7) {
        int rem = (col - 1) % 26;
        buf[n++] = (char)('A' + rem);
        col = (col - 1) / 26;
    }
    char out[8]{};
    for (int i = 0; i < n; i++) {
        out[i] = buf[n - 1 - i];
    }
    return str::DupTemp(Str(out));
}

static TempStr CellRefTemp(int col, int row) {
    return fmt("%s%d", ColNameTemp(col), row);
}

// cell styles: indexes into cellXfs in styles.xml (keep in sync with kStylesXml)
enum XStyle {
    XsDefault = 0,
    XsHeader,
    XsWrapTop,
    XsSevCritical,
    XsSevMajor,
    XsSevMinor,
    XsSevComment,
    XsStOpen,
    XsStFixed,
    XsStRejected,
    XsTitle,
    XsSubtitle,
    XsKpiNum,
    XsKpiNumRed,
    XsKpiLabel,
    XsSection,
    XsTop,
    XsDim,
    XsCenter,
    XsLink,
    XsCheck,
    XsKpiNumGreen,
    XsBold,
    XsBoldCenter,
};

static int SeverityStyle(ReviewSeverity sev) {
    return XsSevCritical + (int)sev;
}

static int StatusStyle(ReviewStatus st) {
    return XsStOpen + (int)st;
}

// severity colors (fill, text) also used in the chart
static const char* kSevColors[kReviewSeverityCount] = {"F04438", "F97316", "EAB308", "3B82F6"};

// clang-format off
static const char* kStylesXml =
R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
R"(<fonts count="18">)"
R"(<font><sz val="11"/><color theme="1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="11"/><color theme="1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="11"/><color rgb="FFB42318"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="11"/><color rgb="FFC2410C"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="11"/><color rgb="FFA16207"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="11"/><color rgb="FF1D4ED8"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="11"/><color rgb="FF0369A1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="11"/><color rgb="FF15803D"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="11"/><color rgb="FF4B5563"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="18"/><color rgb="FF111827"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><sz val="10"/><color rgb="FF6B7280"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="22"/><color rgb="FF111827"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="22"/><color rgb="FFB42318"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><sz val="9"/><color rgb="FF6B7280"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="12"/><color rgb="FF111827"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><u/><sz val="11"/><color rgb="FF0563C1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="22"/><color rgb="FF15803D"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(<font><b/><sz val="11"/><color rgb="FFFFFFFF"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
R"(</fonts>)"
R"(<fills count="11">)"
R"(<fill><patternFill patternType="none"/></fill>)"
R"(<fill><patternFill patternType="gray125"/></fill>)"
R"(<fill><patternFill patternType="solid"><fgColor rgb="FFFEE4E2"/><bgColor indexed="64"/></patternFill></fill>)"
R"(<fill><patternFill patternType="solid"><fgColor rgb="FFFFEDD5"/><bgColor indexed="64"/></patternFill></fill>)"
R"(<fill><patternFill patternType="solid"><fgColor rgb="FFFEF9C3"/><bgColor indexed="64"/></patternFill></fill>)"
R"(<fill><patternFill patternType="solid"><fgColor rgb="FFDBEAFE"/><bgColor indexed="64"/></patternFill></fill>)"
R"(<fill><patternFill patternType="solid"><fgColor rgb="FFE0F2FE"/><bgColor indexed="64"/></patternFill></fill>)"
R"(<fill><patternFill patternType="solid"><fgColor rgb="FFDCFCE7"/><bgColor indexed="64"/></patternFill></fill>)"
R"(<fill><patternFill patternType="solid"><fgColor rgb="FFF3F4F6"/><bgColor indexed="64"/></patternFill></fill>)"
R"(<fill><patternFill patternType="solid"><fgColor rgb="FFF9FAFB"/><bgColor indexed="64"/></patternFill></fill>)"
R"(<fill><patternFill patternType="solid"><fgColor rgb="FF1F2937"/><bgColor indexed="64"/></patternFill></fill>)"
R"(</fills>)"
R"(<borders count="3">)"
R"(<border><left/><right/><top/><bottom/><diagonal/></border>)"
R"(<border><left/><right/><top/><bottom style="thin"><color rgb="FFD1D5DB"/></bottom><diagonal/></border>)"
R"(<border><left style="thin"><color rgb="FFE5E7EB"/></left><right style="thin"><color rgb="FFE5E7EB"/></right><top style="thin"><color rgb="FFE5E7EB"/></top><bottom style="thin"><color rgb="FFE5E7EB"/></bottom><diagonal/></border>)"
R"(</borders>)"
R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)"
R"(<cellXfs count="24">)"
R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)"
R"(<xf numFmtId="0" fontId="17" fillId="10" borderId="0" xfId="0" applyFont="1" applyFill="1" applyAlignment="1"><alignment vertical="center"/></xf>)"
R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment vertical="top" wrapText="1"/></xf>)"
R"(<xf numFmtId="0" fontId="2" fillId="2" borderId="0" xfId="0" applyFont="1" applyFill="1" applyAlignment="1"><alignment horizontal="center" vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="3" fillId="3" borderId="0" xfId="0" applyFont="1" applyFill="1" applyAlignment="1"><alignment horizontal="center" vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="4" fillId="4" borderId="0" xfId="0" applyFont="1" applyFill="1" applyAlignment="1"><alignment horizontal="center" vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="5" fillId="5" borderId="0" xfId="0" applyFont="1" applyFill="1" applyAlignment="1"><alignment horizontal="center" vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="6" fillId="6" borderId="0" xfId="0" applyFont="1" applyFill="1" applyAlignment="1"><alignment horizontal="center" vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="7" fillId="7" borderId="0" xfId="0" applyFont="1" applyFill="1" applyAlignment="1"><alignment horizontal="center" vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="8" fillId="8" borderId="0" xfId="0" applyFont="1" applyFill="1" applyAlignment="1"><alignment horizontal="center" vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="9" fillId="0" borderId="0" xfId="0" applyFont="1"/>)"
R"(<xf numFmtId="0" fontId="10" fillId="0" borderId="0" xfId="0" applyFont="1"/>)"
R"(<xf numFmtId="0" fontId="11" fillId="9" borderId="2" xfId="0" applyFont="1" applyFill="1" applyBorder="1" applyAlignment="1"><alignment horizontal="center" vertical="center"/></xf>)"
R"(<xf numFmtId="0" fontId="12" fillId="9" borderId="2" xfId="0" applyFont="1" applyFill="1" applyBorder="1" applyAlignment="1"><alignment horizontal="center" vertical="center"/></xf>)"
R"(<xf numFmtId="0" fontId="13" fillId="9" borderId="2" xfId="0" applyFont="1" applyFill="1" applyBorder="1" applyAlignment="1"><alignment horizontal="center" vertical="center" wrapText="1"/></xf>)"
R"(<xf numFmtId="0" fontId="14" fillId="0" borderId="1" xfId="0" applyFont="1" applyBorder="1"/>)"
R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="10" fillId="0" borderId="0" xfId="0" applyFont="1" applyAlignment="1"><alignment vertical="top" wrapText="1"/></xf>)"
R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment horizontal="center" vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="15" fillId="0" borderId="0" xfId="0" applyFont="1"/>)"
R"(<xf numFmtId="0" fontId="7" fillId="0" borderId="0" xfId="0" applyFont="1" applyAlignment="1"><alignment horizontal="center" vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="16" fillId="9" borderId="2" xfId="0" applyFont="1" applyFill="1" applyBorder="1" applyAlignment="1"><alignment horizontal="center" vertical="center"/></xf>)"
R"(<xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1" applyAlignment="1"><alignment vertical="top"/></xf>)"
R"(<xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1" applyAlignment="1"><alignment horizontal="center"/></xf>)"
R"(</cellXfs>)"
R"(<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>)"
R"(<dxfs count="0"/><tableStyles count="0" defaultTableStyle="TableStyleMedium2" defaultPivotStyle="PivotStyleLight16"/>)"
R"(</styleSheet>)";
// clang-format on

static const char* kXmlDecl = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
static const char* kNsMain = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
static const char* kNsRel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

// builds one worksheet's <sheetData>
struct SheetBuilder {
    str::Builder rows;
    int row = 0; // current 1-based row, 0 = none open
    int maxCol = 0;
    int maxRow = 0;

    void Row(int r, float heightPt = 0) {
        EndRow();
        row = r;
        if (r > maxRow) {
            maxRow = r;
        }
        if (heightPt > 0) {
            rows.Append(fmt("<row r=\"%d\" ht=\"%.1f\" customHeight=\"1\">", r, heightPt));
        } else {
            rows.Append(fmt("<row r=\"%d\">", r));
        }
    }
    void EndRow() {
        if (row > 0) {
            rows.Append(StrL("</row>"));
        }
        row = 0;
    }
    void TrackCol(int col) {
        if (col > maxCol) {
            maxCol = col;
        }
    }
    void Text(int col, Str s, int style = XsDefault) {
        TrackCol(col);
        rows.Append(fmt("<c r=\"%s\"", CellRefTemp(col, row)));
        if (style != XsDefault) {
            rows.Append(fmt(" s=\"%d\"", style));
        }
        rows.Append(StrL(" t=\"inlineStr\"><is><t xml:space=\"preserve\">"));
        XmlEsc(rows, s);
        rows.Append(StrL("</t></is></c>"));
    }
    void Num(int col, int v, int style = XsDefault) {
        TrackCol(col);
        rows.Append(fmt("<c r=\"%s\"", CellRefTemp(col, row)));
        if (style != XsDefault) {
            rows.Append(fmt(" s=\"%d\"", style));
        }
        rows.Append(fmt("><v>%d</v></c>", v));
    }
    void Empty(int col, int style) {
        TrackCol(col);
        rows.Append(fmt("<c r=\"%s\" s=\"%d\"/>", CellRefTemp(col, row), style));
    }
    TempStr DimensionTemp() {
        return fmt("A1:%s", CellRefTemp(maxCol, maxRow > 0 ? maxRow : 1));
    }
};

struct ColWidth {
    int col; // 0-based
    float width;
};

static void AppendCols(str::Builder& b, const ColWidth* cols, int n) {
    if (n == 0) {
        return;
    }
    b.Append(StrL("<cols>"));
    for (int i = 0; i < n; i++) {
        b.Append(fmt("<col min=\"%d\" max=\"%d\" width=\"%.1f\" customWidth=\"1\"/>", cols[i].col + 1, cols[i].col + 1,
                     cols[i].width));
    }
    b.Append(StrL("</cols>"));
}

static Str UserNameOr(const ReviewModel* m, Str userId) {
    return m->UserName(userId);
}

static bool IsVisibleComment(const ReviewComment* c) {
    return !c->deleted;
}

// report order: open first, then by severity, page and id
static void SortedComments(const ReviewModel* m, Vec<ReviewComment*>& out) {
    for (ReviewComment* c : m->comments) {
        if (IsVisibleComment(c)) {
            out.Append(c);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](ReviewComment* a, ReviewComment* b) {
        if (a->status != b->status) {
            return (int)a->status < (int)b->status;
        }
        if (a->severity != b->severity) {
            return (int)a->severity < (int)b->severity;
        }
        int pa = a->curPage > 0 ? a->curPage : a->page;
        int pb = b->curPage > 0 ? b->curPage : b->page;
        if (pa != pb) {
            return pa < pb;
        }
        return a->created < b->created;
    });
}

static const ReviewXlsxImage* FindImage(const ReviewXlsxArgs* args, Str key) {
    for (const ReviewXlsxImage& img : args->images) {
        if (str::Eq(img.commentKey, key) && len(img.png) > 0 && img.dx > 0 && img.dy > 0) {
            return &img;
        }
    }
    return nullptr;
}

// --- Comments sheet ---

static SeqStrings gCommentCols =
    "ID\0Severity\0Status\0Page\0Sheet\0Component\0Comment\0Reviewer\0Date\0Round\0Found in\0Location\0+1\0Replies\0Last "
    "change\0Snapshot\0";
constexpr int kCommentColsCount = 16;
constexpr int kSnapshotCol = 15;

struct PlacedImage {
    const ReviewXlsxImage* img = nullptr;
    int row = 0; // 0-based
    int cx = 0;  // EMU
    int cy = 0;
};

static TempStr BuildCommentsSheet(ReviewXlsxArgs* args, Vec<ReviewComment*>& comments, Vec<PlacedImage>& placed,
                                  int* lastRowOut) {
    ReviewModel* m = args->model;
    SheetBuilder sb;
    sb.Row(1, 22);
    for (int i = 0; i < kCommentColsCount; i++) {
        sb.Text(i, SeqStrByIndex(gCommentCols, i));
    }
    int r = 2;
    for (ReviewComment* c : comments) {
        const ReviewXlsxImage* img = FindImage(args, c->key);
        float rowHeight = 0;
        if (img) {
            // thumbnail at most 150x80 px, row height in points (0.75 pt per px) + padding
            float scale = std::min(150.f / (float)img->dx, 80.f / (float)img->dy);
            scale = std::min(scale, 1.f);
            int pxDx = (int)((float)img->dx * scale);
            int pxDy = (int)((float)img->dy * scale);
            PlacedImage pi;
            pi.img = img;
            pi.row = r - 1;
            pi.cx = pxDx * 9525;
            pi.cy = pxDy * 9525;
            placed.Append(pi);
            rowHeight = (float)pxDy * 0.75f + 6.f;
        }
        sb.Row(r, rowHeight);
        int page = c->curPage > 0 ? c->curPage : c->page;
        sb.Text(0, m->DisplayIdTemp(c), XsBold);
        sb.Text(1, ReviewSeverityName(c->severity), SeverityStyle(c->severity));
        sb.Text(2, ReviewStatusName(c->status), StatusStyle(c->status));
        sb.Num(3, page, XsCenter);
        sb.Text(4, c->sheet, XsWrapTop);
        str::Builder comp;
        comp.Append(c->component);
        if (len(c->net) > 0) {
            if (!comp.IsEmpty()) {
                comp.Append(StrL(" / "));
            }
            comp.Append(StrL("net "));
            comp.Append(c->net);
        }
        sb.Text(5, Str(comp.els, (int)comp.len), XsWrapTop);
        sb.Text(6, c->text, XsWrapTop);
        sb.Text(7, UserNameOr(m, c->userId), XsTop);
        sb.Text(8, ReviewFormatTimeTemp(c->created), XsTop);
        sb.Num(9, c->round, XsCenter);
        sb.Text(10, c->rev, XsWrapTop);
        sb.Text(11, ReviewAnchorStateLabel(c->anchorState), XsTop);
        sb.Num(12, len(c->agrees), XsCenter);
        str::Builder replies;
        for (ReviewReply* rp : c->replies) {
            if (!replies.IsEmpty()) {
                replies.AppendChar('\n');
            }
            replies.Append(fmt("%s (%s): %s", UserNameOr(m, rp->by), ReviewFormatTimeTemp(rp->time), rp->text));
        }
        sb.Text(13, Str(replies.els, (int)replies.len), XsWrapTop);
        TempStr last = {};
        if (len(c->history) > 0) {
            ReviewHistoryItem* h = c->history.Last();
            last = fmt("%s by %s, %s", h->what, UserNameOr(m, h->by), ReviewFormatTimeTemp(h->time));
        } else {
            last = fmt("created by %s", UserNameOr(m, c->userId));
        }
        sb.Text(14, last, XsDim);
        sb.Text(15, StrL(""), XsTop);
        r++;
    }
    if (comments.IsEmpty()) {
        // a table needs at least one data row
        sb.Row(r);
        sb.Text(0, StrL(""));
        sb.Text(6, StrL("No review comments yet"), XsDim);
        r++;
    }
    sb.EndRow();
    *lastRowOut = r - 1;

    static const ColWidth cols[] = {
        {0, 10},  {1, 11}, {2, 10}, {3, 7},  {4, 24},  {5, 13}, {6, 52}, {7, 18},
        {8, 17},  {9, 8},  {10, 20}, {11, 18}, {12, 6}, {13, 42}, {14, 30}, {15, 24},
    };

    str::Builder b;
    b.Append(Str(kXmlDecl));
    b.Append(fmt("<worksheet xmlns=\"%s\" xmlns:r=\"%s\">", Str(kNsMain), Str(kNsRel)));
    b.Append(StrL("<sheetPr><tabColor rgb=\"FF2563EB\"/><pageSetUpPr fitToPage=\"1\"/></sheetPr>"));
    b.Append(fmt("<dimension ref=\"%s\"/>", sb.DimensionTemp()));
    b.Append(StrL("<sheetViews><sheetView tabSelected=\"1\" zoomScale=\"100\" workbookViewId=\"0\"><pane ySplit=\"1\" "
                  "topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/><selection pane=\"bottomLeft\" "
                  "activeCell=\"A2\" sqref=\"A2\"/></sheetView></sheetViews>"));
    b.Append(StrL("<sheetFormatPr defaultRowHeight=\"15\"/>"));
    AppendCols(b, cols, (int)dimof(cols));
    b.Append(StrL("<sheetData>"));
    b.Append(Str(sb.rows.els, (int)sb.rows.len));
    b.Append(StrL("</sheetData>"));
    b.Append(StrL("<pageMargins left=\"0.4\" right=\"0.4\" top=\"0.5\" bottom=\"0.5\" header=\"0.3\" footer=\"0.3\"/>"));
    b.Append(StrL("<pageSetup orientation=\"landscape\" fitToWidth=\"1\" fitToHeight=\"0\"/>"));
    if (len(placed) > 0) {
        b.Append(StrL("<drawing r:id=\"rId2\"/>"));
    }
    b.Append(StrL("<tableParts count=\"1\"><tablePart r:id=\"rId1\"/></tableParts>"));
    b.Append(StrL("</worksheet>"));
    return str::DupTemp(Str(b.els, (int)b.len));
}

static TempStr BuildCommentsTable(int lastRow) {
    str::Builder b;
    TempStr ref = fmt("A1:%s", CellRefTemp(kCommentColsCount - 1, lastRow));
    b.Append(Str(kXmlDecl));
    b.Append(fmt("<table xmlns=\"%s\" id=\"1\" name=\"ReviewComments\" displayName=\"ReviewComments\" ref=\"%s\" "
                 "totalsRowShown=\"0\">",
                 Str(kNsMain), ref));
    b.Append(fmt("<autoFilter ref=\"%s\"/>", ref));
    b.Append(fmt("<tableColumns count=\"%d\">", kCommentColsCount));
    for (int i = 0; i < kCommentColsCount; i++) {
        b.Append(fmt("<tableColumn id=\"%d\" name=\"", i + 1));
        XmlEsc(b, SeqStrByIndex(gCommentCols, i));
        b.Append(StrL("\"/>"));
    }
    b.Append(StrL("</tableColumns>"));
    b.Append(StrL("<tableStyleInfo name=\"TableStyleMedium2\" showFirstColumn=\"0\" showLastColumn=\"0\" "
                  "showRowStripes=\"1\" showColumnStripes=\"0\"/>"));
    b.Append(StrL("</table>"));
    return str::DupTemp(Str(b.els, (int)b.len));
}

static TempStr BuildImagesDrawing(Vec<PlacedImage>& placed) {
    str::Builder b;
    b.Append(Str(kXmlDecl));
    b.Append(StrL("<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
                  "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                  "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"));
    int n = len(placed);
    for (int i = 0; i < n; i++) {
        PlacedImage& pi = placed[i];
        b.Append(fmt("<xdr:oneCellAnchor><xdr:from><xdr:col>%d</xdr:col><xdr:colOff>38100</xdr:colOff><xdr:row>%d</"
                     "xdr:row><xdr:rowOff>38100</xdr:rowOff></xdr:from><xdr:ext cx=\"%d\" cy=\"%d\"/>",
                     kSnapshotCol, pi.row, pi.cx, pi.cy));
        b.Append(fmt("<xdr:pic><xdr:nvPicPr><xdr:cNvPr id=\"%d\" name=\"Snapshot %d\"/><xdr:cNvPicPr><a:picLocks "
                     "noChangeAspect=\"1\"/></xdr:cNvPicPr></xdr:nvPicPr>",
                     i + 2, i + 1));
        b.Append(fmt("<xdr:blipFill><a:blip r:embed=\"rId%d\"/><a:stretch><a:fillRect/></a:stretch></xdr:blipFill>",
                     i + 1));
        b.Append(fmt("<xdr:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%d\" cy=\"%d\"/></a:xfrm><a:prstGeom "
                     "prst=\"rect\"><a:avLst/></a:prstGeom><a:ln w=\"9525\"><a:solidFill><a:srgbClr "
                     "val=\"D1D5DB\"/></a:solidFill></a:ln></xdr:spPr></xdr:pic><xdr:clientData/></xdr:oneCellAnchor>",
                     pi.cx, pi.cy));
    }
    b.Append(StrL("</xdr:wsDr>"));
    return str::DupTemp(Str(b.els, (int)b.len));
}

// --- Dashboard sheet ---

struct DashboardInfo {
    int sevRowFirst = 0; // rows of the severity table (for the chart)
    int sevRowLast = 0;
    int chartRow = 0; // 0-based anchor row
    int chartCol = 0;
    int counts[kReviewSeverityCount][kReviewStatusCount]{};
    int maxOpen = 0;
};

static TempStr BuildDashboardSheet(ReviewXlsxArgs* args, Vec<ReviewComment*>& comments, DashboardInfo* di) {
    ReviewModel* m = args->model;
    for (ReviewComment* c : comments) {
        di->counts[(int)c->severity][(int)c->status]++;
    }
    int openTotal = 0, fixedTotal = 0, rejectedTotal = 0;
    for (int s = 0; s < kReviewSeverityCount; s++) {
        openTotal += di->counts[s][0];
        fixedTotal += di->counts[s][1];
        rejectedTotal += di->counts[s][2];
        di->maxOpen = std::max(di->maxOpen, di->counts[s][0]);
    }
    int nSigned = 0;
    int nReviewers = 0;
    for (ReviewUser* u : m->users) {
        nReviewers++;
        if (u->signedOff) {
            nSigned++;
        }
    }

    SheetBuilder sb;
    sb.Row(1, 30);
    sb.Text(1, StrL("Design review report"), XsTitle);
    sb.Row(2);
    str::Builder sub;
    sub.Append(args->title);
    sub.Append(fmt("  \xc2\xb7  generated %s  \xc2\xb7  round %d", ReviewFormatTimeTemp(args->generatedTime), m->round));
    if (len(m->designer) > 0) {
        sub.Append(fmt("  \xc2\xb7  designer: %s", UserNameOr(m, m->designer)));
    }
    sb.Text(1, Str(sub.els, (int)sub.len), XsSubtitle);

    // KPI cards
    sb.Row(4, 34);
    sb.Num(1, di->counts[(int)ReviewSeverity::Critical][0], XsKpiNumRed);
    sb.Num(2, openTotal, XsKpiNum);
    sb.Num(3, fixedTotal, XsKpiNumGreen);
    sb.Num(4, rejectedTotal, XsKpiNum);
    sb.Text(5, fmt("%d / %d", nSigned, nReviewers), XsKpiNum);
    sb.Num(6, len(comments), XsKpiNum);
    sb.Row(5, 28);
    sb.Text(1, StrL("Open critical"), XsKpiLabel);
    sb.Text(2, StrL("Open"), XsKpiLabel);
    sb.Text(3, StrL("Fixed"), XsKpiLabel);
    sb.Text(4, StrL("Rejected"), XsKpiLabel);
    sb.Text(5, StrL("Reviewers signed off"), XsKpiLabel);
    sb.Text(6, StrL("Total comments"), XsKpiLabel);

    // by severity
    sb.Row(7, 20);
    sb.Text(1, StrL("Comments by severity"), XsSection);
    sb.Row(8);
    sb.Text(1, StrL("Severity"), XsHeader);
    sb.Text(2, StrL("Open"), XsHeader);
    sb.Text(3, StrL("Fixed"), XsHeader);
    sb.Text(4, StrL("Rejected"), XsHeader);
    sb.Text(5, StrL("Total"), XsHeader);
    int r = 9;
    di->sevRowFirst = r;
    for (int s = 0; s < kReviewSeverityCount; s++) {
        sb.Row(r);
        sb.Text(1, ReviewSeverityName((ReviewSeverity)s), SeverityStyle((ReviewSeverity)s));
        sb.Num(2, di->counts[s][0], XsCenter);
        sb.Num(3, di->counts[s][1], XsCenter);
        sb.Num(4, di->counts[s][2], XsCenter);
        sb.Num(5, di->counts[s][0] + di->counts[s][1] + di->counts[s][2], XsBoldCenter);
        r++;
    }
    di->sevRowLast = r - 1;
    sb.Row(r);
    sb.Text(1, StrL("Total"), XsBold);
    sb.Num(2, openTotal, XsBoldCenter);
    sb.Num(3, fixedTotal, XsBoldCenter);
    sb.Num(4, rejectedTotal, XsBoldCenter);
    sb.Num(5, len(comments), XsBoldCenter);
    r += 2;
    di->chartRow = 6;
    di->chartCol = 7;

    // by reviewer
    sb.Row(r, 20);
    sb.Text(1, StrL("By reviewer"), XsSection);
    r++;
    sb.Row(r);
    sb.Text(1, StrL("Reviewer"), XsHeader);
    sb.Text(2, StrL("Comments"), XsHeader);
    sb.Text(3, StrL("Open"), XsHeader);
    sb.Text(4, StrL("Pages reviewed"), XsHeader);
    sb.Text(5, StrL("Signed off"), XsHeader);
    sb.Text(6, StrL("Last active"), XsHeader);
    r++;
    int reviewerFirst = r;
    for (ReviewUser* u : m->users) {
        int nc = 0, no = 0;
        for (ReviewComment* c : comments) {
            if (str::EqI(c->userId, u->userId)) {
                nc++;
                if (c->status == ReviewStatus::Open) {
                    no++;
                }
            }
        }
        sb.Row(r);
        sb.Text(1, len(u->name) > 0 ? u->name : u->userId, XsTop);
        sb.Num(2, nc, XsCenter);
        sb.Num(3, no, XsCenter);
        sb.Num(4, len(u->reviewedPages), XsCenter);
        sb.Text(5, u->signedOff ? StrL("\xe2\x9c\x93 yes") : StrL("\xe2\x80\x94"), u->signedOff ? XsCheck : XsCenter);
        sb.Text(6, ReviewFormatTimeTemp(u->lastSeen), XsDim);
        r++;
    }
    int reviewerLast = r - 1;
    r++;

    // sheets with most open comments
    sb.Row(r, 20);
    sb.Text(1, StrL("Sheets with open comments"), XsSection);
    r++;
    sb.Row(r);
    sb.Text(1, StrL("Sheet"), XsHeader);
    sb.Text(2, StrL("Page"), XsHeader);
    sb.Text(3, StrL("Open"), XsHeader);
    sb.Text(4, StrL("Critical"), XsHeader);
    sb.Text(5, StrL("Total"), XsHeader);
    r++;
    // aggregate per page
    struct PageAgg {
        int page;
        int open;
        int critical;
        int total;
        Str sheet;
    };
    Vec<PageAgg> pages;
    for (ReviewComment* c : comments) {
        int page = c->curPage > 0 ? c->curPage : c->page;
        PageAgg* found = nullptr;
        for (PageAgg& pa : pages) {
            if (pa.page == page) {
                found = &pa;
                break;
            }
        }
        if (!found) {
            PageAgg pa{page, 0, 0, 0, c->sheet};
            pages.Append(pa);
            found = &pages.Last();
        }
        found->total++;
        if (c->status == ReviewStatus::Open) {
            found->open++;
            if (c->severity == ReviewSeverity::Critical) {
                found->critical++;
            }
        }
        if (len(found->sheet) == 0) {
            found->sheet = c->sheet;
        }
    }
    std::stable_sort(pages.begin(), pages.end(), [](const PageAgg& a, const PageAgg& b) {
        if (a.critical != b.critical) {
            return a.critical > b.critical;
        }
        if (a.open != b.open) {
            return a.open > b.open;
        }
        return a.page < b.page;
    });
    int pagesFirst = r;
    int shown = 0;
    for (PageAgg& pa : pages) {
        if (pa.open == 0 || shown >= 15) {
            continue;
        }
        sb.Row(r);
        sb.Text(1, len(pa.sheet) > 0 ? pa.sheet : StrL("(untitled)"), XsTop);
        sb.Num(2, pa.page, XsCenter);
        sb.Num(3, pa.open, XsCenter);
        sb.Num(4, pa.critical, XsCenter);
        sb.Num(5, pa.total, XsCenter);
        r++;
        shown++;
    }
    int pagesLast = r - 1;
    if (shown == 0) {
        sb.Row(r);
        sb.Text(1, StrL("No open comments \xf0\x9f\x8e\x89"), XsDim);
        r++;
    }
    if (len(args->pdfLink) > 0) {
        r++;
        sb.Row(r);
        sb.Text(1, StrL("Open the PDF"), XsLink);
    }
    int linkRow = r;
    sb.EndRow();

    static const ColWidth cols[] = {{0, 2}, {1, 30}, {2, 13}, {3, 13}, {4, 15}, {5, 20}, {6, 17}};
    str::Builder b;
    b.Append(Str(kXmlDecl));
    b.Append(fmt("<worksheet xmlns=\"%s\" xmlns:r=\"%s\">", Str(kNsMain), Str(kNsRel)));
    b.Append(StrL("<sheetPr><tabColor rgb=\"FF7C3AED\"/></sheetPr>"));
    b.Append(fmt("<dimension ref=\"%s\"/>", sb.DimensionTemp()));
    b.Append(StrL("<sheetViews><sheetView showGridLines=\"0\" workbookViewId=\"0\"/></sheetViews>"));
    b.Append(StrL("<sheetFormatPr defaultRowHeight=\"15\"/>"));
    AppendCols(b, cols, (int)dimof(cols));
    b.Append(StrL("<sheetData>"));
    b.Append(Str(sb.rows.els, (int)sb.rows.len));
    b.Append(StrL("</sheetData>"));
    // data bars
    int prio = 1;
    auto dataBar = [&](int col, int r1, int r2, const char* color) {
        if (r2 < r1) {
            return;
        }
        TempStr ref = fmt("%s:%s", CellRefTemp(col, r1), CellRefTemp(col, r2));
        b.Append(fmt("<conditionalFormatting sqref=\"%s\"><cfRule type=\"dataBar\" priority=\"%d\"><dataBar><cfvo "
                     "type=\"num\" val=\"0\"/><cfvo type=\"max\"/><color rgb=\"FF%s\"/></dataBar></cfRule></"
                     "conditionalFormatting>",
                     ref, prio++, Str(color)));
    };
    dataBar(2, di->sevRowFirst, di->sevRowLast, "F87171");
    dataBar(3, reviewerFirst, reviewerLast, "60A5FA");
    if (shown > 0) {
        dataBar(3, pagesFirst, pagesLast, "F87171");
    }
    if (len(args->pdfLink) > 0) {
        b.Append(fmt("<hyperlinks><hyperlink ref=\"%s\" r:id=\"rId2\"/></hyperlinks>", CellRefTemp(1, linkRow)));
    }
    b.Append(StrL("<pageMargins left=\"0.5\" right=\"0.5\" top=\"0.5\" bottom=\"0.5\" header=\"0.3\" footer=\"0.3\"/>"));
    b.Append(StrL("<pageSetup orientation=\"landscape\"/>"));
    b.Append(StrL("<drawing r:id=\"rId1\"/>"));
    b.Append(StrL("</worksheet>"));
    return str::DupTemp(Str(b.els, (int)b.len));
}

static TempStr BuildChart(DashboardInfo* di) {
    str::Builder b;
    b.Append(Str(kXmlDecl));
    b.Append(StrL("<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
                  "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                  "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"));
    b.Append(StrL("<c:roundedCorners val=\"0\"/><c:chart>"));
    b.Append(StrL("<c:title><c:tx><c:rich><a:bodyPr/><a:p><a:pPr><a:defRPr sz=\"1200\" b=\"1\"/></a:pPr><a:r><a:rPr "
                  "lang=\"en-US\" sz=\"1200\" b=\"1\"/><a:t>Open comments by severity</a:t></a:r></a:p></c:rich></"
                  "c:tx><c:overlay val=\"0\"/></c:title>"));
    b.Append(StrL("<c:autoTitleDeleted val=\"0\"/><c:plotArea><c:layout/>"));
    b.Append(StrL("<c:barChart><c:barDir val=\"col\"/><c:grouping val=\"clustered\"/><c:varyColors val=\"0\"/>"));
    b.Append(StrL("<c:ser><c:idx val=\"0\"/><c:order val=\"0\"/>"));
    b.Append(StrL("<c:tx><c:strRef><c:f>'Dashboard'!$C$8</c:f><c:strCache><c:ptCount val=\"1\"/><c:pt "
                  "idx=\"0\"><c:v>Open</c:v></c:pt></c:strCache></c:strRef></c:tx>"));
    b.Append(StrL("<c:spPr><a:solidFill><a:srgbClr val=\"6366F1\"/></a:solidFill></c:spPr><c:invertIfNegative "
                  "val=\"0\"/>"));
    for (int s = 0; s < kReviewSeverityCount; s++) {
        b.Append(fmt("<c:dPt><c:idx val=\"%d\"/><c:invertIfNegative val=\"0\"/><c:bubble3D val=\"0\"/><c:spPr><a:"
                     "solidFill><a:srgbClr val=\"%s\"/></a:solidFill></c:spPr></c:dPt>",
                     s, Str(kSevColors[s])));
    }
    b.Append(StrL("<c:dLbls><c:spPr><a:noFill/><a:ln><a:noFill/></a:ln></c:spPr><c:showLegendKey val=\"0\"/><c:showVal "
                  "val=\"1\"/><c:showCatName val=\"0\"/><c:showSerName val=\"0\"/><c:showPercent val=\"0\"/><c:"
                  "showBubbleSize val=\"0\"/></c:dLbls>"));
    b.Append(fmt("<c:cat><c:strRef><c:f>'Dashboard'!$B$%d:$B$%d</c:f><c:strCache><c:ptCount val=\"%d\"/>",
                 di->sevRowFirst, di->sevRowLast, kReviewSeverityCount));
    for (int s = 0; s < kReviewSeverityCount; s++) {
        b.Append(fmt("<c:pt idx=\"%d\"><c:v>%s</c:v></c:pt>", s, ReviewSeverityName((ReviewSeverity)s)));
    }
    b.Append(StrL("</c:strCache></c:strRef></c:cat>"));
    b.Append(fmt("<c:val><c:numRef><c:f>'Dashboard'!$C$%d:$C$%d</c:f><c:numCache><c:formatCode>General</"
                 "c:formatCode><c:ptCount val=\"%d\"/>",
                 di->sevRowFirst, di->sevRowLast, kReviewSeverityCount));
    for (int s = 0; s < kReviewSeverityCount; s++) {
        b.Append(fmt("<c:pt idx=\"%d\"><c:v>%d</c:v></c:pt>", s, di->counts[s][0]));
    }
    b.Append(StrL("</c:numCache></c:numRef></c:val></c:ser>"));
    b.Append(StrL("<c:gapWidth val=\"60\"/><c:axId val=\"10\"/><c:axId val=\"100\"/></c:barChart>"));
    b.Append(StrL("<c:catAx><c:axId val=\"10\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling><c:delete "
                  "val=\"0\"/><c:axPos val=\"b\"/><c:numFmt formatCode=\"General\" sourceLinked=\"0\"/><c:majorTickMark "
                  "val=\"none\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/><c:crossAx "
                  "val=\"100\"/><c:crosses val=\"autoZero\"/><c:auto val=\"1\"/><c:lblAlgn val=\"ctr\"/><c:lblOffset "
                  "val=\"100\"/><c:noMultiLvlLbl val=\"0\"/></c:catAx>"));
    int majorUnit = std::max(1, (di->maxOpen + 4) / 5);
    b.Append(fmt("<c:valAx><c:axId val=\"100\"/><c:scaling><c:orientation val=\"minMax\"/><c:min val=\"0\"/></"
                 "c:scaling><c:delete val=\"0\"/><c:axPos val=\"l\"/><c:majorGridlines><c:spPr><a:ln w=\"6350\"><a:"
                 "solidFill><a:srgbClr val=\"E5E7EB\"/></a:solidFill></a:ln></c:spPr></c:majorGridlines><c:numFmt "
                 "formatCode=\"0\" sourceLinked=\"0\"/><c:majorTickMark val=\"none\"/><c:minorTickMark "
                 "val=\"none\"/><c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"10\"/><c:crosses "
                 "val=\"autoZero\"/><c:crossBetween val=\"between\"/><c:majorUnit val=\"%d\"/></c:valAx>",
                 majorUnit));
    b.Append(StrL("</c:plotArea><c:plotVisOnly val=\"1\"/><c:dispBlanksAs val=\"gap\"/></c:chart>"));
    b.Append(StrL("<c:spPr><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill><a:ln w=\"9525\"><a:solidFill><a:"
                  "srgbClr val=\"E5E7EB\"/></a:solidFill></a:ln></c:spPr>"));
    b.Append(StrL("</c:chartSpace>"));
    return str::DupTemp(Str(b.els, (int)b.len));
}

static TempStr BuildChartDrawing(DashboardInfo* di) {
    str::Builder b;
    b.Append(Str(kXmlDecl));
    b.Append(StrL("<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
                  "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                  "xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
                  "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"));
    b.Append(fmt("<xdr:oneCellAnchor><xdr:from><xdr:col>%d</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>%d</"
                 "xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from><xdr:ext cx=\"4572000\" cy=\"2743200\"/>",
                 di->chartCol, di->chartRow));
    b.Append(StrL("<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"2\" name=\"Severity "
                  "chart\"/><xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr><xdr:xfrm><a:off x=\"0\" y=\"0\"/><a:ext "
                  "cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic><a:graphicData "
                  "uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"><c:chart r:id=\"rId1\"/></"
                  "a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:oneCellAnchor>"));
    b.Append(StrL("</xdr:wsDr>"));
    return str::DupTemp(Str(b.els, (int)b.len));
}

// --- Progress sheet ---

static TempStr BuildProgressSheet(ReviewXlsxArgs* args, Vec<ReviewComment*>& comments) {
    ReviewModel* m = args->model;
    SheetBuilder sb;
    int r = 1;
    sb.Row(r, 30);
    sb.Text(1, StrL("Review progress"), XsTitle);
    r += 2;

    // reviewers
    sb.Row(r, 20);
    sb.Text(1, StrL("Reviewers"), XsSection);
    r++;
    sb.Row(r);
    sb.Text(1, StrL("Reviewer"), XsHeader);
    sb.Text(2, StrL("Assigned pages"), XsHeader);
    sb.Text(3, StrL("Pages reviewed"), XsHeader);
    sb.Text(4, StrL("Checklist"), XsHeader);
    sb.Text(5, StrL("Signed off"), XsHeader);
    sb.Text(6, StrL("Note"), XsHeader);
    r++;
    StrVec checklist;
    if (len(m->checklist) > 0) {
        Split(&checklist, m->checklist, StrL("\n"), true);
    }
    int nChecks = len(checklist);
    for (ReviewUser* u : m->users) {
        sb.Row(r);
        sb.Text(1, len(u->name) > 0 ? u->name : u->userId, XsTop);
        Str assigned = m->AssignedPages(u->userId);
        sb.Text(2, len(assigned) > 0 ? assigned : StrL("\xe2\x80\x94"), XsCenter);
        sb.Num(3, len(u->reviewedPages), XsCenter);
        int done = 0;
        for (Str item : checklist) {
            if (u->checkedItems.Contains(item)) {
                done++;
            }
        }
        sb.Text(4, nChecks > 0 ? fmt("%d / %d", done, nChecks) : StrL("\xe2\x80\x94"), XsCenter);
        sb.Text(5, u->signedOff ? fmt("\xe2\x9c\x93 %s", ReviewFormatTimeTemp(u->signedOffTime)) : StrL("\xe2\x80\x94"),
                u->signedOff ? XsCheck : XsCenter);
        sb.Text(6, u->signOffNote, XsWrapTop);
        r++;
    }
    r++;

    // checklist matrix
    if (nChecks > 0) {
        sb.Row(r, 20);
        sb.Text(1, StrL("Checklist"), XsSection);
        r++;
        sb.Row(r);
        sb.Text(1, StrL("Item"), XsHeader);
        int col = 2;
        for (ReviewUser* u : m->users) {
            sb.Text(col++, len(u->name) > 0 ? u->name : u->userId, XsHeader);
        }
        r++;
        for (Str item : checklist) {
            sb.Row(r);
            sb.Text(1, item, XsWrapTop);
            col = 2;
            for (ReviewUser* u : m->users) {
                bool done = u->checkedItems.Contains(item);
                sb.Text(col++, done ? StrL("\xe2\x9c\x93") : StrL(""), XsCheck);
            }
            r++;
        }
        r++;
    }

    // sheets
    sb.Row(r, 20);
    sb.Text(1, StrL("Sheets"), XsSection);
    r++;
    sb.Row(r);
    sb.Text(1, StrL("Page"), XsHeader);
    sb.Text(2, StrL("Sheet"), XsHeader);
    sb.Text(3, StrL("Assigned to"), XsHeader);
    sb.Text(4, StrL("Reviewed by"), XsHeader);
    sb.Text(5, StrL("Open"), XsHeader);
    sb.Text(6, StrL("Total"), XsHeader);
    r++;
    int nPages = std::min(args->pageCount, 2000);
    for (int page = 1; page <= nPages; page++) {
        Str sheet;
        if (page - 1 < len(args->pageTitles)) {
            sheet = args->pageTitles.At(page - 1);
        }
        int open = 0, total = 0;
        for (ReviewComment* c : comments) {
            int p = c->curPage > 0 ? c->curPage : c->page;
            if (p != page) {
                continue;
            }
            total++;
            if (c->status == ReviewStatus::Open) {
                open++;
            }
            if (len(sheet) == 0) {
                sheet = c->sheet;
            }
        }
        str::Builder assigned;
        str::Builder reviewed;
        TempStr pageStr = fmt("%d", page);
        for (ReviewUser* u : m->users) {
            Str ranges = m->AssignedPages(u->userId);
            Str name = len(u->name) > 0 ? u->name : u->userId;
            if (len(ranges) > 0 && ReviewPageInRanges(ranges, page)) {
                if (!assigned.IsEmpty()) {
                    assigned.Append(StrL(", "));
                }
                assigned.Append(name);
            }
            if (u->reviewedPages.Contains(pageStr)) {
                if (!reviewed.IsEmpty()) {
                    reviewed.Append(StrL(", "));
                }
                reviewed.Append(name);
            }
        }
        sb.Row(r);
        sb.Num(1, page, XsCenter);
        sb.Text(2, sheet, XsTop);
        sb.Text(3, Str(assigned.els, (int)assigned.len), XsTop);
        if (reviewed.IsEmpty()) {
            sb.Text(4, StrL(""), XsTop);
        } else {
            sb.Text(4, fmt("\xe2\x9c\x93 %s", Str(reviewed.els, (int)reviewed.len)), XsCheck);
        }
        sb.Num(5, open, XsCenter);
        sb.Num(6, total, XsCenter);
        r++;
    }
    sb.EndRow();

    static const ColWidth cols[] = {{0, 2}, {1, 28}, {2, 26}, {3, 22}, {4, 26}, {5, 20}, {6, 24}};
    str::Builder b;
    b.Append(Str(kXmlDecl));
    b.Append(fmt("<worksheet xmlns=\"%s\" xmlns:r=\"%s\">", Str(kNsMain), Str(kNsRel)));
    b.Append(StrL("<sheetPr><tabColor rgb=\"FF16A34A\"/></sheetPr>"));
    b.Append(fmt("<dimension ref=\"%s\"/>", sb.DimensionTemp()));
    b.Append(StrL("<sheetViews><sheetView showGridLines=\"0\" workbookViewId=\"0\"/></sheetViews>"));
    b.Append(StrL("<sheetFormatPr defaultRowHeight=\"15\"/>"));
    AppendCols(b, cols, (int)dimof(cols));
    b.Append(StrL("<sheetData>"));
    b.Append(Str(sb.rows.els, (int)sb.rows.len));
    b.Append(StrL("</sheetData>"));
    b.Append(StrL("<pageMargins left=\"0.5\" right=\"0.5\" top=\"0.5\" bottom=\"0.5\" header=\"0.3\" footer=\"0.3\"/>"));
    b.Append(StrL("</worksheet>"));
    return str::DupTemp(Str(b.els, (int)b.len));
}

// --- package ---

Str ReviewWriteXlsx(ReviewXlsxArgs* args) {
    Vec<ReviewComment*> comments;
    SortedComments(args->model, comments);

    Vec<PlacedImage> placed;
    int lastRow = 1;
    TempStr commentsSheet = BuildCommentsSheet(args, comments, placed, &lastRow);
    TempStr table = BuildCommentsTable(lastRow);
    DashboardInfo di;
    TempStr dashboard = BuildDashboardSheet(args, comments, &di);
    TempStr chart = BuildChart(&di);
    TempStr chartDrawing = BuildChartDrawing(&di);
    TempStr progress = BuildProgressSheet(args, comments);
    bool hasImages = len(placed) > 0;

    ZipWriterStored zip;

    str::Builder ct;
    ct.Append(Str(kXmlDecl));
    ct.Append(StrL("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"));
    ct.Append(StrL("<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                   "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                   "<Default Extension=\"png\" ContentType=\"image/png\"/>"));
    ct.Append(StrL(
        "<Override PartName=\"/xl/workbook.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet2.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet3.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "<Override PartName=\"/xl/tables/table1.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml\"/>"
        "<Override PartName=\"/xl/drawings/drawing2.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>"
        "<Override PartName=\"/xl/charts/chart1.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/>"
        "<Override PartName=\"/docProps/core.xml\" "
        "ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
        "<Override PartName=\"/docProps/app.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"));
    if (hasImages) {
        ct.Append(StrL("<Override PartName=\"/xl/drawings/drawing1.xml\" "
                       "ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>"));
    }
    ct.Append(StrL("</Types>"));
    zip.Add(StrL("[Content_Types].xml"), Str(ct.els, (int)ct.len));

    zip.Add(StrL("_rels/.rels"),
            StrL("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                 "<Relationship Id=\"rId1\" "
                 "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
                 "Target=\"xl/workbook.xml\"/>"
                 "<Relationship Id=\"rId2\" "
                 "Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" "
                 "Target=\"docProps/core.xml\"/>"
                 "<Relationship Id=\"rId3\" "
                 "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" "
                 "Target=\"docProps/app.xml\"/>"
                 "</Relationships>"));

    TempStr isoTime = ReviewFormatTimeIsoUtcTemp(args->generatedTime);
    str::Builder core;
    core.Append(Str(kXmlDecl));
    core.Append(StrL("<cp:coreProperties "
                     "xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                     "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
                     "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"><dc:title>"));
    XmlEsc(core, fmt("Design review: %s", args->title));
    core.Append(fmt("</dc:title><dc:creator>SmartPDF</dc:creator><dcterms:created "
                    "xsi:type=\"dcterms:W3CDTF\">%s</dcterms:created><dcterms:modified "
                    "xsi:type=\"dcterms:W3CDTF\">%s</dcterms:modified></cp:coreProperties>",
                    isoTime, isoTime));
    zip.Add(StrL("docProps/core.xml"), Str(core.els, (int)core.len));
    zip.Add(StrL("docProps/app.xml"),
            StrL("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
                 "<Application>SmartPDF</Application></Properties>"));

    zip.Add(StrL("xl/workbook.xml"),
            StrL("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                 "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                 "<workbookPr/><bookViews><workbookView activeTab=\"0\"/></bookViews><sheets>"
                 "<sheet name=\"Comments\" sheetId=\"1\" r:id=\"rId1\"/>"
                 "<sheet name=\"Dashboard\" sheetId=\"2\" r:id=\"rId2\"/>"
                 "<sheet name=\"Progress\" sheetId=\"3\" r:id=\"rId3\"/>"
                 "</sheets><calcPr calcId=\"191029\"/></workbook>"));
    zip.Add(StrL("xl/_rels/workbook.xml.rels"),
            StrL("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                 "<Relationship Id=\"rId1\" "
                 "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                 "Target=\"worksheets/sheet1.xml\"/>"
                 "<Relationship Id=\"rId2\" "
                 "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                 "Target=\"worksheets/sheet2.xml\"/>"
                 "<Relationship Id=\"rId3\" "
                 "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                 "Target=\"worksheets/sheet3.xml\"/>"
                 "<Relationship Id=\"rId4\" "
                 "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
                 "Target=\"styles.xml\"/>"
                 "<Relationship Id=\"rId5\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" "
                 "Target=\"theme/theme1.xml\"/>"
                 "</Relationships>"));
    zip.Add(StrL("xl/styles.xml"), Str(kStylesXml));
    str::Builder theme;
    for (const char* part : kThemeXmlParts) {
        theme.Append(Str(part));
    }
    zip.Add(StrL("xl/theme/theme1.xml"), Str(theme.els, (int)theme.len));

    // Comments
    zip.Add(StrL("xl/worksheets/sheet1.xml"), commentsSheet);
    str::Builder rels1;
    rels1.Append(Str(kXmlDecl));
    rels1.Append(StrL("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                      "<Relationship Id=\"rId1\" "
                      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/table\" "
                      "Target=\"../tables/table1.xml\"/>"));
    if (hasImages) {
        rels1.Append(StrL("<Relationship Id=\"rId2\" "
                          "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" "
                          "Target=\"../drawings/drawing1.xml\"/>"));
    }
    rels1.Append(StrL("</Relationships>"));
    zip.Add(StrL("xl/worksheets/_rels/sheet1.xml.rels"), Str(rels1.els, (int)rels1.len));
    zip.Add(StrL("xl/tables/table1.xml"), table);
    if (hasImages) {
        zip.Add(StrL("xl/drawings/drawing1.xml"), BuildImagesDrawing(placed));
        str::Builder drels;
        drels.Append(Str(kXmlDecl));
        drels.Append(StrL("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"));
        int n = len(placed);
        for (int i = 0; i < n; i++) {
            drels.Append(fmt("<Relationship Id=\"rId%d\" "
                             "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                             "Target=\"../media/image%d.png\"/>",
                             i + 1, i + 1));
        }
        drels.Append(StrL("</Relationships>"));
        zip.Add(StrL("xl/drawings/_rels/drawing1.xml.rels"), Str(drels.els, (int)drels.len));
        for (int i = 0; i < n; i++) {
            zip.Add(fmt("xl/media/image%d.png", i + 1), placed[i].img->png);
        }
    }

    // Dashboard
    zip.Add(StrL("xl/worksheets/sheet2.xml"), dashboard);
    str::Builder rels2;
    rels2.Append(Str(kXmlDecl));
    rels2.Append(StrL("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                      "<Relationship Id=\"rId1\" "
                      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" "
                      "Target=\"../drawings/drawing2.xml\"/>"));
    if (len(args->pdfLink) > 0) {
        rels2.Append(StrL("<Relationship Id=\"rId2\" "
                          "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" "
                          "Target=\""));
        XmlEsc(rels2, args->pdfLink);
        rels2.Append(StrL("\" TargetMode=\"External\"/>"));
    }
    rels2.Append(StrL("</Relationships>"));
    zip.Add(StrL("xl/worksheets/_rels/sheet2.xml.rels"), Str(rels2.els, (int)rels2.len));
    zip.Add(StrL("xl/drawings/drawing2.xml"), chartDrawing);
    zip.Add(StrL("xl/drawings/_rels/drawing2.xml.rels"),
            StrL("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                 "<Relationship Id=\"rId1\" "
                 "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart\" "
                 "Target=\"../charts/chart1.xml\"/></Relationships>"));
    zip.Add(StrL("xl/charts/chart1.xml"), chart);

    // Progress
    zip.Add(StrL("xl/worksheets/sheet3.xml"), progress);

    return zip.Finish();
}
