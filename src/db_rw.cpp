// db_rw.cpp
//
// $Id: db_rw.cpp,v 1.27 2001-10-17 16:35:29 sdennis Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#ifdef STANDALONE
#undef MEMORY_BASED
#endif
#include "externs.h"

#include "mudconf.h"
#include "db.h"
#include "vattr.h"
#include "attrs.h"
#include "alloc.h"
#include "powers.h"

extern void FDECL(db_grow, (dbref));

extern struct object *db;

static int g_version;
static int g_format;
static int g_flags;

/* ---------------------------------------------------------------------------
 * getboolexp1: Get boolean subexpression from file.
 */

static BOOLEXP *getboolexp1(FILE *f)
{
    BOOLEXP *b;
    char *buff, *s;
    int c, d, anum;

    c = getc(f);
    switch (c)
    {
    case '\n':
        ungetc(c, f);
        return TRUE_BOOLEXP;

    case EOF:

        // Unexpected EOF in boolexp.
        //
        Tiny_Assert(0);
        break;

    case '(':
        b = alloc_bool("getboolexp1.openparen");
        switch (c = getc(f)) {
        case NOT_TOKEN:
            b->type = BOOLEXP_NOT;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        case INDIR_TOKEN:
            b->type = BOOLEXP_INDIR;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        case IS_TOKEN:
            b->type = BOOLEXP_IS;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        case CARRY_TOKEN:
            b->type = BOOLEXP_CARRY;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        case OWNER_TOKEN:
            b->type = BOOLEXP_OWNER;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        default:
            ungetc(c, f);
            b->sub1 = getboolexp1(f);
            if ((c = getc(f)) == '\n')
                c = getc(f);
            switch (c) {
            case AND_TOKEN:
                b->type = BOOLEXP_AND;
                break;
            case OR_TOKEN:
                b->type = BOOLEXP_OR;
                break;
            default:
                goto error;
            }
            b->sub2 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        }

    case '-':

        // obsolete NOTHING key, eat it.
        //
        while ((c = getc(f)) != '\n')
        {
            Tiny_Assert(c != EOF);
        }
        ungetc(c, f);
        return TRUE_BOOLEXP;
        break;

    case '"':
        ungetc(c, f);
        buff = alloc_lbuf("getboolexp_quoted");
        StringCopy(buff, getstring_noalloc(f, 1));
        c = fgetc(f);
        if (c == EOF) {
            free_lbuf(buff);
            return TRUE_BOOLEXP;
        }

        b = alloc_bool("getboolexp1_quoted");
        anum = mkattr(buff);
        if (anum <= 0) {
            free_bool(b);
            free_lbuf(buff);
            goto error;
        }
        free_lbuf(buff);
        b->thing = anum;

        // if last character is : then this is an attribute lock. A
        // last character of / means an eval lock.
        //
        if ((c == ':') || (c == '/'))
        {
            if (c == '/')
                b->type = BOOLEXP_EVAL;
            else
                b->type = BOOLEXP_ATR;
            buff = alloc_lbuf("getboolexp1.attr_lock");
            StringCopy(buff, getstring_noalloc(f, 1));
            b->sub1 = (BOOLEXP *)StringClone(buff);
            free_lbuf(buff);
        }
        return b;

    default: // dbref or attribute.

        ungetc(c, f);
        b = alloc_bool("getboolexp1.default");
        b->type = BOOLEXP_CONST;
        b->thing = 0;

        // This is either an attribute, eval, or constant lock. Constant locks
        // are of the form <num>, while attribute and eval locks are of the
        // form <anam-or-anum>:<string> or <aname-or-anum>/<string>
        // respectively. The characters <nl>, |, and & terminate the string.
        //
        if (Tiny_IsDigit[(unsigned int)c])
        {
            while (Tiny_IsDigit[(unsigned int)(c = getc(f))])
            {
                b->thing = b->thing * 10 + c - '0';
            }
        }
        else if (Tiny_IsAlpha[(unsigned char)c] || c == '_')
        {
            buff = alloc_lbuf("getboolexp1.atr_name");

            for (s = buff;
                 ((c = getc(f)) != EOF) && (c != '\n') &&
                 (c != ':') && (c != '/');
                 *s++ = c) ;

            if (c == EOF)
            {
                free_lbuf(buff);
                free_bool(b);
                goto error;
            }
            *s = '\0';

            // Look the name up as an attribute. If not found, create a new
            // attribute.
            //
            anum = mkattr(buff);
            if (anum <= 0)
            {
                free_bool(b);
                free_lbuf(buff);
                goto error;
            }
            free_lbuf(buff);
            b->thing = anum;
        }
        else
        {
            free_bool(b);
            goto error;
        }

        // if last character is : then this is an attribute lock. A last
        // character of / means an eval lock.
        //
        if ((c == ':') || (c == '/'))
        {
            if (c == '/')
                b->type = BOOLEXP_EVAL;
            else
                b->type = BOOLEXP_ATR;
            buff = alloc_lbuf("getboolexp1.attr_lock");
            for (  s = buff;

                   ((c = getc(f)) != EOF)
                && (c != '\n')
                && (c != ')')
                && (c != OR_TOKEN)
                && (c != AND_TOKEN);

                   *s++ = c)
            {
                // Nothing
            }
            if (c == EOF)
                goto error;
            *s++ = 0;
            b->sub1 = (BOOLEXP *)StringClone(buff);
            free_lbuf(buff);
        }
        ungetc(c, f);
        return b;
    }

error:

    // Bomb Out.
    //
    Tiny_Assert(0);
    return TRUE_BOOLEXP;
}

/* ---------------------------------------------------------------------------
 * getboolexp: Read a boolean expression from the flat file.
 */

static BOOLEXP *getboolexp(FILE *f)
{
    BOOLEXP *b;
    char c;

    b = getboolexp1(f);
    c = getc(f);
    Tiny_Assert(c == '\n');

    if (g_format == F_MUX)
    {
        if ((c = getc(f)) != '\n')
        {
            ungetc(c, f);
        }
    }
    return b;
}

/* ---------------------------------------------------------------------------
 * get_list: Read attribute list from flat file.
 */

static int get_list(FILE *f, dbref i, int new_strings)
{
    dbref atr;
    int c;
    char *buff;
#ifdef STANDALONE
    dbref aowner;
    int xflags, aflags, anum;
    char *ownp, *flagp, *buf2, *buf2p;
#endif

    buff = alloc_lbuf("get_list");
    while (1)
    {
        switch (c = getc(f))
        {
        case '>':   // read # then string
            atr = getref(f);
            if (atr > 0)
            {
                // Store the attr
                //
                atr_add_raw(i, atr,
                     (char *)getstring_noalloc(f, new_strings));
            }
            else
            {
                // Silently discard
                //
                getstring_noalloc(f, new_strings);
            }
            break;
#ifdef STANDALONE
        case ']':   // Pern 1.13 style text attribute
            StringCopy(buff, (char *)getstring_noalloc(f, new_strings));

            // Get owner number
            //
            ownp = (char *)strchr(buff, '^');
            if (!ownp)
            {
                Log.tinyprintf("Bad format in attribute on object %d" ENDLINE, i);
                free_lbuf(buff);
                return 0;
            }
            *ownp++ = '\0';

            // Get attribute flags
            //
            flagp = (char *)strchr(ownp, '^');
            if (!flagp)
            {
                Log.tinyprintf("Bad format in attribute on object %d" ENDLINE, i);
                free_lbuf(buff);
                return 0;
            }
            *flagp++ = '\0';

            // Convert Pern-style owner and flags to 2.0 format
            //
            aowner = Tiny_atol(ownp);
            xflags = Tiny_atol(flagp);
            aflags = 0;

            if (!aowner)
                aowner = NOTHING;
            if (xflags & 0x10)
                aflags |= AF_LOCK | AF_NOPROG;
            if (xflags & 0x20)
                aflags |= AF_NOPROG;

            if (!strcmp(buff, "XYXXY"))
            {
                s_Pass(i, (char *)getstring_noalloc(f, new_strings));
            }
            else
            {
                // Look up the attribute name in the attribute table. If the
                // name isn't found, create a new attribute. If the create
                // fails, try prefixing the attr name with ATR_ (Pern allows
                // attributes to start with a non-alphabetic character.)
                //
                anum = mkattr(buff);
                if (anum < 0)
                {
                    buf2 = alloc_mbuf("get_list.new_attr_name");
                    buf2p = buf2;
                    safe_mb_str((char *)"ATR_", buf2, &buf2p);
                    safe_mb_str(buff, buf2, &buf2p);
                    *buf2p = '\0';
                    anum = mkattr(buf2);
                    free_mbuf(buf2);
                }

                // MAILFOLDERS under MUX must be owned by the
                // player, not GOD
                //
                if (!strcmp(buff, "MAILFOLDERS"))
                {
                    aowner = Owner(i);
                }
                if (anum < 0)
                {
                    Log.tinyprintf("Bad attribute name '%s' on object %d, ignoring..." ENDLINE, buff, i);
                    (void)getstring_noalloc(f, new_strings);
                }
                else
                {
                    atr_add(i, anum, (char *)getstring_noalloc(f, new_strings), aowner, aflags);
                }
            }
            break;
#endif
        case '\n':  // ignore newlines. They're due to v(r).

            break;

        case '<':   // end of list

            free_lbuf(buff);
            c = getc(f);
            if (c != '\n')
            {
                ungetc(c, f);
                Log.tinyprintf("No line feed on object %d" ENDLINE, i);
                return 1;
            }
            return 1;

        default:
            Log.tinyprintf("Bad character '%c' when getting attributes on object %d" ENDLINE, c, i);

            // We've found a bad spot.  I hope things aren't too bad.
            //
            getstring_noalloc(f, new_strings);
        }
    }
}

/* ---------------------------------------------------------------------------
 * putbool_subexp: Write a boolean sub-expression to the flat file.
 */
static void putbool_subexp(FILE *f, BOOLEXP *b)
{
    ATTR *va;

    switch (b->type)
    {
    case BOOLEXP_IS:

        putc('(', f);
        putc(IS_TOKEN, f);
        putbool_subexp(f, b->sub1);
        putc(')', f);
        break;

    case BOOLEXP_CARRY:

        putc('(', f);
        putc(CARRY_TOKEN, f);
        putbool_subexp(f, b->sub1);
        putc(')', f);
        break;

    case BOOLEXP_INDIR:

        putc('(', f);
        putc(INDIR_TOKEN, f);
        putbool_subexp(f, b->sub1);
        putc(')', f);
        break;

    case BOOLEXP_OWNER:

        putc('(', f);
        putc(OWNER_TOKEN, f);
        putbool_subexp(f, b->sub1);
        putc(')', f);
        break;

    case BOOLEXP_AND:

        putc('(', f);
        putbool_subexp(f, b->sub1);
        putc(AND_TOKEN, f);
        putbool_subexp(f, b->sub2);
        putc(')', f);
        break;

    case BOOLEXP_OR:

        putc('(', f);
        putbool_subexp(f, b->sub1);
        putc(OR_TOKEN, f);
        putbool_subexp(f, b->sub2);
        putc(')', f);
        break;

    case BOOLEXP_NOT:

        putc('(', f);
        putc(NOT_TOKEN, f);
        putbool_subexp(f, b->sub1);
        putc(')', f);
        break;

    case BOOLEXP_CONST:

        putref(f, b->thing);
        break;

    case BOOLEXP_ATR:

        va = atr_num(b->thing);
        if (va)
        {
            fprintf(f, "%s:%s", va->name, (char *)b->sub1);
        }
        else
        {
            fprintf(f, "%d:%s\n", b->thing, (char *)b->sub1);
        }
        break;

    case BOOLEXP_EVAL:

        va = atr_num(b->thing);
        if (va)
        {
            fprintf(f, "%s/%s\n", va->name, (char *)b->sub1);
        }
        else
        {
            fprintf(f, "%d/%s\n", b->thing, (char *)b->sub1);
        }
        break;

    default:

        Log.tinyprintf("Unknown boolean type in putbool_subexp: %d" ENDLINE, b->type);
        break;
    }
}

/* ---------------------------------------------------------------------------
 * putboolexp: Write boolean expression to the flat file.
 */

static void putboolexp(FILE *f, BOOLEXP *b)
{
    if (b != TRUE_BOOLEXP) {
        putbool_subexp(f, b);
    }
    putc('\n', f);
}

dbref db_read(FILE *f, int *db_format, int *db_version, int *db_flags)
{
    dbref i, anum;
    int ch;
    const char *tstr;
    int read_3flags, read_money, read_timestamps, read_new_strings;
    int read_powers;
    int deduce_version, deduce_name, deduce_zone, deduce_timestamps;
    int aflags, f1, f2, f3;
    BOOLEXP *tempbool;
    char *buff;
    int len;
    int nVisualWidth;

    g_format = F_UNKNOWN;
    g_version = 0;
    g_flags = 0;

    BOOL header_gotten = FALSE;
    BOOL size_gotten = FALSE;
    BOOL nextattr_gotten = FALSE;

    BOOL read_attribs = TRUE;
    BOOL read_name = TRUE;
    BOOL read_zone = FALSE;
    BOOL read_key = TRUE;
    read_money = 1;
    read_3flags = 0;
    read_timestamps = 0;
    read_new_strings = 0;
    read_powers = 0;
    deduce_version = 1;
    deduce_zone = 1;
    deduce_name = 1;
    deduce_timestamps = 1;
#ifdef STANDALONE
    Log.WriteString("Reading ");
    Log.Flush();
    int iDotCounter = 0;
#endif
    db_free();
    for (i = 0;; i++)
    {
#ifdef STANDALONE
        if (!iDotCounter)
        {
            iDotCounter = 100;
            fputc('.', stderr);
            fflush(stderr);
        }
        iDotCounter--;
#endif

        switch (ch = getc(f))
        {
        case '-':   // Misc tag
            switch (ch = getc(f))
            {
            case 'R':   // Record number of players
                mudstate.record_players = getref(f);
                break;
            default:
                getstring_noalloc(f, 0);
            }
            break;

        case '+':

            // MUX header
            //
            switch (ch = getc(f))
            {
            case 'X':

                // MUX VERSION
                //
                if (header_gotten)
                {
                    Log.tinyprintf(ENDLINE "Duplicate MUX version header entry at object %d, ignored." ENDLINE, i);
                    tstr = getstring_noalloc(f, 0);
                    break;
                }
                header_gotten = TRUE;
                deduce_version = 0;
                g_format = F_MUX;
                g_version = getref(f);
                Tiny_Assert((g_version & MANDFLAGS) == MANDFLAGS);

                // Otherwise extract feature flags
                //
                if (g_version & V_GDBM)
                {
                    read_attribs = FALSE;
                    read_name = !(g_version & V_ATRNAME);
                }
                read_key = !(g_version & V_ATRKEY);
                read_money = !(g_version & V_ATRMONEY);
                read_3flags = (g_version & V_3FLAGS);
                read_powers = (g_version & V_POWERS);
                read_new_strings = (g_version & V_QUOTED);
                g_flags = g_version & ~V_MASK;

                g_version &= V_MASK;
                deduce_name = 0;
                deduce_version = 0;
                deduce_zone = 0;
                break;

            case 'S':   // SIZE
                if (size_gotten)
                {
                    Log.tinyprintf(ENDLINE "Duplicate size entry at object %d, ignored." ENDLINE, i);
                    tstr = getstring_noalloc(f, 0);
                }
                else
                {
                    mudstate.min_size = getref(f);
                    size_gotten = TRUE;
                }
                break;

            case 'A':   // USER-NAMED ATTRIBUTE
                anum = getref(f);
                tstr = getstring_noalloc(f, read_new_strings);
                if (Tiny_IsDigit[(unsigned char)*tstr])
                {
                    aflags = 0;
                    while (Tiny_IsDigit[(unsigned char)*tstr])
                    {
                        aflags = (aflags * 10) + (*tstr++ - '0');
                    }
                    tstr++; // skip ':'
                }
                else
                {
                    aflags = mudconf.vattr_flags;
                }
                {
                    int nName;
                    BOOL bValid;
                    char *pName = MakeCanonicalAttributeName(tstr, &nName, &bValid);
                    if (bValid)
                    {
                        vattr_define_LEN(pName, nName, anum, aflags);
                    }
                }
                break;

            case 'F':   // OPEN USER ATTRIBUTE SLOT
                anum = getref(f);
                break;

            case 'N':   // NEXT ATTR TO ALLOC WHEN NO FREELIST
                if (nextattr_gotten)
                {
                    Log.tinyprintf(ENDLINE "Duplicate next free vattr entry at object %d, ignored." ENDLINE, i);
                    tstr = getstring_noalloc(f, 0);
                }
                else
                {
                    mudstate.attr_next = getref(f);
                    nextattr_gotten = TRUE;
                }
                break;

            default:
                Log.tinyprintf(ENDLINE "Unexpected character '%c' in MUX header near object #%d, ignored." ENDLINE, ch, i);
                tstr = getstring_noalloc(f, 0);
            }
            break;

        case '!':   // MUX entry
            if (deduce_version)
            {
                g_format = F_MUX;
                g_version = 1;
                deduce_name = 0;
                deduce_zone = 0;
                deduce_version = 0;
            }
            else if (deduce_zone)
            {
                deduce_zone = 0;
                read_zone = FALSE;
            }
            i = getref(f);
            db_grow(i + 1);

            if (read_name)
            {
                tstr = getstring_noalloc(f, read_new_strings);
                if (deduce_name)
                {
                    if (Tiny_IsDigit[(unsigned char)*tstr])
                    {
                        read_name = FALSE;
                        s_Location(i, Tiny_atol(tstr));
                    }
                    else
                    {
                        buff = alloc_lbuf("dbread.s_Name");
                        len = ANSI_TruncateToField(tstr, MBUF_SIZE, buff, MBUF_SIZE, &nVisualWidth, ANSI_ENDGOAL_NORMAL);
                        s_Name(i, buff);
                        free_lbuf(buff);

                        s_Location(i, getref(f));
                    }
                    deduce_name = 0;
                }
                else
                {
                    buff = alloc_lbuf("dbread.s_Name");
                    len = ANSI_TruncateToField(tstr, MBUF_SIZE, buff, MBUF_SIZE, &nVisualWidth, ANSI_ENDGOAL_NORMAL);
                    s_Name(i, buff);
                    free_lbuf(buff);

                    s_Location(i, getref(f));
                }
            }
            else
            {
                s_Location(i, getref(f));
            }

            // ZONE
            //
            if (read_zone)
            {
                int zone = getref(f);
                if (zone < NOTHING)
                {
                    zone = NOTHING;
                }
                s_Zone(i, zone);
            }
#if 0
            else
            {
                s_Zone(i, NOTHING);
            }
#endif

            // CONTENTS and EXITS
            //
            s_Contents(i, getref(f));
            s_Exits(i, getref(f));

            // LINK
            //
            s_Link(i, getref(f));

            // NEXT
            //
            s_Next(i, getref(f));

            // LOCK
            //
            if (read_key)
            {
                tempbool = getboolexp(f);
                atr_add_raw(i, A_LOCK,
                unparse_boolexp_quiet(1, tempbool));
                free_boolexp(tempbool);
            }

            // OWNER
            //
            s_Owner(i, getref(f));

            // PARENT
            //
            s_Parent(i, getref(f));

            // PENNIES
            //
            if (read_money)
            {
                s_Pennies(i, getref(f));
            }

            // FLAGS
            //
            f1 = getref(f);
            f2 = getref(f);

            if (read_3flags)
                f3 = getref(f);
            else
                f3 = 0;

            s_Flags(i, f1);
            s_Flags2(i, f2);
            s_Flags3(i, f3);

            if (read_powers)
            {
                f1 = getref(f);
                f2 = getref(f);
                s_Powers(i, f1);
                s_Powers2(i, f2);
            }

            // ATTRIBUTES
            //
            if (read_attribs)
            {
                if (!get_list(f, i, read_new_strings))
                {
                    Log.tinyprintf(ENDLINE "Error reading attrs for object #%d" ENDLINE, i);
                    return -1;
                }
            }

            // check to see if it's a player
            //
            if (isPlayer(i))
            {
                c_Connected(i);
            }
            break;

        case '*':   // EOF marker
            tstr = getstring_noalloc(f, 0);
            if (strncmp(tstr, "**END OF DUMP***", 16))
            {
                Log.tinyprintf(ENDLINE "Bad EOF marker at object #%d" ENDLINE, i);
                return -1;
            }
            else
            {
#ifdef STANDALONE
                Log.WriteString(ENDLINE);
                Log.Flush();
#endif
                *db_version = g_version;
                *db_format = g_format;
                *db_flags = g_flags;
#ifndef STANDALONE
                load_player_names();
#endif
                return mudstate.db_top;
            }

        case EOF:
            Log.tinyprintf(ENDLINE "Unexpected end of file near object #%d" ENDLINE, i);
            return -1;

        default:
            if (Tiny_IsPrint[(unsigned char)ch])
            {
                Log.tinyprintf(ENDLINE "Illegal character '%c' near object #%d" ENDLINE, ch, i);
            }
            else
            {
                Log.tinyprintf(ENDLINE "Illegal character 0x%02x near object #%d" ENDLINE, ch, i);
            }
            return -1;
        }
    }
}

static int db_write_object(FILE *f, dbref i, int db_format, int flags)
{
#ifndef STANDALONE
    ATTR *a;
#endif

    char *got, *as;
    dbref aowner;
    int ca, aflags, j;
    BOOLEXP *tempbool;

    if (!(flags & V_ATRNAME))
    {
        putstring(f, Name(i));
    }
    putref(f, Location(i));
    putref(f, Zone(i));
    putref(f, Contents(i));
    putref(f, Exits(i));
    putref(f, Link(i));
    putref(f, Next(i));
    if (!(flags & V_ATRKEY))
    {
        got = atr_get(i, A_LOCK, &aowner, &aflags);
        tempbool = parse_boolexp(GOD, got, 1);
        free_lbuf(got);
        putboolexp(f, tempbool);
        if (tempbool)
        {
            free_boolexp(tempbool);
        }
    }
    putref(f, Owner(i));
    putref(f, Parent(i));
    if (!(flags & V_ATRMONEY))
    {
        putref(f, Pennies(i));
    }
    putref(f, Flags(i));
    putref(f, Flags2(i));
    if (flags & V_3FLAGS)
    {
        putref(f, Flags3(i));
    }
    if (flags & V_POWERS)
    {
        putref(f, Powers(i));
        putref(f, Powers2(i));
    }

    // Write the attribute list.
    //
    if ((!(flags & V_GDBM)))
    {
        char buf[SBUF_SIZE];
        buf[0] = '>';
        for (ca = atr_head(i, &as); ca; ca = atr_next(&as))
        {
#ifndef STANDALONE
            a = atr_num(ca);
            if (!a)
            {
                continue;
            }
            j = a->number;
#else
            j = ca;
#endif
            if (j < A_USER_START)
            {
                switch (j)
                {
                case A_NAME:
                    if (!(flags & V_ATRNAME))
                    {
                        continue;
                    }
                    break;

                case A_LOCK:
                    if (!(flags & V_ATRKEY))
                    {
                        continue;
                    }
                    break;

                case A_LIST:
                case A_MONEY:
                    continue;
                }
            }
            // Format is: ">%d\n", j
            //
            got = atr_get_raw(i, j);
            int n = Tiny_ltoa(j, buf+1) + 1;
            buf[n++] = '\n';
            fwrite(buf, sizeof(char), n, f);
            putstring(f, got);
        }
        fwrite("<\n", sizeof(char), 2, f);
    }
    return 0;
}

dbref db_write(FILE *f, int format, int version)
{
    dbref i;
    int flags;
    VATTR *vp;

#ifndef MEMORY_BASED
    al_store();
#endif

    switch (format)
    {
    case F_MUX:
        flags = version;
        break;
    default:
        Log.WriteString("Can only write MUX format." ENDLINE);
        return -1;
    }
#ifdef STANDALONE
    Log.WriteString("Writing ");
    Log.Flush();
#endif
    i = mudstate.attr_next;
    fprintf(f, "+X%d\n+S%d\n+N%d\n", flags, mudstate.db_top, i);
    fprintf(f, "-R%d\n", mudstate.record_players);

    // Dump user-named attribute info.
    //
    vp = vattr_first();
    char Buffer[LBUF_SIZE];
    Buffer[0] = '+';
    Buffer[1] = 'A';
    while (vp != NULL)
    {
        if (!(vp->flags & AF_DELETED))
        {
            // Format is: "+A%d\n\"%d:%s\"\n", vp->number, vp->flags, vp->name
            //
            char *pBuffer = Buffer+2;
            pBuffer += Tiny_ltoa(vp->number, pBuffer);
            *pBuffer++ = '\n';
            *pBuffer++ = '"';
            pBuffer += Tiny_ltoa(vp->flags, pBuffer);
            *pBuffer++ = ':';
            int nNameLength = strlen(vp->name);
            memcpy(pBuffer, vp->name, nNameLength);
            pBuffer += nNameLength;
            *pBuffer++ = '"';
            *pBuffer++ = '\n';
            fwrite(Buffer, sizeof(char), pBuffer-Buffer, f);
        }
        vp = vattr_next(vp);
    }

#ifdef STANDALONE
    int iDotCounter = 0;
#endif
    char buf[SBUF_SIZE];
    buf[0] = '!';
    DO_WHOLE_DB(i)
    {
#ifdef STANDALONE
        if (!iDotCounter)
        {
            iDotCounter = 100;
            fputc('.', stderr);
            fflush(stderr);
        }
        iDotCounter--;
#endif

        if (!(isGarbage(i)))
        {
            // Format is: "!%d\n", i
            //
            int n = Tiny_ltoa(i, buf+1) + 1;
            buf[n++] = '\n';
            fwrite(buf, sizeof(char), n, f);
            db_write_object(f, i, format, flags);
        }
    }
    fputs("***END OF DUMP***\n", f);
#ifdef STANDALONE
    Log.WriteString(ENDLINE);
    Log.Flush();
#endif
    return mudstate.db_top;
}
