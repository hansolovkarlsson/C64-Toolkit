/*
 * parser.c - turns the token array from lexer.c into an AST (parser.c
 * builds trees of the Node type from cc64.h), and drives the whole
 * two-pass compilation strategy described in cc64.h's overview.
 *
 * ===================================================================
 * RECURSIVE DESCENT, IN ONE PARAGRAPH
 * ===================================================================
 * This parser is "recursive descent": one function per grammar rule,
 * each calling the functions for the rules nested inside it. `if (E)
 * S` is parsed by parse_stmt() calling parse_expr() for E and
 * parse_stmt() (yes, itself) for S. There's no parser-generator, no
 * table of states - the call graph of these functions directly
 * mirrors the grammar, which is exactly what makes hand-written
 * recursive descent easy to read: to see how `while` loops parse,
 * just go read the T_WHILE branch of parse_stmt() below.
 *
 * ===================================================================
 * EXPRESSIONS: PRECEDENCE CLIMBING
 * ===================================================================
 * C has about 15 levels of operator precedence (`*` binds tighter
 * than `+`, which binds tighter than `<<`, and so on). The standard
 * recursive-descent trick for this is one function per precedence
 * level, each calling the *next tighter* level for its operands:
 *
 *   parse_logor -> parse_logand -> parse_bitor -> parse_bitxor ->
 *   parse_bitand -> parse_equality -> parse_relational ->
 *   parse_shift -> parse_additive -> parse_term -> parse_unary ->
 *   parse_postfix -> parse_primary
 *
 * Each level looks like: "parse one thing at the next tighter level,
 * then while the next token is an operator at *this* level, consume
 * it and parse another thing at the next tighter level, combining
 * them into a bigger node." That loop is what makes `a + b + c` parse
 * as left-associative ((a+b)+c) rather than right-associative. Look
 * at parse_additive() below for the simplest example of the pattern;
 * every other precedence level is the same shape with different
 * tokens.
 *
 * parse_primary(), at the bottom, is where the recursion actually
 * stops: numbers, identifiers, calls, parenthesized sub-expressions.
 *
 * ===================================================================
 * THE TWO PASSES
 * ===================================================================
 * pass_a() walks every token ONCE, recognizing just enough structure
 * to record each function's signature (name, return type, parameter
 * types) and each global's type into the symbol tables from symtab.c
 * - but it never parses a function body, it just skips over the
 * balanced { } with skip_balanced() and moves on. This is deliberately
 * shallow and fast.
 *
 * pass_b() then rewinds to the start of the token array and does the
 * real work: for each function, it re-parses the signature (already
 * validated by pass_a, so this time it's just being skipped past) and
 * then genuinely parses the body with parse_block(), immediately
 * calling emit_function() (codegen_stmt.c) to generate code for it
 * before moving to the next function. Because pass_a already recorded
 * every function and global that exists anywhere in the file, code in
 * one function can reference another function or global no matter
 * which one appears first in the source text.
 */

#include "cc64.h"

/* ===================================================================
 * Shared token cursor
 * ===================================================================
 * g_pos is the parser's read position into g_toks[]. It's deliberately
 * NOT in cc64.h / exposed to other files: every other file that needs
 * to inspect tokens could read g_toks[] directly with its own
 * index, and nothing outside this file ever needs to know "where the
 * parser currently is". Keeping it file-static is a small example of
 * information hiding - the rest of the compiler simply has no way to
 * accidentally interfere with the parser's cursor.
 * =================================================================== */

static int g_pos = 0;

static Token *cur(void) { return &g_toks[g_pos]; }
static Token *peekAt(int off) { return &g_toks[g_pos + off]; }
static Token *advance(void) { return &g_toks[g_pos++]; }
static int check(TokKind k) { return cur()->kind == k; }
/* Consumes the current token if it matches `k`, or fails with a clear
 * error naming what was expected (`what`, e.g. "';'") - this is the
 * workhorse used everywhere the grammar requires a specific token,
 * like the closing ')' of an if-condition. */
static Token *expect(TokKind k, const char *what) {
    if (!check(k)) fatal(cur()->line, "expected %s", what);
    return advance();
}

static int is_type_kw(TokKind k) { return k == T_INT || k == T_CHAR || k == T_VOID || k == T_STRUCT || k == T_UNION || k == T_ENUM || k == T_UNSIGNED; }

/* Whether the CURRENT token starts a type - a keyword (is_type_kw()
 * above) OR a plain identifier that happens to be a registered
 * `typedef` name. This is the one place a typedef name's TEXT (not
 * just its token kind, T_IDENT either way) has to be consulted to
 * decide "is this a declaration or an expression?" - every call site
 * that used to check is_type_kw(cur()->kind) alone to make that
 * decision (pass_a()'s top-level dispatch and parameter parsing,
 * parse_stmt()'s local-declaration and for-loop-init detection) now
 * calls this instead. Struct/union member parsing doesn't need this
 * check at all - a struct/union body is unconditionally a list of
 * member declarations, nothing else, so it just calls
 * parse_type_prefix() directly, which is typedef-aware on its own
 * (see its own comment below). */
static int cur_is_type(void) {
    return is_type_kw(cur()->kind) || (check(T_IDENT) && find_typedef(cur()->text) != NULL);
}

/* Several parsing functions call each other in a cycle that doesn't
 * follow a strict "top to bottom" order (parse_primary() needs
 * parse_assign() for call arguments and parse_expr() for parenthesized
 * sub-expressions, both of which are defined much further down, in
 * terms of parse_primary() itself via the precedence chain) - these
 * forward declarations are what make that legal C. None of these four
 * need to be visible outside this file, so they stay `static` rather
 * than moving to cc64.h. */
static Node *parse_expr(void);
static Node *parse_assign(void);
static Node *parse_stmt(void);
static Node *parse_block(void);
static Node *parse_switch(void);

/* ===================================================================
 * Pass A: scan top-level declarations (signatures only, no bodies)
 * =================================================================== */

/* Consumes a `{ ... }` block without looking at what's inside it,
 * correctly handling nested braces by tracking depth. This is how
 * pass_a() skips a function body cheaply: it doesn't need to
 * understand the body yet, just find where it ends. */
static void skip_balanced(TokKind open, TokKind close) {
    int depth = 1;
    advance(); /* consume the opening token already checked by caller */
    while (depth > 0) {
        if (check(T_EOF)) fatal(cur()->line, "unexpected end of file (unbalanced braces)");
        if (cur()->kind == open) depth++;
        else if (cur()->kind == close) depth--;
        advance();
    }
}

static int type_from_tok(TokKind k) {
    if (k == T_CHAR) return TY_CHAR;
    if (k == T_INT) return TY_INT;
    return -1; /* void */
}

/* Parses "type [*]" (base type + optional pointer star) - the type
 * prefix shared by every kind of declaration: globals, locals,
 * parameters, struct members, and function return types. Does NOT
 * consume an identifier afterward - callers differ on what comes
 * next (a function name that might be followed by `(`, a parameter
 * name possibly followed by more parameters, a struct member name
 * followed by `;`, ...), so this only handles the part that's
 * genuinely identical everywhere: `int`, `char`, `void`, `struct Tag`/
 * `union Tag`/`enum Tag`, or a `typedef` name, then an optional `*`.
 *
 * The typedef case is checked FIRST and returns early, rather than
 * joining the shared `*isPointer = 0; if (check(T_STAR)) ...` tail
 * below every other branch uses: a typedef name can already BE a
 * pointer type (`typedef char *String;`), so it needs its own
 * pointer-to-pointer check (rejecting `String *sp;`, since this
 * compiler doesn't support pointer-to-pointer at all) rather than
 * letting the shared tail blindly reset *isPointer to 0 first. */
static void parse_type_prefix(int *type, int *structTag, int *isPointer, int *isUnsigned) {
    *isUnsigned = 0;
    if (check(T_UNSIGNED)) {
        advance();
        *isUnsigned = 1;
        /* `unsigned` alone means `unsigned int`, matching real C - so
         * `int`/`char` after it are both optional, but nothing else
         * (struct/union/enum/void/a typedef name) is a valid follow,
         * since isUnsigned only ever means something for TY_INT (see
         * CType's own comment in cc64.h) - reject those explicitly
         * with a clear message rather than letting the caller stumble
         * into a confusing "expected identifier"/"expected ';'" once
         * it finds struct/enum/void/the typedef name still sitting
         * there unconsumed. */
        if (check(T_STRUCT) || check(T_UNION))
            fatal(cur()->line, "'unsigned' cannot be combined with '%s'",
                  check(T_UNION) ? "union" : "struct");
        if (check(T_ENUM))
            fatal(cur()->line, "'unsigned' cannot be combined with 'enum'");
        if (check(T_VOID))
            fatal(cur()->line, "'unsigned void' is not valid ('void' has no signedness)");
        if (check(T_IDENT) && find_typedef(cur()->text))
            fatal(cur()->line, "'unsigned' cannot be combined with typedef name '%s'", cur()->text);
        if (check(T_INT) || check(T_CHAR)) {
            *type = type_from_tok(advance()->kind);
        } else {
            *type = TY_INT; /* bare 'unsigned' means 'unsigned int'; whatever
                                token follows (an identifier) is the caller's
                                to consume, same as a bare 'int' would leave it */
        }
        *structTag = -1;
        *isPointer = 0;
        if (check(T_STAR)) { *isPointer = 1; advance(); }
        return;
    }
    if (check(T_IDENT)) {
        TypedefEntry *td = find_typedef(cur()->text);
        if (td) {
            Token *nameTok = advance();
            *type = td->type;
            *structTag = td->structTag;
            *isPointer = td->isPointer;
            *isUnsigned = td->isUnsigned;
            if (check(T_STAR)) {
                if (*isPointer)
                    fatal(cur()->line, "pointer-to-pointer is not supported in this "
                                        "version ('%s' is already a pointer type)",
                                        nameTok->text);
                *isPointer = 1;
                advance();
            }
            return;
        }
    }
    if (check(T_STRUCT) || check(T_UNION)) {
        int wantUnion = check(T_UNION);
        advance();
        if (!check(T_IDENT))
            fatal(cur()->line, "expected %s tag name after '%s'",
                  wantUnion ? "union" : "struct", wantUnion ? "union" : "struct");
        Token *tagTok = advance();
        char *tag = tagTok->text;
        int tagIdx = find_or_create_struct_tag(tag);
        /* A tag already fully defined as the OTHER kind is always a
         * real error, not something to defer the way an incomplete
         * (not-yet-defined) tag is below - struct and union tags share
         * one namespace (see StructDef's own comment in cc64.h), so
         * `union Foo` used where `Foo` is already a real struct can
         * never become valid no matter what comes later in the file. */
        if (g_structs[tagIdx].defined && g_structs[tagIdx].isUnion != wantUnion)
            fatal(tagTok->line, "'%s' is a %s, not a %s", tag,
                  g_structs[tagIdx].isUnion ? "union" : "struct",
                  wantUnion ? "union" : "struct");
        /* Not yet defined - tentatively record which keyword THIS
         * reference used, so an error about this still-incomplete tag
         * (e.g. a by-value parameter check, which fires regardless of
         * completeness) says the right word. A real StructDef, freshly
         * created by find_or_create_struct_tag() above, defaults
         * isUnion to 0 (memset), which would otherwise make every
         * as-yet-undefined tag look like a struct even when every
         * reference to it so far has said `union`. */
        if (!g_structs[tagIdx].defined) g_structs[tagIdx].isUnion = wantUnion;
        *type = TY_STRUCT;
        *structTag = tagIdx;
    } else if (check(T_ENUM)) {
        /* `enum Tag` used as a type is always just `int` (see
         * EnumConst's own comment in cc64.h) - the tag is required
         * (matching struct's own requirement above), and this branch is
         * only ever reached for a type USE ("enum Color c;"), never a
         * definition ("enum Color { ... };"), since pass_a()/pass_b()
         * both detect and fully handle the definition form before
         * falling through to the general parse_type_prefix() path - see
         * parse_enum_def(). Unlike `struct Tag` just above, which is
         * allowed to reference a tag that's only been forward-declared
         * so far (find_or_create_struct_tag() makes an incomplete entry
         * on demand), `enum Tag` has to already be FULLY defined by
         * this point - real C has no such thing as an incomplete enum
         * forward reference, so there's nothing to create here, only to
         * check for; a misspelled or not-yet-defined tag is a compile
         * error immediately, not something deferred. */
        advance();
        if (!check(T_IDENT)) fatal(cur()->line, "expected enum tag name after 'enum'");
        Token *tagTok = advance();
        if (!find_enum_tag(tagTok->text))
            fatal(tagTok->line, "'enum %s' used here but never defined", tagTok->text);
        *type = TY_INT;
        *structTag = -1;
    } else if (check(T_INT) || check(T_CHAR) || check(T_VOID)) {
        *type = type_from_tok(advance()->kind);
        *structTag = -1;
    } else {
        fatal(cur()->line, "expected a type");
    }
    *isPointer = 0;
    if (check(T_STAR)) { *isPointer = 1; advance(); }
    if (*type < 0 && *isPointer) fatal(cur()->line, "'void *' is not supported in this version");
}

/* `struct Tag { member-decl* };` or `union Tag { member-decl* };` -
 * parsed and fully resolved (every member's offset, and the whole
 * aggregate's total size) entirely here in pass_a, since a body is
 * just a list of declarations with no expressions or statements that
 * would need deferring to pass_b.
 *
 * Shared between both keywords, parameterized by `isUnion`, rather
 * than two near-identical copies: struct and union differ in exactly
 * one thing - how a member's OFFSET (and the aggregate's total SIZE)
 * is computed. A struct member gets the next free byte, accumulating
 * as usual; a union member always starts at byte 0 (every member
 * overlaps every other one, C's whole point of a union), and the
 * aggregate's size is its WIDEST member's width, not their sum.
 * Everything else - member type restrictions, duplicate-name checking,
 * how a StructMember/StructDef gets filled in - is identical, and
 * keeping it as one function is what guarantees a future fix to any of
 * that shared logic can't accidentally apply to only one of the two.
 *
 * struct and union tags share one namespace/table (g_structs) here,
 * matching real C - you can't have both a `struct Foo` and a
 * `union Foo`, and this function's own redefinition check enforces
 * that directly. See find_or_create_struct_tag()'s comment in
 * symtab.c for how self- and mutually-referential aggregates (a member
 * pointing back to its own struct/union, or to another one defined
 * later in the file) work - equally for both keywords, since neither
 * one cares at reference time which kind the tag will turn out to be,
 * only once a member needs the target's actual size. */
static void parse_struct_or_union_def(int isUnion) {
    const char *kw = isUnion ? "union" : "struct";
    advance(); /* 'struct' or 'union' */
    char *tagname = advance()->text; /* tag name, already confirmed T_IDENT by the caller */
    int tagIdx = find_or_create_struct_tag(tagname);
    StructDef *sd = &g_structs[tagIdx];
    if (sd->defined) {
        if (sd->isUnion != isUnion)
            fatal(cur()->line, "'%s' is already defined as a %s, not a %s",
                  tagname, sd->isUnion ? "union" : "struct", kw);
        fatal(cur()->line, "redefinition of %s '%s'", kw, tagname);
    }
    sd->isUnion = isUnion;
    advance(); /* '{' */
    int offset = 0;    /* struct: next free byte, accumulating */
    int maxWidth = 0;  /* union: widest member seen so far */
    while (!check(T_RBRACE)) {
        int mtype, mStructTag, mIsPointer, mIsUnsigned;
        parse_type_prefix(&mtype, &mStructTag, &mIsPointer, &mIsUnsigned);
        if (mtype < 0) fatal(cur()->line, "'void' is not a valid member type");
        if (mtype == TY_STRUCT && !mIsPointer)
            fatal(cur()->line, "%s members must be int, char, or a pointer "
                                "(a nested struct/union-by-value member is not "
                                "supported in this version - use a pointer instead)", kw);
        if (!check(T_IDENT)) fatal(cur()->line, "expected member name");
        char *mname = advance()->text;
        if (check(T_LBRACKET)) fatal(cur()->line, "array members are not supported in this version");
        expect(T_SEMI, "';'");
        for (int i = 0; i < sd->nmembers; i++)
            if (strcmp(sd->members[i].name, mname) == 0)
                fatal(cur()->line, "duplicate member '%s' in %s '%s'", mname, kw, tagname);
        if (sd->nmembers >= (int)(sizeof(sd->members)/sizeof(sd->members[0])))
            fatal(cur()->line, "too many members in %s '%s'", kw, tagname);
        StructMember *m = &sd->members[sd->nmembers++];
        memset(m, 0, sizeof(*m));
        strncpy(m->name, mname, sizeof(m->name)-1);
        m->type = mtype; m->isPointer = mIsPointer; m->structTag = mStructTag;
        m->isUnsigned = mIsUnsigned;
        m->width = var_width(mtype, mIsPointer, mStructTag); /* always 2 for a pointer
            member - safe even if mStructTag is still incomplete (self/mutual
            reference), since var_width() checks isPointer before ever touching
            g_structs[structTag] */
        if (isUnion) {
            m->offset = 0;
            if (m->width > maxWidth) maxWidth = m->width;
        } else {
            m->offset = offset;
            offset += m->width;
        }
    }
    advance(); /* '}' */
    expect(T_SEMI, "';'");
    sd->size = isUnion ? maxWidth : offset;
    sd->defined = 1;
}

/* Parses a constant integer for a context that needs one this early -
 * a global initializer, a `case` label, an array size - none of which
 * have a real constant-expression evaluator behind them (see
 * `.struct`'s identical restriction in c64asm-reference.md §10 for the
 * sibling assembler's take on the same underlying limitation: nothing
 * here runs before a symbol table exists to look anything more general
 * up in). Accepts a plain literal (decimal/hex/char) or the name of an
 * already-defined enum constant, each optionally negated by a leading
 * '-' when `allowNeg` is set (array sizes pass 0 - a negative size is
 * never valid, so there's nothing to allow). `what` names the context
 * in the error message when neither matches. */
static long parse_const_value(int allowNeg, const char *what) {
    int neg = 0;
    if (allowNeg && check(T_MINUS)) { neg = 1; advance(); }
    if (check(T_NUM) || check(T_CHARLIT)) {
        long v = advance()->ival;
        return neg ? -v : v;
    }
    if (check(T_IDENT)) {
        EnumConst *ec = find_enum_const(cur()->text);
        if (ec) { advance(); return neg ? -ec->value : ec->value; }
    }
    fatal(cur()->line, "%s must be a constant integer literal or a "
                        "previously-declared enum constant", what);
    return 0; /* unreachable */
}

/* `enum [Tag] { NAME [= value], ... };` - parsed and fully resolved
 * (every enumerator's actual integer value) entirely here in pass_a,
 * the same as struct - a purely compile-time source of named
 * constants, nothing here emits any code or storage. Unlike struct,
 * the tag (if given at all - it's optional here, unlike struct's own
 * required tag) isn't grouped with anything: `enum Tag` used later as
 * a TYPE is just an alias for `int` (see parse_type_prefix()), so
 * there's no per-enum layout to look back up the way a struct
 * reference needs - only whether the tag was ever defined at all
 * (g_enumtags, checked by parse_type_prefix()) and the flat list of
 * constants themselves (g_enumconsts, symtab.c), with no association
 * kept between a constant and which enum it came from. An enumerator's
 * own value must be a plain literal, optionally negated - NOT a
 * reference to an earlier enumerator in the same enum (`B = A + 1`
 * doesn't work) - for the same "no constant-expression evaluator yet"
 * reason parse_const_value() above exists at all; omit `= value` and
 * it defaults to one more than the previous enumerator (zero for the
 * first), exactly like real C.
 *
 * Unlike a struct tag, an enum tag can never be forward-referenced -
 * real C doesn't allow `enum Tag;` as an incomplete forward
 * declaration the way `struct Tag;` works, so there's no equivalent of
 * find_or_create_struct_tag()'s "create an incomplete entry" case here
 * to worry about; a tag simply doesn't exist until this function
 * finishes defining it. */
static void parse_enum_def(void) {
    advance(); /* 'enum' */
    char *tagname = NULL;
    if (check(T_IDENT)) {
        Token *tagTok = advance();
        tagname = tagTok->text;
        if (find_enum_tag(tagname))
            fatal(tagTok->line, "redefinition of enum '%s'", tagname);
    }
    expect(T_LBRACE, "'{'");
    if (check(T_RBRACE)) fatal(cur()->line, "'enum' requires at least one enumerator");
    long next = 0;
    while (!check(T_RBRACE)) {
        if (!check(T_IDENT)) fatal(cur()->line, "expected an enumerator name");
        Token *nt = advance();
        char *ename = nt->text;
        long value = next;
        if (check(T_ASSIGN)) {
            advance();
            int neg = 0;
            if (check(T_MINUS)) { neg = 1; advance(); }
            if (!check(T_NUM) && !check(T_CHARLIT))
                fatal(cur()->line, "enumerator '%s's value must be a constant "
                                    "integer literal", ename);
            value = advance()->ival;
            if (neg) value = -value;
        }
        if (is_builtin(ename)) fatal(nt->line, "'%s' is a reserved builtin name", ename);
        if (find_enum_const(ename)) fatal(nt->line, "redefinition of enumerator '%s'", ename);
        if (find_global(ename)) fatal(nt->line, "'%s' is already declared as a global", ename);
        if (find_func(ename)) fatal(nt->line, "'%s' is already declared as a function", ename);
        if (g_nenumconsts >= (int)(sizeof(g_enumconsts)/sizeof(g_enumconsts[0])))
            fatal(nt->line, "too many enum constants");
        EnumConst *ec = &g_enumconsts[g_nenumconsts++];
        strncpy(ec->name, ename, sizeof(ec->name)-1);
        ec->value = value;
        next = value + 1;
        if (check(T_COMMA)) { advance(); continue; } /* trailing comma before '}' is fine too */
        break;
    }
    expect(T_RBRACE, "'}'");
    expect(T_SEMI, "';'");
    if (tagname) {
        if (g_nenumtags >= (int)(sizeof(g_enumtags)/sizeof(g_enumtags[0])))
            fatal(0, "too many enum tags");
        strncpy(g_enumtags[g_nenumtags++], tagname, sizeof(g_enumtags[0])-1);
    }
}

/* `typedef <type-prefix> Name;` - parsed and fully resolved entirely
 * here in pass_a, the same as struct/union/enum: a purely compile-time
 * source of an alias, nothing here emits any code or storage. Reuses
 * parse_type_prefix() itself for the underlying type-prefix, which is
 * what makes `typedef struct Tag Tag;`/`typedef int MyInt;`/
 * `typedef char *String;` all just work with no new type-parsing logic
 * of its own - see TypedefEntry's own comment in cc64.h for exactly
 * what this does and doesn't support (no inline anonymous-aggregate
 * definitions combined with the typedef in one statement, top-level
 * only, same as struct/union/enum). */
static void parse_typedef_def(void) {
    advance(); /* 'typedef' */
    int type, structTag, isPointer, isUnsigned;
    parse_type_prefix(&type, &structTag, &isPointer, &isUnsigned);
    if (!check(T_IDENT)) fatal(cur()->line, "expected an identifier after typedef's type");
    Token *nameTok = advance();
    char *name = nameTok->text;
    if (check(T_LBRACKET)) fatal(cur()->line, "array typedefs are not supported in this version");
    if (is_builtin(name)) fatal(nameTok->line, "'%s' is a reserved builtin name", name);
    if (find_typedef(name)) fatal(nameTok->line, "redefinition of typedef '%s'", name);
    if (find_global(name)) fatal(nameTok->line, "'%s' is already declared as a global", name);
    if (find_func(name)) fatal(nameTok->line, "'%s' is already declared as a function", name);
    if (find_enum_const(name)) fatal(nameTok->line, "'%s' is already declared as an enum constant", name);
    expect(T_SEMI, "';'");
    if (g_ntypedefs >= (int)(sizeof(g_typedefs)/sizeof(g_typedefs[0])))
        fatal(nameTok->line, "too many typedefs");
    TypedefEntry *td = &g_typedefs[g_ntypedefs++];
    strncpy(td->name, name, sizeof(td->name)-1);
    td->type = type; td->isPointer = isPointer; td->structTag = structTag;
    td->isUnsigned = isUnsigned;
}

/* One iteration of this loop handles exactly one top-level
 * declaration: a `struct Tag { ... };` definition, a function
 * (`type [*] name ( params ) { body }`, body skipped, not parsed), or
 * a global variable (`type [*] name [size];`, optionally with a
 * simple constant initializer). Everything this function records goes
 * straight into g_structs[]/g_funcs[]/g_globals[]. */
void pass_a(void) {
    g_pos = 0;
    while (!check(T_EOF)) {
        /* Unlike struct/union/enum, `typedef` needs no lookahead to
         * detect - the keyword itself is unambiguous, never a valid
         * start of anything else. */
        if (check(T_TYPEDEF)) {
            parse_typedef_def();
            continue;
        }
        /* A struct DEFINITION (`struct Tag {`) is the one top-level
         * form that doesn't fit the "type [*] name" shape everything
         * else here has - it needs to be recognized before falling
         * into the general parse_type_prefix() path, which would
         * otherwise treat `struct Tag` as if it were introducing a
         * variable or function called `Tag`. Peeking two tokens ahead
         * (past 'struct' and the tag name) to check for '{' is what
         * distinguishes a definition from `struct Tag *next;` or
         * `struct Tag g;`, both of which DO fall through to the
         * general path below. */
        if (check(T_STRUCT) && peekAt(1)->kind == T_IDENT && peekAt(2)->kind == T_LBRACE) {
            parse_struct_or_union_def(0);
            continue;
        }
        /* Same for `union Tag {` - union's tag is required, same as
         * struct's, so the detection shape is identical. */
        if (check(T_UNION) && peekAt(1)->kind == T_IDENT && peekAt(2)->kind == T_LBRACE) {
            parse_struct_or_union_def(1);
            continue;
        }
        /* Same idea for `enum`, except the tag is optional here, so
         * there are two definition shapes to recognize instead of
         * struct's one: anonymous ("enum {") and tagged ("enum Tag {").
         * Anything else starting with 'enum' (just "enum Tag" with no
         * '{' following) falls through to the general type-prefix path
         * below, same as `struct Tag *next;` already does. */
        if (check(T_ENUM) && (peekAt(1)->kind == T_LBRACE ||
                               (peekAt(1)->kind == T_IDENT && peekAt(2)->kind == T_LBRACE))) {
            parse_enum_def();
            continue;
        }

        if (!cur_is_type())
            fatal(cur()->line, "expected type at top level");
        int rtype, rStructTag, rIsPointer, rIsUnsigned;
        parse_type_prefix(&rtype, &rStructTag, &rIsPointer, &rIsUnsigned);
        if (!check(T_IDENT)) fatal(cur()->line, "expected identifier after type");
        char *name = advance()->text;
        if (is_builtin(name))
            fatal(cur()->line, "'%s' is a reserved builtin name", name);

        if (check(T_LPAREN)) {
            /* function */
            advance();
            FnSym *fn = find_func(name);
            if (!fn) {
                if (g_nfuncs >= (int)(sizeof(g_funcs)/sizeof(g_funcs[0])))
                    fatal(cur()->line, "too many functions");
                fn = &g_funcs[g_nfuncs++];
                memset(fn, 0, sizeof(*fn));
                strncpy(fn->name, name, sizeof(fn->name)-1);
                fn->retType = rtype;
                fn->retIsPointer = rIsPointer;
                fn->retStructTag = rStructTag;
                fn->retIsUnsigned = rIsUnsigned;
            }
            if (rtype == TY_STRUCT && !rIsPointer) {
                const char *kw = g_structs[rStructTag].isUnion ? "union" : "struct";
                fatal(cur()->line, "function '%s' returns a %s by value; return "
                                    "'%s %s *' instead (structs/unions must be passed by "
                                    "pointer in this version)", name, kw, kw, g_structs[rStructTag].name);
            }
            fn->nparams = 0;
            if (check(T_VOID) && peekAt(1)->kind == T_RPAREN) {
                advance();
            } else if (!check(T_RPAREN)) {
                for (;;) {
                    if (!cur_is_type())
                        fatal(cur()->line, "expected parameter type");
                    int ptype, pStructTag, pIsPointer, pIsUnsigned;
                    parse_type_prefix(&ptype, &pStructTag, &pIsPointer, &pIsUnsigned);
                    if (ptype < 0) fatal(cur()->line, "void is not a valid parameter type");
                    if (!check(T_IDENT)) fatal(cur()->line, "expected parameter name");
                    char *pname = advance()->text;
                    if (check(T_LBRACKET))
                        fatal(cur()->line, "array parameters are not supported; use a pointer instead");
                    if (ptype == TY_STRUCT && !pIsPointer) {
                        const char *kw = g_structs[pStructTag].isUnion ? "union" : "struct";
                        fatal(cur()->line, "parameter '%s' is a %s passed by value; use "
                                            "'%s %s *%s' instead (structs/unions must be passed "
                                            "by pointer in this version)", pname, kw, kw,
                                            g_structs[pStructTag].name, pname);
                    }
                    if (fn->nparams >= 32) fatal(cur()->line, "too many parameters");
                    fn->paramTypes[fn->nparams] = ptype;
                    fn->paramIsPointer[fn->nparams] = pIsPointer;
                    fn->paramStructTag[fn->nparams] = pStructTag;
                    fn->paramIsUnsigned[fn->nparams] = pIsUnsigned;
                    strncpy(fn->paramNames[fn->nparams], pname, 63);
                    fn->nparams++;
                    if (check(T_COMMA)) { advance(); continue; }
                    break;
                }
            }
            expect(T_RPAREN, "')'");
            if (check(T_SEMI)) { advance(); continue; } /* prototype only */
            if (!check(T_LBRACE)) fatal(cur()->line, "expected '{' or ';'");
            fn->defined = 1;
            skip_balanced(T_LBRACE, T_RBRACE);
            continue;
        }

        /* global variable */
        if (rtype < 0) fatal(cur()->line, "'void' is not a valid variable type");
        GSym g; memset(&g, 0, sizeof(g));
        strncpy(g.name, name, sizeof(g.name)-1);
        g.type = rtype;
        g.isPointer = rIsPointer;
        g.structTag = rStructTag;
        g.isUnsigned = rIsUnsigned;
        if (check(T_LBRACKET)) {
            if (rIsPointer) fatal(cur()->line, "arrays of pointers are not supported in this version");
            advance();
            g.isArray = 1;
            g.arrLen = (int)parse_const_value(0, "array size");
            expect(T_RBRACKET, "']'");
        }
        if (rtype == TY_STRUCT && !rIsPointer)
            require_complete_struct(rStructTag, cur()->line); /* need a known size to allocate storage */
        if (check(T_ASSIGN)) {
            advance();
            if (g.isPointer) fatal(cur()->line, "pointer initializers are not supported in this version");
            if (g.type == TY_STRUCT) fatal(cur()->line, "struct/union initializers are not supported in this version");
            g.hasInit = 1;
            g.initVal = parse_const_value(1, "global initializer's value");
        }
        expect(T_SEMI, "';'");
        if (find_global(g.name)) fatal(cur()->line, "redefinition of global '%s'", g.name);
        if (g_nglobals >= (int)(sizeof(g_globals)/sizeof(g_globals[0])))
            fatal(cur()->line, "too many globals");
        g_globals[g_nglobals++] = g;
    }
}

/* ===================================================================
 * Parser: expressions (precedence climbing - see file header)
 * =================================================================== */

/* The bottom of the precedence chain: literals, identifiers, function
 * calls, and parenthesized sub-expressions (which "reset" back to the
 * loosest precedence, parse_expr(), since anything can appear inside
 * parentheses regardless of its own precedence). */
static Node *parse_primary(void) {
    Token *t = cur();
    if (t->kind == T_NUM) {
        advance();
        Node *n = node_new(N_NUM, t->line); n->ival = t->ival; return n;
    }
    if (t->kind == T_CHARLIT) {
        advance();
        Node *n = node_new(N_NUM, t->line); n->ival = t->ival; return n;
    }
    if (t->kind == T_STRLIT) {
        advance();
        Node *n = node_new(N_STR, t->line); n->sval = t->text; return n;
    }
    if (t->kind == T_IDENT) {
        advance();
        if (check(T_LPAREN)) {
            /* function call: name ( arg, arg, ... ) */
            advance();
            Node *call = node_new(N_CALL, t->line);
            call->name = t->text;
            Node *head = NULL, *tail = NULL;
            if (!check(T_RPAREN)) {
                for (;;) {
                    Node *arg = parse_assign(); /* each arg is itself a full expression */
                    if (!head) head = tail = arg; else { tail->next = arg; tail = arg; }
                    if (check(T_COMMA)) { advance(); continue; }
                    break;
                }
            }
            expect(T_RPAREN, "')'");
            call->a = head; /* arguments, chained through ->next */
            return call;
        }
        Node *n = node_new(N_IDENT, t->line); n->name = t->text; return n;
    }
    if (t->kind == T_LPAREN) {
        advance();
        Node *e = parse_expr();
        expect(T_RPAREN, "')'");
        return e;
    }
    fatal(t->line, "unexpected token in expression");
    return NULL; /* unreachable */
}

/* Postfix operators: array/pointer indexing `a[i]`, member access
 * `.`/`->`, and postfix `x++`/`x--`. These bind tighter than anything
 * else and can chain (`a[i].next->val++` indexes, then two member
 * accesses, then increments the result), which is why this is a loop
 * around parse_primary() rather than a single check.
 *
 * `a->b` is parsed as sugar for `(*a).b`: it builds an N_DEREF node
 * wrapping `a`, then an N_MEMBER node wrapping THAT - so codegen only
 * ever has to handle one member-access shape (N_MEMBER whose base is
 * already "the struct itself", never "a pointer to it"), and gets
 * dereferencing for free from the machinery `*p` already has. */
static Node *parse_postfix(void) {
    Node *n = parse_primary();
    for (;;) {
        if (check(T_LBRACKET)) {
            Token *lb = advance();
            Node *idx = parse_expr();
            expect(T_RBRACKET, "']'");
            Node *ix = node_new(N_INDEX, lb->line);
            ix->a = n; ix->b = idx;
            n = ix;
        } else if (check(T_DOT)) {
            Token *t = advance();
            if (!check(T_IDENT)) fatal(cur()->line, "expected member name after '.'");
            char *mname = advance()->text;
            Node *m = node_new(N_MEMBER, t->line);
            m->a = n; m->name = mname;
            n = m;
        } else if (check(T_ARROW)) {
            Token *t = advance();
            if (!check(T_IDENT)) fatal(cur()->line, "expected member name after '->'");
            char *mname = advance()->text;
            Node *deref = node_new(N_DEREF, t->line); deref->a = n;
            Node *m = node_new(N_MEMBER, t->line);
            m->a = deref; m->name = mname;
            n = m;
        } else if (check(T_INC)) {
            Token *t = advance();
            Node *p = node_new(N_POSTINC, t->line); p->a = n; n = p;
        } else if (check(T_DEC)) {
            Token *t = advance();
            Node *p = node_new(N_POSTDEC, t->line); p->a = n; n = p;
        } else break;
    }
    return n;
}

/* Prefix/unary operators. Right-associative by construction: each of
 * these calls parse_unary() again for its operand (not parse_postfix()
 * directly), so `!!x` correctly parses as `!(!x)`. */
static Node *parse_unary(void) {
    if (check(T_MINUS) || check(T_NOT) || check(T_TILDE)) {
        Token *t = advance();
        Node *n = node_new(N_UNARY, t->line);
        n->op[0] = (t->kind == T_MINUS) ? '-' : (t->kind == T_NOT) ? '!' : '~';
        n->a = parse_unary();
        return n;
    }
    if (check(T_INC)) { Token *t = advance(); Node *n = node_new(N_PREINC, t->line); n->a = parse_unary(); return n; }
    if (check(T_DEC)) { Token *t = advance(); Node *n = node_new(N_PREDEC, t->line); n->a = parse_unary(); return n; }
    if (check(T_AMP)) { Token *t = advance(); Node *n = node_new(N_ADDR, t->line); n->a = parse_unary(); return n; }
    if (check(T_STAR)) { Token *t = advance(); Node *n = node_new(N_DEREF, t->line); n->a = parse_unary(); return n; }
    return parse_postfix();
}

static Node *mkbin(const char *op, Node *a, Node *b, int line) {
    Node *n = node_new(N_BINOP, line);
    snprintf(n->op, sizeof(n->op), "%s", op);
    n->a = a; n->b = b;
    return n;
}

/* From here down is the precedence-climbing chain described in the
 * file header comment: each function is "parse one operand at the
 * next tighter level, then greedily consume same-precedence operators
 * in a loop, left-associatively combining as we go." The `for (;;)`
 * loop shape (rather than recursion) is what makes these
 * left-associative instead of right-associative. */
static Node *parse_term(void) {
    Node *n = parse_unary();
    for (;;) {
        if (check(T_STAR)) { Token *t=advance(); n = mkbin("*", n, parse_unary(), t->line); }
        else if (check(T_SLASH)) { Token *t=advance(); n = mkbin("/", n, parse_unary(), t->line); }
        else if (check(T_PERCENT)) { Token *t=advance(); n = mkbin("%", n, parse_unary(), t->line); }
        else break;
    }
    return n;
}
static Node *parse_additive(void) {
    Node *n = parse_term();
    for (;;) {
        if (check(T_PLUS)) { Token *t=advance(); n = mkbin("+", n, parse_term(), t->line); }
        else if (check(T_MINUS)) { Token *t=advance(); n = mkbin("-", n, parse_term(), t->line); }
        else break;
    }
    return n;
}
static Node *parse_shift(void) {
    Node *n = parse_additive();
    for (;;) {
        if (check(T_SHL)) { Token *t=advance(); n = mkbin("<<", n, parse_additive(), t->line); }
        else if (check(T_SHR)) { Token *t=advance(); n = mkbin(">>", n, parse_additive(), t->line); }
        else break;
    }
    return n;
}
static Node *parse_relational(void) {
    Node *n = parse_shift();
    for (;;) {
        if (check(T_LT)) { Token *t=advance(); n = mkbin("<", n, parse_shift(), t->line); }
        else if (check(T_GT)) { Token *t=advance(); n = mkbin(">", n, parse_shift(), t->line); }
        else if (check(T_LE)) { Token *t=advance(); n = mkbin("<=", n, parse_shift(), t->line); }
        else if (check(T_GE)) { Token *t=advance(); n = mkbin(">=", n, parse_shift(), t->line); }
        else break;
    }
    return n;
}
static Node *parse_equality(void) {
    Node *n = parse_relational();
    for (;;) {
        if (check(T_EQ)) { Token *t=advance(); n = mkbin("==", n, parse_relational(), t->line); }
        else if (check(T_NE)) { Token *t=advance(); n = mkbin("!=", n, parse_relational(), t->line); }
        else break;
    }
    return n;
}
static Node *parse_bitand(void) {
    Node *n = parse_equality();
    while (check(T_AMP)) { Token *t=advance(); n = mkbin("&", n, parse_equality(), t->line); }
    return n;
}
static Node *parse_bitxor(void) {
    Node *n = parse_bitand();
    while (check(T_CARET)) { Token *t=advance(); n = mkbin("^", n, parse_bitand(), t->line); }
    return n;
}
static Node *parse_bitor(void) {
    Node *n = parse_bitxor();
    while (check(T_PIPE)) { Token *t=advance(); n = mkbin("|", n, parse_bitxor(), t->line); }
    return n;
}
/* && and || get their own node kinds (N_LOGAND/N_LOGOR) rather than
 * being plain N_BINOP like the operators above - codegen needs to
 * treat them specially to implement short-circuit evaluation (the
 * right-hand side must not even be evaluated when the left side
 * already determines the answer), which an ordinary binary operator
 * doesn't need to worry about. */
static Node *parse_logand(void) {
    Node *n = parse_bitor();
    while (check(T_ANDAND)) {
        Token *t=advance();
        Node *r = node_new(N_LOGAND, t->line); r->a = n; r->b = parse_bitor(); n = r;
    }
    return n;
}
static Node *parse_logor(void) {
    Node *n = parse_logand();
    while (check(T_OROR)) {
        Token *t=advance();
        Node *r = node_new(N_LOGOR, t->line); r->a = n; r->b = parse_logand(); n = r;
    }
    return n;
}

/* Which AST node kinds are valid assignment targets - i.e. actually
 * refer to a storage location (a variable, an array element, or a
 * dereferenced pointer) rather than being a computed, transient
 * value. `x = 5` is fine; `(x + 1) = 5` is not, and is_lvalue_node()
 * is what parse_assign() below uses to reject it with a clear error
 * instead of generating nonsensical code for it. */
static int is_lvalue_node(Node *n) { return n->kind == N_IDENT || n->kind == N_INDEX || n->kind == N_DEREF || n->kind == N_MEMBER; }

/* Assignment is the loosest-binding operator and, unlike everything
 * above, right-associative (`a = b = c` means `a = (b = c)`), which is
 * why this calls parse_assign() recursively for the right-hand side
 * instead of looping. It's also the one place a plain N_BINOP-style
 * table lookup isn't quite enough, since each compound-assignment
 * operator (`+=`, `-=`, ...) needs to remember which underlying
 * operator it stands for. */
static Node *parse_assign(void) {
    Node *n = parse_logor();
    TokKind k = cur()->kind;
    static const struct { TokKind k; const char *op; } cops[] = {
        {T_PLUSEQ,"+"},{T_MINUSEQ,"-"},{T_STAREQ,"*"},{T_SLASHEQ,"/"},{T_PERCENTEQ,"%"},
        {T_AMPEQ,"&"},{T_PIPEEQ,"|"},{T_CARETEQ,"^"},{T_SHLEQ,"<<"},{T_SHREQ,">>"}
    };
    if (k == T_ASSIGN) {
        Token *t = advance();
        if (!is_lvalue_node(n)) fatal(t->line, "left side of '=' is not assignable");
        Node *r = node_new(N_ASSIGN, t->line);
        r->a = n; r->b = parse_assign();
        return r;
    }
    for (size_t i = 0; i < sizeof(cops)/sizeof(cops[0]); i++) {
        if (k == cops[i].k) {
            Token *t = advance();
            if (!is_lvalue_node(n)) fatal(t->line, "left side of '%s' is not assignable", cops[i].op);
            Node *r = node_new(N_COMPOUND_ASSIGN, t->line);
            snprintf(r->op, sizeof(r->op), "%s", cops[i].op);
            r->a = n; r->b = parse_assign();
            return r;
        }
    }
    return n;
}

static Node *parse_expr(void) { return parse_assign(); }

/* ===================================================================
 * Parser: statements
 * =================================================================== */

/* A local variable declaration: `type [*] name [ [size] ] [= expr];`.
 * Unlike a global (see pass_a()), a local's initializer can be any
 * expression, not just a constant literal - it's evaluated at
 * runtime, when the enclosing statement actually executes (see
 * N_VARDECL's case in gen_stmt(), codegen_stmt.c). */
static Node *parse_vardecl(void) {
    Token *tt = cur();
    int type, structTag, isPointer, isUnsigned;
    parse_type_prefix(&type, &structTag, &isPointer, &isUnsigned);
    if (type < 0) fatal(tt->line, "'void' is not a valid variable type");
    if (!check(T_IDENT)) fatal(cur()->line, "expected identifier");
    char *name = advance()->text;
    Node *n = node_new(N_VARDECL, tt->line);
    n->name = name;
    n->declType = type;
    n->declIsPointer = isPointer;
    n->declStructTag = structTag;
    n->declIsUnsigned = isUnsigned;
    if (check(T_LBRACKET)) {
        if (isPointer) fatal(cur()->line, "arrays of pointers are not supported in this version");
        advance();
        n->declArrLen = (int)parse_const_value(0, "array size");
        expect(T_RBRACKET, "']'");
    }
    if (type == TY_STRUCT && !isPointer)
        require_complete_struct(structTag, tt->line); /* need a known size to allocate storage */
    if (check(T_ASSIGN)) {
        advance();
        if (n->declArrLen) fatal(n->line, "array initializers are not supported in this version");
        if (type == TY_STRUCT && !isPointer) fatal(n->line, "struct/union initializers are not supported in this version");
        n->a = parse_assign();
    }
    expect(T_SEMI, "';'");
    /* Registering the local here, as soon as it's parsed (rather than
     * waiting until the whole function is parsed), is what lets later
     * statements in the same function refer to it - parse_primary()'s
     * handling of a bare identifier just looks it up via find_local(),
     * which only works if register_local() has already run for it. */
    register_local(name, n->declType, n->declIsPointer, n->declStructTag, n->declIsUnsigned,
                    n->declArrLen != 0, n->declArrLen, 0, n->line);
    return n;
}

/* `switch (expr) { case CONST: stmt* ... default: stmt* }`.
 *
 * Deliberately flat: only a `case`/`default` label written directly at
 * this brace-depth is recognized as one - a case label nested inside
 * an inner `{ }` block or inside an `if` (Duff's-device-style tricks)
 * is not supported, and parses as a plain (likely erroring) statement
 * instead. Real C's grammar technically allows that, since a label can
 * attach to any statement anywhere, but no C written in an ordinary
 * style relies on it, and skipping it keeps both this parser and
 * codegen's compare-chain-then-body-in-order strategy (see N_SWITCH in
 * codegen_stmt.c) much simpler.
 *
 * Every `case` value must be something parse_const_value() accepts - a
 * plain literal or an enum constant, optionally negated - the same
 * restriction a global variable's initializer already has (see
 * pass_a()), and for a closely related reason: codegen compares the
 * switch value against each case inline, as an immediate operand baked
 * into the generated CMP instructions, which only works for a value
 * known at compile time. */
static Node *parse_switch(void) {
    Token *t = advance(); /* 'switch' */
    expect(T_LPAREN, "'('");
    Node *expr = parse_expr();
    expect(T_RPAREN, "')'");
    expect(T_LBRACE, "'{'");
    Node *head = NULL, *tail = NULL;
    long seen[256]; int nseen = 0;
    int sawDefault = 0;
    while (!check(T_RBRACE)) {
        Node *item;
        if (check(T_CASE)) {
            Token *ct = advance();
            long v = parse_const_value(1, "'case' value");
            expect(T_COLON, "':'");
            for (int i = 0; i < nseen; i++)
                if (seen[i] == v) fatal(ct->line, "duplicate 'case %ld' in this switch", v);
            if (nseen >= (int)(sizeof(seen)/sizeof(seen[0])))
                fatal(ct->line, "too many 'case' labels in one switch");
            seen[nseen++] = v;
            item = node_new(N_CASE, ct->line);
            item->ival = v;
        } else if (check(T_DEFAULT)) {
            Token *dt = advance();
            expect(T_COLON, "':'");
            if (sawDefault) fatal(dt->line, "multiple 'default' labels in one switch");
            sawDefault = 1;
            item = node_new(N_DEFAULT, dt->line);
        } else {
            item = parse_stmt();
        }
        if (!head) head = tail = item; else { tail->next = item; tail = item; }
    }
    expect(T_RBRACE, "'}'");
    Node *n = node_new(N_SWITCH, t->line);
    n->a = expr;
    n->b = head; /* flat list: N_CASE/N_DEFAULT markers interleaved with
                    ordinary statements, in source order - see N_SWITCH
                    in codegen_stmt.c for how fallthrough falls out of
                    just walking this list in order */
    return n;
}

/* One statement. Each branch here corresponds to one statement form
 * in the grammar; the shape closely follows how you'd describe C's
 * statement grammar in prose ("an if is 'if' '(' expr ')' stmt
 * optionally followed by 'else' stmt", etc). */
static Node *parse_stmt(void) {
    if (check(T_LBRACE)) return parse_block();
    if (check(T_IF)) {
        Token *t = advance();
        expect(T_LPAREN, "'('");
        Node *cond = parse_expr();
        expect(T_RPAREN, "')'");
        Node *thenS = parse_stmt();
        Node *elseS = NULL;
        if (check(T_ELSE)) { advance(); elseS = parse_stmt(); }
        Node *n = node_new(N_IF, t->line);
        n->a = cond; n->b = thenS; n->c = elseS;
        return n;
    }
    if (check(T_WHILE)) {
        Token *t = advance();
        expect(T_LPAREN, "'('");
        Node *cond = parse_expr();
        expect(T_RPAREN, "')'");
        Node *body = parse_stmt();
        Node *n = node_new(N_WHILE, t->line);
        n->a = cond; n->b = body;
        return n;
    }
    if (check(T_DO)) {
        Token *t = advance();
        Node *body = parse_stmt();
        expect(T_WHILE, "'while'");
        expect(T_LPAREN, "'('");
        Node *cond = parse_expr();
        expect(T_RPAREN, "')'");
        expect(T_SEMI, "';'");
        Node *n = node_new(N_DOWHILE, t->line);
        n->a = cond; n->b = body;
        return n;
    }
    if (check(T_SWITCH)) return parse_switch();
    if (check(T_FOR)) {
        Token *t = advance();
        expect(T_LPAREN, "'('");
        Node *init = NULL;
        if (!check(T_SEMI)) {
            if (cur_is_type()) init = parse_vardecl(); /* consumes its own ';' */
            else { init = node_new(N_EXPRSTMT, cur()->line); init->a = parse_expr(); expect(T_SEMI, "';'"); }
        } else advance();
        Node *cond = NULL;
        if (!check(T_SEMI)) cond = parse_expr();
        expect(T_SEMI, "';'");
        Node *incr = NULL;
        if (!check(T_RPAREN)) incr = parse_expr();
        expect(T_RPAREN, "')'");
        Node *body = parse_stmt();
        Node *n = node_new(N_FOR, t->line);
        n->a = init; n->b = cond; n->c = incr; n->d = body;
        return n;
    }
    if (check(T_RETURN)) {
        Token *t = advance();
        Node *n = node_new(N_RETURN, t->line);
        if (!check(T_SEMI)) n->a = parse_expr();
        expect(T_SEMI, "';'");
        return n;
    }
    if (check(T_BREAK)) { Token *t = advance(); expect(T_SEMI, "';'"); return node_new(N_BREAK, t->line); }
    if (check(T_CONTINUE)) { Token *t = advance(); expect(T_SEMI, "';'"); return node_new(N_CONTINUE, t->line); }
    if (check(T_SEMI)) { Token *t = advance(); return node_new(N_EMPTY, t->line); }
    if (cur_is_type()) return parse_vardecl();
    /* Anything else is an expression statement: an expression followed
     * by ';', kept only for its side effects (e.g. a bare function
     * call, or `x = 5;`). */
    Node *n = node_new(N_EXPRSTMT, cur()->line);
    n->a = parse_expr();
    expect(T_SEMI, "';'");
    return n;
}

/* `{ stmt* }` - a block is just a list of statements, chained through
 * each Node's `next` pointer and hung off the block node's `a`. */
static Node *parse_block(void) {
    Token *t = expect(T_LBRACE, "'{'");
    Node *n = node_new(N_BLOCK, t->line);
    Node *head = NULL, *tail = NULL;
    while (!check(T_RBRACE)) {
        Node *s = parse_stmt();
        if (!head) head = tail = s; else { tail->next = s; tail = s; }
    }
    expect(T_RBRACE, "'}'");
    n->a = head;
    return n;
}

/* ===================================================================
 * Pass B: real parse of each function body, immediately followed by
 * codegen for that function (see emit_function() in codegen_stmt.c).
 * By the time this runs, pass_a() has already populated g_funcs[] and
 * g_globals[] with every symbol in the file, so nothing parsed here
 * can hit an "undeclared function" surprise just because that
 * function happens to be defined later in the source text.
 * =================================================================== */

void pass_b(void) {
    g_pos = 0;
    while (!check(T_EOF)) {
        /* typedef - already fully recorded by pass_a(). Re-running
         * parse_type_prefix() for real (rather than a hand-rolled
         * token-count skip, the way struct/union/enum's own type-
         * prefix gets skipped below) is what correctly handles every
         * shape the underlying type-prefix can take here, including a
         * typedef name itself (`typedef MyInt MyInt2;`) - the exact
         * same ambiguity that motivated cur_is_type() elsewhere, so
         * reusing the one function that already resolves it correctly
         * beats duplicating that logic as a second, parallel skip
         * rule. */
        if (check(T_TYPEDEF)) {
            advance(); /* 'typedef' */
            int t, st, ip, iu; /* discarded - already recorded by pass_a() */
            parse_type_prefix(&t, &st, &ip, &iu);
            advance(); /* the new type name */
            expect(T_SEMI, "';'");
            continue;
        }
        /* struct definition - already fully processed by pass_a();
         * skip it the same way skip_balanced() skips a function body,
         * rather than falling into the "type [*] name" skip logic
         * below, which doesn't apply to it at all. */
        if (check(T_STRUCT) && peekAt(1)->kind == T_IDENT && peekAt(2)->kind == T_LBRACE) {
            advance(); advance(); /* 'struct' Tag */
            skip_balanced(T_LBRACE, T_RBRACE);
            expect(T_SEMI, "';'");
            continue;
        }
        /* Same for a union definition - identical shape to struct's. */
        if (check(T_UNION) && peekAt(1)->kind == T_IDENT && peekAt(2)->kind == T_LBRACE) {
            advance(); advance(); /* 'union' Tag */
            skip_balanced(T_LBRACE, T_RBRACE);
            expect(T_SEMI, "';'");
            continue;
        }
        /* Same for an enum definition - both the anonymous and tagged
         * forms, matching pass_a()'s own two-shape detection. */
        if (check(T_ENUM) && (peekAt(1)->kind == T_LBRACE ||
                               (peekAt(1)->kind == T_IDENT && peekAt(2)->kind == T_LBRACE))) {
            advance(); /* 'enum' */
            if (check(T_IDENT)) advance(); /* optional tag */
            skip_balanced(T_LBRACE, T_RBRACE);
            expect(T_SEMI, "';'");
            continue;
        }

        /* type keyword - already recorded by pass_a, just skip it.
         * `struct Tag`/`union Tag`/`enum Tag` are two tokens where
         * int/char/void - and a typedef name, which is always exactly
         * one token no matter what it's an alias for, even a pointer
         * type (see TypedefEntry's own comment: a typedef'd pointer
         * type can never have an extra '*' written after it at a use
         * site, since that's rejected as pointer-to-pointer back in
         * pass_a() - so there's never a second token to account for
         * here either) - are just one. `unsigned` is one or two: the
         * keyword alone, or the keyword plus an optional trailing
         * `int`/`char` - the same shape parse_type_prefix() itself
         * accepts. */
        if (check(T_STRUCT) || check(T_UNION) || check(T_ENUM)) { advance(); advance(); }
        else if (check(T_UNSIGNED)) {
            advance();
            if (check(T_INT) || check(T_CHAR)) advance();
        }
        else advance();
        if (check(T_STAR)) advance(); /* optional pointer '*'; already validated in pass A */
        char *name = advance()->text; /* ident */

        if (check(T_LPAREN)) {
            advance();
            FnSym *fn = find_func(name);
            /* Skip the parameter list tokens - pass_a already recorded
             * their types/names into fn->paramTypes[]/paramNames[]. */
            int depth = 1;
            while (depth > 0) {
                if (check(T_LPAREN)) depth++;
                else if (check(T_RPAREN)) depth--;
                advance();
            }
            if (check(T_SEMI)) { advance(); continue; } /* prototype only, no body to compile */
            /* Function body: reset the per-function locals table, seed
             * it with the parameters (so the body can refer to them
             * immediately), then really parse the body and hand it to
             * codegen. g_curfn is set for the duration so codegen and
             * infer_type() know which function's locals are in scope. */
            g_nlocals = 0;
            g_curfn = fn;
            for (int i = 0; i < fn->nparams; i++)
                register_local(fn->paramNames[i], fn->paramTypes[i], fn->paramIsPointer[i],
                                fn->paramStructTag[i], fn->paramIsUnsigned[i], 0, 0, 1, 0);
            Node *body = parse_block();
            emit_function(fn, body);
            g_curfn = NULL;
            continue;
        }

        /* Global variable: already recorded by pass_a(), so just skip
         * its tokens here rather than re-parsing and re-validating it. */
        if (check(T_LBRACKET)) { advance(); advance(); expect(T_RBRACKET, "']'"); }
        if (check(T_ASSIGN)) { advance(); if (check(T_MINUS)) advance(); advance(); }
        expect(T_SEMI, "';'");
    }
}
