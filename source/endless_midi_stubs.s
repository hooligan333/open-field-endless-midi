/* The pack `endless midi`, versions 4 and 5, unchanged from version 3 but for this line (endless_midi.cpp says what it is for): its three routines at the first
 * instruction of three functions of OS 1.7.3+0. None holds an instruction of the firmware.
 *
 * The pack's table (endless_midi_table.s, made from profile.json) has the loader put a jump to each routine over the
 * function's first eight bytes, and copy those eight bytes (whole instructions, none relative to its own place) into
 * the routine's room with a jump back behind them. A jump to the room is the firmware's function as it always was.
 *
 *   endless_midi_on    0x8f00875c  the MIDI note-on function,  (message)
 *   endless_midi_off   0x8f00866c  the MIDI note-off function, (message)
 *   endless_midi_semi  0x8fc9f9e4  key number -> semitone,     (key); answers the semitone in R0
 *
 * Each is entered as the function would be: R0 the argument, RETS the caller's return, SP the caller's (with the
 * twelve bytes of argument space every call has above it). At a function's first instruction the registers a call may
 * change (R1-R3, P0-P2, ASTAT) hold nothing the function can depend on but its arguments, and each of these three has
 * one argument, in R0. So a routine keeps RETS and R0 on the stack (8 bytes, below the entry SP), calls its C++
 * function with twelve bytes of argument space, and then either
 *
 *   returns to the caller itself (RTS) with RETS and SP as at entry: the function's work was ours to do; or
 *   puts R0 back, RETS back, SP back, and jumps to its room (JUMP.L names no register; room and routine are in the
 *   same pack): the firmware's function then runs with exactly the R0, RETS, SP and stack it was called with.
 *
 * R4-R7, P3-P5 and FP are never touched here, and the C++ functions keep them as every compiled function does.
 * The C++ functions use no register but R0-R7, P0-P5, SP, FP, RETS and the flags (build.sh holds their code to that).
 *
 * endless_midi_semi also hands its C++ function the caller's return address (R1 = RETS), by which that function
 * knows endless's own recorder from every other caller. Its answer 0x80000000 means "not ours": the room.
 * The stack each takes below the entry SP: 20 bytes and what its C++ function takes.
 */
.text
.align 2

.global endless_midi_on
.type endless_midi_on, STT_FUNC
endless_midi_on:
    [--SP] = RETS;
    [--SP] = R0;                        /* the message; 8 bytes pushed */
    SP += -12;                          /* the argument space of the call */
    CALL endless_midi_note_on;          /* int (const unsigned char* message): 1 ours, 0 the firmware's */
    SP += 12;
    R1 = R0;
    R0 = [SP++];                        /* the message again */
    R2 = [SP++];
    RETS = R2;                          /* the caller's return; SP is the entry SP again */
    CC = R1 == 0;
    IF CC JUMP .Lon_room;
    RTS;                                /* ours: back to the caller, as the function would have returned */
.Lon_room:
    JUMP.L endless_midi_on_room;        /* the firmware's own note-on, from its first instruction */
.size endless_midi_on, .-endless_midi_on

.global endless_midi_off
.type endless_midi_off, STT_FUNC
endless_midi_off:
    [--SP] = RETS;
    [--SP] = R0;
    SP += -12;
    CALL endless_midi_note_off;         /* int (const unsigned char* message): 1 ours, 0 the firmware's */
    SP += 12;
    R1 = R0;
    R0 = [SP++];
    R2 = [SP++];
    RETS = R2;
    CC = R1 == 0;
    IF CC JUMP .Loff_room;
    RTS;
.Loff_room:
    JUMP.L endless_midi_off_room;       /* the firmware's own note-off */
.size endless_midi_off, .-endless_midi_off

.global endless_midi_semi
.type endless_midi_semi, STT_FUNC
endless_midi_semi:
    R1 = RETS;                          /* argument 2: who asks (the caller's return address) */
    [--SP] = RETS;
    [--SP] = R0;                        /* the key */
    SP += -12;
    CALL endless_midi_semitone;         /* int (unsigned key, unsigned from): the semitone, or 0x80000000 not ours */
    SP += 12;
    R1 = R0;
    R0 = [SP++];                        /* the key again */
    R2 = [SP++];
    RETS = R2;                          /* SP is the entry SP again */
    R2 = 0;
    BITSET (R2, 31);                    /* 0x80000000 */
    CC = R1 == R2;
    IF CC JUMP .Lsemi_room;
    R0 = R1;                            /* ours: the semitone, back to the caller */
    RTS;
.Lsemi_room:
    JUMP.L endless_midi_semi_room;      /* the firmware's own function, from its first instruction */
.size endless_midi_semi, .-endless_midi_semi

/* Zeroed data, which the loader clears when it loads the pack. Each room: the eight bytes its trampoline copies and
 * the eight of the jump back, filled by the loader when it applies the table. */
.section .bss
.balign 4
.global endless_midi_on_room
.type endless_midi_on_room, STT_OBJECT
endless_midi_on_room:
    .space 16
.size endless_midi_on_room, .-endless_midi_on_room
.global endless_midi_off_room
.type endless_midi_off_room, STT_OBJECT
endless_midi_off_room:
    .space 16
.size endless_midi_off_room, .-endless_midi_off_room
.global endless_midi_semi_room
.type endless_midi_semi_room, STT_OBJECT
endless_midi_semi_room:
    .space 16
.size endless_midi_semi_room, .-endless_midi_semi_room
