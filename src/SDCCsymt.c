/*-------------------------------------------------------------------------
  SDCCsymt.c - Code file for Symbols table related structures and MACRO's.
        Written By -  Sandeep Dutta . sandeep.dutta@usa.net (1998)

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

   In other words, you are welcome to use, share and improve this program.
   You are forbidden to forbid anyone else to use, share and improve
   what you give them.   Help stamp out software-hoarding!
-------------------------------------------------------------------------*/

#include "common.h"
#include "newalloc.h"
#include "dbuf_string.h"

#include "SDCCsymt.h"

value *aggregateToPointer (value *val);
void printTypeChainRaw (sym_link * start, FILE * of);

void printFromToType(sym_link *from, sym_link *to) {
  fprintf (stderr, "from type '");
  printTypeChain (from, stderr);
  fprintf (stderr, "'\nto type '");
  printTypeChain (to, stderr);
  fprintf (stderr, "'\n");
}

/* noun strings */
char *nounName(sym_link *sl) {
  switch (SPEC_NOUN(sl))
    {
    case V_INT: {
      if (SPEC_LONG(sl)) return "long";
      if (SPEC_SHORT(sl)) return "short";
      return "int";
    }
    case V_FLOAT: return "float";
    case V_FIXED16X16: return "fixed16x16";
    case V_CHAR: return "char";
    case V_VOID: return "void";
    case V_STRUCT: return "struct";
    case V_LABEL: return "label";
    case V_BITFIELD: return "bitfield";
    case V_BIT: return "bit";
    case V_SBIT: return "sbit";
    case V_DOUBLE: return "double";
    }
  return "unknown";
};

bucket *SymbolTab[256];         /* the symbol    table  */
bucket *StructTab[256];         /* the structure table  */
bucket *TypedefTab[256];        /* the typedef   table  */
bucket *LabelTab[256];          /* the Label     table  */
bucket *enumTab[256];           /* enumerated    table  */

/*------------------------------------------------------------------*/
/* initSymt () - initialises symbol table related stuff             */
/*------------------------------------------------------------------*/
void
initSymt ()
{
  int i = 0;

  for (i = 0; i < 256; i++)
    SymbolTab[i] = StructTab[i] = (void *) NULL;


}
/*-----------------------------------------------------------------*/
/* newBucket - allocates & returns a new bucket                    */
/*-----------------------------------------------------------------*/
bucket *
newBucket ()
{
  bucket *bp;

  bp = Safe_alloc ( sizeof (bucket));

  return bp;
}

/*-----------------------------------------------------------------*/
/* hashKey - computes the hashkey given a symbol name              */
/*-----------------------------------------------------------------*/
int
hashKey (const char *s)
{
  unsigned long key = 0;

  while (*s)
    key += *s++;
  return key % 256;
}

/*-----------------------------------------------------------------*/
/* addSym - adds a symbol to the hash Table                        */
/*-----------------------------------------------------------------*/
void
addSym (bucket ** stab,
        void *sym,
        char *sname,
        int level,
        int block,
        int checkType)
{
  int i;                        /* index into the hash Table */
  bucket *bp;                   /* temp bucket    *          */

  if (checkType) {
    symbol *csym = (symbol *)sym;

    if (getenv("DEBUG_SANITY")) {
      fprintf (stderr, "addSym: %s ", sname);
    }
    /* make sure the type is complete and sane */
    checkTypeSanity(csym->etype, csym->name);
  }

  /* prevent overflow of the (r)name buffers */
  if (strlen(sname)>SDCC_SYMNAME_MAX) {
    werror (W_SYMBOL_NAME_TOO_LONG, SDCC_SYMNAME_MAX);
    sname[SDCC_SYMNAME_MAX]='\0';
  }

  /* the symbols are always added at the head of the list  */
  i = hashKey (sname);
  /* get a free entry */
  bp = Safe_alloc ( sizeof (bucket));

  bp->sym = sym;                /* update the symbol pointer */
  bp->level = level;            /* update the nest level     */
  bp->block = block;
  strncpyz (bp->name, sname, sizeof(bp->name)); /* copy the name into place */

  /* if this is the first entry */
  if (stab[i] == NULL)
    {
      bp->prev = bp->next = (void *) NULL;      /* point to nothing */
      stab[i] = bp;
    }
  /* not first entry then add @ head of list */
  else
    {
      bp->prev = NULL;
      stab[i]->prev = bp;
      bp->next = stab[i];
      stab[i] = bp;
    }
}

/*-----------------------------------------------------------------*/
/* deleteSym - deletes a symbol from the hash Table entry          */
/*-----------------------------------------------------------------*/
void
deleteSym (bucket ** stab, void *sym, char *sname)
{
  int i = 0;
  bucket *bp;

  i = hashKey (sname);

  bp = stab[i];
  /* find the symbol */
  while (bp)
    {
      if (bp->sym == sym)       /* found it then break out */
        break;                  /* of the loop       */
      bp = bp->next;
    }

  if (!bp)                      /* did not find it */
    return;

  /* if this is the first one in the chain */
  if (!bp->prev)
    {
      stab[i] = bp->next;
      if (stab[i])              /* if chain ! empty */
        stab[i]->prev = (void *) NULL;
    }
  /* middle || end of chain */
  else
    {
      if (bp->next)             /* if not end of chain */
        bp->next->prev = bp->prev;

      bp->prev->next = bp->next;
    }
}

/*-----------------------------------------------------------------*/
/* findSym - finds a symbol in a table                             */
/*-----------------------------------------------------------------*/
void *
findSym (bucket ** stab, void *sym, const char *sname)
{
  bucket *bp;

  bp = stab[hashKey (sname)];
  while (bp)
    {
      if (bp->sym == sym || strcmp (bp->name, sname) == 0)
        break;
      bp = bp->next;
    }

  return (bp ? bp->sym : (void *) NULL);
}

/*-----------------------------------------------------------------*/
/* findSymWithLevel - finds a symbol with a name & level           */
/*-----------------------------------------------------------------*/
void *
findSymWithLevel (bucket ** stab, symbol * sym)
{
  bucket *bp;

  bp = stab[hashKey (sym->name)];

  /**
   **  do the search from the head of the list since the
   **  elements are added at the head it is ensured that
   ** we will find the deeper definitions before we find
   ** the global ones. we need to check for symbols with
   ** level <= to the level given, if levels match then block
   ** numbers need to match as well
   **/
  while (bp)
    {
      if (strcmp (bp->name, sym->name) == 0 && bp->level <= sym->level)
        {
          /* if this is parameter then nothing else need to be checked */
          if (((symbol *) (bp->sym))->_isparm)
            return (bp->sym);
          /* if levels match then block numbers should also match */
          if (bp->level && bp->level == sym->level && bp->block == sym->block)
            return (bp->sym);
          /* if levels don't match then we are okay */
          if (bp->level && bp->level != sym->level && bp->block <= sym->block)
            return (bp->sym);
          /* if this is a global variable then we are ok too */
          if (bp->level == 0)
            return (bp->sym);
        }

      bp = bp->next;
    }

  return (void *) NULL;
}

/*-----------------------------------------------------------------*/
/* findSymWithBlock - finds a symbol with name in with a block     */
/*-----------------------------------------------------------------*/
void *
findSymWithBlock (bucket ** stab, symbol * sym, int block)
{
  bucket *bp;

  bp = stab[hashKey (sym->name)];
  while (bp)
    {
      if (strcmp (bp->name, sym->name) == 0 &&
          bp->block <= block)
        break;
      bp = bp->next;
    }

  return (bp ? bp->sym : (void *) NULL);
}

/*------------------------------------------------------------------*/
/* newSymbol () - returns a new pointer to a symbol                 */
/*------------------------------------------------------------------*/
symbol *
newSymbol (char *name, int scope)
{
  symbol *sym;

  sym = Safe_alloc ( sizeof (symbol));

  strncpyz (sym->name, name, sizeof(sym->name)); /* copy the name */
  sym->level = scope;           /* set the level */
  sym->block = currBlockno;
  sym->lineDef = lexLineno;    /* set the line number */
  sym->fileDef = lexFilename;
  return sym;
}

/*------------------------------------------------------------------*/
/* newLink - creates a new link (declarator,specifier)              */
/*------------------------------------------------------------------*/
sym_link *
newLink (SYM_LINK_CLASS select)
{
  sym_link *p;

  p = Safe_alloc ( sizeof (sym_link));
  p->class=select;

  return p;
}

/*------------------------------------------------------------------*/
/* newStruct - creats a new structdef from the free list            */
/*------------------------------------------------------------------*/
structdef *
newStruct (char *tag)
{
  structdef *s;

  s = Safe_alloc ( sizeof (structdef));

  strncpyz (s->tag, tag, sizeof(s->tag));               /* copy the tag */
  return s;
}

/*------------------------------------------------------------------*/
/* sclsFromPtr - Return the storage class a pointer points into.    */
/*               S_FIXED is returned for generic pointers or other  */
/*               unexpected cases                                   */
/*------------------------------------------------------------------*/
STORAGE_CLASS
sclsFromPtr(sym_link *ptr)
{
  switch (DCL_TYPE (ptr))
    {
    case POINTER:
      return S_DATA;
    case GPOINTER:
      return S_FIXED;
    case FPOINTER:
      return S_XDATA;
    case CPOINTER:
      return S_CODE;
    case IPOINTER:
      return S_IDATA;
    case PPOINTER:
      return S_PDATA;
    case EEPPOINTER:
      return S_EEPROM;
    case FUNCTION:
      return S_CODE;
    default:
      return S_FIXED;
    }
}

/*------------------------------------------------------------------*/
/* pointerTypes - do the computation for the pointer types          */
/*------------------------------------------------------------------*/
void
pointerTypes (sym_link * ptr, sym_link * type)
{
  sym_link * p;
  sym_link * etype;

  if (IS_SPEC (ptr))
    return;

  /* find the last unknown pointer type */
  p = ptr;
  while (p) {
    if (IS_PTR (p) && DCL_TYPE(p)==UPOINTER)
      ptr = p;
    p = p->next;
  }

  /* could not find it */
  if (!ptr || IS_SPEC (ptr) || !IS_PTR (ptr))
    return;

  if (IS_PTR(ptr) && DCL_TYPE(ptr)!=UPOINTER) {
    pointerTypes (ptr->next, type);
    return;
  }

  /* change the pointer type depending on the
     storage class of the etype */
  etype = getSpec (type);
  if (IS_SPEC (etype))
    {
      switch (SPEC_SCLS (etype))
        {
        case S_XDATA:
          DCL_TYPE (ptr) = FPOINTER;
          break;
        case S_IDATA:
          DCL_TYPE (ptr) = IPOINTER;
          break;
        case S_PDATA:
          DCL_TYPE (ptr) = PPOINTER;
          break;
        case S_DATA:
          DCL_TYPE (ptr) = POINTER;
          break;
        case S_CODE:
          DCL_TYPE (ptr) = CPOINTER;
          break;
        case S_EEPROM:
          DCL_TYPE (ptr) = EEPPOINTER;
          break;
        default:
          DCL_TYPE (ptr) = port->unqualified_pointer;
          break;
        }
      /* the storage class of etype ends here */
      SPEC_SCLS (etype) = 0;
    }

  /* now change all the remaining unknown pointers
     to generic pointers */
  while (ptr)
    {
      if (!IS_SPEC (ptr) && DCL_TYPE (ptr) == UPOINTER)
        DCL_TYPE (ptr) = port->unqualified_pointer;
      ptr = ptr->next;
    }

  /* same for the type although it is highly unlikely that
     type will have a pointer */
  while (type)
    {
      if (!IS_SPEC (type) && DCL_TYPE (type) == UPOINTER)
        DCL_TYPE (type) = port->unqualified_pointer;
      type = type->next;
    }
}

/*------------------------------------------------------------------*/
/* addDecl - adds a declarator @ the end of a chain                 */
/*------------------------------------------------------------------*/
void
addDecl (symbol * sym, int type, sym_link * p)
{
  static sym_link *empty = NULL;
  sym_link *head;
  sym_link *tail;
  sym_link *t;

  if (getenv("SDCC_DEBUG_FUNCTION_POINTERS"))
    fprintf (stderr, "SDCCsymt.c:addDecl(%s,%d,%p)\n", sym->name, type, p);

  if (empty == NULL)
    empty = newLink(SPECIFIER);

  /* if we are passed a link then set head & tail */
  if (p)
    {
      tail = head = p;
      while (tail->next)
        tail = tail->next;
    }
  else
    {
      head = tail = newLink (DECLARATOR);
      DCL_TYPE (head) = type;
    }

  /* if this is the first entry   */
  if (!sym->type)
    {
      sym->type = head;
      sym->etype = tail;
    }
  else if (IS_SPEC (sym->etype) && IS_SPEC (head) && head == tail)
    {
      sym->etype = mergeSpec (sym->etype, head, sym->name);
    }
  else if (IS_SPEC (sym->etype) && !IS_SPEC (head) && head == tail)
    {
      t = sym->type;
      while (t->next != sym->etype)
        t = t->next;
      t->next = head;
      tail->next = sym->etype;
    }
  else if (IS_FUNC (sym->type) && IS_SPEC (sym->type->next) &&
           !memcmp(sym->type->next, empty, sizeof(sym_link)))
    {
      sym->type->next = head;
      sym->etype = tail;
    }
  else
    {
      sym->etype->next = head;
      sym->etype = tail;
    }

  /* if the type is an unknown pointer and has
     a tspec then take the storage class const & volatile
     attribute from the tspec & make it those of this
     symbol */
  if (p &&
      !IS_SPEC (p) &&
      //DCL_TYPE (p) == UPOINTER &&
      DCL_TSPEC (p))
    {
      if (!IS_SPEC (sym->etype))
        {
          sym->etype = sym->etype->next = newLink (SPECIFIER);
        }
      SPEC_SCLS (sym->etype) = SPEC_SCLS (DCL_TSPEC (p));
      DCL_TSPEC (p) = NULL;
    }

  // if there is a function in this type chain
  if (p && funcInChain(sym->type)) {
    processFuncArgs (sym);
  }

  return;
}

/*------------------------------------------------------------------
  checkTypeSanity: prevent the user from doing e.g.:
  unsigned float uf;
  ------------------------------------------------------------------*/
void checkTypeSanity(sym_link *etype, char *name)
{
  char *noun;

  if (!etype) {
    if (getenv("DEBUG_SANITY")) {
      fprintf (stderr, "sanity check skipped for %s (etype==0)\n", name);
    }
    return;
  }

  if (!IS_SPEC(etype)) {
    if (getenv("DEBUG_SANITY")) {
      fprintf (stderr, "sanity check skipped for %s (!IS_SPEC)\n", name);
    }
    return;
  }

  noun=nounName(etype);

  if (getenv("DEBUG_SANITY")) {
    fprintf (stderr, "checking sanity for %s %p\n", name, etype);
  }

  if ((SPEC_NOUN(etype)==V_CHAR ||
       SPEC_NOUN(etype)==V_FLOAT ||
       SPEC_NOUN(etype)==V_FIXED16X16 ||
       SPEC_NOUN(etype)==V_DOUBLE ||
       SPEC_NOUN(etype)==V_VOID) &&
      (SPEC_SHORT(etype) || SPEC_LONG(etype))) {
    // long or short for char float double or void
    werror (E_LONG_OR_SHORT_INVALID, noun, name);
  }
  if ((SPEC_NOUN(etype)==V_FLOAT ||
       SPEC_NOUN(etype)==V_FIXED16X16 ||
       SPEC_NOUN(etype)==V_DOUBLE ||
       SPEC_NOUN(etype)==V_VOID) &&
      (etype->select.s.b_signed || SPEC_USIGN(etype))) {
    // signed or unsigned for float double or void
    werror (E_SIGNED_OR_UNSIGNED_INVALID, noun, name);
  }

  // special case for "short"
  if (SPEC_SHORT(etype)) {
    SPEC_NOUN(etype) = options.shortis8bits ? V_CHAR : V_INT;
    SPEC_SHORT(etype) = 0;
  }

  /* if no noun e.g.
     "const a;" or "data b;" or "signed s" or "long l"
     assume an int */
  if (!SPEC_NOUN(etype)) {
    SPEC_NOUN(etype) = V_INT;
  }

  /* ISO/IEC 9899 J.3.9 implementation defined behaviour: */
  /* a "plain" int bitfield is unsigned */
  if (SPEC_NOUN(etype)==V_BIT ||
      SPEC_NOUN(etype)==V_SBIT) {
    if (!etype->select.s.b_signed)
      SPEC_USIGN(etype) = 1;
  }

  if (etype->select.s.b_signed && SPEC_USIGN(etype)) {
    // signed AND unsigned
    werror (E_SIGNED_AND_UNSIGNED_INVALID, noun, name);
  }
  if (SPEC_SHORT(etype) && SPEC_LONG(etype)) {
    // short AND long
    werror (E_LONG_AND_SHORT_INVALID, noun, name);
  }
}

/*------------------------------------------------------------------*/
/* finalizeSpec                                                     */
/*    currently just a V_CHAR is forced to be unsigned              */
/*      when it's neither signed nor unsigned                       */
/*      and the --funsigned-char command line switch is active      */
/*------------------------------------------------------------------*/
sym_link *
finalizeSpec(sym_link * lnk)
{
  if (options.unsigned_char) {
    sym_link *p = lnk;
      while (p && !IS_SPEC(p))
        p = p->next;
      if(SPEC_NOUN(p) == V_CHAR && !SPEC_USIGN(p) && !p->select.s.b_signed)
        SPEC_USIGN(p) = 1;
  }
  return lnk;
}

/*------------------------------------------------------------------*/
/* mergeSpec - merges two specifiers and returns the new one        */
/*------------------------------------------------------------------*/
sym_link *
mergeSpec (sym_link * dest, sym_link * src, char *name)
{
  if (!IS_SPEC(dest) || !IS_SPEC(src)) {
#if 0
    werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "cannot merge declarator");
    exit (1);
#else
    werror (E_SYNTAX_ERROR, yytext);
    // the show must go on
    return newIntLink();
#endif
  }

  if (SPEC_NOUN(src)) {
    if (!SPEC_NOUN(dest)) {
      SPEC_NOUN(dest)=SPEC_NOUN(src);
    } else {
      /* we shouldn't redeclare the type */
      if (getenv("DEBUG_SANITY")) {
        fprintf (stderr, "mergeSpec: ");
      }
      werror(E_TWO_OR_MORE_DATA_TYPES, name);
    }
  }

  if (SPEC_SCLS(src)) {
    /* if destination has no storage class */
    if (!SPEC_SCLS (dest) || SPEC_SCLS(dest)==S_REGISTER) {
      SPEC_SCLS (dest) = SPEC_SCLS (src);
    } else {
      if (getenv("DEBUG_SANITY")) {
        fprintf (stderr, "mergeSpec: ");
      }
      werror(E_TWO_OR_MORE_STORAGE_CLASSES, name);
    }
  }

  /* copy all the specifications  */

  // we really should do:
#if 0
  if (SPEC_what(src)) {
    if (SPEC_what(dest)) {
      werror(W_DUPLICATE_SPEC, "what");
    }
    SPEC_what(dst)|=SPEC_what(src);
  }
#endif
  // but there are more important thing right now

  SPEC_LONG (dest) |= SPEC_LONG (src);
  SPEC_SHORT(dest) |= SPEC_SHORT(src);
  SPEC_USIGN (dest) |= SPEC_USIGN (src);
  dest->select.s.b_signed|=src->select.s.b_signed;
  SPEC_STAT (dest) |= SPEC_STAT (src);
  SPEC_EXTR (dest) |= SPEC_EXTR (src);
  SPEC_INLINE (dest) |= SPEC_INLINE (src);
  SPEC_CONST(dest) |= SPEC_CONST (src);
  SPEC_ABSA (dest) |= SPEC_ABSA (src);
  SPEC_VOLATILE (dest) |= SPEC_VOLATILE (src);
  SPEC_RESTRICT (dest) |= SPEC_RESTRICT (src);
  SPEC_ADDR (dest) |= SPEC_ADDR (src);
  SPEC_OCLS (dest) = SPEC_OCLS (src);
  SPEC_BLEN (dest) |= SPEC_BLEN (src);
  SPEC_BSTR (dest) |= SPEC_BSTR (src);
  SPEC_TYPEDEF (dest) |= SPEC_TYPEDEF (src);
  SPEC_ENUM (dest) |= SPEC_ENUM (src);
  if (SPEC_ARGREG(src) && !SPEC_ARGREG(dest))
      SPEC_ARGREG(dest) = SPEC_ARGREG(src);

  if (IS_STRUCT (dest) && SPEC_STRUCT (dest) == NULL)
    SPEC_STRUCT (dest) = SPEC_STRUCT (src);

  /* these are the only function attributes that will be set
     in a specifier while parsing */
  FUNC_NONBANKED(dest) |= FUNC_NONBANKED(src);
  FUNC_BANKED(dest) |= FUNC_BANKED(src);
  FUNC_ISCRITICAL(dest) |= FUNC_ISCRITICAL(src);
  FUNC_ISREENT(dest) |= FUNC_ISREENT(src);
  FUNC_ISNAKED(dest) |= FUNC_ISNAKED(src);
  FUNC_ISISR(dest) |= FUNC_ISISR(src);
  FUNC_ISJAVANATIVE(dest) |= FUNC_ISJAVANATIVE(src);
  FUNC_ISBUILTIN(dest) |= FUNC_ISBUILTIN(src);
  FUNC_ISOVERLAY(dest) |= FUNC_ISOVERLAY(src);
  FUNC_INTNO(dest) |= FUNC_INTNO(src);
  FUNC_REGBANK(dest) |= FUNC_REGBANK(src);
  FUNC_ISINLINE (dest) |= FUNC_ISINLINE (src);

  return dest;
}

/*------------------------------------------------------------------*/
/* mergeDeclSpec - merges a specifier and a declarator              */
/*------------------------------------------------------------------*/
sym_link *
mergeDeclSpec (sym_link * dest, sym_link * src, char *name)
{
  sym_link *decl, *spec, *lnk;

  if (IS_SPEC (src))
    {
      if (IS_SPEC (dest))
        {
          return mergeSpec (dest, src, name);
        }
      else
        {
          decl = dest;
          spec = src;
        }
    }
  else
    {
      if (IS_SPEC (dest))
        {
          decl = src;
          spec = dest;
        }
      else
        {
          werror (E_SYNTAX_ERROR, yytext);
          // the show must go on
          return newIntLink();
        }
    }

  DCL_PTR_CONST(decl) |= SPEC_CONST(spec);
  DCL_PTR_VOLATILE(decl) |= SPEC_VOLATILE(spec);
  DCL_PTR_RESTRICT(decl) |= SPEC_RESTRICT(spec);

  SPEC_CONST(spec) = SPEC_VOLATILE(spec) = SPEC_RESTRICT(spec) = 0;

  lnk = decl;
  while (lnk && !IS_SPEC(lnk->next))
    lnk = lnk->next;
  lnk->next = mergeSpec(spec, lnk->next, name);
  return decl;
}

/*-------------------------------------------------------------------*/
/* genSymName - generates and returns a name used for anonymous vars */
/*-------------------------------------------------------------------*/
char *
genSymName (int level)
{
  static int gCount = 0;
  static char gname[SDCC_NAME_MAX + 1];

  SNPRINTF (gname, sizeof(gname), "__%04d%04d", level, gCount++);
  return gname;
}

/*------------------------------------------------------------------*/
/* getSpec - returns the specifier part from a declaration chain    */
/*------------------------------------------------------------------*/
sym_link *
getSpec (sym_link * p)
{
  sym_link *loop;

  loop = p;
  while (p && !(IS_SPEC (p)))
    p = p->next;

  return p;
}

/*------------------------------------------------------------------*/
/* newCharLink() - creates an char type                             */
/*------------------------------------------------------------------*/
sym_link *
newCharLink ()
{
  sym_link *p;

  p = newLink (SPECIFIER);
  SPEC_NOUN (p) = V_CHAR;

  return p;
}

/*------------------------------------------------------------------*/
/* newFloatLink - a new Float type                                  */
/*------------------------------------------------------------------*/
sym_link *
newFloatLink ()
{
  sym_link *p;

  p = newLink (SPECIFIER);
  SPEC_NOUN (p) = V_FLOAT;

  return p;
}

/*------------------------------------------------------------------*/
/* newFixed16x16Link - a new Float type                             */
/*------------------------------------------------------------------*/
sym_link *
newFixed16x16Link ()
{
  sym_link *p;

  p = newLink (SPECIFIER);
  SPEC_NOUN (p) = V_FIXED16X16;

  return p;
}

/*------------------------------------------------------------------*/
/* newLongLink() - new long type                                    */
/*------------------------------------------------------------------*/
sym_link *
newLongLink ()
{
  sym_link *p;

  p = newLink (SPECIFIER);
  SPEC_NOUN (p) = V_INT;
  SPEC_LONG (p) = 1;

  return p;
}

/*------------------------------------------------------------------*/
/* newIntLink() - creates an int type                               */
/*------------------------------------------------------------------*/
sym_link *
newIntLink ()
{
  sym_link *p;

  p = newLink (SPECIFIER);
  SPEC_NOUN (p) = V_INT;

  return p;
}

/*------------------------------------------------------------------*/
/* newBoolLink() - creates an bool type                             */
/*------------------------------------------------------------------*/
sym_link *
newBoolLink ()
{
  sym_link *p;

  p = newLink (SPECIFIER);
  SPEC_NOUN (p) = V_BIT;

  return p;
}

/*------------------------------------------------------------------*/
/* getSize - returns size of a type chain in bytes                  */
/*------------------------------------------------------------------*/
unsigned int
getSize (sym_link * p)
{
  /* if nothing return 0 */
  if (!p)
    return 0;
  if (IS_SPEC (p))
    {                           /* if this is the specifier then */
      switch (SPEC_NOUN (p))
        {                       /* depending on the specifier type */
        case V_INT:
          return (IS_LONG (p) ? LONGSIZE : INTSIZE);
        case V_FLOAT:
          return FLOATSIZE;
        case V_FIXED16X16:
          return (4);
        case V_CHAR:
          return CHARSIZE;
        case V_VOID:
          return 0;
        case V_STRUCT:
          return SPEC_STRUCT (p)->size;
        case V_LABEL:
          return 0;
        case V_SBIT:
        case V_BIT:
          return BITSIZE;
        case V_BITFIELD:
          return ((SPEC_BLEN (p) / 8) + (SPEC_BLEN (p) % 8 ? 1 : 0));
        default:
          return 0;
        }
    }

  /* this is a declarator */
  switch (DCL_TYPE (p))
    {
    case ARRAY:
      if (DCL_ELEM(p)) {
        return DCL_ELEM (p) * getSize (p->next);
      } else {
          //    werror (E_INTERNAL_ERROR, __FILE__, __LINE__,
          //    "can not tell the size of an array[]");
        return 0;
      }
    case IPOINTER:
    case PPOINTER:
    case POINTER:
      return (PTRSIZE);
    case EEPPOINTER:
    case FPOINTER:
    case CPOINTER:
    case FUNCTION:
      return (IFFUNC_BANKED (p) ? GPTRSIZE : FPTRSIZE);
    case GPOINTER:
      return (GPTRSIZE);

    default:
      return 0;
    }
}

/*------------------------------------------------------------------*/
/* checkStructFlexArray - check tree behind a struct                */
/*------------------------------------------------------------------*/
static bool
checkStructFlexArray (symbol *sym, sym_link *p)
{
  /* if nothing return FALSE */
  if (!p)
    return FALSE;

  if (IS_SPEC (p))
    {
      /* (nested) struct with flexible array member? */
      if (IS_STRUCT (p) && SPEC_STRUCT (p)->b_flexArrayMember)
        {
          werror (W_INVALID_FLEXARRAY);
          return FALSE;
        }
      return FALSE;
    }

  /* this is a declarator */
  if (IS_ARRAY (p))
    {
      /* flexible array member? */
      if (!DCL_ELEM(p))
        {
          if (!options.std_c99)
            werror (W_C89_NO_FLEXARRAY);
          return TRUE;
        }
      /* walk tree */
      return checkStructFlexArray (sym, p->next);
    }
  return FALSE;
}

/*------------------------------------------------------------------*/
/* bitsForType - returns # of bits required to store this type      */
/*------------------------------------------------------------------*/
unsigned int
bitsForType (sym_link * p)
{
  /* if nothing return 0 */
  if (!p)
    return 0;

  if (IS_SPEC (p))
    {                           /* if this is the specifier then */

      switch (SPEC_NOUN (p))
        {                       /* depending on the specifier type */
        case V_INT:
          return (IS_LONG (p) ? LONGSIZE * 8 : INTSIZE * 8);
        case V_FLOAT:
          return FLOATSIZE * 8;
        case V_FIXED16X16:
          return (32);
        case V_CHAR:
          return CHARSIZE * 8;
        case V_VOID:
          return 0;
        case V_STRUCT:
          return SPEC_STRUCT (p)->size * 8;
        case V_LABEL:
          return 0;
        case V_SBIT:
        case V_BIT:
          return 1;
        case V_BITFIELD:
          return SPEC_BLEN (p);
        default:
          return 0;
        }
    }

  /* this is a specifier  */
  switch (DCL_TYPE (p))
    {
    case ARRAY:
      return DCL_ELEM (p) * getSize (p->next) * 8;
    case IPOINTER:
    case PPOINTER:
    case POINTER:
      return (PTRSIZE * 8);
    case EEPPOINTER:
    case FPOINTER:
    case CPOINTER:
    case FUNCTION:
      return (FPTRSIZE * 8);
    case GPOINTER:
      return (GPTRSIZE * 8);

    default:
      return 0;
    }
}

/*------------------------------------------------------------------*/
/* copySymbolChain - copies a symbol chain                          */
/*------------------------------------------------------------------*/
symbol *
copySymbolChain (symbol * src)
{
  symbol *dest;

  if (!src)
    return NULL;

  dest = copySymbol (src);
  dest->next = copySymbolChain (src->next);
  return dest;
}

/*------------------------------------------------------------------*/
/* copySymbol - makes a copy of a symbol                            */
/*------------------------------------------------------------------*/
symbol *
copySymbol (symbol * src)
{
  symbol *dest;

  if (!src)
    return NULL;

  dest = newSymbol (src->name, src->level);
  memcpy (dest, src, sizeof (symbol));
  dest->level = src->level;
  dest->block = src->block;
  dest->ival = copyIlist (src->ival);
  dest->type = copyLinkChain (src->type);
  dest->etype = getSpec (dest->type);
  dest->next = NULL;
  dest->key = src->key;
  dest->allocreq = src->allocreq;
  return dest;
}

/*------------------------------------------------------------------*/
/* reverseSyms - reverses the links for a symbol chain              */
/*------------------------------------------------------------------*/
symbol *
reverseSyms (symbol * sym)
{
  symbol *prev, *curr, *next;

  if (!sym)
    return NULL;

  prev = sym;
  curr = sym->next;

  while (curr)
    {
      next = curr->next;
      curr->next = prev;
      prev = curr;
      curr = next;
    }
  sym->next = (void *) NULL;
  return prev;
}

/*------------------------------------------------------------------*/
/* reverseLink - reverses the links for a type chain                */
/*------------------------------------------------------------------*/
sym_link *
reverseLink (sym_link * type)
{
  sym_link *prev, *curr, *next;

  if (!type)
    return NULL;

  prev = type;
  curr = type->next;

  while (curr)
    {
      next = curr->next;
      curr->next = prev;
      prev = curr;
      curr = next;
    }
  type->next = (void *) NULL;
  return prev;
}

/*------------------------------------------------------------------*/
/* addSymChain - adds a symbol chain to the symboltable             */
/*------------------------------------------------------------------*/
void
addSymChain (symbol ** symHead)
{
  symbol *sym = *symHead;
  symbol *csym = NULL;
  symbol **symPtrPtr;
  int error = 0;

  for (; sym != NULL; sym = sym->next)
    {
      changePointer(sym->type);
      checkTypeSanity(sym->etype, sym->name);

      if (!sym->level && !(IS_SPEC(sym->etype) && IS_TYPEDEF(sym->etype)))
        checkDecl (sym, 0);
      else
        {
          /* if this is an array without any dimension
             then update the dimension from the initial value */
          if (IS_ARRAY (sym->type) && !DCL_ELEM (sym->type))
            DCL_ELEM (sym->type) = getNelements (sym->type, sym->ival);
        }

      /* if already exists in the symbol table on the same level */
      if ((csym = findSymWithLevel (SymbolTab, sym)) &&
          csym->level == sym->level)
        {
          /* if not formal parameter and not in file scope
             then show symbol redefined error
             else check if symbols have compatible types */
          if (!sym->_isparm && sym->level > 0)
            error = 1;
          else
            {
              /* If the previous definition was for an array with incomplete
                 type, and the new definition has completed the type, update
                 the original type to match */
              if (IS_DECL(csym->type) && DCL_TYPE(csym->type)==ARRAY
                  && IS_DECL(sym->type) && DCL_TYPE(sym->type)==ARRAY)
                {
                  if (!DCL_ELEM(csym->type) && DCL_ELEM(sym->type))
                    DCL_ELEM(csym->type) = DCL_ELEM(sym->type);
                }

              #if 0
              /* If only one of the definitions used the "at" keyword, copy */
              /* the address to the other. */
              if (IS_SPEC(csym->etype) && SPEC_ABSA(csym->etype)
                  && IS_SPEC(sym->etype) && !SPEC_ABSA(sym->etype))
                {
                  SPEC_ABSA (sym->etype) = 1;
                  SPEC_ADDR (sym->etype) = SPEC_ADDR (csym->etype);
                }
              if (IS_SPEC(csym->etype) && !SPEC_ABSA(csym->etype)
                  && IS_SPEC(sym->etype) && SPEC_ABSA(sym->etype))
                {
                  SPEC_ABSA (csym->etype) = 1;
                  SPEC_ADDR (csym->etype) = SPEC_ADDR (sym->etype);
                }
              #endif

              error = 0;
              if (csym->ival && sym->ival)
                error = 1;
              if (compareTypeExact (csym->type, sym->type, sym->level) != 1)
                error = 1;
            }

          if (error)
            {
              /* one definition extern ? */
              if (IS_EXTERN (csym->etype) || IS_EXTERN (sym->etype))
                werror (E_EXTERN_MISMATCH, sym->name);
              else
                werror (E_DUPLICATE, sym->name);
              werrorfl (csym->fileDef, csym->lineDef, E_PREVIOUS_DEF);
              #if 0
              fprintf (stderr, "from type '");
              printTypeChain (csym->type, stderr);
              if (IS_SPEC (csym->etype) && SPEC_ABSA (csym->etype))
                fprintf(stderr, " at 0x%x", SPEC_ADDR (csym->etype));
              fprintf (stderr, "'\nto type '");
              printTypeChain (sym->type, stderr);
              if (IS_SPEC (sym->etype) && SPEC_ABSA (sym->etype))
                fprintf(stderr, " at 0x%x", SPEC_ADDR (sym->etype));
              fprintf (stderr, "'\n");
              #endif
              continue;
            }

          if (csym->ival && !sym->ival)
            sym->ival = csym->ival;

          if (!csym->cdef && !sym->cdef && IS_EXTERN (sym->etype))
            {
              /* if none of symbols is a compiler defined function
                 and at least one is not extern
                 then set the new symbol to non extern */
              SPEC_EXTR(sym->etype) = SPEC_EXTR(csym->etype);
            }

          /* delete current entry */
          deleteSym (SymbolTab, csym, csym->name);
          deleteFromSeg(csym);

          symPtrPtr = symHead;
          while (*symPtrPtr && *symPtrPtr != csym)
            symPtrPtr = &(*symPtrPtr)->next;
          if (*symPtrPtr == csym)
            *symPtrPtr = csym->next;
        }

      /* add new entry */
      addSym (SymbolTab, sym, sym->name, sym->level, sym->block, 1);
    }
}


/*------------------------------------------------------------------*/
/* funcInChain - DCL Type 'FUNCTION' found in type chain            */
/*------------------------------------------------------------------*/
int
funcInChain (sym_link * lnk)
{
  while (lnk)
    {
      if (IS_FUNC (lnk))
        return 1;
      lnk = lnk->next;
    }
  return 0;
}

/*------------------------------------------------------------------*/
/* structElemType - returns the type info of a struct member        */
/*------------------------------------------------------------------*/
sym_link *
structElemType (sym_link * stype, value * id)
{
  symbol *fields = (SPEC_STRUCT (stype) ? SPEC_STRUCT (stype)->fields : NULL);
  sym_link *type, *etype;
  sym_link *petype = getSpec (stype);

  if (fields && id)
    {
      /* look for the id */
      while (fields)
        {
          if (strcmp (fields->rname, id->name) == 0)
            {
              sym_link *t;
              type = copyLinkChain (fields->type);
              etype = getSpec (type);
              SPEC_SCLS (etype) = (SPEC_SCLS (petype) == S_REGISTER ?
                                   SPEC_SCLS (etype) : SPEC_SCLS (petype));
              SPEC_OCLS (etype) = (SPEC_SCLS (petype) == S_REGISTER ?
                                   SPEC_OCLS (etype) : SPEC_OCLS (petype));
              /* find the first non-array link */
              t = type;
              while (IS_ARRAY (t))
                t = t->next;
              if (IS_SPEC (t))
                SPEC_CONST (t) |= SPEC_CONST (stype);
              else
                DCL_PTR_CONST (t) |= SPEC_CONST (stype);
              return type;
            }
          fields = fields->next;
        }
    }

  werror (E_NOT_MEMBER, id->name);

  // the show must go on
  return newIntLink();
}

/*------------------------------------------------------------------*/
/* getStructElement - returns element of a tructure definition      */
/*------------------------------------------------------------------*/
symbol *
getStructElement (structdef * sdef, symbol * sym)
{
  symbol *field;

  for (field = sdef->fields; field; field = field->next)
    if (strcmp (field->name, sym->name) == 0)
      return field;

  werror (E_NOT_MEMBER, sym->name);

  return sdef->fields;
}

/*------------------------------------------------------------------*/
/* compStructSize - computes the size of a structure                */
/*------------------------------------------------------------------*/
int
compStructSize (int su, structdef * sdef)
{
  int sum = 0, usum = 0;
  int bitOffset = 0;
  symbol *loop;

  /* for the identifiers  */
  loop = sdef->fields;
  while (loop)
    {
      /* create the internal name for this variable */
      SNPRINTF (loop->rname, sizeof(loop->rname), "_%s", loop->name);
      if (su == UNION)
        {
          sum = 0;
          bitOffset = 0;
        }
      SPEC_VOLATILE (loop->etype) |= (su == UNION ? 1 : 0);

      /* if this is a bit field  */
      if (loop->bitVar)
        {
          SPEC_BUNNAMED (loop->etype) = loop->bitUnnamed;

          /* change it to a unsigned bit */
          SPEC_NOUN (loop->etype) = V_BITFIELD;
          /* ISO/IEC 9899 J.3.9 implementation defined behaviour: */
          /* a "plain" int bitfield is unsigned */
          if (!loop->etype->select.s.b_signed)
            SPEC_USIGN(loop->etype) = 1;

          if (loop->bitVar == BITVAR_PAD)
            {
              /* A zero length bitfield forces padding */
              SPEC_BLEN (loop->etype) = 0;
              SPEC_BSTR (loop->etype) = bitOffset;
              if (bitOffset > 0)
                bitOffset = 8; /* padding is not needed when at bit 0 */
              loop->offset = sum;
            }
          else
            {
              SPEC_BLEN (loop->etype) = loop->bitVar;

              if (bitOffset == 8)
                {
                  bitOffset = 0;
                  sum++;
                }
              /* check if this fit into the remaining   */
              /* bits of this byte else align it to the */
              /* next byte boundary                     */
              if (loop->bitVar <= (8 - bitOffset))
                {
                  /* fits into current byte */
                  loop->offset = sum;
                  SPEC_BSTR (loop->etype) = bitOffset;
                  bitOffset += loop->bitVar;
                }
              else if (!bitOffset)
                {
                  /* does not fit, but is already byte aligned */
                  loop->offset = sum;
                  SPEC_BSTR (loop->etype) = bitOffset;
                  bitOffset += loop->bitVar;
                }
              else
                {
                  if( TARGET_IS_PIC16 && getenv("PIC16_PACKED_BITFIELDS") )
                    {
                      /* if PIC16 && enviroment variable is set, then
                       * tightly pack bitfields, this means that when a
                       * bitfield goes beyond byte alignment, do not
                       * automatically start allocatint from next byte,
                       * but also use the available bits first */
                      fprintf(stderr, ": packing bitfields in structures\n");
                      SPEC_BSTR (loop->etype) = bitOffset;
                      bitOffset += loop->bitVar;
                      loop->offset = (su == UNION ? sum = 0 : sum);
                    }
                  else
                    {
                      /* does not fit; need to realign first */
                      sum++;
                      loop->offset = (su == UNION ? sum = 0 : sum);
                      bitOffset = 0;
                      SPEC_BSTR (loop->etype) = bitOffset;
                      bitOffset += loop->bitVar;
                    }
                }
              while (bitOffset>8)
                {
                  bitOffset -= 8;
                  sum++;
                }
            }
        }
      else
        {
          /* This is a non-bit field. Make sure we are */
          /* byte aligned first */
          if (bitOffset)
            {
              sum++;
              loop->offset = (su == UNION ? sum = 0 : sum);
              bitOffset = 0;
            }
          loop->offset = sum;
          checkDecl (loop, 1);
          sum += getSize (loop->type);

          /* search for "flexible array members" */
          /* and do some syntax checks */
          if (su == STRUCT && checkStructFlexArray (loop, loop->type))
            {
              /* found a "flexible array member" */
              sdef->b_flexArrayMember = TRUE;
              /* is another struct-member following? */
              if (loop->next)
                werror (E_FLEXARRAY_NOTATEND);
              /* is it the first struct-member? */
              else if (loop == sdef->fields)
                werror (E_FLEXARRAY_INEMPTYSTRCT);
            }
        }

      loop = loop->next;

      /* if union then size = sizeof largest field */
      if (su == UNION)
        {
          /* For UNION, round up after each field */
          sum += ((bitOffset+7)/8);
          usum = max (usum, sum);
        }
    }

  /* For STRUCT, round up after all fields processed */
  if (su != UNION)
    sum += ((bitOffset+7)/8);

  return (su == UNION ? usum : sum);
}

/*-------------------------------------------------------------------*/
/* promoteAnonStructs - promote anonymous struct/union's fields into */
/*                      an enclosing struct/union                    */
/*-------------------------------------------------------------------*/
void
promoteAnonStructs (int su, structdef * sdef)
{
  symbol *field;
  symbol *subfield;
  symbol **tofield;
  symbol *nextfield;
  symbol *dupfield;
  int base;

  tofield = &sdef->fields;
  field = sdef->fields;
  while (field)
    {
      nextfield = field->next;
      if (!*field->name && IS_STRUCT (field->type))
        {
          /* Found an anonymous struct/union. Replace it */
          /* with the fields it contains and adjust all  */
          /* the offsets */

          base = field->offset;
          subfield = copySymbolChain (SPEC_STRUCT (field->type)->fields);
          if (!subfield)
            continue;           /* just in case it's empty */

          *tofield = subfield;
          for (;;)
            {
              /* check for field name conflicts resulting from promotion */
              dupfield = sdef->fields;
              while (dupfield && dupfield != subfield)
                {
                  if (*subfield->name && !strcmp (dupfield->name, subfield->name))
                    {
                      werrorfl (subfield->fileDef, subfield->lineDef,
                                E_DUPLICATE_MEMBER,
                                su==STRUCT ? "struct" : "union",
                                subfield->name);
                      werrorfl (dupfield->fileDef, dupfield->lineDef,
                                E_PREVIOUS_DEF);
                    }
                  dupfield = dupfield->next;
                }

              subfield->offset += base;
              if (subfield->next)
                subfield = subfield->next;
              else
                break;
            }
          subfield->next = nextfield;
          tofield = &subfield->next;
        }
      else
        tofield = &field->next;
      field = nextfield;
    }
}


/*------------------------------------------------------------------*/
/* checkSClass - check the storage class specification              */
/*------------------------------------------------------------------*/
static void
checkSClass (symbol * sym, int isProto)
{
  sym_link *t;

  if (getenv("DEBUG_SANITY")) {
    fprintf (stderr, "checkSClass: %s \n", sym->name);
  }

  /* type is literal can happen for enums change to auto */
  if (SPEC_SCLS (sym->etype) == S_LITERAL && !SPEC_ENUM (sym->etype))
    SPEC_SCLS (sym->etype) = S_AUTO;

  /* if sfr or sbit then must also be volatile */
  if (SPEC_SCLS (sym->etype) == S_SBIT ||
      SPEC_SCLS (sym->etype) == S_SFR)
    {
      SPEC_VOLATILE (sym->etype) = 1;
    }

  /* make sure restrict is only used with pointers */
  if (SPEC_RESTRICT (sym->etype))
    {
      werrorfl (sym->fileDef, sym->lineDef, E_BAD_RESTRICT);
      SPEC_RESTRICT (sym->etype) = 0;
    }
  t = sym->type;
  while (t)
    {
      if (IS_DECL (t) && DCL_PTR_RESTRICT (t) && !IS_PTR (t))
        {
          werrorfl (sym->fileDef, sym->lineDef, E_BAD_RESTRICT);
          DCL_PTR_RESTRICT (t) = 0;
          break;
        }
      t = t->next;
    }

  /* if absolute address given then it mark it as
     volatile -- except in the PIC port */

#if !OPT_DISABLE_PIC || !OPT_DISABLE_PIC16
  /* The PIC port uses a different peep hole optimizer based on "pCode" */
  if (!TARGET_IS_PIC && !TARGET_IS_PIC16)
#endif

    if (IS_ABSOLUTE (sym->etype))
      SPEC_VOLATILE (sym->etype) = 1;

  if (TARGET_IS_MCS51 &&
      IS_ABSOLUTE (sym->etype) &&
      SPEC_SCLS (sym->etype) == S_SFR)
    {
      int n, size;
      unsigned addr;

      if (SPEC_NOUN (sym->etype) == V_CHAR)
        size = 8;
      else if (SPEC_LONG (sym->etype) == 0)
        size = 16;
      else
        size = 32;

      addr = SPEC_ADDR (sym->etype);
      for (n=0; n<size; n+=8)
        if (((addr >> n) & 0xFF) < 0x80)
          werror (W_SFR_ABSRANGE, sym->name);
    }

  /* If code memory is read only, then pointers to code memory */
  /* implicitly point to constants -- make this explicit       */
  CodePtrPointsToConst (sym->type);

  /* global variables declared const put into code */
  /* if no other storage class specified */
  if (sym->level == 0 &&
      SPEC_SCLS(sym->etype) == S_FIXED &&
      !IS_FUNC(sym->type))
    {
      /* find the first non-array link */
      t = sym->type;
      while (IS_ARRAY(t))
        t = t->next;
      if (IS_CONSTANT (t))
        SPEC_SCLS (sym->etype) = S_CODE;
    }

  /* global variable in code space is a constant */
  if (sym->level == 0 &&
      SPEC_SCLS (sym->etype) == S_CODE &&
      port->mem.code_ro)
    {
      /* find the first non-array link */
      t = sym->type;
      while (IS_ARRAY(t))
        t = t->next;
      if (IS_SPEC(t))
        SPEC_CONST (t) = 1;
      else
        DCL_PTR_CONST (t) = 1;
    }

  /* if bit variable then no storage class can be */
  /* specified since bit is already a storage */
  if (IS_BITVAR (sym->etype) &&
      (SPEC_SCLS (sym->etype) != S_FIXED &&
       SPEC_SCLS (sym->etype) != S_SBIT &&
       SPEC_SCLS (sym->etype) != S_BIT)
     )
    {
      werror (E_BITVAR_STORAGE, sym->name);
      SPEC_SCLS (sym->etype) = S_FIXED;
    }

  /* extern variables cannot be initialized */
  if (IS_EXTERN (sym->etype) && sym->ival)
    {
      werror (E_EXTERN_INIT, sym->name);
      sym->ival = NULL;
    }

  /* if this is an automatic symbol */
  if (sym->level && (options.stackAuto || reentrant)) {
    if (SPEC_SCLS (sym->etype) != S_BIT) {
      if ((SPEC_SCLS (sym->etype) == S_AUTO ||
           SPEC_SCLS (sym->etype) == S_FIXED ||
           SPEC_SCLS (sym->etype) == S_REGISTER ||
           SPEC_SCLS (sym->etype) == S_STACK ||
           SPEC_SCLS (sym->etype) == S_XSTACK)) {
        SPEC_SCLS (sym->etype) = S_AUTO;
      } else {
        /* storage class may only be specified for statics */
        if (!IS_STATIC(sym->etype)) {
          werror (E_AUTO_ASSUMED, sym->name);
        }
      }
    }
  }

  /* automatic symbols cannot be given   */
  /* an absolute address ignore it      */
  if (sym->level && !IS_STATIC(sym->etype) &&
      SPEC_ABSA (sym->etype) &&
      (options.stackAuto || reentrant))
    {
      werror (E_AUTO_ABSA, sym->name);
      SPEC_ABSA (sym->etype) = 0;
    }

  /* arrays & pointers cannot be defined for bits   */
  /* SBITS or SFRs or BIT                           */
  if ((IS_ARRAY (sym->type) || IS_PTR (sym->type)) &&
      !IS_FUNCPTR (sym->type) &&
      (SPEC_NOUN (sym->etype) == V_BIT ||
       SPEC_NOUN (sym->etype) == V_SBIT ||
       SPEC_NOUN (sym->etype) == V_BITFIELD ||
       SPEC_SCLS (sym->etype) == S_SFR))
    werror (E_BIT_ARRAY, sym->name);

  /* if this is a bit|sbit then set length & start  */
  if (SPEC_NOUN (sym->etype) == V_BIT ||
      SPEC_NOUN (sym->etype) == V_SBIT)
    {
      SPEC_BLEN (sym->etype) = 1;
      SPEC_BSTR (sym->etype) = 0;
    }

  if (!isProto) {
    /* variables declared in CODE space must have */
    /* initializers if not an extern */
    if (SPEC_SCLS (sym->etype) == S_CODE &&
        sym->ival == NULL &&
        !sym->_isparm &&
        //!sym->level &&
        port->mem.code_ro &&
        !IS_EXTERN (sym->etype) &&
        !SPEC_ABSA (sym->etype) &&
        !funcInChain (sym->type))
      werror (E_CODE_NO_INIT, sym->name);
  }

  /* if parameter or local variable then change */
  /* the storage class to reflect where the var will go */
  if (sym->level && SPEC_SCLS (sym->etype) == S_FIXED
   && !IS_STATIC(sym->etype)
      )
    {
      if (options.stackAuto || (currFunc && IFFUNC_ISREENT (currFunc->type)))
        {
          SPEC_SCLS (sym->etype) = (options.useXstack ?
                                    S_XSTACK : S_STACK);
        }
      else
        {
          /* hack-o-matic! I see no reason why the useXstack option should ever
           * control this allocation, but the code was originally that way, and
           * changing it for non-390 ports breaks the compiler badly.
           */
          bool useXdata = (TARGET_IS_DS390 || TARGET_IS_DS400) ?
                1 : options.useXstack;
          SPEC_SCLS (sym->etype) = (useXdata ?
                                    S_XDATA : S_FIXED);
        }
    }
}

/*------------------------------------------------------------------*/
/* changePointer - change pointer to functions                      */
/*------------------------------------------------------------------*/
void
changePointer (sym_link * p)
{
  /* go thru the chain of declarations   */
  /* if we find a pointer to a function  */
  /* change it to a ptr to code area     */
  /* unless the function is banked.      */
  for (; p; p = p->next)
    {
      if (!IS_SPEC (p) && DCL_TYPE (p) == UPOINTER)
        DCL_TYPE (p) = port->unqualified_pointer;
      if (IS_PTR (p) && IS_FUNC (p->next))
        if (!IFFUNC_BANKED(p->next))
        DCL_TYPE (p) = CPOINTER;
    }
}

/*------------------------------------------------------------------*/
/* checkDecl - does semantic validation of a declaration            */
/*------------------------------------------------------------------*/
int
checkDecl (symbol * sym, int isProto)
{
  checkSClass (sym, isProto);        /* check the storage class     */
  changePointer (sym->type);         /* change pointers if required */

  /* if this is an array without any dimension
     then update the dimension from the initial value */
  if (IS_ARRAY (sym->type) && !DCL_ELEM (sym->type))
    DCL_ELEM (sym->type) = getNelements (sym->type, sym->ival);

  return 0;
}

/*------------------------------------------------------------------*/
/* copyLinkChain - makes a copy of the link chain & rets ptr 2 head */
/*------------------------------------------------------------------*/
sym_link *
copyLinkChain (sym_link * p)
{
  sym_link *head, *curr, *loop;

  /* note: v_struct and v_struct->fields are not copied! */
  curr = p;
  head = loop = (curr ? newLink (p->class) : (void *) NULL);
  while (curr)
    {
      memcpy (loop, curr, sizeof (sym_link));   /* copy it */
      loop->next = (curr->next ? newLink (curr->next->class) : (void *) NULL);
      loop = loop->next;
      curr = curr->next;
    }

  return head;
}

/*------------------------------------------------------------------*/
/* cleanUpBlock - cleansup the symbol table specified for all the   */
/*                symbols in the given block                        */
/*------------------------------------------------------------------*/
void
cleanUpBlock (bucket ** table, int block)
{
  int i;
  bucket *chain;

  /* go thru the entire  table  */
  for (i = 0; i < 256; i++)
    {
      for (chain = table[i]; chain; chain = chain->next)
        {
          if (chain->block >= block)
            {
              deleteSym (table, chain->sym, chain->name);
            }
        }
    }
}

/*------------------------------------------------------------------*/
/* cleanUpLevel - cleansup the symbol table specified for all the   */
/*                symbols in the given level                        */
/*------------------------------------------------------------------*/
void
cleanUpLevel (bucket ** table, int level)
{
  int i;
  bucket *chain;

  /* go thru the entire  table  */
  for (i = 0; i < 256; i++)
    {
      for (chain = table[i]; chain; chain = chain->next)
        {
          if (chain->level >= level)
            {
              deleteSym (table, chain->sym, chain->name);
            }
        }
    }
}

/*------------------------------------------------------------------*/
/* computeTypeOr - computes the resultant type from two types       */
/*------------------------------------------------------------------*/
static sym_link *
computeTypeOr (sym_link * etype1, sym_link * etype2, sym_link * reType)
{
  /* sanity check */
  assert (   (IS_CHAR (etype1) || IS_BIT (etype1))
          && (IS_CHAR (etype2) || IS_BIT (etype2)));

  if (SPEC_USIGN (etype1) == SPEC_USIGN (etype2))
    {
      SPEC_USIGN (reType) = SPEC_USIGN (etype1);
      return reType;
    }

  if (SPEC_USIGN (etype1))
    {
      if (   IS_LITERAL (etype2)
          && floatFromVal (valFromType (etype2)) >= 0)
        SPEC_USIGN (reType) = 1;
      else
        {
          /* promote to int */
          SPEC_USIGN (reType) = 0;
          SPEC_NOUN (reType) = V_INT;
        }
    }
  else /* etype1 signed */
    {
      if (   IS_LITERAL (etype2)
          && floatFromVal (valFromType (etype2)) <= 127)
        SPEC_USIGN (reType) = 0;
      else
        {
          /* promote to int */
          SPEC_USIGN (reType) = 0;
          SPEC_NOUN (reType) = V_INT;
        }
    }

  if (SPEC_USIGN (etype2))
    {
      if (   IS_LITERAL (etype1)
          && floatFromVal (valFromType (etype1)) >= 0)
        SPEC_USIGN (reType) = 1;
      else
        {
          /* promote to int */
          SPEC_USIGN (reType) = 0;
          SPEC_NOUN (reType) = V_INT;
        }
    }
  else /* etype2 signed */
    {
      if (   IS_LITERAL (etype1)
          && floatFromVal (valFromType (etype1)) <= 127)
        SPEC_USIGN (reType) = 0;
      else
        {
          /* promote to int */
          SPEC_USIGN (reType) = 0;
          SPEC_NOUN (reType) = V_INT;
        }
    }
  return reType;
}

/*------------------------------------------------------------------*/
/* computeType - computes the resultant type from two types         */
/*------------------------------------------------------------------*/
sym_link *
computeType (sym_link * type1, sym_link * type2,
             RESULT_TYPE resultType, int op)
{
  sym_link *rType;
  sym_link *reType;
  sym_link *etype1 = getSpec (type1);
  sym_link *etype2;

  etype2 = type2 ? getSpec (type2) : type1;

  /* if one of them is a float then result is a float */
  /* here we assume that the types passed are okay */
  /* and can be cast to one another                */
  /* which ever is greater in size */
  if (IS_FLOAT (etype1) || IS_FLOAT (etype2))
    rType = newFloatLink ();
  /* if both are fixed16x16 then result is float */
  else if (IS_FIXED16X16(etype1) && IS_FIXED16X16(etype2))
    rType = newFixed16x16Link();
  else if (IS_FIXED16X16(etype1) && IS_FLOAT (etype2))
    rType = newFloatLink ();
  else if (IS_FLOAT (etype1) && IS_FIXED16X16 (etype2) )
    rType = newFloatLink ();

  /* if both are bitvars choose the larger one */
  else if (IS_BITVAR (etype1) && IS_BITVAR (etype2))
    rType = SPEC_BLEN (etype1) >= SPEC_BLEN (etype2) ?
            copyLinkChain (type1) : copyLinkChain (type2);

  /* if only one of them is a bit variable then the other one prevails */
  else if (IS_BITVAR (etype1) && !IS_BITVAR (etype2))
    {
      rType = copyLinkChain (type2);
      /* bitfield can have up to 16 bits */
      if (getSize (etype1) > 1)
        SPEC_NOUN (getSpec (rType)) = V_INT;
    }
  else if (IS_BITVAR (etype2) && !IS_BITVAR (etype1))
    {
      rType = copyLinkChain (type1);
      /* bitfield can have up to 16 bits */
      if (getSize (etype2) > 1)
        SPEC_NOUN (getSpec (rType)) = V_INT;
    }
  /* if one of them is a pointer or array then that
     prevails */
  else if (IS_PTR (type1) || IS_ARRAY (type1))
    rType = copyLinkChain (type1);
  else if (IS_PTR (type2) || IS_ARRAY (type2))
    rType = copyLinkChain (type2);
  else if (getSize (type1) > getSize (type2))
    rType = copyLinkChain (type1);
  else
    rType = copyLinkChain (type2);

  reType = getSpec (rType);

  /* avoid conflicting types */
  reType->select.s.b_signed = 0;

  /* if result is a literal then make not so */
  if (IS_LITERAL (reType))
    SPEC_SCLS (reType) = S_REGISTER;

  switch (resultType)
    {
      case RESULT_TYPE_IFX:
        if (TARGET_IS_HC08)
          break;
        //fallthrough
      case RESULT_TYPE_BIT:
        if (op == ':')
          {
            SPEC_NOUN (reType) = V_BIT;
            return rType;
          }
        break;
      case RESULT_TYPE_CHAR:
        if (IS_BITVAR (reType))
          {
            SPEC_NOUN (reType) = V_CHAR;
            SPEC_SCLS (reType) = 0;
            SPEC_USIGN (reType) = 0;
            return rType;
          }
        break;
      case RESULT_TYPE_INT:
      case RESULT_TYPE_NONE:
      case RESULT_TYPE_OTHER:
        if (IS_BIT (reType))
          {
            SPEC_NOUN (reType) = V_CHAR;
            SPEC_SCLS (reType) = 0;
            SPEC_USIGN (reType) = 0;
            return rType;
          }
        else if (IS_BITFIELD (reType))
          {
            /* could be smarter, but it depends on the op */
            /* this is for the worst case: a multiplication of 4 * 4 bit */
            SPEC_NOUN (reType) = SPEC_BLEN (reType) <= 4 ? V_CHAR : V_INT;
            SPEC_SCLS (reType) = 0;
            SPEC_USIGN (reType) = 0;
            return rType;
          }
        else if (IS_CHAR (reType))
          {
            /* promotion of some special cases */
            switch (op)
              {
                case '|':
                case '^':
                  return computeTypeOr (etype1, etype2, reType);
                case '&':
                  if (SPEC_USIGN (etype1) != SPEC_USIGN (etype2))
                    {
                      SPEC_USIGN (reType) = 1;
                      return rType;
                    }
                  break;
                case '*':
                  SPEC_NOUN (reType) = V_INT;
                  SPEC_USIGN (reType) = 0;
                  return rType;
                case '/':
                  /* if both are unsigned char then no promotion required */
                  if (!(SPEC_USIGN (etype1) && SPEC_USIGN (etype2)))
                    {
                      SPEC_NOUN (reType) = V_INT;
                      SPEC_USIGN (reType) = 0;
                      return rType;
                    }
                  break;
                default:
                  break;
              }
          }
        break;
      default:
        break;
    }

  /* SDCC's sign promotion:
     - if one or both operands are unsigned, the resultant type will be unsigned
       (except char, see below)
     - if an operand is promoted to a larger type (char -> int, int -> long),
       the larger type will be signed

     SDCC tries hard to avoid promotion to int and does 8 bit calculation as
     much as possible. We're leaving ISO IEC 9899 here and have to extrapolate
     the standard. The standard demands, that the result has to be the same
     "as if" the promotion would have been performed:

     - if the result of an operation with two char's is promoted to a
       larger type, the result will be signed.

     More sophisticated are these:
     - if the result of an operation with two char's is a char again,
       the result will only then be unsigned, if both operands are
       unsigned. In all other cases the result will be signed.

       This seems to be contradictionary to the first two rules, but it makes
       real sense (all types are char's):

        A signed char can be negative; this must be preserved in the result
                -1 * 100 = -100;

        Only if both operands are unsigned it's safe to make the result
        unsigned; this helps to avoid overflow:
                2 * 100 =  200;

     - ToDo: document '|', '^' and '&'

     Homework: - why is (200 * 200 < 0) true?
               - why is { char l = 200, r = 200; (r * l > 0) } true?
  */

  if (!IS_FLOAT (reType)
      && (   (SPEC_USIGN (etype1)
              /* if this operand is promoted to a larger type,
                 then it will be promoted to a signed type */
              && !(bitsForType (etype1) < bitsForType (reType))
              /* char require special handling */
              && !IS_CHAR (etype1))
          || /* same for 2nd operand */
             (SPEC_USIGN (etype2)
              && !(bitsForType (etype2) < bitsForType (reType))
              && !IS_CHAR (etype2))
          || /* if both are 'unsigned char' and not promoted
                let the result be unsigned too */
             (   SPEC_USIGN (etype1)
              && SPEC_USIGN (etype2)
              && IS_CHAR (etype1)
              && IS_CHAR (etype2)
              && IS_CHAR (reType))))
    SPEC_USIGN (reType) = 1;
  else
    SPEC_USIGN (reType) = 0;

  return rType;
}

/*------------------------------------------------------------------*/
/* compareFuncType - compare function prototypes                    */
/*------------------------------------------------------------------*/
int
compareFuncType (sym_link * dest, sym_link * src)
{
  value *exargs, *acargs;
  value *checkValue;
  int argCnt = 0;

  /* if not type then some kind of error */
  if (!dest || !src)
    return 0;

  /* check the return value type   */
  if (compareType (dest->next, src->next) <= 0)
    return 0;

  /* Really, reentrant should match regardless of argCnt, but     */
  /* this breaks some existing code (the fp lib functions). If    */
  /* the first argument is always passed the same way, this       */
  /* lax checking is ok (but may not be true for in future ports) */
  if (IFFUNC_ISREENT (dest) != IFFUNC_ISREENT (src)
      && argCnt>1)
    {
      //printf("argCnt = %d\n",argCnt);
      return 0;
    }

  if (IFFUNC_ISWPARAM (dest) != IFFUNC_ISWPARAM (src))
    {
      return 0;
    }

  if (IFFUNC_ISSHADOWREGS (dest) != IFFUNC_ISSHADOWREGS (src))
    {
      return 0;
    }

  /* compare expected args with actual args */
  exargs = FUNC_ARGS(dest);
  acargs = FUNC_ARGS(src);

  /* for all the expected args do */
  for (argCnt = 1;
       exargs && acargs;
       exargs = exargs->next, acargs = acargs->next, argCnt++)
    {
      /* If the actual argument is an array, any prototype
       * will have modified it to a pointer. Duplicate that
       * change here.
       */
      if (IS_AGGREGATE (acargs->type))
        {
          checkValue = copyValue (acargs);
          aggregateToPointer (checkValue);
        }
      else
        {
          checkValue = acargs;
        }

      if (compareType (exargs->type, checkValue->type) <= 0)
        {
          return 0;
        }
    }

  /* if one them ended we have a problem */
  if ((exargs && !acargs && !IS_VOID (exargs->type)) ||
      (!exargs && acargs && !IS_VOID (acargs->type)))
    {
      return 0;
    }

  return 1;
}

int
comparePtrType (sym_link * dest, sym_link * src, bool bMustCast)
{
  int res;

  if (IS_VOID (src->next) && IS_VOID (dest->next))
    return bMustCast ? -1 : 1;
  if ((IS_VOID (src->next) && !IS_VOID (dest->next)) ||
      (!IS_VOID (src->next) && IS_VOID (dest->next)) )
    return -1;
  res = compareType (dest->next, src->next);
  if (res == 1)
    return bMustCast ? -1 : 1;
  else if (res == -2)
    return -2;
  else
    return 0;
}

/*--------------------------------------------------------------------*/
/* compareType - will do type check return 1 if match, 0 if no match, */
/*               -1 if castable, -2 if only signedness differs        */
/*--------------------------------------------------------------------*/
int
compareType (sym_link * dest, sym_link * src)
{
  if (!dest && !src)
    return 1;

  if (dest && !src)
    return 0;

  if (src && !dest)
    return 0;

  /* if dest is a declarator then */
  if (IS_DECL (dest))
    {
      if (IS_DECL (src))
        {
          /* banked function pointer */
          if (IS_GENPTR (dest) && IS_GENPTR (src))
            {
              if (IS_FUNC (src->next) && IS_VOID(dest->next))
                return -1;
              if (IS_FUNC (dest->next) && IS_VOID(src->next))
                return -1;
              return comparePtrType(dest, src, FALSE);
            }

          if (DCL_TYPE (src) == DCL_TYPE (dest))
            {
              if (IS_FUNC(src))
                {
                  return compareFuncType(dest, src);
                }
              return comparePtrType(dest, src, FALSE);
            }
          if (IS_PTR (dest) && IS_GENPTR (src) && IS_VOID(src->next))
            {
              return -1;
            }
          if (IS_PTR (src) &&
              (IS_GENPTR (dest) ||
               ((DCL_TYPE(src) == POINTER) && (DCL_TYPE(dest) == IPOINTER))
             ))
            {
              return comparePtrType(dest, src, TRUE);
            }
          if (IS_PTR (dest) && IS_ARRAY (src))
            {
              value *val=aggregateToPointer (valFromType(src));
              int res=compareType (dest, val->type);
              Safe_free(val->type);
              Safe_free(val);
              return res;
            }
          if (IS_PTR (dest) && IS_FUNC (dest->next) && IS_FUNC (src))
            return compareType (dest->next, src);
          return 0;
        }
      else if (IS_PTR (dest) && IS_INTEGRAL (src))
        return -1;
      else
        return 0;
    }

  if (IS_PTR (src) && IS_VOID (dest))
    return -1;

  /* if one is a specifier and the other is not */
  if ((IS_SPEC (src) && !IS_SPEC (dest)) ||
      (IS_SPEC (dest) && !IS_SPEC (src)))
    return 0;

  /* if one of them is a void then ok */
  if (SPEC_NOUN (dest) == V_VOID &&
      SPEC_NOUN (src) != V_VOID)
    return -1;

  if (SPEC_NOUN (dest) != V_VOID &&
      SPEC_NOUN (src) == V_VOID)
    return -1;

  /* if they are both bitfields then if the lengths
     and starts don't match */
  if (IS_BITFIELD (dest) && IS_BITFIELD (src) &&
      (SPEC_BLEN (dest) != SPEC_BLEN (src) ||
       SPEC_BSTR (dest) != SPEC_BSTR (src)))
    return -1;

  /* it is a specifier */
  if (SPEC_NOUN (dest) != SPEC_NOUN (src))
    {
      if (SPEC_USIGN (dest) == SPEC_USIGN (src) &&
          IS_INTEGRAL (dest) && IS_INTEGRAL (src) &&
          /* I would prefer
          bitsForType (dest) == bitsForType (src))
             instead of the next two lines, but the regression tests fail with
             them; I guess it's a problem with replaceCheaperOp  */
          getSize (dest) == getSize (src) &&
          (IS_BIT (dest) == IS_BIT (src)))
        return 1;
      else if (IS_ARITHMETIC (dest) && IS_ARITHMETIC (src))
        return -1;
      else
        return 0;
    }
  else if (IS_STRUCT (dest))
    {
      if (SPEC_STRUCT (dest) != SPEC_STRUCT (src))
        return 0;
      else
        return 1;
    }
  if (SPEC_LONG (dest) != SPEC_LONG (src))
    return -1;

  if (SPEC_USIGN (dest) != SPEC_USIGN (src))
    return -2;

  return 1;
}

/*--------------------------------------------------------------------*/
/* compareTypeExact - will do type check return 1 if match exactly    */
/*--------------------------------------------------------------------*/
int
compareTypeExact (sym_link * dest, sym_link * src, int level)
{
  STORAGE_CLASS srcScls, destScls;

  if (!dest && !src)
    return 1;

  if (dest && !src)
    return 0;

  if (src && !dest)
    return 0;

  /* if dest is a declarator then */
  if (IS_DECL (dest))
    {
      if (IS_DECL (src))
        {
          if (DCL_TYPE (src) == DCL_TYPE (dest))
            {
              if ((DCL_TYPE (src) == ARRAY) && (DCL_ELEM (src) != DCL_ELEM (dest)))
                return 0;
              if (DCL_PTR_CONST (src) != DCL_PTR_CONST (dest))
                return 0;
              if (DCL_PTR_VOLATILE (src) != DCL_PTR_VOLATILE (dest))
                return 0;
              if (IS_FUNC(src))
                {
                  value *exargs, *acargs, *checkValue;

                  /* verify function return type */
                  if (!compareTypeExact (dest->next, src->next, -1))
                    return 0;
                  if (FUNC_ISISR (dest) != FUNC_ISISR (src))
                    return 0;
                  if (FUNC_REGBANK (dest) != FUNC_REGBANK (src))
                    return 0;
                  if (IFFUNC_ISNAKED (dest) != IFFUNC_ISNAKED (src))
                    return 0;
                  #if 0
                  if (IFFUNC_ISREENT (dest) != IFFUNC_ISREENT (src) && argCnt>1)
                    return 0;
                  #endif

                  /* compare expected args with actual args */
                  exargs = FUNC_ARGS(dest);
                  acargs = FUNC_ARGS(src);

                  /* for all the expected args do */
                  for (; exargs && acargs; exargs = exargs->next, acargs = acargs->next)
                    {
                      //checkTypeSanity(acargs->etype, acargs->name);

                      if (IS_AGGREGATE (acargs->type))
                        {
                          checkValue = copyValue (acargs);
                          aggregateToPointer (checkValue);
                        }
                      else
                        checkValue = acargs;

                      #if 0
                      if (!compareTypeExact (exargs->type, checkValue->type, -1))
                        return 0;
                      #endif
                    }

                    /* if one them ended we have a problem */
                    if ((exargs && !acargs && !IS_VOID (exargs->type)) ||
                        (!exargs && acargs && !IS_VOID (acargs->type)))
                      return 0;
                    return 1;
                }
              return compareTypeExact (dest->next, src->next, level);
            }
          return 0;
        }
      return 0;
    }

  /* if one is a specifier and the other is not */
  if ((IS_SPEC (src) && !IS_SPEC (dest)) ||
      (IS_SPEC (dest) && !IS_SPEC (src)))
    return 0;

  /* if one of them is a void then ok */
  if (SPEC_NOUN (dest) != SPEC_NOUN (src))
    return 0;

  /* if they are both bitfields then if the lengths
     and starts don't match */
  if (IS_BITFIELD (dest) && IS_BITFIELD (src) &&
      (SPEC_BLEN (dest) != SPEC_BLEN (src) ||
       SPEC_BSTR (dest) != SPEC_BSTR (src)))
    return 0;

  if (IS_INTEGRAL (dest))
    {
      /* signedness must match */
      if (SPEC_USIGN (dest) != SPEC_USIGN (src))
        return 0;
      /* size must match */
      if (SPEC_LONG (dest) != SPEC_LONG (src))
        return 0;
      if (SPEC_SHORT (dest) != SPEC_SHORT (src))
        return 0;
    }

  if (IS_STRUCT (dest))
    {
      if (SPEC_STRUCT (dest) != SPEC_STRUCT (src))
        return 0;
    }

  if (SPEC_CONST (dest) != SPEC_CONST (src))
    return 0;
  if (SPEC_VOLATILE (dest) != SPEC_VOLATILE (src))
    return 0;
  if (SPEC_STAT (dest) != SPEC_STAT (src))
    return 0;
  if (SPEC_ABSA (dest) != SPEC_ABSA (src))
    return 0;
  if (SPEC_ABSA (dest) && SPEC_ADDR (dest) != SPEC_ADDR (src))
    return 0;

  destScls = SPEC_SCLS (dest);
  srcScls = SPEC_SCLS (src);

  /* Compensate for const to const code change in checkSClass() */
  if (((!level) & port->mem.code_ro) && SPEC_CONST (dest))
    {
      if (srcScls == S_CODE && destScls == S_FIXED)
        destScls = S_CODE;
      if (destScls == S_CODE && srcScls == S_FIXED)
        srcScls = S_CODE;
    }

  /* compensate for allocGlobal() */
  if ((srcScls == S_FIXED || srcScls == S_AUTO)
      && port->mem.default_globl_map == xdata
      && !level)
    srcScls = S_XDATA;

  if (level>0 && !SPEC_STAT (dest))
    {
      /* Compensate for hack-o-matic in checkSClass() */
      if (options.stackAuto || (currFunc && IFFUNC_ISREENT (currFunc->type)))
        {
          if (destScls == S_FIXED)
            destScls = (options.useXstack ? S_XSTACK : S_STACK);
          if (srcScls == S_FIXED)
            srcScls = (options.useXstack ? S_XSTACK : S_STACK);
        }
      else if (TARGET_IS_DS390 || TARGET_IS_DS400 || options.useXstack)
        {
          if (destScls == S_FIXED)
            destScls = S_XDATA;
          if (srcScls == S_FIXED)
            srcScls = S_XDATA;
        }
    }

  if (srcScls != destScls)
    {
      #if 0
      printf ("level = %d\n", level);
      printf ("SPEC_SCLS (src) = %d, SPEC_SCLS (dest) = %d\n",
                SPEC_SCLS (src), SPEC_SCLS (dest));
      printf ("srcScls = %d, destScls = %d\n",srcScls, destScls);
      #endif
      return 0;
    }

  return 1;
}

/*------------------------------------------------------------------*/
/* inCalleeSaveList - return 1 if found in callee save list         */
/*------------------------------------------------------------------*/
static int
calleeCmp(void *p1, void *p2)
{
  return (strcmp((char *)p1, (char *)(p2)) == 0);
}

bool
inCalleeSaveList(char *s)
{
  if (options.all_callee_saves)
    return 1;
  return isinSetWith(options.calleeSavesSet, s, calleeCmp);
}

/*-----------------------------------------------------------------*/
/* aggregateToPointer:     change an aggregate type function       */
/*                         argument to a pointer to that type.     */
/*-----------------------------------------------------------------*/
value *
aggregateToPointer (value * val)
{
  if (IS_AGGREGATE (val->type))
    {
      /* if this is a structure */
      if (IS_STRUCT (val->type))
        {
          werror (E_STRUCT_AS_ARG, val->name);
		  return NULL;
        }

      /* change to a pointer depending on the */
      /* storage class specified        */
      switch (SPEC_SCLS (val->etype))
        {
        case S_IDATA:
          DCL_TYPE (val->type) = IPOINTER;
          break;
        case S_PDATA:
          DCL_TYPE (val->type) = PPOINTER;
          break;
        case S_FIXED:
          if (SPEC_OCLS(val->etype)) {
            DCL_TYPE(val->type)=PTR_TYPE(SPEC_OCLS(val->etype));
          } else {
            // this happens for (external) function parameters
            DCL_TYPE (val->type) = port->unqualified_pointer;
          }
          break;
        case S_AUTO:
          DCL_TYPE (val->type) = PTR_TYPE(SPEC_OCLS(val->etype));
          break;
        case S_DATA:
        case S_REGISTER:
          DCL_TYPE (val->type) = POINTER;
          break;
        case S_CODE:
          DCL_TYPE (val->type) = CPOINTER;
          break;
        case S_XDATA:
          DCL_TYPE (val->type) = FPOINTER;
          break;
        case S_EEPROM:
          DCL_TYPE (val->type) = EEPPOINTER;
          break;
        default:
          DCL_TYPE (val->type) = port->unqualified_pointer;
        }

      /* is there is a symbol associated then */
      /* change the type of the symbol as well */
      if (val->sym)
        {
          val->sym->type = copyLinkChain (val->type);
          val->sym->etype = getSpec (val->sym->type);
        }
    }
  return val;
}

/*------------------------------------------------------------------*/
/* checkFunction - does all kinds of check on a function            */
/*------------------------------------------------------------------*/
int
checkFunction (symbol * sym, symbol *csym)
{
  value *exargs, *acargs;
  value *checkValue;
  int argCnt = 0;

  if (getenv("DEBUG_SANITY")) {
    fprintf (stderr, "checkFunction: %s ", sym->name);
  }

  if (!IS_DECL(sym->type) || DCL_TYPE(sym->type)!=FUNCTION)
    {
      werror(E_SYNTAX_ERROR, sym->name);
      return 0;
    }

  /* move inline specifier from return type to function attributes */
  if (IS_INLINE (sym->etype))
    {
      SPEC_INLINE (sym->etype) = 0;
      FUNC_ISINLINE (sym->type) = 1;
    }

  /* make sure the type is complete and sane */
  checkTypeSanity(sym->etype, sym->name);

  /* if not type then some kind of error */
  if (!sym->type)
    return 0;

  /* if the function has no type then make it return int */
  if (!sym->type->next)
    sym->type->next = sym->etype = newIntLink ();

  /* function cannot return aggregate */
  if (IS_AGGREGATE (sym->type->next))
    {
      werror (E_FUNC_AGGR, sym->name);
      return 0;
    }

  /* check if this function is defined as calleeSaves
     then mark it as such */
  FUNC_CALLEESAVES (sym->type) = inCalleeSaveList (sym->name);

  /* if interrupt service routine  */
  /* then it cannot have arguments */
  if (IFFUNC_ARGS (sym->type) && FUNC_ISISR (sym->type))
    {
      if (!IS_VOID (FUNC_ARGS (sym->type)->type))
        {
          werror (E_INT_ARGS, sym->name);
          FUNC_ARGS (sym->type)=NULL;
        }
    }

  if (IFFUNC_ISSHADOWREGS (sym->type) && !FUNC_ISISR (sym->type))
    {
      werror (E_SHADOWREGS_NO_ISR, sym->name);
    }

  for (argCnt=1, acargs = FUNC_ARGS (sym->type);
       acargs;
       acargs=acargs->next, argCnt++)
    {
      if (!acargs->sym)
        {
          // this can happen for reentrant functions
          werror(E_PARAM_NAME_OMITTED, sym->name, argCnt);
          // the show must go on: synthesize a name and symbol
          SNPRINTF (acargs->name, sizeof(acargs->name), "_%s_PARM_%d", sym->name, argCnt);
          acargs->sym = newSymbol (acargs->name, 1);
          SPEC_OCLS (acargs->etype) = istack;
          acargs->sym->type = copyLinkChain (acargs->type);
          acargs->sym->etype = getSpec (acargs->sym->type);
          acargs->sym->_isparm = 1;
          strncpyz (acargs->sym->rname, acargs->name, sizeof(acargs->sym->rname));
        }
      else if (strcmp(acargs->sym->name, acargs->sym->rname)==0)
        {
          // synthesized name
          werror(E_PARAM_NAME_OMITTED, sym->name, argCnt);
        }
    }
  argCnt--;

  /*JCF: Mark the register bank as used*/
  RegBankUsed[FUNC_REGBANK (sym->type)] = 1;

  if (!csym && !(csym = findSym (SymbolTab, sym, sym->name)))
    return 1;                   /* not defined nothing more to check  */

  /* check if body already present */
  if (csym && IFFUNC_HASBODY (csym->type))
    {
      werror (E_FUNC_BODY, sym->name);
      return 0;
    }

  /* check the return value type   */
  if (compareType (csym->type, sym->type) <= 0)
    {
      werror (E_PREV_DEF_CONFLICT, csym->name, "type");
      printFromToType(csym->type, sym->type);
      return 0;
    }

  if (FUNC_ISISR (csym->type) != FUNC_ISISR (sym->type))
    {
      werror (E_PREV_DEF_CONFLICT, csym->name, "interrupt");
    }

  /* I don't think this is necessary for interrupts. An isr is a  */
  /* root in the calling tree.                                    */
  if ((FUNC_REGBANK (csym->type) != FUNC_REGBANK (sym->type)) &&
      (!FUNC_ISISR (sym->type)))
    {
      werror (E_PREV_DEF_CONFLICT, csym->name, "using");
    }

  if (IFFUNC_ISNAKED (csym->type) != IFFUNC_ISNAKED (sym->type))
    {
// disabled since __naked has no influence on the calling convention
//      werror (E_PREV_DEF_CONFLICT, csym->name, "__naked");
      FUNC_ISNAKED (sym->type) = 1;
    }

  /* Really, reentrant should match regardless of argCnt, but     */
  /* this breaks some existing code (the fp lib functions). If    */
  /* the first argument is always passed the same way, this       */
  /* lax checking is ok (but may not be true for in future ports) */
  if (IFFUNC_ISREENT (csym->type) != IFFUNC_ISREENT (sym->type)
      && argCnt>1)
    {
      //printf("argCnt = %d\n",argCnt);
      werror (E_PREV_DEF_CONFLICT, csym->name, "reentrant");
    }

  if (IFFUNC_ISWPARAM (csym->type) != IFFUNC_ISWPARAM (sym->type))
    {
      werror (E_PREV_DEF_CONFLICT, csym->name, "wparam");
    }

  if (IFFUNC_ISSHADOWREGS (csym->type) != IFFUNC_ISSHADOWREGS (sym->type))
    {
      werror (E_PREV_DEF_CONFLICT, csym->name, "shadowregs");
    }

  /* compare expected args with actual args */
  exargs = FUNC_ARGS (csym->type);
  acargs = FUNC_ARGS (sym->type);

  /* for all the expected args do */
  for (argCnt = 1;
       exargs && acargs;
       exargs = exargs->next, acargs = acargs->next, argCnt++)
    {
      if (getenv("DEBUG_SANITY")) {
        fprintf (stderr, "checkFunction: %s ", exargs->name);
      }
      /* make sure the type is complete and sane */
      checkTypeSanity(exargs->etype, exargs->name);

      /* If the actual argument is an array, any prototype
       * will have modified it to a pointer. Duplicate that
       * change here.
       */
      if (IS_AGGREGATE (acargs->type))
        {
          checkValue = copyValue (acargs);
          aggregateToPointer (checkValue);
        }
      else
        {
          checkValue = acargs;
        }

      if (compareType (exargs->type, checkValue->type) <= 0)
        {
          werror (E_ARG_TYPE, argCnt);
          printFromToType(exargs->type, checkValue->type);
          return 0;
        }
    }

  /* if one them ended we have a problem */
  if ((exargs && !acargs && !IS_VOID (exargs->type)) ||
      (!exargs && acargs && !IS_VOID (acargs->type)))
    werror (E_ARG_COUNT);

  /* replace with this defition */
  sym->cdef = csym->cdef;
  deleteSym (SymbolTab, csym, csym->name);
  deleteFromSeg(csym);
  addSym (SymbolTab, sym, sym->name, sym->level, sym->block, 1);
  if (IS_EXTERN (csym->etype) && !IS_EXTERN (sym->etype))
    {
      addSet (&publics, sym);
    }
  return 1;
}

/*------------------------------------------------------------------*/
/* cdbStructBlock - calls struct printing for a blcks               */
/*------------------------------------------------------------------*/
void cdbStructBlock (int block)
{
  int i;
  bucket **table = StructTab;
  bucket *chain;

  /* go thru the entire  table  */
  for (i = 0; i < 256; i++)
    {
      for (chain = table[i]; chain; chain = chain->next)
        {
          if (chain->block >= block)
            {
              if(debugFile)
                debugFile->writeType((structdef *)chain->sym, chain->block, 0, NULL);
            }
        }
    }
}

/*-----------------------------------------------------------------*/
/* processFuncPtrArgs - does some processing with args of func ptrs*/
/*-----------------------------------------------------------------*/
void
processFuncPtrArgs (sym_link * funcType)
{
  value *val = FUNC_ARGS(funcType);

  /* if it is void then remove parameters */
  if (val && IS_VOID (val->type))
    {
      FUNC_ARGS(funcType) = NULL;
      return;
    }
}

/*-----------------------------------------------------------------*/
/* processFuncArgs - does some processing with function args       */
/*-----------------------------------------------------------------*/
void
processFuncArgs (symbol * func)
{
  value *val;
  int pNum = 1;
  sym_link *funcType=func->type;

  if (getenv("SDCC_DEBUG_FUNCTION_POINTERS"))
    fprintf (stderr, "SDCCsymt.c:processFuncArgs(%s)\n", func->name);

  /* find the function declaration within the type */
  while (funcType && !IS_FUNC(funcType))
    funcType=funcType->next;

  /* if this function has variable argument list */
  /* then make the function a reentrant one    */
  if (IFFUNC_HASVARARGS(funcType) || (options.stackAuto && !func->cdef))
    FUNC_ISREENT(funcType)=1;

  /* check if this function is defined as calleeSaves
     then mark it as such */
  FUNC_CALLEESAVES(funcType) = inCalleeSaveList (func->name);

  /* loop thru all the arguments   */
  val = FUNC_ARGS(funcType);

  /* if it is void then remove parameters */
  if (val && IS_VOID (val->type))
    {
      FUNC_ARGS(funcType) = NULL;
      return;
    }

  /* reset regparm for the port */
  (*port->reset_regparms) ();

  /* if any of the arguments is an aggregate */
  /* change it to pointer to the same type */
  while (val)
    {
      int argreg = 0;
      char buffer[SDCC_NAME_MAX+1];

      SNPRINTF (buffer, sizeof(buffer), "%s parameter %d", func->name, pNum);
      checkTypeSanity (val->etype, buffer);

      /* mark it as a register parameter if
         the function does not have VA_ARG
         and as port dictates */
      if (!IFFUNC_HASVARARGS(funcType) &&
          (argreg = (*port->reg_parm) (val->type, FUNC_ISREENT(funcType))))
        {
          SPEC_REGPARM (val->etype) = 1;
          SPEC_ARGREG(val->etype) = argreg;
        }
      else if (IFFUNC_ISREENT(funcType))
        {
          FUNC_HASSTACKPARM(funcType) = 1;
        }

      if (IS_AGGREGATE (val->type))
        {
          aggregateToPointer (val);
        }

      val = val->next;
      pNum++;
    }

  /* if this is an internal generated function call */
  if (func->cdef)
    {
      /* ignore --stack-auto for this one, we don't know how it is compiled */
      /* simply trust on --int-long-reent or --float-reent */
      if (IFFUNC_ISREENT(funcType))
        {
          return;
        }
    }
  else
    {
      /* if this function is reentrant or */
      /* automatics r 2b stacked then nothing */
      if (IFFUNC_ISREENT (funcType) || options.stackAuto)
        return;
    }

  val = FUNC_ARGS(funcType);
  pNum = 1;
  while (val)
    {
      /* if a symbolname is not given  */
      /* synthesize a variable name */
      if (!val->sym)
        {
          SNPRINTF (val->name, sizeof(val->name),
                    "_%s_PARM_%d", func->name, pNum++);
          val->sym = newSymbol (val->name, 1);
          if (SPEC_SCLS(val->etype) == S_BIT)
            SPEC_OCLS (val->etype) = bit;
          else
            SPEC_OCLS (val->etype) = port->mem.default_local_map;
          val->sym->type = copyLinkChain (val->type);
          val->sym->etype = getSpec (val->sym->type);
          val->sym->_isparm = 1;
          strncpyz (val->sym->rname, val->name, sizeof(val->sym->rname));
          addSymChain (&val->sym);
        }
      else                      /* symbol name given create synth name */
        {
          SNPRINTF (val->name, sizeof(val->name), "_%s_PARM_%d", func->name, pNum++);
          strncpyz (val->sym->rname, val->name, sizeof(val->sym->rname));
          val->sym->_isparm = 1;
          if (SPEC_SCLS(val->etype) == S_BIT)
            SPEC_OCLS (val->etype) = SPEC_OCLS (val->sym->etype) = bit;
          else
            SPEC_OCLS (val->etype) = SPEC_OCLS (val->sym->etype) =
              port->mem.default_local_map;
        }
      if (SPEC_OCLS (val->sym->etype) == pdata)
        val->sym->iaccess = 1;
      if (!isinSet(operKeyReset, val->sym))
        {
          addSet (&operKeyReset, val->sym);
          applyToSet (operKeyReset, resetParmKey);
        }
      val = val->next;
    }
}

/*-----------------------------------------------------------------*/
/* isSymbolEqual - compares two symbols return 1 if they match     */
/*-----------------------------------------------------------------*/
int
isSymbolEqual (symbol * dest, symbol * src)
{
  /* if pointers match then equal */
  if (dest == src)
    return 1;

  /* if one of them is null then don't match */
  if (!dest || !src)
    return 0;

  /* if both of them have rname match on rname */
  if (dest->rname[0] && src->rname[0])
    return (!strcmp (dest->rname, src->rname));

  /* otherwise match on name */
  return (!strcmp (dest->name, src->name));
}

void PT(sym_link *type)
{
        printTypeChain(type,0);
}
/*-----------------------------------------------------------------*/
/* printTypeChain - prints the type chain in human readable form   */
/*-----------------------------------------------------------------*/
void
printTypeChain (sym_link * start, FILE * of)
{
  struct dbuf_s dbuf;
  int nlr = 0;

  if (!of)
    {
      of = stdout;
      nlr = 1;
    }

  dbuf_init (&dbuf, 1024);
  dbuf_printTypeChain (start, &dbuf);
  dbuf_write_and_destroy (&dbuf, of);

  if (nlr)
    putc ('\n', of);
}

void
dbuf_printTypeChain (sym_link * start, struct dbuf_s *dbuf)
{
  value *args;
  sym_link * type, * search;
  STORAGE_CLASS scls;

  if (start==NULL) {
    dbuf_append_str (dbuf, "void");
    return;
  }

  /* Print the chain as it is written in the source: */
  /* start with the last entry.                      */
  /* However, the storage class at the end of the    */
  /* chain really applies to the first in the chain! */

  for (type = start; type && type->next; type = type->next)
    ;
  if (IS_SPEC (type))
    scls=SPEC_SCLS(type);
  else
    scls=0;
  while (type)
    {
      if (IS_DECL (type))
        {
          switch (DCL_TYPE (type))
            {
            case FUNCTION:
              dbuf_printf (dbuf, "function %s %s",
                       (IFFUNC_ISBUILTIN(type) ? "__builtin__" : " "),
                       (IFFUNC_ISJAVANATIVE(type) ? "_JavaNative" : " "));
              dbuf_append_str (dbuf, "( ");
              for (args = FUNC_ARGS(type);
                   args;
                   args=args->next) {
                dbuf_printTypeChain(args->type, dbuf);
                if (args->next)
                  dbuf_append_str (dbuf, ", ");
              }
              dbuf_append_str (dbuf, ") ");
              break;
            case GPOINTER:
              dbuf_append_str (dbuf, "generic*");
              break;
            case CPOINTER:
              dbuf_append_str (dbuf, "code*");
              break;
            case FPOINTER:
              dbuf_append_str (dbuf, "xdata*");
              break;
            case EEPPOINTER:
              dbuf_append_str (dbuf, "eeprom*");
              break;
            case POINTER:
              dbuf_append_str (dbuf, "near*");
              break;
            case IPOINTER:
              dbuf_append_str (dbuf, "idata*");
              break;
            case PPOINTER:
              dbuf_append_str (dbuf, "pdata*");
              break;
            case UPOINTER:
              dbuf_append_str (dbuf, "unknown*");
              break;
            case ARRAY:
              if (DCL_ELEM(type))
                {
                  dbuf_printf (dbuf, "[%d]", DCL_ELEM(type));
                }
              else
                {
                  dbuf_append_str (dbuf, "[]");
                }
              break;
            default:
              dbuf_append_str (dbuf, "unknown?");
              break;
            }
          if (!IS_FUNC(type))
            {
              if (DCL_PTR_VOLATILE (type))
                {
                  dbuf_append_str (dbuf, " volatile");
                }
              if (DCL_PTR_CONST (type))
                {
                  dbuf_append_str (dbuf, " const");
                }
              if (DCL_PTR_RESTRICT (type))
                {
                  dbuf_append_str (dbuf, " restrict");
                }
            }
        }
      else
        {
          if (SPEC_VOLATILE (type))
            dbuf_append_str (dbuf, "volatile-");
          if (SPEC_CONST (type))
            dbuf_append_str (dbuf, "const-");
          if (SPEC_USIGN (type))
            dbuf_append_str (dbuf, "unsigned-");
          switch (SPEC_NOUN (type))
            {
            case V_INT:
              if (IS_LONG (type))
                dbuf_append_str (dbuf, "long-");
              dbuf_append_str (dbuf, "int");
              break;

            case V_CHAR:
              dbuf_append_str (dbuf, "char");
              break;

            case V_VOID:
              dbuf_append_str (dbuf, "void");
              break;

            case V_FLOAT:
              dbuf_append_str (dbuf, "float");
              break;

            case V_FIXED16X16:
              dbuf_append_str (dbuf, "fixed16x16");
              break;

            case V_STRUCT:
              dbuf_printf (dbuf, "struct %s", SPEC_STRUCT (type)->tag);
              break;

            case V_SBIT:
              dbuf_append_str (dbuf, "sbit");
              break;

            case V_BIT:
              dbuf_append_str (dbuf, "bit");
              break;

            case V_BITFIELD:
              dbuf_printf (dbuf, "bitfield {%d,%d}", SPEC_BSTR (type), SPEC_BLEN (type));
              break;

            case V_DOUBLE:
              dbuf_append_str (dbuf, "double");
              break;

            default:
              dbuf_append_str (dbuf, "unknown type");
              break;
            }
        }
      if (type==start)
        {
          switch (scls)
            {
            case S_FIXED:       dbuf_append_str (dbuf, " fixed"); break;
            case S_AUTO:        dbuf_append_str (dbuf, " auto"); break;
            case S_REGISTER:    dbuf_append_str (dbuf, " register"); break;
            case S_DATA:        dbuf_append_str (dbuf, " data"); break;
            case S_XDATA:       dbuf_append_str (dbuf, " xdata"); break;
            case S_SFR:         dbuf_append_str (dbuf, " sfr"); break;
            case S_SBIT:        dbuf_append_str (dbuf, " sbit"); break;
            case S_CODE:        dbuf_append_str (dbuf, " code"); break;
            case S_IDATA:       dbuf_append_str (dbuf, " idata"); break;
            case S_PDATA:       dbuf_append_str (dbuf, " pdata"); break;
            case S_LITERAL:     dbuf_append_str (dbuf, " literal"); break;
            case S_STACK:       dbuf_append_str (dbuf, " stack"); break;
            case S_XSTACK:      dbuf_append_str (dbuf, " xstack"); break;
            case S_BIT:         dbuf_append_str (dbuf, " bit"); break;
            case S_EEPROM:      dbuf_append_str (dbuf, " eeprom"); break;
            default: break;
            }
        }

      /* search entry in list before "type" */
      for (search = start; search && search->next != type;)
        search = search->next;
      type = search;
      if (type)
          dbuf_append_char(dbuf, ' ');
    }
}

/*--------------------------------------------------------------------*/
/* printTypeChainRaw - prints the type chain in human readable form   */
/*                     in the raw data structure ordering             */
/*--------------------------------------------------------------------*/
void
printTypeChainRaw (sym_link * start, FILE * of)
{
  int nlr = 0;
  value *args;
  sym_link * type;

  if (!of)
    {
      of = stdout;
      nlr = 1;
    }

  if (start==NULL) {
    fprintf (of, "void");
    return;
  }

  type = start;

  while (type)
    {
      if (IS_DECL (type))
        {
          if (!IS_FUNC(type)) {
            if (DCL_PTR_VOLATILE (type)) {
              fprintf (of, "volatile-");
            }
            if (DCL_PTR_CONST (type)) {
              fprintf (of, "const-");
            }
            if (DCL_PTR_RESTRICT (type)) {
              fprintf (of, "restrict-");
            }
          }
          switch (DCL_TYPE (type))
            {
            case FUNCTION:
              if (IFFUNC_ISINLINE(type)) {
                fprintf (of, "inline-");
              }
              fprintf (of, "function %s %s",
                       (IFFUNC_ISBUILTIN(type) ? "__builtin__" : " "),
                       (IFFUNC_ISJAVANATIVE(type) ? "_JavaNative" : " "));
              fprintf (of, "( ");
              for (args = FUNC_ARGS(type);
                   args;
                   args=args->next) {
                printTypeChain(args->type, of);
                if (args->next)
                  fprintf(of, ", ");
              }
              fprintf (of, ") ");
              break;
            case GPOINTER:
              fprintf (of, "generic* ");
              break;
            case CPOINTER:
              fprintf (of, "code* ");
              break;
            case FPOINTER:
              fprintf (of, "xdata* ");
              break;
            case EEPPOINTER:
              fprintf (of, "eeprom* ");
              break;
            case POINTER:
              fprintf (of, "near* ");
              break;
            case IPOINTER:
              fprintf (of, "idata* ");
              break;
            case PPOINTER:
              fprintf (of, "pdata* ");
              break;
            case UPOINTER:
              fprintf (of, "unknown* ");
              break;
            case ARRAY:
              if (DCL_ELEM(type)) {
                fprintf (of, "[%d] ", DCL_ELEM(type));
              } else {
                fprintf (of, "[] ");
              }
              break;
            }
          if (DCL_TSPEC(type))
            {
              fprintf (of, "{");
              printTypeChainRaw(DCL_TSPEC(type), of);
              fprintf (of, "}");
            }
        }
      else if (IS_SPEC (type))
        {
        switch (SPEC_SCLS (type))
          {
          case S_DATA: fprintf (of, "data-"); break;
          case S_XDATA: fprintf (of, "xdata-"); break;
          case S_SFR: fprintf (of, "sfr-"); break;
          case S_SBIT: fprintf (of, "sbit-"); break;
          case S_CODE: fprintf (of, "code-"); break;
          case S_IDATA: fprintf (of, "idata-"); break;
          case S_PDATA: fprintf (of, "pdata-"); break;
          case S_LITERAL: fprintf (of, "literal-"); break;
          case S_STACK: fprintf (of, "stack-"); break;
          case S_XSTACK: fprintf (of, "xstack-"); break;
          case S_BIT: fprintf (of, "bit-"); break;
          case S_EEPROM: fprintf (of, "eeprom-"); break;
          default: break;
          }
          if (SPEC_VOLATILE (type))
            fprintf (of, "volatile-");
          if (SPEC_CONST (type))
            fprintf (of, "const-");
          if (SPEC_USIGN (type))
            fprintf (of, "unsigned-");
          switch (SPEC_NOUN (type))
            {
            case V_INT:
              if (IS_LONG (type))
                fprintf (of, "long-");
              fprintf (of, "int");
              break;

            case V_CHAR:
              fprintf (of, "char");
              break;

            case V_VOID:
              fprintf (of, "void");
              break;

            case V_FLOAT:
              fprintf (of, "float");
              break;

            case V_FIXED16X16:
              fprintf (of, "fixed16x16");
              break;

            case V_STRUCT:
              fprintf (of, "struct %s", SPEC_STRUCT (type)->tag);
              break;

            case V_SBIT:
              fprintf (of, "sbit");
              break;

            case V_BIT:
              fprintf (of, "bit");
              break;

            case V_BITFIELD:
              fprintf (of, "bitfield {%d,%d}", SPEC_BSTR (type), SPEC_BLEN (type));
              break;

            case V_DOUBLE:
              fprintf (of, "double");
              break;

            default:
              fprintf (of, "unknown type");
              break;
            }
        }
      else
        fprintf (of, "NOT_SPEC_OR_DECL");
      type = type->next;
      if (type)
        fputc (' ', of);
    }
  if (nlr)
    fprintf (of, "\n");
}


/*-----------------------------------------------------------------*/
/* powof2 - returns power of two for the number if number is pow 2 */
/*-----------------------------------------------------------------*/
int
powof2 (TYPE_TARGET_ULONG num)
{
  int nshifts = 0;
  int n1s = 0;

  while (num)
    {
      if (num & 1)
        n1s++;
      num >>= 1;
      nshifts++;
    }

  if (n1s > 1 || nshifts == 0)
    return -1;
  return nshifts - 1;
}

symbol *__fsadd;
symbol *__fssub;
symbol *__fsmul;
symbol *__fsdiv;
symbol *__fseq;
symbol *__fsneq;
symbol *__fslt;
symbol *__fslteq;
symbol *__fsgt;
symbol *__fsgteq;

symbol *__fps16x16_add;
symbol *__fps16x16_sub;
symbol *__fps16x16_mul;
symbol *__fps16x16_div;
symbol *__fps16x16_eq;
symbol *__fps16x16_neq;
symbol *__fps16x16_lt;
symbol *__fps16x16_lteq;
symbol *__fps16x16_gt;
symbol *__fps16x16_gteq;

/* Dims: mul/div/mod, BYTE/WORD/DWORD, SIGNED/UNSIGNED/BOTH */
symbol *__muldiv[3][3][4];
/* Dims: BYTE/WORD/DWORD SIGNED/UNSIGNED */
sym_link *__multypes[3][2];
/* Dims: to/from float, BYTE/WORD/DWORD, SIGNED/USIGNED */
symbol *__conv[2][3][2];
/* Dims: to/from fixed16x16, BYTE/WORD/DWORD/FLOAT, SIGNED/USIGNED */
symbol *__fp16x16conv[2][4][2];
/* Dims: shift left/shift right, BYTE/WORD/DWORD, SIGNED/UNSIGNED */
symbol *__rlrr[2][3][2];

sym_link *charType;
sym_link *floatType;
sym_link *fixed16x16Type;

static char *
_mangleFunctionName(char *in)
{
  if (port->getMangledFunctionName)
    {
      return port->getMangledFunctionName(in);
    }
  else
    {
      return in;
    }
}

/*-----------------------------------------------------------------*/
/* typeFromStr - create a typechain from an encoded string         */
/* basic types -        'c' - char                                 */
/*                      's' - short                                */
/*                      'i' - int                                  */
/*                      'l' - long                                 */
/*                      'f' - float                                */
/*                      'q' - fixed16x16                           */
/*                      'v' - void                                 */
/*                      '*' - pointer - default (GPOINTER)         */
/* modifiers -          'u' - unsigned                             */
/* pointer modifiers -  'g' - generic                              */
/*                      'x' - xdata                                */
/*                      'p' - code                                 */
/*                      'd' - data                                 */
/*                      'F' - function                             */
/* examples : "ig*" - generic int *                                */
/*            "cx*" - char xdata *                                 */
/*            "ui" -  unsigned int                                 */
/*-----------------------------------------------------------------*/
sym_link *typeFromStr (char *s)
{
    sym_link *r = newLink(DECLARATOR);
    int usign = 0;

    do {
        sym_link *nr;
        switch (*s) {
        case 'u' :
            usign = 1;
            s++;
            continue ;
            break ;
        case 'c':
            r->class = SPECIFIER;
            SPEC_NOUN(r) = V_CHAR;
            break;
        case 's':
        case 'i':
            r->class = SPECIFIER;
            SPEC_NOUN(r) = V_INT;
            break;
        case 'l':
            r->class = SPECIFIER;
            SPEC_NOUN(r) = V_INT;
            SPEC_LONG(r) = 1;
            break;
        case 'f':
            r->class = SPECIFIER;
            SPEC_NOUN(r) = V_FLOAT;
            break;
        case 'q':
            r->class = SPECIFIER;
            SPEC_NOUN(r) = V_FIXED16X16;
            break;
        case 'v':
            r->class = SPECIFIER;
            SPEC_NOUN(r) = V_VOID;
            break;
        case '*':
            DCL_TYPE(r) = port->unqualified_pointer;
            break;
        case 'g':
        case 'x':
        case 'p':
        case 'd':
        case 'F':
            assert(*(s+1)=='*');
            nr = newLink(DECLARATOR);
            nr->next = r;
            r = nr;
            switch (*s) {
            case 'g':
                DCL_TYPE(r) = GPOINTER;
                break;
            case 'x':
                DCL_TYPE(r) = FPOINTER;
                break;
            case 'p':
                DCL_TYPE(r) = CPOINTER;
                break;
            case 'd':
                DCL_TYPE(r) = POINTER;
                break;
            case 'F':
                DCL_TYPE(r) = FUNCTION;
                nr = newLink(DECLARATOR);
                nr->next = r;
                r = nr;
                DCL_TYPE(r) = CPOINTER;
                break;
            }
            s++;
            break;
        default:
            werror(E_INTERNAL_ERROR, __FILE__, __LINE__,
                   "typeFromStr: unknown type");
            break;
        }
        if (IS_SPEC(r) && usign) {
            SPEC_USIGN(r) = 1;
            usign = 0;
        }
        s++;
    } while (*s);
    return r;
}

/*-----------------------------------------------------------------*/
/* initCSupport - create functions for C support routines          */
/*-----------------------------------------------------------------*/
void
initCSupport ()
{
  const char *smuldivmod[] =
  {
    "mul", "div", "mod"
  };
  const char *sbwd[] =
  {
    "char", "int", "long", "fixed16x16",
  };
  const char *fp16x16sbwd[] =
  {
    "char", "int", "long", "float",
  };
  const char *ssu[] =
  {
    "s", "su", "us", "u"
  };
  const char *srlrr[] =
  {
    "rl", "rr"
  };

  int bwd, su, muldivmod, tofrom, rlrr;

  if (getenv("SDCC_NO_C_SUPPORT")) {
    /* for debugging only */
    return;
  }

  for (bwd = 0; bwd < 3; bwd++)
    {
      sym_link *l = NULL;
      switch (bwd)
        {
        case 0:
          l = newCharLink ();
          break;
        case 1:
          l = newIntLink ();
          break;
        case 2:
          l = newLongLink ();
          break;
        default:
          assert (0);
        }
      __multypes[bwd][0] = l;
      __multypes[bwd][1] = copyLinkChain (l);
      SPEC_USIGN (__multypes[bwd][1]) = 1;
    }

  floatType = newFloatLink ();
  fixed16x16Type = newFixed16x16Link ();
  charType = (options.unsigned_char) ? UCHARTYPE : SCHARTYPE;

  __fsadd = funcOfType ("__fsadd", floatType, floatType, 2, options.float_rent);
  __fssub = funcOfType ("__fssub", floatType, floatType, 2, options.float_rent);
  __fsmul = funcOfType ("__fsmul", floatType, floatType, 2, options.float_rent);
  __fsdiv = funcOfType ("__fsdiv", floatType, floatType, 2, options.float_rent);
  __fseq = funcOfType ("__fseq", charType, floatType, 2, options.float_rent);
  __fsneq = funcOfType ("__fsneq", charType, floatType, 2, options.float_rent);
  __fslt = funcOfType ("__fslt", charType, floatType, 2, options.float_rent);
  __fslteq = funcOfType ("__fslteq", charType, floatType, 2, options.float_rent);
  __fsgt = funcOfType ("__fsgt", charType, floatType, 2, options.float_rent);
  __fsgteq = funcOfType ("__fsgteq", charType, floatType, 2, options.float_rent);

  __fps16x16_add = funcOfType ("__fps16x16_add", fixed16x16Type, fixed16x16Type, 2, options.float_rent);
  __fps16x16_sub = funcOfType ("__fps16x16_sub", fixed16x16Type, fixed16x16Type, 2, options.float_rent);
  __fps16x16_mul = funcOfType ("__fps16x16_mul", fixed16x16Type, fixed16x16Type, 2, options.float_rent);
  __fps16x16_div = funcOfType ("__fps16x16_div", fixed16x16Type, fixed16x16Type, 2, options.float_rent);
  __fps16x16_eq = funcOfType ("__fps16x16_eq", charType, fixed16x16Type, 2, options.float_rent);
  __fps16x16_neq = funcOfType ("__fps16x16_neq", charType, fixed16x16Type, 2, options.float_rent);
  __fps16x16_lt = funcOfType ("__fps16x16_lt", charType, fixed16x16Type, 2, options.float_rent);
  __fps16x16_lteq = funcOfType ("__fps16x16_lteq", charType, fixed16x16Type, 2, options.float_rent);
  __fps16x16_gt = funcOfType ("__fps16x16_gt", charType, fixed16x16Type, 2, options.float_rent);
  __fps16x16_gteq = funcOfType ("__fps16x16_gteq", charType, fixed16x16Type, 2, options.float_rent);

  for (tofrom = 0; tofrom < 2; tofrom++)
    {
      for (bwd = 0; bwd < 3; bwd++)
        {
          for (su = 0; su < 2; su++)
            {
              if (tofrom)
                {
                  SNPRINTF (buffer, sizeof(buffer), "__fs2%s%s", ssu[su*3], sbwd[bwd]);
                  __conv[tofrom][bwd][su] = funcOfType (buffer, __multypes[bwd][su], floatType, 1, options.float_rent);
                }
              else
                {
                  SNPRINTF (buffer, sizeof(buffer), "__%s%s2fs", ssu[su*3], sbwd[bwd]);
                  __conv[tofrom][bwd][su] = funcOfType (buffer, floatType, __multypes[bwd][su], 1, options.float_rent);
                }
            }
        }
    }

  for (tofrom = 0; tofrom < 2; tofrom++)
    {
      for (bwd = 0; bwd < 4; bwd++)
        {
          for (su = 0; su < 2; su++)
            {
              if (tofrom)
                {
                  SNPRINTF (buffer, sizeof(buffer), "__fps16x162%s%s", ssu[su*3], fp16x16sbwd[bwd]);
                  if(bwd == 3) {
                    __fp16x16conv[tofrom][bwd][su] = funcOfType (buffer, floatType, fixed16x16Type, 1, options.float_rent);
                  } else
                    __fp16x16conv[tofrom][bwd][su] = funcOfType (buffer, __multypes[bwd][su], fixed16x16Type, 1, options.float_rent);
                }
              else
                {
                  SNPRINTF (buffer, sizeof(buffer), "__%s%s2fps16x16", ssu[su*3], fp16x16sbwd[bwd]);
                  if(bwd == 3) {
                    __fp16x16conv[tofrom][bwd][su] = funcOfType (buffer, fixed16x16Type, floatType, 1, options.float_rent);
                  } else
                    __fp16x16conv[tofrom][bwd][su] = funcOfType (buffer, fixed16x16Type, __multypes[bwd][su], 1, options.float_rent);
                }
            }
        }
    }

/*
  for (muldivmod = 0; muldivmod < 3; muldivmod++)
    {
      for (bwd = 0; bwd < 3; bwd++)
        {
          for (su = 0; su < 2; su++)
            {
              SNPRINTF (buffer, sizeof(buffer),
                        "_%s%s%s",
                       smuldivmod[muldivmod],
                       ssu[su*3],
                       sbwd[bwd]);
              __muldiv[muldivmod][bwd][su] = funcOfType (_mangleFunctionName(buffer), __multypes[bwd][su], __multypes[bwd][su], 2, options.intlong_rent);
              FUNC_NONBANKED (__muldiv[muldivmod][bwd][su]->type) = 1;
            }
        }
    }

  muluint() and mulsint() resp. mululong() and mulslong() return the same result.
  Therefore they've been merged into mulint() and mullong().
*/

  /* byte */
  bwd = 0;
  for (su = 0; su < 4; su++)
    {
      for (muldivmod = 0; muldivmod < 3; muldivmod++)
        {
          /* muluchar, mulschar, mulsuchar and muluschar are separate functions, because e.g. the z80
             port is sign/zero-extending to int before calling mulint() */
          /* div and mod : s8_t x s8_t -> s8_t should be s8_t x s8_t -> s16_t, see below */
          if (!TARGET_IS_PIC16 || muldivmod != 1 || su != 0)
            {
              SNPRINTF (buffer, sizeof(buffer),
                  "_%s%s%s",
                  smuldivmod[muldivmod],
                  ssu[su],
                  sbwd[bwd]);
              __muldiv[muldivmod][bwd][su] = funcOfType (
                  _mangleFunctionName(buffer),
                  __multypes[bwd][su%2],
                  __multypes[bwd][su/2],
                  2,
                  options.intlong_rent);
              FUNC_NONBANKED (__muldiv[muldivmod][bwd][su]->type) = 1;
            }
        }
    }

  for (bwd = 1; bwd < 3; bwd++)
    {
      for (su = 0; su < 2; su++)
        {
          for (muldivmod = 1; muldivmod < 3; muldivmod++)
            {
              /* div and mod : s8_t x s8_t -> s8_t should be s8_t x s8_t -> s16_t, see below */
              if (!TARGET_IS_PIC16 || muldivmod != 1 || bwd != 0 || su != 0)
              {
                SNPRINTF (buffer, sizeof(buffer),
                    "_%s%s%s",
                    smuldivmod[muldivmod],
                    ssu[su*3],
                    sbwd[bwd]);
                __muldiv[muldivmod][bwd][su] = funcOfType (
                    _mangleFunctionName(buffer),
                    __multypes[bwd][su],
                    __multypes[bwd][su],
                    2,
                    options.intlong_rent);
                FUNC_NONBANKED (__muldiv[muldivmod][bwd][su]->type) = 1;
              }
            }
        }
    }

  if (TARGET_IS_PIC16)
  {
    /* PIC16 port wants __divschar/__modschar to return an int, so that both
     * 100 / -4 = -25 and -128 / -1 = 128 can be handled correctly
     * (first one would have to be sign extended, second one must not be).
     * Similarly, modschar should be handled, but the iCode introduces cast
     * here and forces '% : s8 x s8 -> s8' ... */
    su = 0; bwd = 0;
    for (muldivmod = 1; muldivmod < 2; muldivmod++) {
      SNPRINTF (buffer, sizeof(buffer),
          "_%s%s%s",
          smuldivmod[muldivmod],
          ssu[su],
          sbwd[bwd]);
      __muldiv[muldivmod][bwd][su] = funcOfType (
          _mangleFunctionName(buffer),
          __multypes[1][su],
          __multypes[bwd][su],
          2,
          options.intlong_rent);
      FUNC_NONBANKED (__muldiv[muldivmod][bwd][su]->type) = 1;
    }
  }

  /* mul only */
  muldivmod = 0;
  /* signed only */
  su = 0;
  /* word and doubleword */
  for (bwd = 1; bwd < 3; bwd++)
    {
      /* mul, int/long */
      SNPRINTF (buffer, sizeof(buffer),
                "_%s%s",
                smuldivmod[muldivmod],
                sbwd[bwd]);
      __muldiv[muldivmod][bwd][0] = funcOfType (_mangleFunctionName(buffer), __multypes[bwd][su], __multypes[bwd][su], 2, options.intlong_rent);
      FUNC_NONBANKED (__muldiv[muldivmod][bwd][0]->type) = 1;
      /* signed = unsigned */
      __muldiv[muldivmod][bwd][1] = __muldiv[muldivmod][bwd][0];
    }

  for (rlrr = 0; rlrr < 2; rlrr++)
    {
      for (bwd = 0; bwd < 3; bwd++)
        {
          for (su = 0; su < 2; su++)
            {
              SNPRINTF (buffer, sizeof(buffer),
                        "_%s%s%s",
                       srlrr[rlrr],
                       ssu[su*3],
                       sbwd[bwd]);
              __rlrr[rlrr][bwd][su] = funcOfType (_mangleFunctionName(buffer), __multypes[bwd][su], __multypes[0][0], 2, options.intlong_rent);
              FUNC_NONBANKED (__rlrr[rlrr][bwd][su]->type) = 1;
            }
        }
    }
}

/*-----------------------------------------------------------------*/
/* initBuiltIns - create prototypes for builtin functions          */
/*-----------------------------------------------------------------*/
void initBuiltIns()
{
    int i;
    symbol *sym;

    if (!port->builtintable) return ;

    for (i = 0 ; port->builtintable[i].name ; i++) {
        sym = funcOfTypeVarg(port->builtintable[i].name,port->builtintable[i].rtype,
                             port->builtintable[i].nParms,port->builtintable[i].parm_types);
        FUNC_ISBUILTIN(sym->type) = 1;
        FUNC_ISREENT(sym->type) = 0;    /* can never be reentrant */
    }
}

sym_link *validateLink(sym_link         *l,
                        const char      *macro,
                        const char      *args,
                        const char      select,
                        const char      *file,
                        unsigned        line)
{
  if (l && l->class==select)
    {
        return l;
    }
    fprintf(stderr,
            "Internal error: validateLink failed in %s(%s) @ %s:%u:"
            " expected %s, got %s\n",
            macro, args, file, line,
            DECLSPEC2TXT(select), l ? DECLSPEC2TXT(l->class) : "null-link");
    exit(EXIT_FAILURE);
    return l; // never reached, makes compiler happy.
}

/*--------------------------------------------------------------------*/
/* newEnumType - create an integer type compatible with enumerations  */
/*--------------------------------------------------------------------*/
sym_link *
newEnumType (symbol *enumlist)
{
  int min, max, v;
  symbol *sym;
  sym_link *type;

  if (!enumlist)
    {
      type = newLink (SPECIFIER);
      SPEC_NOUN (type) = V_INT;
      return type;
    }

  /* Determine the range of the enumerated values */
  sym = enumlist;
  min = max = (int) ulFromVal (valFromType (sym->type));
  for (sym = sym->next; sym; sym = sym->next)
    {
      v = (int) ulFromVal (valFromType (sym->type));
      if (v<min)
        min = v;
      if (v>max)
        max = v;
    }

  /* Determine the smallest integer type that is compatible with this range */
  type = newLink (SPECIFIER);
  if (min>=0 && max<=255)
    {
      SPEC_NOUN (type) = V_CHAR;
      SPEC_USIGN (type) = 1;
    }
  else if (min>=-128 && max<=127)
    {
      SPEC_NOUN (type) = V_CHAR;
    }
  else if (min>=0 && max<=65535)
    {
      SPEC_NOUN (type) = V_INT;
      SPEC_USIGN (type) = 1;
    }
  else if (min>=-32768 && max<=32767)
    {
      SPEC_NOUN (type) = V_INT;
    }
  else
    {
      SPEC_NOUN (type) = V_INT;
      SPEC_LONG (type) = 1;
      if (min>=0)
        SPEC_USIGN (type) = 1;
    }

  return type;
}

/*-------------------------------------------------------------------*/
/* isConstant - check if the type is constant                        */
/*-------------------------------------------------------------------*/
int
isConstant (sym_link * type)
{
  if (!type)
    return 0;

  while (IS_ARRAY (type))
    type = type->next;

  if (IS_SPEC (type))
    return SPEC_CONST (type);
  else
    return DCL_PTR_CONST (type);
}

/*-------------------------------------------------------------------*/
/* isVolatile - check if the type is volatile                        */
/*-------------------------------------------------------------------*/
int
isVolatile (sym_link * type)
{
  if (!type)
    return 0;

  while (IS_ARRAY (type))
    type = type->next;

  if (IS_SPEC (type))
    return SPEC_VOLATILE (type);
  else
    return DCL_PTR_VOLATILE (type);
}

/*-------------------------------------------------------------------*/
/* isRestrict - check if the type is restricted                      */
/*-------------------------------------------------------------------*/
int
isRestrict (sym_link * type)
{
  if (!type)
    return 0;

  while (IS_ARRAY (type))
    type = type->next;

  if (IS_SPEC (type))
    return SPEC_RESTRICT (type);
  else
    return DCL_PTR_RESTRICT (type);
}
