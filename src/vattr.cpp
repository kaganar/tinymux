// vattr.cpp -- Manages the user-defined attributes.
//
// $Id: vattr.cpp,v 1.1 2000-04-11 07:14:48 sdennis Exp $
//
// MUX 2.0
// Portions are derived from MUX 1.6. Portions are original work.
//
// Copyright (C) 1998 through 2000 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved. Permission is given to
// use this code for building and hosting text-based game servers.
// Permission is given to use this code for other non-commercial
// purposes. To use this code for commercial purposes other than
// building/hosting text-based game servers, contact the author at
// Stephen Dennis <sdennis@svdltd.com> for another license.
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "mudconf.h"
#include "vattr.h"
#include "alloc.h"
#include "htab.h"
#include "attrs.h"

static char FDECL(*store_string, (char *));

/*
 * Allocate space for strings in lumps this big. 
 */

#define STRINGBLOCK 1000

/*
 * Current block we're putting stuff in 
 */

static char *stringblock = (char *)0;

/*
 * High water mark. 
 */

static int stringblock_hwm = 0;

void vattr_init(void)
{
    return;
}

VATTR *vattr_findLEN(char *pAttrName, int nAttrName)
{
    if (pAttrName == NULL || !ok_attr_name(pAttrName)) return NULL;

    unsigned long nHash = CRC32_ProcessBuffer(0, pAttrName, nAttrName);

    CHashTable *pht = &mudstate.vattr_name_htab;
    HP_DIRINDEX iDir = pht->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        int anum;
        pht->Copy(iDir, &nRecord, &anum);
        VATTR *va = (VATTR *)anum_table[anum];
        if (strcmp(pAttrName, va->name) == 0)
        {
            return va;
        }
        iDir = pht->FindNextKey(iDir, nHash);
    }
    return NULL;
}

VATTR *vattr_alloc(char *name, int flags)
{
    int number = mudstate.attr_next++;
    anum_extend(number);
    return vattr_define(name, number, flags);
}

VATTR *vattr_define(char *name, int number, int flags)
{
    if (name == NULL) return NULL;

    // Be ruthless.
    //
    int nName = strlen(name);
    if (nName > MAX_VNAME_LENGTH)
    {
        name[MAX_VNAME_LENGTH] = '\0';
        nName = MAX_VNAME_LENGTH;
    }

    if (!ok_attr_name(name))
        return NULL;

    // _strupr
    //
    for (int i = 0; i < nName; i++)
    {
        name[i] = Tiny_ToUpper[(unsigned char)name[i]];
    }
    VATTR *vp = vattr_findLEN(name, nName);
    if (vp) return vp;

    vp = (VATTR *)MEMALLOC(sizeof(VATTR), __FILE__, __LINE__);

    // NOTE: By using store_string, the only way to release the memory associated with a user
    // attribute name is to @restart the game.
    //
    vp->name = store_string(name);
    vp->flags = flags;
    vp->number = number;

    // This entry cannot already be in the hash table because we've checked it
    // above with vattr_findLEN.
    //
    unsigned long nHash = CRC32_ProcessBuffer(0, vp->name, nName);
    mudstate.vattr_name_htab.Insert(sizeof(number), nHash, &number);

    anum_extend(vp->number);
    anum_set(vp->number, (ATTR *) vp);
    return vp;
}

#ifndef STANDALONE
extern int anum_alc_top;

// There are five data structures which must remain mutually consistent: The attr_name_htab, vattr_name_htab,
// the anum_table, the A_LIST for every object, and the attribute database.
//
void dbclean_CheckANHtoAT(dbref player)
{
    notify(player, "1. Checking (v)attr_name_htabs to anum_table mapping...");

    // This test traverses the attr_name_htab/vattr_name_htab and verifies that the corresponding anum_table
    // entry exists and is valid.
    //
    int nAttributes = 0;
    int nPredefined = 0;
    int nUserDefined = 0;
    int nOutOfBounds = 0;
    int nInvalid = 0;

    for (ATTR *pa = (ATTR *)hash_firstentry(&mudstate.attr_name_htab);
         pa;
         pa = (ATTR *)hash_nextentry(&mudstate.attr_name_htab))
    {
        nAttributes++;
        int iAttr = pa->number;
        if (iAttr <= 0 || iAttr > anum_alc_top)
        {
            nOutOfBounds++;
        }
        else
        {
            if (iAttr < A_USER_START)
            {
                nPredefined++;
            }
            else
            {
                nInvalid++;
            }

            ATTR *pb = (ATTR *) anum_get(iAttr);
            if (pb != pa)
            {
                nInvalid++;
            }
        }
    }

    for (VATTR *va = vattr_first(); va; va = vattr_next(va))
    {
        nAttributes++;
        int iAttr = va->number;
        if (iAttr <= 0 || iAttr > anum_alc_top)
        {
            nOutOfBounds++;
        }
        else
        {
            if (iAttr < A_USER_START)
            {
                nInvalid++;
            }
            else
            {
                nUserDefined++;
            }

            VATTR *vb = (VATTR *) anum_get(iAttr);
            if (vb != va)
            {
                nInvalid++;
            }
        }
    }

    notify(player, tprintf("   Total Attributes: %d", nAttributes));
    notify(player, tprintf("   Predefined: %d", nPredefined));
    notify(player, tprintf("   User Defined: %d", nUserDefined));
    notify(player, tprintf("   Index Out of Bounds: %d", nOutOfBounds));
    notify(player, tprintf("   Inconsistent: %d", nInvalid));
    notify(player, "   Done.");
}

void dbclean_CheckATtoANH(dbref player)
{
    notify(player, "2. Checking anum_table to vattr_name_htab mapping...");

    // This test traverses the anum_table and verifies that the corresponding attr_name_htab and
    // vattr_name_htab entries exist and are valid.
    //
    int nAttributes = 0;
    int nPredefined = 0;
    int nUserDefined = 0;
    int nInvalid = 0;
    int nEmpty = 0;
    for (int iAttr = 1; iAttr <= anum_alc_top; iAttr++)
    {
        if (iAttr < A_USER_START)
        {
            ATTR *pa = (ATTR *) anum_get(iAttr);
            if (pa)
            {
                nPredefined++;
                nAttributes++;

                // Convert name to upper case.
                //
                char Buffer[SBUF_SIZE];
                strcpy(Buffer, pa->name);
                _strupr(Buffer);

                // Fetch the attribute structure pointer -- which should match the one
                // from the corresponding table entry.
                //
                ATTR *pb = (ATTR *) hashfindLEN(Buffer, strlen(Buffer), &mudstate.attr_name_htab);
                if (pb != pa)
                {
                    nInvalid++;
                }
            }
            else
            {
                nEmpty++;
            }
        }
        else
        {
            VATTR *va = (VATTR *) anum_get(iAttr);
            if (va)
            {
                nUserDefined++;
                nAttributes++;
                VATTR *vb = vattr_findLEN(va->name, strlen(va->name));
                if (vb != va)
                {
                    nInvalid++;
                }
            }
            else
            {
                nEmpty++;
            }
        }
    }

    notify(player, tprintf("   Total Attributes: %d", nAttributes));
    notify(player, tprintf("   Predefined: %d", nPredefined));
    notify(player, tprintf("   User Defined: %d", nUserDefined));
    notify(player, tprintf("   Empty: %d", nEmpty));
    notify(player, tprintf("   Inconsistent: %d", nInvalid));
    notify(player, "   Done.");
}

void dbclean_CheckALISTtoAT(dbref player)
{
    notify(player, "3. Checking ALIST to anum_table mapping...");

    // Traverse every attribute on every object and make sure that attribute is represented in the attribute table.
    //
    dbref iObject;
    int nInvalid = 0;
    int nDangle = 0;
    int nALIST = 0;
    atr_push();
    DO_WHOLE_DB(iObject)
    {
        char *as;

        for (int iAttr = atr_head(iObject, &as); iAttr; iAttr = atr_next(&as))
        {
            if (iAttr <= 0)
            {
                nInvalid++;
            }
            else if (iAttr < A_USER_START)
            {
                ATTR *pa = (ATTR *) anum_get(iAttr);
                if (pa == NULL)
                {
                    nInvalid++;
                }
            }
            else if (iAttr <= anum_alc_top)
            {
                VATTR *va = (VATTR *) anum_get(iAttr);
                if (va == NULL)
                {
                    // We can try to fix this one.
                    //
                    char *pRecord = atr_get_raw(iObject, iAttr);
                    if (pRecord)
                    {
                        // If the attribute exists in the DB, then the easiest thing to do
                        // is add a dummy attribute name.
                        //
                        vattr_define(tprintf("DANGLINGATTR-%08d", iAttr), iAttr, 0);
                        nDangle++;
                    }
                    else
                    {
                        // Otherwise, the easiest thing to do is remove it from the ALIST.
                        //
                        atr_clr(iObject, iAttr);
                        nALIST++;
                    }
                }
            }
            else
            {
                nInvalid++;
            }
        }
    }
    notify(player, tprintf("   Invalid: %d", nInvalid));
    notify(player, tprintf("   DANGLINGATTR-99999999 added: %d", nDangle));
    notify(player, tprintf("   ALIST prunes: %d", nALIST));
    atr_pop();
}

void dbclean_CheckALISTtoDB(dbref player)
{
    notify(player, "4. Checking ALIST against attribute DB on disk...");

    // Traverse every attribute on every object and make sure that attribute is represented attribute database.
    //
    dbref iObject;
    int nInvalid = 0;
    int nMissing = 0;
    atr_push();
    DO_WHOLE_DB(iObject)
    {
        char *as;

        for (int iAttr = atr_head(iObject, &as); iAttr; iAttr = atr_next(&as))
        {
            if (iAttr <= 0)
            {
                nInvalid++;
            }
            else if (iAttr <= anum_alc_top)
            {
                char *pRecord = atr_get_raw(iObject, iAttr);
                if (!pRecord)
                {
                    // The contents are gone. The easiest thing to do is remove it from the ALIST.
                    //
                    atr_clr(iObject, iAttr);
                    nMissing++;
                }
            }
            else
            {
                nInvalid++;
            }
        }
    }
    notify(player, tprintf("   Invalid: %d", nInvalid));
    notify(player, tprintf("   DB prunes: %d", nMissing));
    atr_pop();
}

void dbclean_IntegrityChecking(dbref player)
{
    dbclean_CheckANHtoAT(player);
    dbclean_CheckATtoANH(player);
    dbclean_CheckALISTtoAT(player);
    dbclean_CheckALISTtoDB(player);
}

int dbclean_RemoveStaleAttributeNames(void)
{
    VATTR *va;

    // Clear every valid attribute's AF_ISUSED flag
    //
    extern int anum_alc_top;
    int iAttr;
    for (iAttr = A_USER_START; iAttr <= anum_alc_top; iAttr++)
    {
        va = (VATTR *) anum_get(iAttr);
        if (va != NULL) 
        {
            va->flags &= ~AF_ISUSED;
        }
    }

    // Traverse every attribute on every object and mark it's attribute as AF_ISUSED.
    //
    dbref iObject;
    atr_push();
    DO_WHOLE_DB(iObject)
    {
        char *as;

        for (int attr = atr_head(iObject, &as); attr; attr = atr_next(&as))
        {
            if (attr >= A_USER_START)
            {
                va = (VATTR *) anum_get(attr);
                if (va != NULL)
                {
                    va->flags |= AF_ISUSED;
                }
            }
        }
    }
    atr_pop();

    // Traverse the attribute table again and remove the ones that aren't AF_ISUSED,
    // and count how many vattributes -are- used.
    //
    int cVAttributes = 0;
    for (iAttr = A_USER_START; iAttr <= anum_alc_top; iAttr++)
    {
        va = (VATTR *) anum_get(iAttr);
        if (va != NULL)
        {
            if ((AF_ISUSED & (va->flags)) != AF_ISUSED)
            {
                anum_set(iAttr, NULL);

                // Delete from hashtable.
                //
                unsigned long nHash = CRC32_ProcessBuffer(0, va->name, strlen(va->name));
                CHashTable *pht = &mudstate.vattr_name_htab;
                HP_DIRINDEX iDir = pht->FindFirstKey(nHash);
                while (iDir != HF_FIND_END)
                {
                    HP_HEAPLENGTH nRecord;
                    int anum;
                    pht->Copy(iDir, &nRecord, &anum);
                    if (iAttr == anum)
                    {
                        pht->Remove(iDir);
                    }
                    iDir = pht->FindNextKey(iDir, nHash);
                }

                MEMFREE((char *)va, __FILE__, __LINE__);
            }
            else
            {
                cVAttributes++;
                va->flags &= ~AF_ISUSED;
            }
        }
    }
    return cVAttributes;
}

void dbclean_RenumberAttributes(int cVAttributes)
{
    VATTR *va;

    // Now that all the stale attribute entries have been removed, we can
    // begin the interesting task of renumbering the attributes that remain.

    // The range [A_USER_START, A_USER_START+cVAttributes] will be left
    // alone. The range (A_USER_START+cVAttribute, anum_alc_top] can be
    // reallocated from the first range. To create this mapping from old
    // attribute numbers to new ones, we need the following table:
    //
    int iMapStart = A_USER_START+cVAttributes+1;
    int iMapEnd = anum_alc_top;
    int nMap = iMapEnd - iMapStart + 1;
    int *aMap = (int *)MEMALLOC(sizeof(int) * nMap, __FILE__, __LINE__);
    if (aMap)
    {
        int iSweep = A_USER_START;
        memset(aMap, 0, sizeof(int) * nMap);
        for (int i = nMap - 1; i >= 0 && iSweep < iMapStart; i--)
        {
            int iAttr = iMapStart + i;
            va = (VATTR *) anum_get(iAttr);
            if (va != NULL)
            {
                while (anum_get(iSweep))
                {
                    iSweep++;
                }
                int iAllocated = iSweep++;
                aMap[i] = iAllocated;


                // Change vattr_name_htab mapping as well to point to
                // iAllocated instead of iAttr.
                //
                unsigned long nHash;
                nHash = CRC32_ProcessBuffer(0, va->name, strlen(va->name));
                CHashTable *pht = &mudstate.vattr_name_htab;
                HP_DIRINDEX iDir = pht->FindFirstKey(nHash);
                while (iDir != HF_FIND_END)
                {
                    HP_HEAPLENGTH nRecord;
                    int anum;
                    pht->Copy(iDir, &nRecord, &anum);
                    if (anum == iAttr)
                    {
                        pht->Update(iDir, sizeof(int), &iAllocated);
                        break;
                    }
                    iDir = pht->FindNextKey(iDir, nHash);
                }

                va->number = iAllocated;
                anum_set(iAllocated, (ATTR *)va);
                anum_set(iAttr, NULL);
                mudstate.attr_next = iAttr;
            }
        }

        // aMap contains a unique map from old, high-numbered attribute
        // entries to new, low-numbered, empty attribute entries. We can
        // traverse all the attributes on all the objects again and look for
        // attributes numbers in the range [iMapStart, iMapEnd]. FETCHing
        // them out of the database using the old attribute number, STOREing
        // them in the database using the new attribute number, and
        // TM_DELETEing them under the old attributes number.
        //
        atr_push();
        dbref iObject;
        DO_WHOLE_DB(iObject)
        {
            char *as;

            for ( int iAttr = atr_head(iObject, &as);
                  iAttr;
                  iAttr = atr_next(&as)
                )
            {
                if (iMapStart <= iAttr && iAttr <= iMapEnd)
                {
                    int iNew = aMap[iAttr-iMapStart];
                    if (iNew)
                    {
                        dbref iOwner;
                        int   iFlag;
                        char *pRecord = atr_get(iObject, iAttr, &iOwner, &iFlag);
                        atr_add_raw(iObject, iNew, pRecord);
                        free_lbuf(pRecord);
                        atr_add_raw(iObject, iAttr, NULL);
                    }
                }
            }
        }
        atr_pop();

        MEMFREE((char *)aMap, __FILE__, __LINE__);
    }
}

void do_dbclean(dbref player, dbref cause, int key)
{
#ifndef MEMORY_BASED
    // Save cached modified attribute list
    //
    al_store();
#endif // MEMORY_BASED

    notify(player, "Checking Integrity of the attribute data structures...");
    dbclean_IntegrityChecking(player);
    notify(player, "Removing stale attributes names...");
    int cVAttributes = dbclean_RemoveStaleAttributeNames();
    notify(player, "Renumbering and compacting attribute numbers...");
    dbclean_RenumberAttributes(cVAttributes);
    notify(player, tprintf("Next Attribute number to allocate: %d", mudstate.attr_next));
    notify(player, "Checking Integrity of the attribute data structures...");
    dbclean_IntegrityChecking(player);
    notify(player, "@dbclean completed..");
}
#endif
        
void vattr_delete(char *name)
{
    if (!ok_attr_name(name))
        return;

    _strupr(name);

    // Delete from hashtable.
    //
    unsigned long nHash = CRC32_ProcessBuffer(0, name, strlen(name));
    CHashTable *pht = &mudstate.vattr_name_htab;
    HP_DIRINDEX iDir = pht->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        int anum;
        pht->Copy(iDir, &nRecord, &anum);
        if (strcmp(name, anum_table[anum]->name) == 0)
        {
            VATTR *vp = (VATTR *)anum_table[anum];
            anum_set(anum, NULL);
            pht->Remove(iDir);
            MEMFREE((char *)vp, __FILE__, __LINE__);
        }
        iDir = pht->FindNextKey(iDir, nHash);
    }
}

VATTR *vattr_rename(char *name, char *newname)
{
    _strupr(name);
    if (!ok_attr_name(name))
        return (NULL);

    /*
     * Be ruthless. 
     */

    if (strlen(name) > MAX_VNAME_LENGTH)
        name[MAX_VNAME_LENGTH] = '\0';

    _strupr(newname);
    if (!ok_attr_name(newname))
        return (NULL);
    
    // Find and Delete old name from hashtable.
    //
    unsigned long nHash = CRC32_ProcessBuffer(0, name, strlen(name));
    CHashTable *pht = &mudstate.vattr_name_htab;
    HP_DIRINDEX iDir = pht->FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        int anum;
        pht->Copy(iDir, &nRecord, &anum);
        VATTR *vp = (VATTR *)anum_table[anum];
        if (strcmp(name, vp->name) == 0)
        {
            pht->Remove(iDir);

            // Add in new name. iDir is no longer valid, so don't use it.
            //
            vp->name = store_string(newname);
            nHash = CRC32_ProcessBuffer(0, newname, strlen(newname));
            pht->Insert(sizeof(int), nHash, &anum);
            return (VATTR *)anum_table[anum];
        }
        iDir = pht->FindNextKey(iDir, nHash);
    }
    return NULL;
}

VATTR *NDECL(vattr_first)
{
    HP_HEAPLENGTH nRecord;
    int anum;
    HP_DIRINDEX iDir = mudstate.vattr_name_htab.FindFirst(&nRecord, &anum);
    if (iDir != HF_FIND_END)
    {
        return (VATTR *)anum_table[anum];
    }
    return NULL;

}

VATTR *vattr_next(VATTR *vp)
{
    if (vp == NULL)
        return vattr_first();

    HP_HEAPLENGTH nRecord;
    int anum;
    HP_DIRINDEX iDir = mudstate.vattr_name_htab.FindNext(&nRecord, &anum);
    if (iDir != HF_FIND_END)
    {
        return (VATTR *)anum_table[anum];
    }
    return NULL;
}

/*
 * Some goop for efficiently storing strings we expect to
 * keep forever. There is no freeing mechanism.
 */

static char *store_string(char *str)
{
    int len;
    char *ret;

    len = strlen(str);

    /*
     * If we have no block, or there's not enough room left in the * * *
     * current one, get a new one. 
     */

    if (!stringblock || (STRINGBLOCK - stringblock_hwm) < (len + 1))
    {
        // NOTE: These allocations are -never- freed, and this is intentional.
        //
        stringblock = (char *)MEMALLOC(STRINGBLOCK, __FILE__, __LINE__);
        if (!stringblock)
            return ((char *)0);
        stringblock_hwm = 0;
    }
    ret = stringblock + stringblock_hwm;
    StringCopy(ret, str);
    stringblock_hwm += (len + 1);
    return (ret);
}
