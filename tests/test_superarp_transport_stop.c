#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/midi_fx_api_v1.h"
#include "host/plugin_api_v1.h"

extern midi_fx_api_v1_t* move_midi_fx_init(const host_api_v1_t *host);

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static int has_note_on_for(const uint8_t out_msgs[][3], int count, uint8_t note) {
    int i;
    for (i = 0; i < count; i++) {
        if ((out_msgs[i][0] & 0xF0) == 0x90 && out_msgs[i][1] == note && out_msgs[i][2] > 0) {
            return 1;
        }
    }
    return 0;
}

static int has_note_off_for(const uint8_t out_msgs[][3], int count, uint8_t note) {
    int i;
    for (i = 0; i < count; i++) {
        if ((out_msgs[i][0] & 0xF0) == 0x80 && out_msgs[i][1] == note) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    host_api_v1_t host;
    midi_fx_api_v1_t *api;
    void *inst;
    uint8_t out_msgs[64][3];
    int out_lens[64];
    uint8_t note_on[3] = {0x90, 60, 100};
    uint8_t stop_msg[1] = {0xFC};
    int count;
    int i;

    memset(&host, 0, sizeof(host));
    host.api_version = MOVE_PLUGIN_API_VERSION;

    api = move_midi_fx_init(&host);
    if (!api || !api->create_instance || !api->destroy_instance || !api->set_param || !api->process_midi || !api->tick) {
        fail("superarp API init/callbacks missing");
    }

    inst = api->create_instance(".", NULL);
    if (!inst) fail("create_instance returned NULL");

    api->set_param(inst, "sync", "internal");
    api->set_param(inst, "rate", "1/32");
    api->set_param(inst, "bpm", "240");
    api->set_param(inst, "gate", "1600");
    api->set_param(inst, "max_voices", "8");

    /* Arm held note. */
    count = api->process_midi(inst, note_on, 3, out_msgs, out_lens, 64);
    (void)count;

    /* Emit first arp note so a voice is active when Stop arrives. */
    count = api->tick(inst, 1500, 48000, out_msgs, out_lens, 64);
    if (!has_note_on_for(out_msgs, count, 60)) {
        fail("expected NOTE_ON before transport stop");
    }

    /* Stop transport while note is active: expect CC123 + explicit NOTE_OFF. */
    count = api->process_midi(inst, stop_msg, 1, out_msgs, out_lens, 8);
    if (count < 2) {
        fail("expected at least CC123 + NOTE_OFF on stop");
    }
    if (!(out_msgs[0][0] == 0xB0 && out_msgs[0][1] == 123 && out_msgs[0][2] == 0)) {
        fail("first stop message should be CC123 all-notes-off");
    }
    if (!has_note_off_for(out_msgs, count, 60)) {
        fail("expected explicit NOTE_OFF for active note on stop");
    }

    for (i = 0; i < count; i++) {
        if ((out_msgs[i][0] & 0xF0) == 0x00) {
            fail("stop output contained invalid zero-status MIDI message");
        }
    }

    api->destroy_instance(inst);
    printf("PASS: superarp transport stop emits explicit note-offs\n");
    return 0;
}

