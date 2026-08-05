/*
 * macro.h - macro definition and expansion, and where ".include" is
 * recognized (see includes.h for the file-resolution/reading side of
 * that).
 *
 * This is a preprocessing step over raw source *text*, run before
 * split_line() (lineparser.h) ever sees a line -- it knows nothing
 * about labels, opcodes, or addressing modes, only about
 * ".macro"/".endmacro" blocks, parameter substitution, recognizing when
 * a line invokes a macro name instead of a real mnemonic, and
 * recognizing ".include" lines. See fileio.c for where this plugs into
 * the pipeline: it sits directly between "read a raw line from a file"
 * (fileio.c for the main file, includes.c for an included one) and
 * "hand a line to split_line()".
 *
 * Syntax:
 *     .macro NAME param1, param2
 *             ; body, referencing \param1, \param2
 *     .endmacro
 * invoked like a pseudo-instruction:
 *     NAME arg1, arg2
 *
 * A label defined inside a macro's body should use an @-prefixed local
 * label (see locallabels.h) rather than an ordinary global one -- that
 * makes it automatically distinct on every separate invocation, with no
 * extra parameter or other bookkeeping needed on the caller's part.
 *
 * Deliberate limitations (documented in c64asm-reference.md, not
 * oversights):
 *   - Macros must be defined before they're used -- there's no separate
 *     pre-scan of the whole file for macro definitions first, so this
 *     stays a simple single-pass expansion.
 *   - A macro invocation can't share a line with a label ("foo: SOME_MACRO
 *     x" doesn't work) -- put the label on its own line above instead.
 *   - Macro arguments are split the same comma/paren/quote-aware way
 *     directive argument lists are (see split_args() in strutils.h) --
 *     which means a full indexed operand like "(ptr),Y" can't be passed
 *     as a single argument, since its comma sits outside any
 *     parentheses. Parameterize the base address and bake the ",Y" into
 *     the macro body instead.
 *   - Nested *definitions* (a ".macro" inside another macro's body) are
 *     rejected outright. Nested *invocations* (one macro calling
 *     another) are fine, up to MAX_MACRO_EXPANSION_DEPTH deep, which
 *     exists purely to turn "a macro that (directly or indirectly)
 *     invokes itself" into a clear error instead of a hang.
 */

#ifndef C64ASM_MACRO_H
#define C64ASM_MACRO_H

#include "common.h"

/*
 * One '.struct' field's *shape*, recorded by expand_struct() alongside
 * the ordinary "Name.field = offset" symbol it already emits -- kind is
 * 'b' (.byte, 1 byte), 'w' (.word, 2 bytes), or 'o' (.res/.ds/.fill,
 * `size` bytes). Exposed here (rather than kept static in macro.c)
 * purely so assembler.c's '.tag'/'.endtag' handling can check each
 * field's shape, not just the whole struct's total size -- see
 * c64asm-reference.md §12 and find_struct_field_list() below.
 */
typedef struct {
    char name[MAX_IDENT];
    char kind;
    long size;
} TagFieldInfo;

typedef struct {
    char struct_name[MAX_IDENT];
    TagFieldInfo fields[MAX_TAG_FIELDS];
    int field_count;
} StructFieldList;

/*
 * Looks up a '.struct'-defined struct's field list by name, in
 * declaration order. Returns NULL if `struct_name` was never the
 * subject of a '.struct'/'.endstruct' block -- true for a
 * '.tag'-referenced name that doesn't exist at all (already reported
 * elsewhere, via the missing "Name.size" symbol) and, in principle,
 * for a hand-defined "Name.size" constant with no real '.struct'
 * behind it, though that's an unusual thing to write on purpose.
 */
StructFieldList *find_struct_field_list(const char *struct_name);

/*
 * Feeds one raw source line through macro processing.
 *
 * - If a ".macro"/".endmacro" definition is in progress, absorbs this
 *   line as part of its body (or ends the definition, for ".endmacro").
 * - If the line is ".include "path"", splices that file's lines in via
 *   includes_process_file() (includes.h), recursively feeding each one
 *   back through this same function.
 * - If the line invokes an already-defined macro, expands it: each body
 *   line has its parameters substituted, then is recursively fed back
 *   through this same function -- which is what makes a macro invoking
 *   another macro, or a macro invoking an ordinary instruction, both
 *   "just work" without any special-casing.
 * - Otherwise, this is an ordinary line: split_line() (lineparser.h) is
 *   called on it and the result appended to g_lines, exactly as
 *   fileio.c's load_source() used to do directly before macro support
 *   existed.
 *
 * raw_line: one line of source, as fgets() would hand it over.
 * filename: the display name of the file raw_line came from. Threaded
 *           alongside line_no everywhere line_no already flows through
 *           this function, including through macro expansion -- so an
 *           error inside an expanded macro body is attributed to the
 *           file *and* line of the invocation, not wherever the macro
 *           itself happened to be defined.
 * line_no:  1-based line number within that file. Every line generated
 *           by expanding one macro invocation is attributed to the
 *           *invocation's* file and line number, not a line within the
 *           macro's own definition -- simpler than tracking definition-
 *           relative positions, at the cost of an error inside a macro
 *           body pointing you to where it was *called* rather than the
 *           exact body line. For a hobbyist assembler's macro bodies
 *           (typically a handful of lines), that's a reasonable trade.
 *
 * This same function signature doubles as the IncludeLineCallback
 * (includes.h) passed to includes_process_file() -- both here and in
 * fileio.c's load_source(), for the top-level file.
 */
void macro_process_line(const char *raw_line, const char *filename, int line_no);

#endif
