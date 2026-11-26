// The pack `endless midi`, version 6: steps of the stock sequencer `endless` recorded from a MIDI keyboard at the
// pitches played, over the keyboard's whole range. OS 1.7.3+0 under open-field 0.12 or later.
//
// Versions: 1 and 2 changed two arguments of endless's player and could not work (the player never records). 3 is this
// design, but asked whether endless's own controller takes the keys, which it never does, and took no note on a unit.
// 4 was a probe build that wrote down what each test read on the unit. 5 was 3 with the one test put right, with the
// probe's lines: it recorded on a unit (2026-11-25). 6 is 5 without the probe and without a log line for every note.
//
// What the firmware does (read in the owner's own OS 1.7.3 image, 2026-11-24; NOTES.md has every address):
//   - A step is recorded by endless's controller when a note KEY goes down while shift is held on endless's screen.
//     It keeps the key in its list of held keys, has endless's data add a note to the last step, and sounds the note.
//     At the key's up it takes the key off the list and ends the note.
//   - The step holds the key's SEMITONE (0 to 23, F first), which the recorder gets only by asking one small
//     function, key -> semitone, at five places (two at a down, two at an up, one when the screen is left).
//   - Nothing holds that number to 0..23: it is kept as a signed number, saved and loaded as one, and played as
//     53 + semitone + (held key - 7) + 12 x octave whenever that lies in 0..127. The screen shows only the step count.
//   - A MIDI note never reaches any of this: the MIDI note-on and note-off functions hand it to the sequencer's player.
//   - The firmware has its own way of making a key press from another thread: a 16-byte key event (key, value, 0, 0)
//     posted to the key queue, then the key thread woken. Its own "MIDI as keys" function does exactly that.
//
// What this pack does. While the unit's shift is held on endless's screen (the sequencers' controller takes the keys,
// the synth's sequencer is switched on and endless is the one picked; see endless_up() below), the unit is on the synth
// and endless is not latched, a MIDI note-on is not handed to the firmware's note-on function. Instead:
//   - the pack lends the note one of the 24 note keys (the key of the note's own pitch class when it is free), notes
//     for that key the semitone that makes the unit sound exactly this note, note - 53 - 12 x octave, and posts the
//     key's down with the note's velocity, as the firmware's own function would;
//   - when endless's recorder then asks key -> semitone for that key, from one of its five places, the pack answers
//     the note's semitone in place of the key's. Every other caller gets the key's own semitone, as always: some
//     of them index small tables with it, and they never see a number outside 0..23;
//   - the note's MIDI note-off posts the key's up; the recorder's last question about the key gives the key back.
// Every down the pack posts is followed by exactly one up, whatever happens between (the screen left, shift let go):
// a key press without its release would be worse than a note at the wrong pitch.
// So the recorder, the step, the sounded note and the note-off are the firmware's own code, on its own threads, and
// only the number it works with is ours. What is recorded is an ordinary endless sequence: the unit saves, loads and
// plays it without this pack.
//
// The three routines of endless_midi_stubs.s call the three functions marked STUB below. Threads:
//   endless_midi_note_on, _note_off   the MIDI handler's thread.
//   endless_midi_semitone             whichever thread works a key event off: the key thread, or the MIDI handler's
//                                     own inside the post when the firmware takes the event at once.
//   endless_midi_input                the key thread (the card's input function): which note keys the owner holds.
//   endless_midi_frame                the display thread: the key table read once, lent keys that were never given
//                                     back freed, the log (one line at the start, the counts after they moved).
// A lent key's four words and the word `busy` change only inside an op1f_mask section. The functions the stubs call
// write no log line, wait for nothing, divide nothing and have no loop the compiler could make a hardware loop of
// (build.sh holds their code to R0-R7, P0-P5, SP, FP, RETS and the flags).
//
// Compiled for the computer as it is with OP1F_HOST (host/check.cpp), where the firmware's words are the test's.

#include "build_r6e_card.h"

typedef unsigned int u32;
typedef unsigned char u8;

extern "C" const Op1fCard op1f_card_endless_midi OP1F_NAMED("op1f_card_endless_midi");

enum {
    KEYS = 24,                  // the note keys, by semitone (F first)
    KEY_FIRST = 0x0d,           // the lowest key number a note key has
    KEY_SPAN = 0x29,            // and how many numbers they lie in (0x0d .. 0x35)
    NOTE_OF_KEY_0 = 53,         // the unit's lowest key, F, at octave 0
    FOLD = 19,                  // (note + 19) mod 24: the key of the note's own pitch class, as the firmware folds
    OCTAVE_MOST = 4,            // the unit's octave: -4 .. 4
    NOT_OURS = (int)0x80000000, // endless_midi_semitone's answer for "the firmware's own"
    DOWN_POSTED = 1, UP_POSTED = 2,
    UP_OWED = 3,                // given back by the recorder (the screen was left) while the MIDI key is still down
    UP_FRAMES = 30,             // an up posted for a key the recorder never took: the key is freed after this
    EXPECT_FRAMES = 60,         // a key event the pack posted is looked for by its input function for this long
    COUNTS_STILL = 128,         // the log's counts line: written once the counts have stood still for this many frames
    COUNTS_LATEST = 3600        // (about 2 s), or this many frames (a minute) after they first moved, whichever comes first
};

// ---- the firmware's words and functions (OS 1.7.3+0; the pack's table holds three checksums of this firmware, so
// under any other the loader leaves the pack out and none of these is ever read)
#ifdef OP1F_HOST
extern u8 em_shift_held, em_latched;
extern u32 em_drum_mode, em_instrument, em_endless_up;
extern int em_octaves[8];
extern u32 em_key_ids[KEYS];
extern "C" void em_post(const u32* event);
extern "C" void em_wake();
extern "C" void em_note_end(u32 note);
#define FW_SHIFT_HELD (em_shift_held)
#define FW_LATCHED (em_latched)
#define FW_DRUM_MODE (em_drum_mode)
#define FW_INSTRUMENT (em_instrument)
#define FW_OCTAVES (em_octaves)
#define FW_KEY_IDS (em_key_ids)
#define FW_POST(e) em_post(e)
#define FW_WAKE() em_wake()
#define FW_NOTE_END(n) em_note_end(n)
static int endless_up() { return em_endless_up != 0; }
#else
typedef void (*PostFn)(const u32* event);
typedef void (*WakeFn)();
typedef void (*NoteFn)(u32 note, u32 velocity, u32 third, u32 fourth, u32 fifth, u32 sixth);
#define FW_SHIFT_HELD (*(const volatile u8*)0x84c96230u)        // not 0 while the unit's shift (key 0x26) is held
#define FW_LATCHED (*(const volatile u8*)0x84d0f321u)           // not 0 while endless is latched (its recorder then takes no key)
#define FW_DRUM_MODE (*(const volatile u32*)0x84d21088u)        // 1 while the unit is on the drum
#define FW_INSTRUMENT (*(const volatile u32*)0x11904704u)       // the synth sound the unit is on, 0..7
#define FW_OCTAVES ((const volatile int*)0x0802be08u)           // each synth sound's keyboard octave
#define FW_KEY_IDS ((const volatile u32*)0x8490c9f8u)           // the 24 note keys' numbers, by semitone
#define FW_ACTIVE_CONTROLLER (*(const volatile u32*)0x08018bd4u)   // the controller that takes the keys now, or 0
#define FW_ENDLESS_TABLE 0x8400ec58u                            // the table of functions of endless's controller
// The sequencers' controller (read 2026-11-25 and seen on a unit the same day; NOTES.md): while a sequencer's screen
// is up, the controller that takes the keys is not the sequencer's own but one object for all seven, which hands
// each key on to the controller of the sequencer picked, and shift + key only while that sequencer is switched on.
#define FW_SEQ_MANAGER 0x08018d2cu                              // the one controller of the sequencers' screen
#define FW_SEQ_MANAGER_TABLE 0x8401011cu                        // its table of functions
#define FW_SEQ_ON_SYNTH (*(const volatile u8*)(FW_SEQ_MANAGER + 9u))   // not 0: the synth's sequencer is switched on (it hands keys on)
#define FW_SEQ_TYPE_SYNTH (*(const volatile u32*)0x84d1ccccu)   // which sequencer the synth has picked, 0..6
#define FW_SEQ_CONTROLLERS ((const volatile u32*)0x08018d38u)   // the seven sequencers' own controllers, by that number
#define FW_SEQ_TYPES 7u
#define FW_POST(e) ((PostFn)0x8c0b2794u)(e)                     // a key event (key, value, 0, 0) into the key queue
#define FW_WAKE() ((WakeFn)0x8c0b2530u)()                       // the key thread woken, as the firmware does behind a post
#define FW_NOTE_END(n) ((NoteFn)0x8f008518u)((n), 0x40u, 1u, 0u, 0u, 0u)   // a note ended, with the words endless's recorder ends its notes with
// an address this unit has memory at and an object can lie at: the DDR, or the L2 the image fills (to 0x0803ffff)
static inline int em_object_at(u32 c) {
    if (c == 0u || (c & 3u) != 0u) return 0;
    return (c >= 0x80000000u && c < 0x90000000u) || (c >= 0x08000000u && c < 0x08040000u);
}
// 1 while endless's recorder gets the keys: the sequencers' controller takes them, it is what the reading took it for,
// the synth's sequencer is switched on (switched off, that controller hands no key on and the press would go to the
// firmware's own shift handling), and the sequencer picked for the synth is endless (its own controller has endless's
// table; its place among the seven is not assumed).
static int endless_up() {
    if (FW_ACTIVE_CONTROLLER != FW_SEQ_MANAGER) return 0;
    if (*(const volatile u32*)FW_SEQ_MANAGER != FW_SEQ_MANAGER_TABLE) return 0;
    if (!FW_SEQ_ON_SYNTH) return 0;
    u32 type = FW_SEQ_TYPE_SYNTH;
    if (type >= FW_SEQ_TYPES) return 0;
    u32 c = FW_SEQ_CONTROLLERS[type];
    if (!em_object_at(c)) return 0;
    return *(const volatile u32*)c == FW_ENDLESS_TABLE;
}
#endif
// The five places of endless's recorder that ask key -> semitone, by the address each call returns to.
static const u32 ASK_DOWN_DATA = 0x8fc92844u;   // the data adds the note to the step
static const u32 ASK_DOWN_SOUND = 0x8c0b87b8u;  // the controller sounds the note
static const u32 ASK_UP_DATA = 0x8fc92684u;     // the data takes the note off its held list
static const u32 ASK_UP_SOUND = 0x8c0b89f4u;    // the controller ends the note: the last question at a key's up
static const u32 ASK_EXIT_SOUND = 0x8c0b88e4u;  // the screen is left with the key held: the note is ended

static const char LOG_FILE[] = "op1f/endless_midi.cpp";    // the log shows the tag `endless_midi`

// ---- a lent key: by the semitone of the key itself (the key's number is FW_KEY_IDS[k])
struct Lent {
    volatile u32 note1;         // the MIDI note + 1, 0: the key is free
    volatile int semitone;      // what the key means to endless's recorder while it is lent
    volatile u32 state;         // DOWN_POSTED, then UP_POSTED; or UP_OWED
    volatile u32 frame;         // the frame of the down, then of the up
    volatile u32 taken;         // the recorder asked about the key at its down: it has the key on its list of held keys
    volatile u32 both;          // the firmware took a later note-on of this note itself: the next note-off is its too
};
static Lent lent[KEYS];
static volatile u32 busy;       // bit k: lent[k] is lent: the recorder's questions about the key are answered
static volatile u32 owed;       // bit k: the key was given back but its MIDI note is still down: its up is still to post
static volatile u32 owner_keys; // bit k: the owner holds note key k (the input function's)
// The pack's own key events come past its input function like any other (the base offers every key on its way to the
// mode shown). So that they are not taken for the owner's, each post is counted here first and counted off there.
static volatile u8 expect_down[KEYS], expect_up[KEYS];
static volatile u32 expect_frame[KEYS];
static u8 key_slot[KEY_SPAN];   // key number - KEY_FIRST -> k + 1, 0: no note key (the frame function makes it once)
static volatile u32 ready;      // key_slot is made, and the firmware's key table read as 24 different note keys
static volatile u32 frame_no;   // the frame function's

// The counts the log's one line gives (written by three threads, each count by one thread only, read by the display
// thread)
static volatile u32 downs, ups, given_back, asked, others, no_key, again, freed_late, ended_latched, passed_on, passed_off, for_both;

static inline u32 bit_of(u32 k) { return 1u << k; }

// 1 when a MIDI note is to be handed on as a key press now
static int armed() {
    if (!ready || !FW_SHIFT_HELD) return 0;
    if (FW_DRUM_MODE != 0u) return 0;                       // the synth only (the firmware's own test of the word): the drum
                                                            // plays the stored number straight onto its 24 keys
    if (FW_LATCHED) return 0;                               // latched, endless's recorder takes no key
    return endless_up();
}

// The key to lend to `note`: its own pitch class's key, that key an octave over, then the first free one. -1: none.
// A key is free when it is not lent and the owner does not hold it. No loop: 24 tests written out by the compiler
// from this chain would be a hardware loop, so the search goes by the lowest free bit.
static int key_for(u32 note, u32 taken) {
    u32 k = note + (u32)FOLD;                               // at most 146: three tests bring it under 24, nothing divided
    if (k >= 96u) k -= 96u;
    if (k >= 48u) k -= 48u;
    if (k >= 24u) k -= 24u;
    if (!(taken & bit_of(k))) return (int)k;
    u32 other = k >= 12u ? k - 12u : k + 12u;
    if (!(taken & bit_of(other))) return (int)other;
    u32 free_keys = ~taken & 0x00ffffffu;
    if (!free_keys) return -1;
    u32 low = free_keys & (0u - free_keys);                 // the lowest free bit
    u32 at = 0;
    if (low & 0x00ff0000u) at = 16u; else if (low & 0x0000ff00u) at = 8u;
    low >>= at;
    if (low & 0xf0u) { at += 4u; low >>= 4; }
    if (low & 0xcu) { at += 2u; low >>= 2; }
    if (low & 0x2u) at += 1u;
    return (int)at;
}

// STUB (endless_midi_on): the firmware's MIDI note-on was called with `message` (status, note, velocity; flags at
// +8). 1: handed on as a key press, the firmware's function is not to run. 0: the firmware's own.
extern "C" int endless_midi_note_on(const u8* message) OP1F_NAMED("endless_midi_note_on");
extern "C" int endless_midi_note_on(const u8* message) {
    u32 note = message[1];
    u32 velocity = (message[8] & 8u) ? 127u : message[2];   // as the firmware's own function reads it
    // velocity 0 is a note-off: the firmware's function calls its note-off function, which is ours too
    if (note > 127u || velocity == 0u || velocity > 127u || !armed()) {
        // The firmware takes this note-on itself. If the note still has one of our keys (its note-off was lost on the
        // way, or two keyboards hold it), the note-off that comes next is for both: ours, to post the key's up, and
        // the firmware's, to end the note it begins now. Without this the firmware's note would never be ended.
        if (velocity != 0u && velocity <= 127u && note <= 127u && (busy | owed)) {
            u32 was = op1f_mask();
            u32 left = busy | owed, at = 0;
            while (left) {
                if ((left & 1u) && lent[at].note1 == note + 1u) { lent[at].both = 1; break; }
                left >>= 1; ++at;
            }
            op1f_unmask(was);
        }
        passed_on = passed_on + 1u;
        return 0;
    }
    int octave = FW_OCTAVES[FW_INSTRUMENT & 7u];
    if (octave < -(int)OCTAVE_MOST) octave = -(int)OCTAVE_MOST;
    if (octave > (int)OCTAVE_MOST) octave = (int)OCTAVE_MOST;
    int semitone = (int)note - (int)NOTE_OF_KEY_0 - 12 * octave;
    u32 was = op1f_mask();
    u32 b = busy, in_use = b | owed, held = 0;
    // the note already has a key (a second down with no up between): nothing more to do
    if (in_use) {
        u32 k = 0, left = in_use;
        while (left) {                                      // only the keys in use, lowest first; not a counted loop
            if ((left & 1u) && lent[k].note1 == note + 1u) { held = 1; break; }
            left >>= 1; ++k;
        }
    }
    int k = held ? -1 : key_for(note, in_use | owner_keys);
    if (k >= 0) {
        Lent* l = &lent[k];
        l->semitone = semitone;
        l->state = DOWN_POSTED;
        l->frame = frame_no;
        l->taken = 0;
        l->both = 0;
        l->note1 = note + 1u;
        busy = b | bit_of((u32)k);
        expect_down[k] = (u8)(expect_down[k] + 1u);
        expect_frame[k] = frame_no;
    }
    op1f_unmask(was);
    if (held) { again = again + 1u; return 1; }
    if (k < 0) { no_key = no_key + 1u; return 0; }
    u32 event[4];
    event[0] = FW_KEY_IDS[k]; event[1] = velocity; event[2] = 0; event[3] = 0;
    downs = downs + 1u;
    FW_POST(event);                                         // the firmware may work it off at once, on this thread
    FW_WAKE();
    return 1;
}

// STUB (endless_midi_off): the firmware's MIDI note-off was called with `message`. 1: the note was one of ours and
// its key's up is posted. 0: the firmware's own (also behind our up, when the firmware began the note a second time).
extern "C" int endless_midi_note_off(const u8* message) OP1F_NAMED("endless_midi_note_off");
extern "C" int endless_midi_note_off(const u8* message) {
    u32 note = message[1];
    int k = -1;
    u32 end_it = 0, both = 0;
    u32 was = op1f_mask();
    u32 left = busy | owed, at = 0;
    while (left) {
        if ((left & 1u) && lent[at].note1 == note + 1u) {
            Lent* l = &lent[at];
            both = l->both;
            l->both = 0;
            if (l->state == (u32)DOWN_POSTED) {
                l->state = UP_POSTED;
                l->frame = frame_no;
                end_it = l->taken && FW_LATCHED;            // latched meanwhile: the recorder will not take this up
                k = (int)at;
            } else if (l->state == (u32)UP_OWED) {          // given back while the MIDI key was down: only the up is left
                l->note1 = 0; l->state = 0;
                owed = owed & ~bit_of(at);
                k = (int)at;
            }
            if (k >= 0) { expect_up[at] = (u8)(expect_up[at] + 1u); expect_frame[at] = frame_no; }
            break;                                          // UP_POSTED: a second note-off, the firmware's own
        }
        left >>= 1; ++at;
    }
    op1f_unmask(was);
    if (k < 0) { passed_off = passed_off + 1u; return 0; }
    u32 event[4];
    event[0] = FW_KEY_IDS[k]; event[1] = 0; event[2] = 0; event[3] = 0;
    ups = ups + 1u;
    FW_POST(event);
    FW_WAKE();
    if (end_it) {
        // The note the recorder began would ring on. It is ended here with the note the recorder itself would end:
        // by the semitone and the octave as it is now (the unit moves a sounding key's note when the octave moves).
        // The key stays lent until the screen is left and the recorder asks its last question about it.
        int octave = FW_OCTAVES[FW_INSTRUMENT & 7u];
        if (octave < -(int)OCTAVE_MOST) octave = -(int)OCTAVE_MOST;
        if (octave > (int)OCTAVE_MOST) octave = (int)OCTAVE_MOST;
        int sounds = (int)NOTE_OF_KEY_0 + lent[k].semitone + 12 * octave;
        ended_latched = ended_latched + 1u;
        if (sounds >= 0 && sounds <= 127) FW_NOTE_END((u32)sounds);
    }
    if (both) {                                             // the firmware has a note of its own to end with this note-off
        for_both = for_both + 1u;
        return 0;
    }
    return 1;
}

// STUB (endless_midi_semi): the firmware asks the semitone of `key`; `from` is the address the call returns to.
// The semitone of the MIDI note the key is lent to, when it is lent and endless's recorder asks; else NOT_OURS, and
// the firmware's own function answers. The recorder's last question about a key gives the key back.
extern "C" int endless_midi_semitone(u32 key, u32 from) OP1F_NAMED("endless_midi_semitone");
extern "C" int endless_midi_semitone(u32 key, u32 from) {
    u32 b = busy;
    if (!b) return NOT_OURS;                                // nothing lent: the usual case, three instructions
    u32 i = key - (u32)KEY_FIRST;
    if (i >= (u32)KEY_SPAN) return NOT_OURS;
    u32 k1 = key_slot[i];
    if (!k1 || !(b & bit_of(k1 - 1u))) return NOT_OURS;
    Lent* l = &lent[k1 - 1u];
    if (from == ASK_DOWN_DATA || from == ASK_DOWN_SOUND) {
        l->taken = 1;                                       // the recorder has the key now: only it gives the key back
        asked = asked + 1u;
        return l->semitone;
    }
    if (from == ASK_UP_DATA) {
        asked = asked + 1u;
        return l->semitone;
    }
    if (from == ASK_UP_SOUND || from == ASK_EXIT_SOUND) {
        int semitone = l->semitone;
        u32 was = op1f_mask();
        busy = busy & ~bit_of(k1 - 1u);
        if (l->state == (u32)DOWN_POSTED) {                 // the MIDI key is still down (the screen was left): its
            l->state = UP_OWED;                             // note-off will post the key's up
            owed = owed | bit_of(k1 - 1u);
        } else {
            l->note1 = 0; l->state = 0;
        }
        op1f_unmask(was);
        asked = asked + 1u;
        given_back = given_back + 1u;
        return semitone;
    }
    others = others + 1u;                                   // another caller: the key's own semitone, from the firmware
    return NOT_OURS;
}

// The card's input function: which note keys the owner holds, so that none of them is lent. Takes nothing.
extern "C" int endless_midi_input(const Op1fInput* e) OP1F_NAMED("endless_midi_input");
extern "C" int endless_midi_input(const Op1fInput* e) {
    if (!e || e->bytes < 36u) return 0;
    u32 what = e->what;
    u32 down = what == (u32)OP1F_DOWN || what == (u32)OP1F_DOWN_SHIFT;
    if (!down && what != (u32)OP1F_UP) return 0;
    u32 i = e->key - (u32)KEY_FIRST;
    if (ready && i < (u32)KEY_SPAN && key_slot[i]) {
        u32 k = (u32)key_slot[i] - 1u, bit = bit_of(k);
        u32 was = op1f_mask();
        if (frame_no - expect_frame[k] > (u32)EXPECT_FRAMES) { expect_down[k] = 0; expect_up[k] = 0; }   // never came past: forgotten
        if (down) {
            if (expect_down[k]) expect_down[k] = (u8)(expect_down[k] - 1u);    // the pack's own post
            else owner_keys = owner_keys | bit;                                // the owner's finger
        } else {
            if (expect_up[k]) expect_up[k] = (u8)(expect_up[k] - 1u);
            else owner_keys = owner_keys & ~bit;
        }
        op1f_unmask(was);
    }
    return down && e->bytes >= 40u ? OP1F_SEEN : 0;         // seen, not taken; the base then hands us the up too
}

// ---- the display thread
static struct {
    u32 started;
    u32 last[9];                // the counts as the last frame found them
    u32 owed, first_frame, moved_frame;   // a line is owed since first_frame; the counts last moved at moved_frame
} seen;

static void free_key(u32 k) {
    u32 was = op1f_mask();
    lent[k].note1 = 0; lent[k].state = 0;
    busy = busy & ~bit_of(k);
    op1f_unmask(was);
}

extern "C" void endless_midi_frame(const Op1fFrame* now) OP1F_NAMED("endless_midi_frame");
extern "C" void endless_midi_frame(const Op1fFrame* now) {
    if (!now || now->bytes < 28u) return;
    u32 f = frame_no + 1u;
    frame_no = f;
    if (!seen.started) {
        seen.started = 1;
        // the firmware's table of the 24 note keys: each a different number in KEY_FIRST .. KEY_FIRST + KEY_SPAN - 1
        u32 ok = 1;
        for (u32 k = 0; k < (u32)KEYS; ++k) {
            u32 i = FW_KEY_IDS[k] - (u32)KEY_FIRST;
            if (i >= (u32)KEY_SPAN || key_slot[i]) { ok = 0; break; }
            key_slot[i] = (u8)(k + 1u);
        }
        ready = ok;
        if (ok)
            op1f_log(3, LOG_FILE, 1, "endless midi: on (version 6). MIDI notes become endless's steps while shift is held on its screen "
                     "(the sequencer on, endless picked for the synth); places 8f00875c 8f00866c 8fc9f9e4");
        else
            op1f_log(2, LOG_FILE, 2, "endless midi: the unit's table of note keys is not as expected: nothing is changed, MIDI goes as it came");
        return;
    }
    // A key whose down the recorder never took (it was worked off without shift, or on another screen) is not given
    // back by a question: it is freed UP_FRAMES after its up was posted, when both have long been worked off. A key
    // the recorder took is given back only by the recorder. A key whose MIDI note-off never comes stays lent.
    u32 b = busy;
    if (b) {
        for (u32 k = 0; k < (u32)KEYS; ++k) {
            if (!(b & bit_of(k))) continue;
            if (lent[k].state == (u32)UP_POSTED && !lent[k].taken && f - lent[k].frame >= (u32)UP_FRAMES) {
                free_key(k);
                freed_late = freed_late + 1u;
            }
        }
    }
    // the log: no line for a note. The counts in one line, once they have stood still for COUNTS_STILL frames after
    // moving (so a stretch of recording gives one line when it pauses), or COUNTS_LATEST frames after they first moved
    // when they never rest. Notes the firmware takes itself move none of these counts: plain playing writes nothing.
    u32 c[9] = { downs, ups, given_back, others, no_key, again, freed_late, ended_latched, for_both };
    u32 moved = 0;
    for (u32 i = 0; i < 9u; ++i) if (c[i] != seen.last[i]) { seen.last[i] = c[i]; moved = 1; }
    if (moved) {
        if (!seen.owed) { seen.owed = 1; seen.first_frame = f; }
        seen.moved_frame = f;
    }
    if (seen.owed && (f - seen.moved_frame >= (u32)COUNTS_STILL || f - seen.first_frame >= (u32)COUNTS_LATEST)) {
        seen.owed = 0;
        op1f_log(3, LOG_FILE, 4, "endless midi: counts: %u downs %u ups handed on, %u keys given back, %u questions answered, %u from other "
                 "callers left to the firmware; %u no key free, %u down again, %u freed late, %u ended latched, %u note-offs for "
                 "both; notes left to the firmware %u on %u off; lent now %08x, ups owed %08x",
                 c[0], c[1], c[2], asked, c[3], c[4], c[5], c[6], c[7], c[8], passed_on, passed_off, busy, owed);
    }
}
