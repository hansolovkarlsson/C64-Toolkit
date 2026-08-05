/*
 * codegen_stmt.c - turns a statement AST node into 6502 instructions
 * (gen_stmt()), and emits the storage layout for global variables and
 * whole functions (emit_global_storage(), emit_function()).
 *
 * This file is the natural companion to codegen_expr.c: expressions
 * produce values, statements produce control flow and side effects.
 * gen_stmt() handles blocks, if/while/for, break/continue/return, and
 * local declarations - for anything that's fundamentally "evaluate an
 * expression for its value", it just calls gen_expr_to_R() from
 * codegen_expr.c and moves on.
 */

#include "cc64.h"

/* Every active loop OR switch's break target, as a simple stack -
 * entering one pushes an entry, leaving it pops it, and `break` always
 * jumps to whatever's on top, matching real C: break exits the
 * innermost enclosing loop or switch, whichever is nearer. `continue`
 * is narrower - it only ever means "the nearest enclosing LOOP, skip
 * over any switch in between" - which is why `kind` exists: N_CONTINUE
 * below scans downward from the top for the first CTX_LOOP entry
 * rather than blindly using the top of the stack the way N_BREAK does.
 * contLbl is meaningless for a CTX_SWITCH entry (switches don't have a
 * continue target of their own) and left NULL. Purely internal to this
 * file: nothing outside gen_stmt() ever needs to know a loop or switch
 * is currently being compiled. */
typedef enum { CTX_LOOP, CTX_SWITCH } CtxKind;
typedef struct { CtxKind kind; char *breakLbl; char *contLbl; } LoopCtx;
static LoopCtx g_loops[64];
static int g_nloops = 0;

/* Compares __zpR (the switch value, still sitting there from just
 * before the compare chain that calls this - see N_SWITCH below)
 * against a compile-time-constant case value, and jumps to `targetLbl`
 * if equal. Both BNEs here only ever need to jump the few bytes to
 * `notEq`, right below - always in range, unlike emit_far_branch's
 * concern - so the only far jump needed is the final unconditional JMP
 * to the (possibly distant) case body, which JMP already handles with
 * no range limit. */
static void gen_case_eq_branch(long value, const char *targetLbl) {
    char *notEq = newlabel();
    emit("    LDA __zpR");
    emit("    CMP #%d", (int)(value & 0xFF));
    emit("    BNE %s", notEq);
    emit("    LDA __zpR+1");
    emit("    CMP #%d", (int)((value >> 8) & 0xFF));
    emit("    BNE %s", notEq);
    emit("    JMP %s", targetLbl);
    emit("%s:", notEq);
}

/* ===================================================================
 * gen_stmt(): the statement-level counterpart to gen_expr_to_R() in
 * codegen_expr.c. Where an expression always produces a VALUE (in
 * __zpR), a statement produces CONTROL FLOW - it's compiled into a
 * sequence of instructions, conditional branches, and labels, with no
 * result to leave anywhere. Every control-flow statement (if/while/
 * for) follows the same shape: evaluate a condition expression with
 * gen_expr_to_R(), test whether __zpR is zero (false) or nonzero
 * (true) with the classic 6502 idiom `LDA lo : ORA hi : branch`
 * (ORing the two bytes together folds a 16-bit zero-test into one
 * flags check), and branch accordingly using emit_far_branch() from
 * codegen.c (never a raw branch instruction - see its comment for why).
 * =================================================================== */

static void gen_stmt(Node *n) {
    switch (n->kind) {
        case N_BLOCK:
            for (Node *s = n->a; s; s = s->next) gen_stmt(s);
            return;
        case N_VARDECL:
            if (n->a) { gen_expr_to_R(n->a);
                char lbl[96]; local_label(lbl, sizeof(lbl), g_curfn->name, n->name);
                gen_store_scalar(lbl, n->declType, n->declIsPointer);
            }
            return;
        case N_EXPRSTMT:
            gen_expr_to_R(n->a);
            return;
        case N_IF: {
            char *Lelse = newlabel(); char *Lend = newlabel();
            gen_expr_to_R(n->a);
            emit("    LDA __zpR");
            emit("    ORA __zpR+1");
            emit_far_branch("BEQ", Lelse);
            gen_stmt(n->b);
            emit("    JMP %s", Lend);
            emit("%s:", Lelse);
            if (n->c) gen_stmt(n->c);
            emit("%s:", Lend);
            return;
        }
        case N_WHILE: {
            char *Ltop = newlabel(); char *Lend = newlabel();
            if (g_nloops >= 64) fatal(n->line, "loops nested too deeply");
            g_loops[g_nloops].kind = CTX_LOOP;
            g_loops[g_nloops].breakLbl = Lend; g_loops[g_nloops].contLbl = Ltop; g_nloops++;
            emit("%s:", Ltop);
            gen_expr_to_R(n->a);
            emit("    LDA __zpR");
            emit("    ORA __zpR+1");
            emit_far_branch("BEQ", Lend);
            gen_stmt(n->b);
            emit("    JMP %s", Ltop);
            emit("%s:", Lend);
            g_nloops--;
            return;
        }
        case N_DOWHILE: {
            /* Unlike N_WHILE, the condition is tested AFTER the body,
             * so the body always runs at least once - Ltop is the
             * body's own start, with no condition check in front of it
             * at all. `continue` still needs to re-run the condition
             * (not jump straight back to the top and skip it), so
             * contLbl points at Lcond, the same way N_FOR's contLbl
             * points at its increment step rather than its own top. */
            char *Ltop = newlabel(); char *Lcond = newlabel(); char *Lend = newlabel();
            if (g_nloops >= 64) fatal(n->line, "loops nested too deeply");
            g_loops[g_nloops].kind = CTX_LOOP;
            g_loops[g_nloops].breakLbl = Lend; g_loops[g_nloops].contLbl = Lcond; g_nloops++;
            emit("%s:", Ltop);
            gen_stmt(n->b);
            emit("%s:", Lcond);
            gen_expr_to_R(n->a);
            emit("    LDA __zpR");
            emit("    ORA __zpR+1");
            emit_far_branch("BNE", Ltop);
            emit("%s:", Lend);
            g_nloops--;
            return;
        }
        case N_SWITCH: {
            CType st = infer_type(n->a);
            if (st.isPointer) fatal(n->line, "'switch' expression must be an integer type, not a pointer");
            if (st.base == TY_STRUCT) fatal(n->line, "'switch' expression must be an integer type, not a struct");
            gen_expr_to_R(n->a);
            /* The switch value sits in __zpR from here through the
             * whole compare chain below, undisturbed: every 'case'
             * label is a compile-time constant enforced by the parser,
             * never a runtime expression, so nothing in between ever
             * calls gen_expr_to_R() again to clobber it. */
            char *Lend = newlabel();
            if (g_nloops >= 64) fatal(n->line, "loops/switches nested too deeply");
            g_loops[g_nloops].kind = CTX_SWITCH;
            g_loops[g_nloops].breakLbl = Lend; g_loops[g_nloops].contLbl = NULL; g_nloops++;

            /* Every case/default marker gets its own label up front
             * (reusing Node's sval field - see its comment in cc64.h),
             * so the compare chain below can reference a later case's
             * label before that case's own body has been walked. */
            char *defaultLbl = NULL;
            for (Node *s = n->b; s; s = s->next) {
                if (s->kind == N_CASE || s->kind == N_DEFAULT) s->sval = newlabel();
                if (s->kind == N_DEFAULT) defaultLbl = s->sval;
            }
            for (Node *s = n->b; s; s = s->next)
                if (s->kind == N_CASE) gen_case_eq_branch(s->ival, s->sval);
            emit("    JMP %s", defaultLbl ? defaultLbl : Lend);

            /* The body: markers become labels, everything else is
             * generated in source order exactly like an ordinary
             * block - which is what gives fallthrough for free, the
             * same way it always falls out of "just keep executing the
             * next statement" in real hardware. */
            for (Node *s = n->b; s; s = s->next) {
                if (s->kind == N_CASE || s->kind == N_DEFAULT) emit("%s:", s->sval);
                else gen_stmt(s);
            }
            emit("%s:", Lend);
            g_nloops--;
            return;
        }
        case N_FOR: {
            char *Ltop = newlabel(); char *Lend = newlabel(); char *Lincr = newlabel();
            if (n->a) gen_stmt(n->a);
            if (g_nloops >= 64) fatal(n->line, "loops nested too deeply");
            g_loops[g_nloops].kind = CTX_LOOP;
            g_loops[g_nloops].breakLbl = Lend; g_loops[g_nloops].contLbl = Lincr; g_nloops++;
            emit("%s:", Ltop);
            if (n->b) {
                gen_expr_to_R(n->b);
                emit("    LDA __zpR");
                emit("    ORA __zpR+1");
                emit_far_branch("BEQ", Lend);
            }
            gen_stmt(n->d);
            emit("%s:", Lincr);
            if (n->c) gen_expr_to_R(n->c);
            emit("    JMP %s", Ltop);
            emit("%s:", Lend);
            g_nloops--;
            return;
        }
        case N_RETURN:
            if (n->a) {
                CType rt = infer_type(n->a);
                int isNullLiteral = (n->a->kind == N_NUM && n->a->ival == 0);
                if (g_curfn->retIsPointer && !rt.isPointer && !isNullLiteral)
                    fatal(n->line, "function '%s' should return a pointer", g_curfn->name);
                if (!g_curfn->retIsPointer && rt.isPointer)
                    fatal(n->line, "function '%s' should not return a pointer", g_curfn->name);
                gen_expr_to_R(n->a);
            }
            emit("    RTS");
            return;
        case N_BREAK:
            /* Nearest enclosing loop OR switch, whichever is innermost -
             * the top of the stack is always right, regardless of kind. */
            if (g_nloops == 0) fatal(n->line, "'break' outside a loop or switch");
            emit("    JMP %s", g_loops[g_nloops-1].breakLbl);
            return;
        case N_CONTINUE: {
            /* Nearest enclosing LOOP specifically - skip over any
             * switch frame(s) on top of the stack, since a switch has
             * no continue target of its own (see the LoopCtx comment
             * above). */
            int i = g_nloops - 1;
            while (i >= 0 && g_loops[i].kind != CTX_LOOP) i--;
            if (i < 0) fatal(n->line, "'continue' outside a loop");
            emit("    JMP %s", g_loops[i].contLbl);
            return;
        }
        case N_EMPTY:
            return;
        default:
            fatal(n->line, "internal: cannot generate statement node %d", n->kind);
    }
}

/* ===================================================================
 * Pass B: full parse + codegen
 * =================================================================== */

/* ===================================================================
 * Emitting storage layout
 * ===================================================================
 * The two functions below don't generate any "logic" - they emit the
 * `.fill`/`.word`/`.byte` assembler directives that reserve and
 * (optionally) initialize memory for every global variable and every
 * function's locals/parameters. This is where the symbol tables
 * built up during parsing (g_globals[], and per-function g_locals[])
 * finally turn into actual memory addresses in the output.
 * =================================================================== */

void emit_global_storage(void) {
    emit("; ---- global variables --------------------------------------------");
    for (int i = 0; i < g_nglobals; i++) {
        GSym *g = &g_globals[i];
        char lbl[96]; global_label(lbl, sizeof(lbl), g->name);
        if (g->isArray) {
            int bytes = g->arrLen * var_width(g->type, 0, g->structTag); /* arrays of pointers not supported */
            emit("%s:", lbl);
            emit("    .fill %d, 0", bytes);
        } else if (g->hasInit) {
            emit("%s:", lbl);
            if (g->type == TY_INT) emit("    .word %ld", g->initVal);
            else emit("    .byte %ld", g->initVal & 0xFF ? (g->initVal & 0xFF) : 0);
        } else {
            emit("%s:", lbl);
            emit("    .fill %d, 0", var_width(g->type, g->isPointer, g->structTag));
        }
    }
    emit(" ");
}

/* ===================================================================
 * Supporting recursion despite fixed-address storage
 * ===================================================================
 * cc64 still gives every function's parameters and locals ONE fixed
 * memory address each (see cc64.h's architecture note) - that part
 * hasn't changed, and neither has how a function reads or writes its
 * own locals (gen_load_scalar/gen_store_scalar in codegen_expr.c are
 * completely unaware any of this exists). What's new is that the
 * CALLER now saves the callee's current frame contents to a software
 * stack before overwriting them with a new call's arguments, and
 * restores them after the call returns - so a function calling
 * itself (directly, or indirectly through some chain of other calls)
 * finds its own storage exactly as it left it once the inner call
 * unwinds, even though the inner call was temporarily using that same
 * memory for its own, different invocation.
 *
 * Concretely: for every function `foo`, emit_function() below emits
 * two extra routines, `__fn_foo_pushframe` and `__fn_foo_popframe`,
 * each hardcoded (unrolled at compile time, since a function's frame
 * size never changes) to copy every byte of `foo`'s parameters and
 * locals to/from a dedicated call stack (__rt_cstack, a large fixed
 * buffer - see main.c - indexed by the 16-bit __rt_csp pointer).
 * gen_call() in codegen_expr.c wraps every call to `foo` with
 * `JSR __fn_foo_pushframe` right before storing the new arguments,
 * and `JSR __fn_foo_popframe` right after the call returns. Because
 * the push/pop routines are per-FUNCTION rather than per-CALL-SITE,
 * a call site never needs to know its callee's frame size itself
 * (which matters because, thanks to the two-pass design, a call can
 * appear textually before the callee's locals have even been parsed
 * yet - see parser.c) - it only ever needs to know the callee's
 * NAME, which is always available.
 *
 * Trace through a small example if this isn't clicking yet: for
 * `int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }`,
 * calling fact(3) pushes fact's frame (saving whatever was there
 * before - stale/irrelevant), sets n=3, and calls fact. Inside, computing
 * `fact(n - 1)` pushes fact's frame AGAIN - this time saving n=3 - sets
 * n=2, and recurses. This continues down to n=1, which returns without
 * recursing further; unwinding back up, each return is immediately
 * followed by a popframe that restores THAT level's n (2, then 3),
 * so each stack frame's `n * fact(n-1)` multiplication happens with
 * the correct n for that level, even though every level used the
 * exact same memory address for its own copy of n.
 * =================================================================== */

/* One function's storage (parameters, then locals - all emitted
 * CONSECUTIVELY, so together they form one contiguous block of frame
 * memory starting at the __fn_<name>_frame label - the frame
 * save/restore loops below depend on that contiguity), its
 * pushframe/popframe routines (see the recursion comment above), and
 * finally its actual code. Called once per function by pass_b() in
 * parser.c, right after that function's body has been parsed. The
 * trailing RTS handles a function that "falls off the end" without an
 * explicit return - falling off the end of a non-void function is
 * undefined behavior in real C, but this compiler would rather emit a
 * harmless extra RTS than generate a function that doesn't return at
 * all. */
void emit_function(FnSym *fn, Node *body) {
    char flbl[96]; func_label(flbl, sizeof(flbl), fn->name);
    emit("; ---- function %s ---------------------------------------------", fn->name);

    /* Storage. The __fn_<name>_frame label marks the start of the
     * whole contiguous frame explicitly, so the copy loops below can
     * address the entire frame as `frame,Y` without depending on
     * knowing which variable happens to come first. */
    int total = 0;
    emit("__fn_%s_frame:", fn->name);
    for (int i = 0; i < fn->nparams; i++) {
        char lbl[96]; local_label(lbl, sizeof(lbl), fn->name, fn->paramNames[i]);
        int w = var_width(fn->paramTypes[i], fn->paramIsPointer[i], fn->paramStructTag[i]);
        emit("%s:", lbl);
        emit("    .fill %d, 0", w);
        total += w;
    }
    for (int i = 0; i < g_nlocals; i++) {
        if (g_locals[i].isParam) continue;
        char lbl[96]; local_label(lbl, sizeof(lbl), fn->name, g_locals[i].name);
        int w = g_locals[i].isArray
            ? g_locals[i].arrLen * var_width(g_locals[i].type, 0, g_locals[i].structTag) /* arrays of pointers not supported */
            : var_width(g_locals[i].type, g_locals[i].isPointer, g_locals[i].structTag);
        emit("%s:", lbl);
        emit("    .fill %d, 0", w);
        total += w;
    }

    /* The copy loops below count with the 8-bit Y register, which
     * caps a recursion-capable frame at 256 bytes. (Exactly 256 still
     * works: Y wraps 255 -> 0, and CPY #(256 & 0xFF) is CPY #0, so
     * the loop exits exactly after byte 255.) A function carrying
     * more than 256 bytes of locals is almost always a large local
     * array, which is cheap to make global instead - and saving/
     * restoring that much data on every single call would be painful
     * anyway, so the limit doubles as a performance guard rail. */
    if (total > 256)
        fatal(0, "function '%s' has %d bytes of parameters+locals, which exceeds "
                 "the 256-byte per-call frame limit; consider making large local "
                 "arrays global", fn->name, total);

    /* pushframe: copy the whole frame to the call stack, then advance
     * __rt_csp past the saved copy. The copy runs BEFORE the pointer
     * moves, so the frame lands at the pre-call __rt_csp position -
     * exactly where popframe's subtract-then-copy will look for it. */
    emit("__fn_%s_pushframe:", fn->name);
    if (total == 0) {
        emit("    RTS"); /* no params or locals - nothing to save */
    } else {
        emit("    LDA __rt_csp");
        emit("    STA __zpAP");
        emit("    LDA __rt_csp+1");
        emit("    STA __zpAP+1");
        emit("    LDY #0");
        emit("__fn_%s_pushfr_loop:", fn->name);
        emit("    LDA __fn_%s_frame,Y", fn->name);
        emit("    STA (__zpAP),Y");
        emit("    INY");
        emit("    CPY #%d", total & 0xFF);
        emit("    BNE __fn_%s_pushfr_loop", fn->name);
        emit("    CLC");
        emit("    LDA __rt_csp");
        emit("    ADC #%d", total & 0xFF);
        emit("    STA __rt_csp");
        emit("    LDA __rt_csp+1");
        emit("    ADC #%d", (total >> 8) & 0xFF);
        emit("    STA __rt_csp+1");
        emit("    JMP __rt_cstack_check"); /* tail call: check RTSes straight back to our caller */
    }
    emit(" ");

    /* popframe: the exact reverse. Subtracting the (compile-time
     * constant) frame size from __rt_csp both rewinds the pointer AND
     * yields the base address to read the saved bytes back from, in
     * one step. */
    emit("__fn_%s_popframe:", fn->name);
    if (total == 0) {
        emit("    RTS");
    } else {
        emit("    SEC");
        emit("    LDA __rt_csp");
        emit("    SBC #%d", total & 0xFF);
        emit("    STA __rt_csp");
        emit("    STA __zpAP");
        emit("    LDA __rt_csp+1");
        emit("    SBC #%d", (total >> 8) & 0xFF);
        emit("    STA __rt_csp+1");
        emit("    STA __zpAP+1");
        emit("    LDY #0");
        emit("__fn_%s_popfr_loop:", fn->name);
        emit("    LDA (__zpAP),Y");
        emit("    STA __fn_%s_frame,Y", fn->name);
        emit("    INY");
        emit("    CPY #%d", total & 0xFF);
        emit("    BNE __fn_%s_popfr_loop", fn->name);
        emit("    RTS");
    }
    emit(" ");

    emit("%s:", flbl);
    gen_stmt(body);
    emit("    RTS"); /* fallback for functions that fall off the end */
    emit(" ");
}

