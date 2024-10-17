/*
** $Id: llex.c,v 2.63.1.2 2013/08/30 15:49:41 roberto Exp roberto $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/


// #include <locale.h>
#include <string.h>

#define llex_c
#define LUA_CORE

#include "lua.h"

#include "lctype.h"
#include "ldo.h"
#include "llex.h"
#include "lobject.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lzio.h"



#define next(ls) (ls->current = zgetc(ls->z))



#define currIsNewline(ls)	(ls->current == '\n' || ls->current == '\r')


/* ORDER RESERVED */
static const char *const luaX_tokens [] = {
    "and", "break", "do", "else", "elseif",
    "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not",
    "^^", "<<", ">>>", ">>", "<<>", ">><",
    "or", "repeat",
    "return", "then", "true", "until", "while",
    "\\", "..", "...", "==", ">=", "<=", "~=", "!=", "::", "<eof>",
    "<number>", "<name>", "<string>", "?", "<eol>"
};


#define save_and_next(ls) (save(ls, ls->current), next(ls))


static l_noret lexerror (LexState *ls, const char *msg, int token);


static void save (LexState *ls, int c) {
  Mbuffer *b = ls->buff;
  if (luaZ_bufflen(b) + 1 > luaZ_sizebuffer(b)) {
    size_t newsize;
    if (luaZ_sizebuffer(b) >= MAX_SIZET/2)
      lexerror(ls, "lexical element too long", 0);
    newsize = luaZ_sizebuffer(b) * 2;
    luaZ_resizebuffer(ls->L, b, newsize);
  }
  b->buffer[luaZ_bufflen(b)++] = cast(char, c);
}


void luaX_init (lua_State *L) {
  int i;
  for (i=0; i<NUM_RESERVED; i++) {
    TString *ts = luaS_new(L, luaX_tokens[i]);
    luaS_fix(ts);  /* reserved words are never collected */
    ts->tsv.extra = cast_byte(i+1);  /* reserved word */
  }
}


const char *luaX_token2str (LexState *ls, int token) {
  if (token < FIRST_RESERVED) {  /* single-byte symbols? */
    lua_assert(token == cast(unsigned char, token));
    return (lisprint(token)) ? luaO_pushfstring(ls->L, LUA_QL("%c"), token) :
                              luaO_pushfstring(ls->L, "char(%d)", token);
  }
  else {
    const char *s = luaX_tokens[token - FIRST_RESERVED];
    if (token < TK_EOS)  /* fixed format (symbols and reserved words)? */
      return luaO_pushfstring(ls->L, LUA_QS, s);
    else  /* names, strings, and numerals */
      return s;
  }
}


void luaX_trackbraces (LexState *ls) {
  ls->braces = ls->t.token == '(' ? 1 : -1;
}


static const char *txtToken (LexState *ls, int token) {
  switch (token) {
    case TK_NAME:
    case TK_STRING:
    case TK_NUMBER:
      save(ls, '\0');
      return luaO_pushfstring(ls->L, LUA_QS, luaZ_buffer(ls->buff));
    default:
      return luaX_token2str(ls, token);
  }
}


static l_noret lexerror (LexState *ls, const char *msg, int token) {
  char buff[LUA_IDSIZE];
  luaO_chunkid(buff, getstr(ls->source), LUA_IDSIZE);
  msg = luaO_pushfstring(ls->L, "%s:%d: %s", buff, ls->linenumber, msg);
  if (token)
    luaO_pushfstring(ls->L, "%s near %s", msg, txtToken(ls, token));
  luaD_throw(ls->L, LUA_ERRSYNTAX);
}


l_noret luaX_syntaxerror (LexState *ls, const char *msg) {
  lexerror(ls, msg, ls->t.token);
}


/*
** creates a new string and anchors it in function's table so that
** it will not be collected until the end of the function's compilation
** (by that time it should be anchored in function's prototype)
*/
TString *luaX_newstring (LexState *ls, const char *str, size_t l) {
  lua_State *L = ls->L;
  TValue *o;  /* entry for `str' */
  TString *ts = luaS_newlstr(L, str, l);  /* create new string */
  setsvalue2s(L, L->top++, ts);  /* temporarily anchor it in stack */
  o = luaH_set(L, ls->fs->h, L->top - 1);
  if (ttisnil(o)) {  /* not in use yet? (see 'addK') */
    /* boolean value does not need GC barrier;
       table has no metatable, so it does not need to invalidate cache */
    setbvalue(o, 1);  /* t[string] = true */
    luaC_checkGC(L);
  }
  else {  /* string already present */
    ts = rawtsvalue(keyfromval(o));  /* re-use value previously stored */
  }
  L->top--;  /* remove string from stack */
  return ts;
}


/*
** increment line number and skips newline sequence (any of
** \n, \r, \n\r, or \r\n)
*/
static void inclinenumber (LexState *ls) {
  int old = ls->current;
  lua_assert(currIsNewline(ls));
  next(ls);  /* skip `\n' or `\r' */
  if (currIsNewline(ls) && ls->current != old)
    next(ls);  /* skip `\n\r' or `\r\n' */
  if (++ls->linenumber >= MAX_INT)
    lexerror(ls, "chunk has too many lines", 0);
  ls->atsol = 1;
}


void luaX_setinput (lua_State *L, LexState *ls, ZIO *z, TString *source,
                    int firstchar) {
  ls->decpoint = '.';
  ls->L = L;
  ls->current = firstchar;
  ls->lookahead.token = TK_EOS;  /* no look-ahead token */
  ls->z = z;
  ls->fs = NULL;
  ls->linenumber = 1;
  ls->atsol = 1;
  ls->emiteol = 0;
  ls->lastline = 1;
  ls->braces = -1;
  ls->source = source;
  ls->envn = luaS_new(L, LUA_ENV);  /* create env name */
  luaS_fix(ls->envn);  /* never collect this name */
  luaZ_resizebuffer(ls->L, ls->buff, LUA_MINBUFFER);  /* initialize buffer */
}



/*
** =======================================================
** LEXICAL ANALYZER
** =======================================================
*/



static int check_next (LexState *ls, const char *set) {
  if (ls->current == '\0' || !strchr(set, ls->current))
    return 0;
  save_and_next(ls);
  return 1;
}


/*
** change all characters 'from' in buffer to 'to'
*/
static void buffreplace (LexState *ls, char from, char to) {
  size_t n = luaZ_bufflen(ls->buff);
  char *p = luaZ_buffer(ls->buff);
  while (n--)
    if (p[n] == from) p[n] = to;
}


#if !defined(getlocaledecpoint)
#define getlocaledecpoint()	(localeconv()->decimal_point[0])
#endif


#define buff2d(b,e)	luaO_str2d(luaZ_buffer(b), luaZ_bufflen(b) - 1, e, 0)

/*
** in case of format error, try to change decimal point separator to
** the one defined in the current locale and check again
*/
static void trydecpoint (LexState *ls, SemInfo *seminfo) {
  char old = ls->decpoint;
  ls->decpoint = '.'; // getlocaledecpoint();
  buffreplace(ls, old, ls->decpoint);  /* try new decimal separator */
  if (!buff2d(ls->buff, &seminfo->r)) {
    /* format error with correct decimal point: no more options */
    buffreplace(ls, ls->decpoint, '.');  /* undo change (for error message) */
    lexerror(ls, "malformed number", TK_NUMBER);
  }
}


/* LUA_NUMBER */
/*
** this function is quite liberal in what it accepts, as 'luaO_str2d'
** will reject ill-formed numerals.
*/
static void read_numeral (LexState *ls, SemInfo *seminfo) {
  const char *expo = "Ee";
  int first = ls->current;
  lua_assert(lisdigit(ls->current));
  save_and_next(ls);
  if (first == '0' && check_next(ls, "Xx"))  /* hexadecimal? */
    expo = "Pp";
  for (;;) {
    if (check_next(ls, expo))  /* exponent part? */
      check_next(ls, "+-");  /* optional exponent sign */
    if (lisxdigit(ls->current) || ls->current == '.')
      save_and_next(ls);
    else  break;
  }
  save(ls, '\0');
  buffreplace(ls, '.', ls->decpoint);  /* follow locale for decimal point */
  if (!buff2d(ls->buff, &seminfo->r))  /* format error? */
    trydecpoint(ls, seminfo); /* try to update decimal point separator */
}


/*
** skip a sequence '[=*[' or ']=*]' and return its number of '='s or
** -1 if sequence is malformed
*/
static int skip_sep (LexState *ls) {
  int count = 0;
  int s = ls->current;
  lua_assert(s == '[' || s == ']');
  save_and_next(ls);
  while (ls->current == '=') {
    save_and_next(ls);
    count++;
  }
  return (ls->current == s) ? count : (-count) - 1;
}


static void read_long_string (LexState *ls, SemInfo *seminfo, int sep) {
  save_and_next(ls);  /* skip 2nd `[' */
  if (currIsNewline(ls))  /* string starts with a newline? */
    inclinenumber(ls);  /* skip it */
  for (;;) {
    switch (ls->current) {
      case EOZ:
        lexerror(ls, (seminfo) ? "unfinished long string" :
                                 "unfinished long comment", TK_EOS);
        break;  /* to avoid warnings */
      case ']': {
        if (skip_sep(ls) == sep) {
          save_and_next(ls);  /* skip 2nd `]' */
          goto endloop;
        }
        break;
      }
      case '\n': case '\r': {
        save(ls, '\n');
        inclinenumber(ls);
        if (!seminfo) luaZ_resetbuffer(ls->buff);  /* avoid wasting space */
        break;
      }
      default: {
        if (seminfo) save_and_next(ls);
        else next(ls);
      }
    }
  } endloop:
  if (seminfo)
    seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + (2 + sep),
                                     luaZ_bufflen(ls->buff) - 2*(2 + sep));
}


static void escerror (LexState *ls, int *c, int n, const char *msg) {
  int i;
  luaZ_resetbuffer(ls->buff);  /* prepare error message */
  save(ls, '\\');
  for (i = 0; i < n && c[i] != EOZ; i++)
    save(ls, c[i]);
  lexerror(ls, msg, TK_STRING);
}


static int readhexaesc (LexState *ls) {
  int c[3], i;  /* keep input for error message */
  int r = 0;  /* result accumulator */
  c[0] = 'x';  /* for error message */
  for (i = 1; i < 3; i++) {  /* read two hexadecimal digits */
    c[i] = next(ls);
    if (!lisxdigit(c[i]))
      escerror(ls, c, i + 1, "hexadecimal digit expected");
    r = (r << 4) + luaO_hexavalue(c[i]);
  }
  return r;
}


static int readdecesc (LexState *ls) {
  int c[3], i;
  int r = 0;  /* result accumulator */
  for (i = 0; i < 3 && lisdigit(ls->current); i++) {  /* read up to 3 digits */
    c[i] = ls->current;
    r = 10*r + c[i] - '0';
    next(ls);
  }
  if (r > UCHAR_MAX)
    escerror(ls, c, i, "decimal escape too large");
  return r;
}

static int read_unicode (LexState *ls, int del, SemInfo *seminfo) {
  // convert utf-8 sequences into p8scii
  // uses list from https://web.archive.org/web/20240217141002/https://gist.github.com/joelsgp/bf930961230731fe370e5c25ba05c5d3

  // by_prefix = {}
  // def update_prefixes(tbl):
  //     for x, i in tbl.items():
  //         if len(x) > 1:
  //             r = by_prefix
  //             last = r
  //             for y in x:
  //                 if y not in r:
  //                     r[y] = {}
  //                 last = r
  //                 r = r[y]
  //             last[y] = i

  // update_prefixes({x: i for i, x in enumerate(pscii_tbl_enc) if i >= 16})
  // update_prefixes({x.encode(): i for x, i in {"¹": 1, "²": 2, "³": 3, "⁴": 4, "⁵": 5, "⁶": 6, "⁷": 7, "⁸": 8, "ᵇ": 11, "ᶜ": 12, "ᵉ": 14, "ᶠ": 15}.items()})

  // def print_u8_handlers(x, ind=0):
  //     iprint = lambda x: print(f"{ind*2*' '}{x}")
  //     if ind>0:
  //         iprint(f"next(ls);")
  //     if isinstance(x, int):
  //         iprint(f"return {x};")
  //         return
  //     iprint("switch(ls->current) {")
  //     for start_byte, rest in x.items():
  //         iprint(f"case {start_byte}:")
  //         print_u8_handlers(rest, ind+1)
  //         if not isinstance(rest, int): iprint("  break;")
  //     iprint("}")

  // fixme: this is massive code bloat and i feel terrible

switch(ls->current) {
case 0xe2:
  next(ls);
  switch(ls->current) {
  case 0x96:
    next(ls);
    switch(ls->current) {
    case 0xae:
      next(ls);
      return 16;
    case 0xa0:
      next(ls);
      return 17;
    case 0xa1:
      next(ls);
      return 18;
    case 0xb6:
      next(ls);
      return 23;
    case 0x88:
      next(ls);
      return 128;
    case 0x92:
      next(ls);
      return 129;
    case 0x91:
      next(ls);
      return 132;
    case 0xa4:
      next(ls);
      return 152;
    case 0xa5:
      next(ls);
      return 153;
    }
    break;
  case 0x81:
    next(ls);
    switch(ls->current) {
    case 0x99:
      next(ls);
      return 19;
    case 0x98:
      next(ls);
      return 20;
    case 0xb4:
      next(ls);
      return 4;
    case 0xb5:
      next(ls);
      return 5;
    case 0xb6:
      next(ls);
      return 6;
    case 0xb7:
      next(ls);
      return 7;
    case 0xb8:
      next(ls);
      return 8;
    }
    break;
  case 0x80:
    next(ls);
    switch(ls->current) {
    case 0x96:
      next(ls);
      return 21;
    case 0xa2:
      next(ls);
      return 27;
    case 0xa6:
      next(ls);
      return 144;
    }
    break;
  case 0x97:
    next(ls);
    switch(ls->current) {
    case 0x80:
      next(ls);
      return 22;
    case 0x8b:
      next(ls);
      return 127;
    case 0x8f:
      next(ls);
      return 134;
    case 0x86:
      next(ls);
      return 143;
    case 0x9c:
      next(ls);
      return 254;
    case 0x9d:
      next(ls);
      return 255;
    }
    break;
  case 0xac:
    next(ls);
    switch(ls->current) {
    case 0x87:
      next(ls);
      switch(ls->current) {
      case 0xef:
        next(ls);
        switch(ls->current) {
        case 0xb8:
          next(ls);
          switch(ls->current) {
          case 0x8f:
            next(ls);
            return 131;
          }
          break;
        }
        break;
      }
      break;
    case 0x85:
      next(ls);
      switch(ls->current) {
      case 0xef:
        next(ls);
        switch(ls->current) {
        case 0xb8:
          next(ls);
          switch(ls->current) {
          case 0x8f:
            next(ls);
            return 139;
          }
          break;
        }
        break;
      }
      break;
    case 0x86:
      next(ls);
      switch(ls->current) {
      case 0xef:
        next(ls);
        switch(ls->current) {
        case 0xb8:
          next(ls);
          switch(ls->current) {
          case 0x8f:
            next(ls);
            return 148;
          }
          break;
        }
        break;
      }
      break;
    }
    break;
  case 0x9c:
    next(ls);
    switch(ls->current) {
    case 0xbd:
      next(ls);
      return 133;
    }
    break;
  case 0x99:
    next(ls);
    switch(ls->current) {
    case 0xa5:
      next(ls);
      return 135;
    case 0xaa:
      next(ls);
      return 141;
    }
    break;
  case 0x98:
    next(ls);
    switch(ls->current) {
    case 0x89:
      next(ls);
      return 136;
    case 0x85:
      next(ls);
      return 146;
    }
    break;
  case 0x8c:
    next(ls);
    switch(ls->current) {
    case 0x82:
      next(ls);
      return 138;
    }
    break;
  case 0x9e:
    next(ls);
    switch(ls->current) {
    case 0xa1:
      next(ls);
      switch(ls->current) {
      case 0xef:
        next(ls);
        switch(ls->current) {
        case 0xb8:
          next(ls);
          switch(ls->current) {
          case 0x8f:
            next(ls);
            return 145;
          }
          break;
        }
        break;
      }
      break;
    }
    break;
  case 0xa7:
    next(ls);
    switch(ls->current) {
    case 0x97:
      next(ls);
      return 147;
    }
    break;
  case 0x88:
    next(ls);
    switch(ls->current) {
    case 0xa7:
      next(ls);
      return 150;
    }
    break;
  case 0x9d:
    next(ls);
    switch(ls->current) {
    case 0x8e:
      next(ls);
      return 151;
    }
    break;
  }
  break;
case 0xe3:
  next(ls);
  switch(ls->current) {
  case 0x80:
    next(ls);
    switch(ls->current) {
    case 0x8c:
      next(ls);
      return 24;
    case 0x8d:
      next(ls);
      return 25;
    case 0x81:
      next(ls);
      return 28;
    case 0x82:
      next(ls);
      return 29;
    }
    break;
  case 0x82:
    next(ls);
    switch(ls->current) {
    case 0x9b:
      next(ls);
      return 30;
    case 0x9c:
      next(ls);
      return 31;
    case 0x80:
      next(ls);
      return 186;
    case 0x81:
      next(ls);
      return 187;
    case 0x82:
      next(ls);
      return 188;
    case 0x84:
      next(ls);
      return 189;
    case 0x86:
      next(ls);
      return 190;
    case 0x88:
      next(ls);
      return 191;
    case 0x89:
      next(ls);
      return 192;
    case 0x8a:
      next(ls);
      return 193;
    case 0x8b:
      next(ls);
      return 194;
    case 0x8c:
      next(ls);
      return 195;
    case 0x8d:
      next(ls);
      return 196;
    case 0x8f:
      next(ls);
      return 197;
    case 0x92:
      next(ls);
      return 198;
    case 0x93:
      next(ls);
      return 199;
    case 0x83:
      next(ls);
      return 201;
    case 0x85:
      next(ls);
      return 202;
    case 0x87:
      next(ls);
      return 203;
    case 0xa2:
      next(ls);
      return 204;
    case 0xa4:
      next(ls);
      return 205;
    case 0xa6:
      next(ls);
      return 206;
    case 0xa8:
      next(ls);
      return 207;
    case 0xaa:
      next(ls);
      return 208;
    case 0xab:
      next(ls);
      return 209;
    case 0xad:
      next(ls);
      return 210;
    case 0xaf:
      next(ls);
      return 211;
    case 0xb1:
      next(ls);
      return 212;
    case 0xb3:
      next(ls);
      return 213;
    case 0xb5:
      next(ls);
      return 214;
    case 0xb7:
      next(ls);
      return 215;
    case 0xb9:
      next(ls);
      return 216;
    case 0xbb:
      next(ls);
      return 217;
    case 0xbd:
      next(ls);
      return 218;
    case 0xbf:
      next(ls);
      return 219;
    }
    break;
  case 0x81:
    next(ls);
    switch(ls->current) {
    case 0x82:
      next(ls);
      return 154;
    case 0x84:
      next(ls);
      return 155;
    case 0x86:
      next(ls);
      return 156;
    case 0x88:
      next(ls);
      return 157;
    case 0x8a:
      next(ls);
      return 158;
    case 0x8b:
      next(ls);
      return 159;
    case 0x8d:
      next(ls);
      return 160;
    case 0x8f:
      next(ls);
      return 161;
    case 0x91:
      next(ls);
      return 162;
    case 0x93:
      next(ls);
      return 163;
    case 0x95:
      next(ls);
      return 164;
    case 0x97:
      next(ls);
      return 165;
    case 0x99:
      next(ls);
      return 166;
    case 0x9b:
      next(ls);
      return 167;
    case 0x9d:
      next(ls);
      return 168;
    case 0x9f:
      next(ls);
      return 169;
    case 0xa1:
      next(ls);
      return 170;
    case 0xa4:
      next(ls);
      return 171;
    case 0xa6:
      next(ls);
      return 172;
    case 0xa8:
      next(ls);
      return 173;
    case 0xaa:
      next(ls);
      return 174;
    case 0xab:
      next(ls);
      return 175;
    case 0xac:
      next(ls);
      return 176;
    case 0xad:
      next(ls);
      return 177;
    case 0xae:
      next(ls);
      return 178;
    case 0xaf:
      next(ls);
      return 179;
    case 0xb2:
      next(ls);
      return 180;
    case 0xb5:
      next(ls);
      return 181;
    case 0xb8:
      next(ls);
      return 182;
    case 0xbb:
      next(ls);
      return 183;
    case 0xbe:
      next(ls);
      return 184;
    case 0xbf:
      next(ls);
      return 185;
    case 0xa3:
      next(ls);
      return 200;
    }
    break;
  case 0x83:
    next(ls);
    switch(ls->current) {
    case 0x81:
      next(ls);
      return 220;
    case 0x84:
      next(ls);
      return 221;
    case 0x86:
      next(ls);
      return 222;
    case 0x88:
      next(ls);
      return 223;
    case 0x8a:
      next(ls);
      return 224;
    case 0x8b:
      next(ls);
      return 225;
    case 0x8c:
      next(ls);
      return 226;
    case 0x8d:
      next(ls);
      return 227;
    case 0x8e:
      next(ls);
      return 228;
    case 0x8f:
      next(ls);
      return 229;
    case 0x92:
      next(ls);
      return 230;
    case 0x95:
      next(ls);
      return 231;
    case 0x98:
      next(ls);
      return 232;
    case 0x9b:
      next(ls);
      return 233;
    case 0x9e:
      next(ls);
      return 234;
    case 0x9f:
      next(ls);
      return 235;
    case 0xa0:
      next(ls);
      return 236;
    case 0xa1:
      next(ls);
      return 237;
    case 0xa2:
      next(ls);
      return 238;
    case 0xa4:
      next(ls);
      return 239;
    case 0xa6:
      next(ls);
      return 240;
    case 0xa8:
      next(ls);
      return 241;
    case 0xa9:
      next(ls);
      return 242;
    case 0xaa:
      next(ls);
      return 243;
    case 0xab:
      next(ls);
      return 244;
    case 0xac:
      next(ls);
      return 245;
    case 0xad:
      next(ls);
      return 246;
    case 0xaf:
      next(ls);
      return 247;
    case 0xb2:
      next(ls);
      return 248;
    case 0xb3:
      next(ls);
      return 249;
    case 0x83:
      next(ls);
      return 250;
    case 0xa3:
      next(ls);
      return 251;
    case 0xa5:
      next(ls);
      return 252;
    case 0xa7:
      next(ls);
      return 253;
    }
    break;
  }
  break;
case 0xc2:
  next(ls);
  switch(ls->current) {
  case 0xa5:
    next(ls);
    return 26;
  case 0xb9:
    next(ls);
    return 1;
  case 0xb2:
    next(ls);
    return 2;
  case 0xb3:
    next(ls);
    return 3;
  }
  break;
case 0xf0:
  next(ls);
  switch(ls->current) {
  case 0x9f:
    next(ls);
    switch(ls->current) {
    case 0x90:
      next(ls);
      switch(ls->current) {
      case 0xb1:
        next(ls);
        return 130;
      }
      break;
    case 0x98:
      next(ls);
      switch(ls->current) {
      case 0x90:
        next(ls);
        return 140;
      }
      break;
    case 0x85:
      next(ls);
      switch(ls->current) {
      case 0xbe:
        next(ls);
        switch(ls->current) {
        case 0xef:
          next(ls);
          switch(ls->current) {
          case 0xb8:
            next(ls);
            switch(ls->current) {
            case 0x8f:
              next(ls);
              return 142;
            }
            break;
          }
          break;
        }
        break;
      }
      break;
    }
    break;
  }
  break;
case 0xec:
  next(ls);
  switch(ls->current) {
  case 0x9b:
    next(ls);
    switch(ls->current) {
    case 0x83:
      next(ls);
      return 137;
    }
    break;
  }
  break;
case 0xcb:
  next(ls);
  switch(ls->current) {
  case 0x87:
    next(ls);
    return 149;
  }
  break;
case 0xe1:
  next(ls);
  switch(ls->current) {
  case 0xb5:
    next(ls);
    switch(ls->current) {
    case 0x87:
      next(ls);
      return 11;
    case 0x89:
      next(ls);
      return 14;
    }
    break;
  case 0xb6:
    next(ls);
    switch(ls->current) {
    case 0x9c:
      next(ls);
      return 12;
    case 0xa0:
      next(ls);
      return 15;
    }
    break;
  }
  break;
  default: return -1;
}


  return -2;
}

static void read_string (LexState *ls, int del, SemInfo *seminfo) {
  save_and_next(ls);  /* keep delimiter (for error messages) */
  while (ls->current != del) {
    switch (ls->current) {
      case EOZ:
        lexerror(ls, "unfinished string", TK_EOS);
        break;  /* to avoid warnings */
      case '\n':
      case '\r':
        lexerror(ls, "unfinished string", TK_STRING);
        break;  /* to avoid warnings */
      case '\\': {  /* escape sequences */
        int c;  /* final character to be saved */
        next(ls);  /* do not save the `\' */
        switch (ls->current) {
          case '*': c = 1; goto read_save;
          case '#': c = 2; goto read_save;
          case '-': c = 3; goto read_save;
          case '|': c = 4; goto read_save;
          case '+': c = 5; goto read_save;
          case '^': c = 6; goto read_save;
          case 'a': c = '\a'; goto read_save;
          case 'b': c = '\b'; goto read_save;
          case 'f': c = '\f'; goto read_save;
          case 'n': c = '\n'; goto read_save;
          case 'r': c = '\r'; goto read_save;
          case 't': c = '\t'; goto read_save;
          case 'v': c = '\v'; goto read_save;
          case 'x': c = readhexaesc(ls); goto read_save;
          case '\n': case '\r':
            inclinenumber(ls); c = '\n'; goto only_save;
          case '\\': case '\"': case '\'':
            c = ls->current; goto read_save;
          case EOZ: goto no_save;  /* will raise an error next loop */
          case 'z': {  /* zap following span of spaces */
            next(ls);  /* skip the 'z' */
            while (lisspace(ls->current)) {
              if (currIsNewline(ls)) inclinenumber(ls);
              else next(ls);
            }
            goto no_save;
          }
          default: {
            if (!lisdigit(ls->current))
              escerror(ls, &ls->current, 1, "invalid escape sequence");
            /* digital escape \ddd */
            c = readdecesc(ls);
            goto only_save;
          }
        }
       read_save: next(ls);  /* read next character */
       only_save: save(ls, c);  /* save 'c' */
       no_save: break;
      }
      default: {
        const int p8scii_eq = read_unicode(ls, del, seminfo);
        if (p8scii_eq == -2) {
          escerror(ls, &ls->current, 1, "unknown utf-8 sequence");
        } else if (p8scii_eq != -1) {
          save(ls, p8scii_eq);
        } else {
          save_and_next(ls);
        }
      }
    }
  }
  save_and_next(ls);  /* skip delimiter */
  seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + 1,
                                   luaZ_bufflen(ls->buff) - 2);
}


static int llex (LexState *ls, SemInfo *seminfo) {
  luaZ_resetbuffer(ls->buff);
  for (;;) {
    int atsol = ls->atsol;
    ls->atsol = 0;  /* assume no longer at start of line */
    switch (ls->current) {
      case '\n': case '\r': {  /* line breaks */
        inclinenumber(ls);
        if (ls->emiteol) { ls->emiteol = 0; return TK_EOL; }
        break;
      }
      case ' ': case '\f': case '\t': case '\v': {  /* spaces */
        next(ls);
        ls->atsol = atsol;  /* still at sol if we already were. */
        break;
      }
      case '?': {  /* '?' at start of line */
        next(ls);
        if (atsol == 1) { ls->emiteol = 1; return TK_PRINT; }
        return '?';
      }
      case '-': {  /* '-' or '--' (comment) */
        next(ls);
        if (ls->current != '-') return '-';
        /* else is a comment */
        next(ls);
        if (ls->current == '[') {  /* long comment? */
          int sep = skip_sep(ls);
          luaZ_resetbuffer(ls->buff);  /* `skip_sep' may dirty the buffer */
          if (sep >= 0) {
            read_long_string(ls, NULL, sep);  /* skip long comment */
            luaZ_resetbuffer(ls->buff);  /* previous call may dirty the buff. */
            break;
          }
        }
        /* else short comment */
        while (!currIsNewline(ls) && ls->current != EOZ)
          next(ls);  /* skip until end of line (or end of file) */
        break;
      }
      case '/': {  /* '/' or '//' (short comment) */
        next(ls);
        if (ls->current != '/') return '/';
        next(ls);
        while (!currIsNewline(ls) && ls->current != EOZ)
          next(ls);  /* skip until end of line (or end of file) */
        break;
      }
      case '[': {  /* long string or simply '[' */
        int sep = skip_sep(ls);
        if (sep >= 0) {
          read_long_string(ls, seminfo, sep);
          return TK_STRING;
        }
        else if (sep == -1) return '[';
        else lexerror(ls, "invalid long string delimiter", TK_STRING);
      }
      case '\\': {
        next(ls);
        return TK_IDIV; 
      }
      case '=': {
        next(ls);
        if (ls->current != '=') return '=';
        else { next(ls); return TK_EQ; }
      }
      case '<': {
        next(ls);
        if (ls->current == '<') {
          next(ls);
          if (ls->current == '>') {
            next(ls);
            return TK_BLROT;
          }
          return TK_BLSHIFT;
        }
        if (ls->current != '=') return '<';
        else { next(ls); return TK_LE; }
      }
      case '>': {
        next(ls);
        if (ls->current == '>') {
          next(ls);
          if (ls->current == '>') {
            next(ls);
            return TK_BRSHIFT;
          }
          if (ls->current == '<') {
            next(ls);
            return TK_BRROT;
          }
          return TK_ARSHIFT;
        }
        if (ls->current != '=') return '>';
        else { next(ls); return TK_GE; }
      }
      case '^': {
        next(ls);
        if (ls->current != '^') { return '^'; }
        else { next(ls); return TK_BXOR; }
      }
      case '~': {
        next(ls);
        if (ls->current != '=') { return '~'; }
        else { next(ls); return TK_NE; }
      }
      case '!': {
        next(ls);
        if (ls->current != '=') return '!';
        else { next(ls); return TK_NE; }
      }
      case ':': {
        next(ls);
        if (ls->current != ':') return ':';
        else { next(ls); return TK_DBCOLON; }
      }
      case '"': case '\'': {  /* short literal strings */
        read_string(ls, ls->current, seminfo);
        return TK_STRING;
      }
      case '.': {  /* '.', '..', '...', or number */
        save_and_next(ls);
        if (check_next(ls, ".")) {
          if (check_next(ls, "."))
            return TK_DOTS;   /* '...' */
          else return TK_CONCAT;   /* '..' */
        }
        else if (!lisdigit(ls->current)) return '.';
        /* else go through */
      }
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        read_numeral(ls, seminfo);
        return TK_NUMBER;
      }
      case EOZ: {
        return TK_EOS;
      }
      default: {
        if (lislalpha(ls->current)) {  /* identifier or reserved word? */
          TString *ts;
          do {
            save_and_next(ls);
          } while (lislalnum(ls->current));
          ts = luaX_newstring(ls, luaZ_buffer(ls->buff),
                                  luaZ_bufflen(ls->buff));
          seminfo->ts = ts;
          if (isreserved(ts))  /* reserved word? */
            return ts->tsv.extra - 1 + FIRST_RESERVED;
          else {
            return TK_NAME;
          }
        }
        else {  /* single-char tokens (+ - / ...) */
          int c = ls->current;
          ls->braces += c == ')' ? -1 :  /* handle brace count for short if */
                        c == '(' ? ls->braces > 0 ? 1 : -1 : 0;
          next(ls);
          return c;
        }
      }
    }
  }
}


void luaX_next (LexState *ls) {
  ls->lastline = ls->linenumber;
  if (ls->lookahead.token != TK_EOS) {  /* is there a look-ahead token? */
    ls->t = ls->lookahead;  /* use this one */
    ls->lookahead.token = TK_EOS;  /* and discharge it */
  }
  else
    ls->t.token = llex(ls, &ls->t.seminfo);  /* read next token */
}


int luaX_lookahead (LexState *ls) {
  lua_assert(ls->lookahead.token == TK_EOS);
  ls->lookahead.token = llex(ls, &ls->lookahead.seminfo);
  return ls->lookahead.token;
}

