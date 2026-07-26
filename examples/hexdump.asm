; hexdump.asm - dumps the raw bytes of "LEVEL1" from disk, in hex,
; with no interpretation of what they mean, plus the true total byte
; count read all the way to EOF.
;
; Built specifically to debug maze.asm's own load_level appearing to
; fill the whole screen with a single repeated tile after loading
; "LEVEL1" from a real disk, despite working correctly in simulation
; every time it was tested there. That symptom -- one value,
; consistently, across a large block -- points at CHRIN being called
; past the file's own actual end without anything noticing: load_level
; only checks READST once, before its own read loop starts, never
; again during the 836-byte tile read that follows -- if the file on
; disk is shorter than expected for any reason (including how it was
; actually written there), every byte past that point would silently
; become whatever the KERNAL returns post-EOF, not a clear error.
;
; This can't tell you *why* the file might be short or malformed on
; disk -- only this program's own output can show that -- but it does
; show the two things needed to find out: the file's own first bytes,
; unfiltered (compare against what LEVEL1.dat is supposed to start
; with: $0F $0B for the start column/row, then "TEST MAZE" plus
; trailing spaces for the title, then $01 repeated for the top border
; row's own first several tiles), and the exact total byte count all
; the way to EOF (compare against 858, LEVEL1.dat's own true size).
;
; Also reports whether OPEN itself succeeded (carry clear) at all,
; and the READST value before any CHRIN, the same two things
; dir_raw.asm already established are worth checking first for
; exactly this kind of problem.

CHROUT = $FFD2
SETLFS = $FFBA
SETNAM = $FFBD
OPEN   = $FFC0
CLOSE  = $FFC3
CHKIN  = $FFC6
CLRCHN = $FFCC
CHRIN  = $FFCF
READST = $FFB7

; --- lib/text.inc's own required zero page ---
str_ptr = $fb
cmp_ptr = $fd
kw_ptr  = $02

; 16-bit total byte count, and print_decimal16's own scratch for
; converting it to the 5 decimal digits actually shown at the end --
; repeated subtraction of 10000/1000/100/10/1, the same technique
; maze.asm's own render_score already uses for its own score display,
; adapted here to print via CHROUT instead of writing to a custom
; character set's own screen codes.
total_count_lo = $033c
total_count_hi = $033d
print_scratch_lo = $033e
print_scratch_hi = $033f
print_amount_lo = $0340
print_amount_hi = $0341
dump_col_count = $0342

        .basic start

        .include "lib/text.inc"

start:
        CLS
        PRINT title_msg

        lda #10
        ldx #<filename
        ldy #>filename
        jsr SETNAM
        lda #$02                   ; logical file 2
        ldx #$08                   ; device 8
        ldy #$02                    ; secondary address 2 (read)
        jsr SETLFS
        jsr OPEN
        bcc open_ok

        ; OPEN failed -- show that plainly, plus the error code real
        ; OPEN leaves in A when carry is set
        pha
        PRINT open_failed_msg
        pla
        jsr print_hex_byte
        lda #13
        jsr CHROUT
        rts

open_ok:
        PRINT open_ok_msg

        ldx #$02
        jsr CHKIN

        ; the READST value right here, before any CHRIN at all --
        ; should be $00 on a healthy channel
        jsr READST
        PRINT first_readst_msg
        jsr print_hex_byte
        lda #13
        jsr CHROUT
        lda #13
        jsr CHROUT

        lda #$00
        sta total_count_lo
        sta total_count_hi
        sta dump_col_count

read_loop:
        jsr READST
        beq @read_one
        jmp read_done               ; EOF or an error -- either way,
                                        ; this is the file's own true
                                        ; end; nothing more to read

@read_one:
        jsr CHRIN
        pha                          ; save the byte just read -- the
                                        ; "already dumped 64 bytes?"
                                        ; check below needs A for
                                        ; itself, and would otherwise
                                        ; clobber the very byte CHRIN
                                        ; just returned before it's
                                        ; ever actually displayed

        lda total_count_lo
        cmp #64
        lda total_count_hi
        sbc #$00
        bcs @skip_display

        pla
        jsr print_hex_byte
        lda #$20
        jsr CHROUT

        inc dump_col_count
        lda dump_col_count
        cmp #16
        bne @count_only
        lda #$00
        sta dump_col_count
        lda #13
        jsr CHROUT
        jmp @count_only

@skip_display:
        pla                          ; still needs cleaning off the
                                        ; stack even when not shown
@count_only:
        inc total_count_lo
        bne read_loop
        inc total_count_hi
        jmp read_loop

read_done:
        jsr CLRCHN
        lda #$02
        jsr CLOSE

        lda #13
        jsr CHROUT
        PRINT total_msg
        lda total_count_lo
        sta print_scratch_lo
        lda total_count_hi
        sta print_scratch_hi
        jsr print_decimal16
        lda #13
        jsr CHROUT
        PRINT expect_msg
        rts

; Prints A as two hex digits via CHROUT.
print_hex_byte:
        pha
        lsr a
        lsr a
        lsr a
        lsr a
        jsr print_hex_nibble
        pla
        and #$0f
        jsr print_hex_nibble
        rts

print_hex_nibble:
        cmp #10
        bcc @digit
        clc
        adc #$07
@digit:
        clc
        adc #$30
        jsr CHROUT
        rts

; Prints the 16-bit value in print_scratch_lo/hi as 5 decimal digits
; via CHROUT (repeated subtraction of 10000/1000/100/10/1 -- the same
; technique maze.asm's own render_score already uses, adapted here to
; call CHROUT instead of writing to a custom character set's own
; screen codes).
print_decimal16:
        lda #<10000
        sta print_amount_lo
        lda #>10000
        sta print_amount_hi
        jsr extract_and_print_digit

        lda #<1000
        sta print_amount_lo
        lda #>1000
        sta print_amount_hi
        jsr extract_and_print_digit

        lda #<100
        sta print_amount_lo
        lda #>100
        sta print_amount_hi
        jsr extract_and_print_digit

        lda #<10
        sta print_amount_lo
        lda #>10
        sta print_amount_hi
        jsr extract_and_print_digit

        lda #<1
        sta print_amount_lo
        lda #>1
        sta print_amount_hi
        jsr extract_and_print_digit
        rts

extract_and_print_digit:
        ldx #$00
@loop:
        lda print_scratch_hi
        cmp print_amount_hi
        bcc @done
        bne @do_subtract
        lda print_scratch_lo
        cmp print_amount_lo
        bcc @done
@do_subtract:
        lda print_scratch_lo
        sec
        sbc print_amount_lo
        sta print_scratch_lo
        lda print_scratch_hi
        sbc print_amount_hi
        sta print_scratch_hi
        inx
        jmp @loop
@done:
        txa
        clc
        adc #$30
        jsr CHROUT
        rts

title_msg:
        .text "LEVEL1 RAW BYTE DUMP", 13, 13, 0
open_ok_msg:
        .text "OPEN: CARRY CLEAR (SUCCESS)", 13, 0
open_failed_msg:
        .text "OPEN: CARRY SET (FAILED). ERROR CODE: ", 0
first_readst_msg:
        .text "READST BEFORE ANY CHRIN: ", 0
total_msg:
        .text "TOTAL BYTES READ TO EOF: ", 0
expect_msg:
        .text 13, "LEVEL1.DAT SHOULD BE EXACTLY 00858 BYTES,", 13
        .text "STARTING WITH 0F 0B 54 45 53 54 20 4D", 13, 0

filename: .text "LEVEL1,S,R"
