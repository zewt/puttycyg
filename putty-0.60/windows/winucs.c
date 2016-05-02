#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>

#include "putty.h"
#include "terminal.h"
#include "misc.h"

/* Character conversion arrays; they are usually taken from windows,
 * the xterm one has the four scanlines that have no unicode 2.0
 * equivalents mapped to their unicode 3.0 locations.
 */
static const WCHAR unitab_xterm_std[32] = {
    0x2666, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
    0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
    0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
    0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7, 0x0020
};

struct cp_list_item {
    char *name;
    int codepage;
    int cp_size;
    const wchar_t *cp_table;
};

static const struct cp_list_item cp_list[] = {
    {"UTF-8", CP_UTF8},
    {0, 0}
};

static void link_font(WCHAR * line_tbl, WCHAR * font_tbl, WCHAR attr);

void init_ucs(Config *cfg, struct unicode_data *ucsdata)
{
    int i, j;
    int used_dtf = 0;
    char tbuf[256];

    for (i = 0; i < 256; i++)
	tbuf[i] = i;

    /* Decide on the Line and Font codepages */
    ucsdata->line_codepage = decode_codepage(cfg->line_codepage);

    if (ucsdata->font_codepage <= 0) { 
	ucsdata->font_codepage=0; 
	ucsdata->dbcs_screenfont=0; 
    }

    if (cfg->vtmode == VT_OEMONLY) {
	ucsdata->font_codepage = 437;
	ucsdata->dbcs_screenfont = 0;
	if (ucsdata->line_codepage <= 0)
	    ucsdata->line_codepage = GetACP();
    } else if (ucsdata->line_codepage <= 0)
	ucsdata->line_codepage = ucsdata->font_codepage;

    /* Collect screen font ucs table */
    if (ucsdata->dbcs_screenfont || ucsdata->font_codepage == 0) {
	get_unitab(ucsdata->font_codepage, ucsdata->unitab_font, 2);
	for (i = 128; i < 256; i++)
	    ucsdata->unitab_font[i] = (WCHAR) (CSET_ACP + i);
    } else {
	get_unitab(ucsdata->font_codepage, ucsdata->unitab_font, 1);

	/* CP437 fonts are often broken ... */
	if (ucsdata->font_codepage == 437)
	    ucsdata->unitab_font[0] = ucsdata->unitab_font[255] = 0xFFFF;
    }
    if (cfg->vtmode == VT_XWINDOWS)
	memcpy(ucsdata->unitab_font + 1, unitab_xterm_std,
	       sizeof(unitab_xterm_std));

    /* Collect OEMCP ucs table */
    get_unitab(CP_OEMCP, ucsdata->unitab_oemcp, 1);

    /* Collect CP437 ucs table for SCO acs */
    if (cfg->vtmode == VT_OEMANSI || cfg->vtmode == VT_XWINDOWS)
	memcpy(ucsdata->unitab_scoacs, ucsdata->unitab_oemcp,
	       sizeof(ucsdata->unitab_scoacs));
    else
	get_unitab(437, ucsdata->unitab_scoacs, 1);

    /* Collect line set ucs table */
    if (ucsdata->line_codepage == ucsdata->font_codepage &&
	(ucsdata->dbcs_screenfont ||
	 cfg->vtmode == VT_POORMAN || ucsdata->font_codepage==0)) {

	/* For DBCS and POOR fonts force direct to font */
	used_dtf = 1;
	for (i = 0; i < 32; i++)
	    ucsdata->unitab_line[i] = (WCHAR) i;
	for (i = 32; i < 256; i++)
	    ucsdata->unitab_line[i] = (WCHAR) (CSET_ACP + i);
	ucsdata->unitab_line[127] = (WCHAR) 127;
    } else {
	get_unitab(ucsdata->line_codepage, ucsdata->unitab_line, 0);
    }

#if 0
    debug(
	  ("Line cp%d, Font cp%d%s\n", ucsdata->line_codepage,
	   ucsdata->font_codepage, ucsdata->dbcs_screenfont ? " DBCS" : ""));

    for (i = 0; i < 256; i += 16) {
	for (j = 0; j < 16; j++) {
	    debug(("%04x%s", ucsdata->unitab_line[i + j], j == 15 ? "" : ","));
	}
	debug(("\n"));
    }
#endif

    /* VT100 graphics - NB: Broken for non-ascii CP's */
    memcpy(ucsdata->unitab_xterm, ucsdata->unitab_line,
	   sizeof(ucsdata->unitab_xterm));
    memcpy(ucsdata->unitab_xterm + '`', unitab_xterm_std,
	   sizeof(unitab_xterm_std));
    ucsdata->unitab_xterm['_'] = ' ';

    /* Generate UCS ->line page table. */
    if (ucsdata->uni_tbl) {
	for (i = 0; i < 256; i++)
	    if (ucsdata->uni_tbl[i])
		sfree(ucsdata->uni_tbl[i]);
	sfree(ucsdata->uni_tbl);
	ucsdata->uni_tbl = 0;
    }
    if (!used_dtf) {
	for (i = 0; i < 256; i++) {
	    if (DIRECT_CHAR(ucsdata->unitab_line[i]))
		continue;
	    if (DIRECT_FONT(ucsdata->unitab_line[i]))
		continue;
	    if (!ucsdata->uni_tbl) {
		ucsdata->uni_tbl = snewn(256, char *);
		memset(ucsdata->uni_tbl, 0, 256 * sizeof(char *));
	    }
	    j = ((ucsdata->unitab_line[i] >> 8) & 0xFF);
	    if (!ucsdata->uni_tbl[j]) {
		ucsdata->uni_tbl[j] = snewn(256, char);
		memset(ucsdata->uni_tbl[j], 0, 256 * sizeof(char));
	    }
	    ucsdata->uni_tbl[j][ucsdata->unitab_line[i] & 0xFF] = i;
	}
    }

    /* Find the line control characters. */
    for (i = 0; i < 256; i++)
	if (ucsdata->unitab_line[i] < ' '
	    || (ucsdata->unitab_line[i] >= 0x7F && 
		ucsdata->unitab_line[i] < 0xA0))
	    ucsdata->unitab_ctrl[i] = i;
	else
	    ucsdata->unitab_ctrl[i] = 0xFF;

    /* Generate line->screen direct conversion links. */
    if (cfg->vtmode == VT_OEMANSI || cfg->vtmode == VT_XWINDOWS)
	link_font(ucsdata->unitab_scoacs, ucsdata->unitab_oemcp, CSET_OEMCP);

    link_font(ucsdata->unitab_line, ucsdata->unitab_font, CSET_ACP);
    link_font(ucsdata->unitab_scoacs, ucsdata->unitab_font, CSET_ACP);
    link_font(ucsdata->unitab_xterm, ucsdata->unitab_font, CSET_ACP);

    if (cfg->vtmode == VT_OEMANSI || cfg->vtmode == VT_XWINDOWS) {
	link_font(ucsdata->unitab_line, ucsdata->unitab_oemcp, CSET_OEMCP);
	link_font(ucsdata->unitab_xterm, ucsdata->unitab_oemcp, CSET_OEMCP);
    }

    if (ucsdata->dbcs_screenfont &&
	ucsdata->font_codepage != ucsdata->line_codepage) {
	/* F***ing Microsoft fonts, Japanese and Korean codepage fonts
	 * have a currency symbol at 0x5C but their unicode value is 
	 * still given as U+005C not the correct U+00A5. */
	ucsdata->unitab_line['\\'] = CSET_OEMCP + '\\';
    }

    /* Last chance, if !unicode then try poorman links. */
    if (cfg->vtmode != VT_UNICODE) {
	static const char poorman_scoacs[] = 
	    "CueaaaaceeeiiiAAE**ooouuyOUc$YPsaiounNao?++**!<>###||||++||++++++--|-+||++--|-+----++++++++##||#aBTPEsyt******EN=+><++-=... n2* ";
	static const char poorman_latin1[] =
	    " !cL.Y|S\"Ca<--R~o+23'u|.,1o>///?AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPBaaaaaaaceeeeiiiionooooo/ouuuuypy";
	static const char poorman_vt100[] = "*#****o~**+++++-----++++|****L.";

	for (i = 160; i < 256; i++)
	    if (!DIRECT_FONT(ucsdata->unitab_line[i]) &&
		ucsdata->unitab_line[i] >= 160 &&
		ucsdata->unitab_line[i] < 256) {
		ucsdata->unitab_line[i] =
		    (WCHAR) (CSET_ACP +
			     poorman_latin1[ucsdata->unitab_line[i] - 160]);
	    }
	for (i = 96; i < 127; i++)
	    if (!DIRECT_FONT(ucsdata->unitab_xterm[i]))
		ucsdata->unitab_xterm[i] =
	    (WCHAR) (CSET_ACP + poorman_vt100[i - 96]);
	for(i=128;i<256;i++) 
	    if (!DIRECT_FONT(ucsdata->unitab_scoacs[i]))
		ucsdata->unitab_scoacs[i] = 
		    (WCHAR) (CSET_ACP + poorman_scoacs[i - 128]);
    }
}

static void link_font(WCHAR * line_tbl, WCHAR * font_tbl, WCHAR attr)
{
    int font_index, line_index, i;
    for (line_index = 0; line_index < 256; line_index++) {
	if (DIRECT_FONT(line_tbl[line_index]))
	    continue;
	for(i = 0; i < 256; i++) {
	    font_index = ((32 + i) & 0xFF);
	    if (line_tbl[line_index] == font_tbl[font_index]) {
		line_tbl[line_index] = (WCHAR) (attr + font_index);
		break;
	    }
	}
    }
}

wchar_t xlat_uskbd2cyrllic(int ch)
{
    static const wchar_t cyrtab[] = {
            0,      1,       2,      3,      4,      5,      6,      7,
            8,      9,      10,     11,     12,     13,     14,     15,
            16,     17,     18,     19,     20,     21,     22,     23,
            24,     25,     26,     27,     28,     29,     30,     31,
            32,     33, 0x042d,     35,     36,     37,     38, 0x044d,
            40,     41,     42, 0x0406, 0x0431, 0x0454, 0x044e, 0x002e,
            48,     49,     50,     51,     52,     53,     54,     55,
            56,     57, 0x0416, 0x0436, 0x0411, 0x0456, 0x042e, 0x002c,
            64, 0x0424, 0x0418, 0x0421, 0x0412, 0x0423, 0x0410, 0x041f,
        0x0420, 0x0428, 0x041e, 0x041b, 0x0414, 0x042c, 0x0422, 0x0429,
        0x0417, 0x0419, 0x041a, 0x042b, 0x0415, 0x0413, 0x041c, 0x0426,
        0x0427, 0x041d, 0x042f, 0x0445, 0x0457, 0x044a,     94, 0x0404,
            96, 0x0444, 0x0438, 0x0441, 0x0432, 0x0443, 0x0430, 0x043f,
        0x0440, 0x0448, 0x043e, 0x043b, 0x0434, 0x044c, 0x0442, 0x0449,
        0x0437, 0x0439, 0x043a, 0x044b, 0x0435, 0x0433, 0x043c, 0x0446,
        0x0447, 0x043d, 0x044f, 0x0425, 0x0407, 0x042a,    126,    127
       };
    return cyrtab[ch&0x7F];
}

int check_compose_internal(int first, int second, int recurse)
{

    static const struct {
	char first, second;
	wchar_t composed;
    } composetbl[] = {
	{
	0x2b, 0x2b, 0x0023}, {
	0x41, 0x41, 0x0040}, {
	0x28, 0x28, 0x005b}, {
	0x2f, 0x2f, 0x005c}, {
	0x29, 0x29, 0x005d}, {
	0x28, 0x2d, 0x007b}, {
	0x2d, 0x29, 0x007d}, {
	0x2f, 0x5e, 0x007c}, {
	0x21, 0x21, 0x00a1}, {
	0x43, 0x2f, 0x00a2}, {
	0x43, 0x7c, 0x00a2}, {
	0x4c, 0x2d, 0x00a3}, {
	0x4c, 0x3d, 0x20a4}, {
	0x58, 0x4f, 0x00a4}, {
	0x58, 0x30, 0x00a4}, {
	0x59, 0x2d, 0x00a5}, {
	0x59, 0x3d, 0x00a5}, {
	0x7c, 0x7c, 0x00a6}, {
	0x53, 0x4f, 0x00a7}, {
	0x53, 0x21, 0x00a7}, {
	0x53, 0x30, 0x00a7}, {
	0x22, 0x22, 0x00a8}, {
	0x43, 0x4f, 0x00a9}, {
	0x43, 0x30, 0x00a9}, {
	0x41, 0x5f, 0x00aa}, {
	0x3c, 0x3c, 0x00ab}, {
	0x2c, 0x2d, 0x00ac}, {
	0x2d, 0x2d, 0x00ad}, {
	0x52, 0x4f, 0x00ae}, {
	0x2d, 0x5e, 0x00af}, {
	0x30, 0x5e, 0x00b0}, {
	0x2b, 0x2d, 0x00b1}, {
	0x32, 0x5e, 0x00b2}, {
	0x33, 0x5e, 0x00b3}, {
	0x27, 0x27, 0x00b4}, {
	0x2f, 0x55, 0x00b5}, {
	0x50, 0x21, 0x00b6}, {
	0x2e, 0x5e, 0x00b7}, {
	0x2c, 0x2c, 0x00b8}, {
	0x31, 0x5e, 0x00b9}, {
	0x4f, 0x5f, 0x00ba}, {
	0x3e, 0x3e, 0x00bb}, {
	0x31, 0x34, 0x00bc}, {
	0x31, 0x32, 0x00bd}, {
	0x33, 0x34, 0x00be}, {
	0x3f, 0x3f, 0x00bf}, {
	0x60, 0x41, 0x00c0}, {
	0x27, 0x41, 0x00c1}, {
	0x5e, 0x41, 0x00c2}, {
	0x7e, 0x41, 0x00c3}, {
	0x22, 0x41, 0x00c4}, {
	0x2a, 0x41, 0x00c5}, {
	0x41, 0x45, 0x00c6}, {
	0x2c, 0x43, 0x00c7}, {
	0x60, 0x45, 0x00c8}, {
	0x27, 0x45, 0x00c9}, {
	0x5e, 0x45, 0x00ca}, {
	0x22, 0x45, 0x00cb}, {
	0x60, 0x49, 0x00cc}, {
	0x27, 0x49, 0x00cd}, {
	0x5e, 0x49, 0x00ce}, {
	0x22, 0x49, 0x00cf}, {
	0x2d, 0x44, 0x00d0}, {
	0x7e, 0x4e, 0x00d1}, {
	0x60, 0x4f, 0x00d2}, {
	0x27, 0x4f, 0x00d3}, {
	0x5e, 0x4f, 0x00d4}, {
	0x7e, 0x4f, 0x00d5}, {
	0x22, 0x4f, 0x00d6}, {
	0x58, 0x58, 0x00d7}, {
	0x2f, 0x4f, 0x00d8}, {
	0x60, 0x55, 0x00d9}, {
	0x27, 0x55, 0x00da}, {
	0x5e, 0x55, 0x00db}, {
	0x22, 0x55, 0x00dc}, {
	0x27, 0x59, 0x00dd}, {
	0x48, 0x54, 0x00de}, {
	0x73, 0x73, 0x00df}, {
	0x60, 0x61, 0x00e0}, {
	0x27, 0x61, 0x00e1}, {
	0x5e, 0x61, 0x00e2}, {
	0x7e, 0x61, 0x00e3}, {
	0x22, 0x61, 0x00e4}, {
	0x2a, 0x61, 0x00e5}, {
	0x61, 0x65, 0x00e6}, {
	0x2c, 0x63, 0x00e7}, {
	0x60, 0x65, 0x00e8}, {
	0x27, 0x65, 0x00e9}, {
	0x5e, 0x65, 0x00ea}, {
	0x22, 0x65, 0x00eb}, {
	0x60, 0x69, 0x00ec}, {
	0x27, 0x69, 0x00ed}, {
	0x5e, 0x69, 0x00ee}, {
	0x22, 0x69, 0x00ef}, {
	0x2d, 0x64, 0x00f0}, {
	0x7e, 0x6e, 0x00f1}, {
	0x60, 0x6f, 0x00f2}, {
	0x27, 0x6f, 0x00f3}, {
	0x5e, 0x6f, 0x00f4}, {
	0x7e, 0x6f, 0x00f5}, {
	0x22, 0x6f, 0x00f6}, {
	0x3a, 0x2d, 0x00f7}, {
	0x6f, 0x2f, 0x00f8}, {
	0x60, 0x75, 0x00f9}, {
	0x27, 0x75, 0x00fa}, {
	0x5e, 0x75, 0x00fb}, {
	0x22, 0x75, 0x00fc}, {
	0x27, 0x79, 0x00fd}, {
	0x68, 0x74, 0x00fe}, {
	0x22, 0x79, 0x00ff},
	    /* Unicode extras. */
	{
	0x6f, 0x65, 0x0153}, {
	0x4f, 0x45, 0x0152},
	    /* Compose pairs from UCS */
	{
	0x41, 0x2D, 0x0100}, {
	0x61, 0x2D, 0x0101}, {
	0x43, 0x27, 0x0106}, {
	0x63, 0x27, 0x0107}, {
	0x43, 0x5E, 0x0108}, {
	0x63, 0x5E, 0x0109}, {
	0x45, 0x2D, 0x0112}, {
	0x65, 0x2D, 0x0113}, {
	0x47, 0x5E, 0x011C}, {
	0x67, 0x5E, 0x011D}, {
	0x47, 0x2C, 0x0122}, {
	0x67, 0x2C, 0x0123}, {
	0x48, 0x5E, 0x0124}, {
	0x68, 0x5E, 0x0125}, {
	0x49, 0x7E, 0x0128}, {
	0x69, 0x7E, 0x0129}, {
	0x49, 0x2D, 0x012A}, {
	0x69, 0x2D, 0x012B}, {
	0x4A, 0x5E, 0x0134}, {
	0x6A, 0x5E, 0x0135}, {
	0x4B, 0x2C, 0x0136}, {
	0x6B, 0x2C, 0x0137}, {
	0x4C, 0x27, 0x0139}, {
	0x6C, 0x27, 0x013A}, {
	0x4C, 0x2C, 0x013B}, {
	0x6C, 0x2C, 0x013C}, {
	0x4E, 0x27, 0x0143}, {
	0x6E, 0x27, 0x0144}, {
	0x4E, 0x2C, 0x0145}, {
	0x6E, 0x2C, 0x0146}, {
	0x4F, 0x2D, 0x014C}, {
	0x6F, 0x2D, 0x014D}, {
	0x52, 0x27, 0x0154}, {
	0x72, 0x27, 0x0155}, {
	0x52, 0x2C, 0x0156}, {
	0x72, 0x2C, 0x0157}, {
	0x53, 0x27, 0x015A}, {
	0x73, 0x27, 0x015B}, {
	0x53, 0x5E, 0x015C}, {
	0x73, 0x5E, 0x015D}, {
	0x53, 0x2C, 0x015E}, {
	0x73, 0x2C, 0x015F}, {
	0x54, 0x2C, 0x0162}, {
	0x74, 0x2C, 0x0163}, {
	0x55, 0x7E, 0x0168}, {
	0x75, 0x7E, 0x0169}, {
	0x55, 0x2D, 0x016A}, {
	0x75, 0x2D, 0x016B}, {
	0x55, 0x2A, 0x016E}, {
	0x75, 0x2A, 0x016F}, {
	0x57, 0x5E, 0x0174}, {
	0x77, 0x5E, 0x0175}, {
	0x59, 0x5E, 0x0176}, {
	0x79, 0x5E, 0x0177}, {
	0x59, 0x22, 0x0178}, {
	0x5A, 0x27, 0x0179}, {
	0x7A, 0x27, 0x017A}, {
	0x47, 0x27, 0x01F4}, {
	0x67, 0x27, 0x01F5}, {
	0x4E, 0x60, 0x01F8}, {
	0x6E, 0x60, 0x01F9}, {
	0x45, 0x2C, 0x0228}, {
	0x65, 0x2C, 0x0229}, {
	0x59, 0x2D, 0x0232}, {
	0x79, 0x2D, 0x0233}, {
	0x44, 0x2C, 0x1E10}, {
	0x64, 0x2C, 0x1E11}, {
	0x47, 0x2D, 0x1E20}, {
	0x67, 0x2D, 0x1E21}, {
	0x48, 0x22, 0x1E26}, {
	0x68, 0x22, 0x1E27}, {
	0x48, 0x2C, 0x1E28}, {
	0x68, 0x2C, 0x1E29}, {
	0x4B, 0x27, 0x1E30}, {
	0x6B, 0x27, 0x1E31}, {
	0x4D, 0x27, 0x1E3E}, {
	0x6D, 0x27, 0x1E3F}, {
	0x50, 0x27, 0x1E54}, {
	0x70, 0x27, 0x1E55}, {
	0x56, 0x7E, 0x1E7C}, {
	0x76, 0x7E, 0x1E7D}, {
	0x57, 0x60, 0x1E80}, {
	0x77, 0x60, 0x1E81}, {
	0x57, 0x27, 0x1E82}, {
	0x77, 0x27, 0x1E83}, {
	0x57, 0x22, 0x1E84}, {
	0x77, 0x22, 0x1E85}, {
	0x58, 0x22, 0x1E8C}, {
	0x78, 0x22, 0x1E8D}, {
	0x5A, 0x5E, 0x1E90}, {
	0x7A, 0x5E, 0x1E91}, {
	0x74, 0x22, 0x1E97}, {
	0x77, 0x2A, 0x1E98}, {
	0x79, 0x2A, 0x1E99}, {
	0x45, 0x7E, 0x1EBC}, {
	0x65, 0x7E, 0x1EBD}, {
	0x59, 0x60, 0x1EF2}, {
	0x79, 0x60, 0x1EF3}, {
	0x59, 0x7E, 0x1EF8}, {
	0x79, 0x7E, 0x1EF9},
	    /* Compatible/possibles from UCS */
	{
	0x49, 0x4A, 0x0132}, {
	0x69, 0x6A, 0x0133}, {
	0x4C, 0x4A, 0x01C7}, {
	0x4C, 0x6A, 0x01C8}, {
	0x6C, 0x6A, 0x01C9}, {
	0x4E, 0x4A, 0x01CA}, {
	0x4E, 0x6A, 0x01CB}, {
	0x6E, 0x6A, 0x01CC}, {
	0x44, 0x5A, 0x01F1}, {
	0x44, 0x7A, 0x01F2}, {
	0x64, 0x7A, 0x01F3}, {
	0x2E, 0x2E, 0x2025}, {
	0x21, 0x21, 0x203C}, {
	0x3F, 0x21, 0x2048}, {
	0x21, 0x3F, 0x2049}, {
	0x52, 0x73, 0x20A8}, {
	0x4E, 0x6F, 0x2116}, {
	0x53, 0x4D, 0x2120}, {
	0x54, 0x4D, 0x2122}, {
	0x49, 0x49, 0x2161}, {
	0x49, 0x56, 0x2163}, {
	0x56, 0x49, 0x2165}, {
	0x49, 0x58, 0x2168}, {
	0x58, 0x49, 0x216A}, {
	0x69, 0x69, 0x2171}, {
	0x69, 0x76, 0x2173}, {
	0x76, 0x69, 0x2175}, {
	0x69, 0x78, 0x2178}, {
	0x78, 0x69, 0x217A}, {
	0x31, 0x30, 0x2469}, {
	0x31, 0x31, 0x246A}, {
	0x31, 0x32, 0x246B}, {
	0x31, 0x33, 0x246C}, {
	0x31, 0x34, 0x246D}, {
	0x31, 0x35, 0x246E}, {
	0x31, 0x36, 0x246F}, {
	0x31, 0x37, 0x2470}, {
	0x31, 0x38, 0x2471}, {
	0x31, 0x39, 0x2472}, {
	0x32, 0x30, 0x2473}, {
	0x31, 0x2E, 0x2488}, {
	0x32, 0x2E, 0x2489}, {
	0x33, 0x2E, 0x248A}, {
	0x34, 0x2E, 0x248B}, {
	0x35, 0x2E, 0x248C}, {
	0x36, 0x2E, 0x248D}, {
	0x37, 0x2E, 0x248E}, {
	0x38, 0x2E, 0x248F}, {
	0x39, 0x2E, 0x2490}, {
	0x64, 0x61, 0x3372}, {
	0x41, 0x55, 0x3373}, {
	0x6F, 0x56, 0x3375}, {
	0x70, 0x63, 0x3376}, {
	0x70, 0x41, 0x3380}, {
	0x6E, 0x41, 0x3381}, {
	0x6D, 0x41, 0x3383}, {
	0x6B, 0x41, 0x3384}, {
	0x4B, 0x42, 0x3385}, {
	0x4D, 0x42, 0x3386}, {
	0x47, 0x42, 0x3387}, {
	0x70, 0x46, 0x338A}, {
	0x6E, 0x46, 0x338B}, {
	0x6D, 0x67, 0x338E}, {
	0x6B, 0x67, 0x338F}, {
	0x48, 0x7A, 0x3390}, {
	0x66, 0x6D, 0x3399}, {
	0x6E, 0x6D, 0x339A}, {
	0x6D, 0x6D, 0x339C}, {
	0x63, 0x6D, 0x339D}, {
	0x6B, 0x6D, 0x339E}, {
	0x50, 0x61, 0x33A9}, {
	0x70, 0x73, 0x33B0}, {
	0x6E, 0x73, 0x33B1}, {
	0x6D, 0x73, 0x33B3}, {
	0x70, 0x56, 0x33B4}, {
	0x6E, 0x56, 0x33B5}, {
	0x6D, 0x56, 0x33B7}, {
	0x6B, 0x56, 0x33B8}, {
	0x4D, 0x56, 0x33B9}, {
	0x70, 0x57, 0x33BA}, {
	0x6E, 0x57, 0x33BB}, {
	0x6D, 0x57, 0x33BD}, {
	0x6B, 0x57, 0x33BE}, {
	0x4D, 0x57, 0x33BF}, {
	0x42, 0x71, 0x33C3}, {
	0x63, 0x63, 0x33C4}, {
	0x63, 0x64, 0x33C5}, {
	0x64, 0x42, 0x33C8}, {
	0x47, 0x79, 0x33C9}, {
	0x68, 0x61, 0x33CA}, {
	0x48, 0x50, 0x33CB}, {
	0x69, 0x6E, 0x33CC}, {
	0x4B, 0x4B, 0x33CD}, {
	0x4B, 0x4D, 0x33CE}, {
	0x6B, 0x74, 0x33CF}, {
	0x6C, 0x6D, 0x33D0}, {
	0x6C, 0x6E, 0x33D1}, {
	0x6C, 0x78, 0x33D3}, {
	0x6D, 0x62, 0x33D4}, {
	0x50, 0x48, 0x33D7}, {
	0x50, 0x52, 0x33DA}, {
	0x73, 0x72, 0x33DB}, {
	0x53, 0x76, 0x33DC}, {
	0x57, 0x62, 0x33DD}, {
	0x66, 0x66, 0xFB00}, {
	0x66, 0x69, 0xFB01}, {
	0x66, 0x6C, 0xFB02}, {
	0x73, 0x74, 0xFB06}, {
	0, 0, 0}
    }, *c;

    int nc = -1;

    for (c = composetbl; c->first; c++) {
	if (c->first == first && c->second == second)
	    return c->composed;
    }

    if (recurse == 0) {
	nc = check_compose_internal(second, first, 1);
	if (nc == -1)
	    nc = check_compose_internal(toupper(first), toupper(second), 1);
	if (nc == -1)
	    nc = check_compose_internal(toupper(second), toupper(first), 1);
    }
    return nc;
}

int check_compose(int first, int second)
{
    return check_compose_internal(first, second, 0);
}

int decode_codepage(char *cp_name)
{
    return CP_UTF8;
}

const char *cp_name(int codepage)
{
    const struct cp_list_item *cpi, *cpno;
    static char buf[32];

    if (codepage == -1) {
	sprintf(buf, "Use font encoding");
	return buf;
    }

    if (codepage > 0 && codepage < 65536)
	sprintf(buf, "CP%03d", codepage);
    else
	*buf = 0;

    if (codepage >= 65536) {
	cpno = 0;
	for (cpi = cp_list; cpi->name; cpi++)
	    if (cpi == cp_list + (codepage - 65536)) {
		cpno = cpi;
		break;
	    }
	if (cpno)
	    for (cpi = cp_list; cpi->name; cpi++) {
		if (cpno->cp_table == cpi->cp_table)
		    return cpi->name;
	    }
    } else {
	for (cpi = cp_list; cpi->name; cpi++) {
	    if (codepage == cpi->codepage)
		return cpi->name;
	}
    }
    return buf;
}

/*
 * Return the nth code page in the list, for use in the GUI
 * configurer.
 */
const char *cp_enumerate(int index)
{
    if (index < 0 || index >= lenof(cp_list))
	return NULL;
    return cp_list[index].name;
}

void get_unitab(int codepage, wchar_t * unitab, int ftype)
{
    char tbuf[4];
    int i, max = 256, flg = MB_ERR_INVALID_CHARS;

    if (ftype)
	flg |= MB_USEGLYPHCHARS;
    if (ftype == 2)
	max = 128;

    if (codepage == CP_UTF8) {
	for (i = 0; i < max; i++)
	    unitab[i] = i;
	return;
    }

    if (codepage == CP_ACP)
	codepage = GetACP();
    else if (codepage == CP_OEMCP)
	codepage = GetOEMCP();

    if (codepage > 0 && codepage < 65536) {
	for (i = 0; i < max; i++) {
	    tbuf[0] = i;

	    if (mb_to_wc(codepage, flg, tbuf, 1, unitab + i, 1)
		!= 1)
		unitab[i] = 0xFFFD;
	}
    } else {
	int j = 256 - cp_list[codepage & 0xFFFF].cp_size;
	for (i = 0; i < max; i++)
	    unitab[i] = i;
	for (i = j; i < max; i++)
	    unitab[i] = cp_list[codepage & 0xFFFF].cp_table[i - j];
    }
}

int wc_to_mb(int codepage, int flags, wchar_t *wcstr, int wclen,
	     char *mbstr, int mblen, char *defchr, int *defused,
	     struct unicode_data *ucsdata)
{
    char *p;
    int i;
    if (ucsdata && codepage == ucsdata->line_codepage && ucsdata->uni_tbl) {
	/* Do this by array lookup if we can. */
	if (wclen < 0) {
	    for (wclen = 0; wcstr[wclen++] ;);   /* will include the NUL */
	}
	for (p = mbstr, i = 0; i < wclen; i++) {
	    wchar_t ch = wcstr[i];
	    int by;
	    char *p1;
	    if (ucsdata->uni_tbl && (p1 = ucsdata->uni_tbl[(ch >> 8) & 0xFF])
		&& (by = p1[ch & 0xFF]))
		*p++ = by;
	    else if (ch < 0x80)
		*p++ = (char) ch;
	    else if (defchr) {
		int j;
		for (j = 0; defchr[j]; j++)
		    *p++ = defchr[j];
		if (defused) *defused = 1;
	    }
#if 1
	    else
		*p++ = '.';
#endif
	    assert(p - mbstr < mblen);
	}
	return p - mbstr;
    } else
	return WideCharToMultiByte(codepage, flags, wcstr, wclen,
				   mbstr, mblen, defchr, defused);
}

int mb_to_wc(int codepage, int flags, char *mbstr, int mblen,
	     wchar_t *wcstr, int wclen)
{
    return MultiByteToWideChar(codepage, flags, mbstr, mblen, wcstr, wclen);
}

int is_dbcs_leadbyte(int codepage, char byte)
{
    return IsDBCSLeadByteEx(codepage, byte);
}
