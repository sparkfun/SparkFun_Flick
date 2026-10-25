/*
 * bl_teensy_audio.cpp - the Teensy Audio library in Baseline (ABI 1.8).
 *
 * Sketches hold small integer handles; the objects and connections are real
 * Audio library objects here. Constructors and methods are reached by number
 * through bl_audio_dispatch.inc, generated from api/audio_api.txt by
 * tools/gen_audio.py. The audio update runs in Teensy's software interrupt as
 * usual; objects may read sketch data (AudioPlayMemory samples, filter
 * coefficients, delay lines) from the sketch's flash or RAM, which is safe
 * because both are always readable while interrupts are on.
 */
#include <Audio.h>
#include <malloc.h>
#include "bl_teensy.h"
#include "bl_audio_dispatch.inc"

#define MAX_OBJECTS     128
#define MAX_CONNECTIONS 256

struct audio_object {
    void *base;         /* AudioStream * or AudioControl * */
    int16_t type;
    bool stream;
};

static audio_object s_obj[MAX_OBJECTS];
static AudioConnection *s_conn[MAX_CONNECTIONS];
static int s_memory_blocks;

static audio_object *object(int h)
{
    return h >= 0 && h < MAX_OBJECTS && s_obj[h].base ? &s_obj[h] : NULL;
}

extern "C" int bl_impl_audio_memory(int blocks)
{
    if (blocks <= 0) {
        return BL_ERR_ARG;
    }
    if (s_memory_blocks) {          // the Audio library takes its pool once
        return blocks <= s_memory_blocks ? BL_OK : BL_ERR_STATE;
    }
    audio_block_t *pool = (audio_block_t *)memalign(32, blocks * sizeof(audio_block_t));   // OCRAM heap
    if (!pool) {
        return BL_ERR_NO_MEM;
    }
    AudioStream::initialize_memory(pool, blocks);
    s_memory_blocks = blocks;
    return BL_OK;
}

extern "C" int bl_impl_audio_create(int type, const bl_audio_arg_t *args, int nargs)
{
    for (int h = 0; h < MAX_OBJECTS; h++) {
        if (s_obj[h].base) {
            continue;
        }
        bool stream = false;
        AudioNoInterrupts();        // a new object joins the update list
        void *base = bl_audio_new(type, args, args ? nargs : 0, &stream);
        AudioInterrupts();
        if (!base) {
            return BL_ERR_ARG;      // no such type in this Baseline
        }
        s_obj[h].base = base;
        s_obj[h].type = (int16_t)type;
        s_obj[h].stream = stream;
        return h;
    }
    return BL_ERR_NO_MEM;
}

extern "C" int bl_impl_audio_connect(int src, int src_port, int dst, int dst_port)
{
    audio_object *s = object(src), *d = object(dst);
    if (!s || !d || !s->stream || !d->stream || src_port < 0 || dst_port < 0) {
        return BL_ERR_ARG;
    }
    for (int h = 0; h < MAX_CONNECTIONS; h++) {
        if (s_conn[h]) {
            continue;
        }
        AudioConnection *c = new AudioConnection();
        if (c->connect(*(AudioStream *)s->base, (unsigned char)src_port, *(AudioStream *)d->base,
                       (unsigned char)dst_port) != 0) {
            delete c;
            return BL_ERR_ARG;      // no such port, or the input is taken
        }
        s_conn[h] = c;
        return h;
    }
    return BL_ERR_NO_MEM;
}

extern "C" int bl_impl_audio_disconnect(int connection)
{
    if (connection < 0 || connection >= MAX_CONNECTIONS || !s_conn[connection]) {
        return BL_ERR_ARG;
    }
    return s_conn[connection]->disconnect() == 0 ? BL_OK : BL_ERR_STATE;
}

extern "C" int bl_impl_audio_reconnect(int connection)
{
    if (connection < 0 || connection >= MAX_CONNECTIONS || !s_conn[connection]) {
        return BL_ERR_ARG;
    }
    return s_conn[connection]->connect() == 0 ? BL_OK : BL_ERR_STATE;
}

extern "C" int bl_impl_audio_call(int h, int method, const bl_audio_arg_t *args, int nargs, bl_audio_arg_t *ret)
{
    audio_object *o = object(h);
    if (!o) {
        return BL_ERR_ARG;
    }
    if (!args) {
        nargs = 0;
    }
    if (method >= 1000 && o->stream) {      // AudioStream's own methods
        AudioStream *s = (AudioStream *)o->base;
        switch (method) {
        case 1000: if (ret) ret->f = s->processorUsage(); return BL_OK;
        case 1001: if (ret) ret->f = s->processorUsageMax(); return BL_OK;
        case 1002: s->processorUsageMaxReset(); return BL_OK;
        default: return BL_ERR_UNSUPPORTED;
        }
    }
    return bl_audio_dispatch(o->type, o->base, method, args, nargs, ret);
}

extern "C" float bl_impl_audio_stat(int what)
{
    switch (what) {
    case BL_AUDIO_STAT_CPU:              return AudioProcessorUsage();
    case BL_AUDIO_STAT_CPU_MAX:          return AudioProcessorUsageMax();
    case BL_AUDIO_STAT_CPU_MAX_RESET:    AudioProcessorUsageMaxReset(); return 0;
    case BL_AUDIO_STAT_MEMORY:           return AudioMemoryUsage();
    case BL_AUDIO_STAT_MEMORY_MAX:       return AudioMemoryUsageMax();
    case BL_AUDIO_STAT_MEMORY_MAX_RESET: AudioMemoryUsageMaxReset(); return 0;
    default:                             return 0;
    }
}

extern "C" void bl_impl_audio_interrupts(int enable)
{
    if (enable) {
        AudioInterrupts();
    } else {
        AudioNoInterrupts();
    }
}
