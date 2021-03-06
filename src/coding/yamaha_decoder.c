#include "../util.h"
#include "coding.h"

/* fixed point amount to scale the current step size */
static const unsigned int scale_step_aica[16] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    230, 230, 230, 230, 307, 409, 512, 614
};

static const int scale_step_adpcmb[16] = {
    57, 57, 57, 57, 77, 102, 128, 153,
    57, 57, 57, 57, 77, 102, 128, 153,
};

/* look-up for 'mul' IMA's sign*((code&7) * 2 + 1) for every code */
static const int scale_delta[16] = {
      1,  3,  5,  7,  9, 11, 13, 15,
     -1, -3, -5, -7, -9,-11,-13,-15
};

/* Yamaha ADPCM-B (aka DELTA-T) expand used in YM2608/YM2610/etc (cross referenced with various sources and .so) */
static void yamaha_adpcmb_expand_nibble(VGMSTREAMCHANNEL * stream, off_t byte_offset, int nibble_shift, int32_t* hist1, int32_t* step_size, int16_t *out_sample) {
    int code, delta, sample;

    code = (read_8bit(byte_offset,stream->streamfile) >> nibble_shift) & 0xf;
    delta = ((((code & 0x7) * 2) + 1) * (*step_size)) >> 3; /* like 'mul' IMA */
    if (code & 8)
        delta = -delta;
    sample = *hist1 + delta;

    sample = clamp16(sample); /* this may not be needed (not done in Aska) but no byte changes */

    *step_size = ((*step_size) * scale_step_adpcmb[code]) >> 6;
    if (*step_size < 0x7f) *step_size = 0x7f;
    else if (*step_size > 0x6000) *step_size = 0x6000;

    *out_sample = sample;
    *hist1 = sample;
}

/* Yamaha AICA expand, slightly filtered vs "ACM" Yamaha ADPCM, same as Creative ADPCM
 * (some info from https://github.com/vgmrips/vgmplay, https://wiki.multimedia.cx/index.php/Creative_ADPCM) */
static void yamaha_aica_expand_nibble(VGMSTREAMCHANNEL * stream, off_t byte_offset, int nibble_shift, int32_t* hist1, int32_t* step_size, int16_t *out_sample) {
    int code, delta, sample;

    *hist1 = *hist1 * 254 / 256; /* hist filter is vital to get correct waveform but not done in many emus */

    code = ((read_8bit(byte_offset,stream->streamfile) >> nibble_shift))&0xf;
    delta = (*step_size * scale_delta[code]) / 8; /* 'mul' IMA with table (not sure if part of encoder) */
    sample = *hist1 + delta;

    sample = clamp16(sample); /* apparently done by official encoder */

    *step_size = ((*step_size) * scale_step_aica[code]) >> 8;
    if (*step_size < 0x7f) *step_size = 0x7f;
    else if (*step_size > 0x6000) *step_size = 0x6000;

    *out_sample = sample;
    *hist1 = sample;
}


/* info about Yamaha ADPCM as created by official yadpcm.acm (in 'Yamaha-ADPCM-ACM-Driver-100-j')
 * - possibly RIFF codec 0x20
 * - simply called "Yamaha ADPCM Codec" (even though not quite like Yamaha ADPCM-B)
 * - block_align = (sample_rate / 0x3C + 0x04) * channels (ex. 0x2E6 for 22050 stereo, probably given in RIFF)
 * - low nibble first, stereo or mono modes (no interleave)
 * - expand (old IMA 'shift+add' style, not 'mul' style):
 *      delta = step_size >> 3;
 *      if (code & 1) delta += step_size >> 2;
 *      if (code & 2) delta += step_size >> 1;
 *      if (code & 4) delta += step_size;
 *      if (code & 8) delta = -delta;
 *      sample = hist + clamp16(delta);
 *   though compiled more like:
 *      sample = hist + (1-2*(code & 8) * (step_size/8 + step_size/2 * (code&2) + step_size/4 * (code&1) + step_size * (code&4))
 * - step_size update:
 *      step_size = clamp_range(step_size * scale_step_aica[code] >> 8, 0x7F, 0x6000)
 */


/* Yamaha AICA ADPCM (also used in YMZ280B with high nibble first) */
void decode_aica(VGMSTREAMCHANNEL * stream, sample_t * outbuf, int channelspacing, int32_t first_sample, int32_t samples_to_do, int channel, int is_stereo) {
    int i, sample_count = 0;
    int16_t out_sample;
    int32_t hist1 = stream->adpcm_history1_16;
    int step_size = stream->adpcm_step_index;

    /* no header (external setup), pre-clamp for wrong values */
    if (step_size < 0x7f) step_size = 0x7f;
    if (step_size > 0x6000) step_size = 0x6000;

    for (i = first_sample; i < first_sample + samples_to_do; i++) {
        off_t byte_offset = is_stereo ?
                stream->offset + i :    /* stereo: one nibble per channel */
                stream->offset + i/2;   /* mono: consecutive nibbles */
        int nibble_shift = is_stereo ?
                (!(channel&1) ? 0:4) :  /* even = low/L, odd = high/R */
                (!(i&1) ? 0:4);         /* low nibble first */

        yamaha_aica_expand_nibble(stream, byte_offset, nibble_shift, &hist1, &step_size, &out_sample);
        outbuf[sample_count] = out_sample;
        sample_count += channelspacing;
    }

    stream->adpcm_history1_16 = hist1;
    stream->adpcm_step_index = step_size;
}

/* tri-Ace Aska ADPCM, Yamaha ADPCM-B with headered frames (reversed from Android SO's .so)
 * implements table with if-else/switchs too but that's too goofy */
void decode_aska(VGMSTREAMCHANNEL * stream, sample_t * outbuf, int channelspacing, int32_t first_sample, int32_t samples_to_do, int channel) {
    int i, sample_count = 0, num_frame;
    int16_t out_sample;
    int32_t hist1 = stream->adpcm_history1_32;
    int step_size = stream->adpcm_step_index;

    /* external interleave */
    int block_samples = (0x40 - 0x04*channelspacing) * 2 / channelspacing;
    num_frame = first_sample / block_samples;
    first_sample = first_sample % block_samples;

    /* header (hist+step) */
    if (first_sample == 0) {
        off_t header_offset = stream->offset + 0x40*num_frame + 0x04*channel;

        hist1     = read_16bitLE(header_offset+0x00,stream->streamfile);
        step_size = read_16bitLE(header_offset+0x02,stream->streamfile);
        /* in most files 1st frame has step 0 but it seems ok and accounted for */
        //if (step_size < 0x7f) step_size = 0x7f;
        //else if (step_size > 0x6000) step_size = 0x6000;
    }

    /* decode nibbles (layout: varies) */
    for (i = first_sample; i < first_sample + samples_to_do; i++) {
        off_t byte_offset = (channelspacing == 2) ?
                (stream->offset + 0x40*num_frame + 0x04*channelspacing) + i :    /* stereo: one nibble per channel */
                (stream->offset + 0x40*num_frame + 0x04*channelspacing) + i/2;   /* mono: consecutive nibbles */
        int nibble_shift = (channelspacing == 2) ?
                (!(channel&1) ? 0:4) :
                (!(i&1) ? 0:4);  /* even = low, odd = high */

        yamaha_adpcmb_expand_nibble(stream, byte_offset, nibble_shift, &hist1, &step_size, &out_sample);
        outbuf[sample_count] = out_sample;
        sample_count += channelspacing;
    }

    stream->adpcm_history1_32 = hist1;
    stream->adpcm_step_index = step_size;
}


/* Yamaha ADPCM with unknown expand variation (noisy), step size is double of normal Yamaha? */
void decode_nxap(VGMSTREAMCHANNEL * stream, sample_t * outbuf, int channelspacing, int32_t first_sample, int32_t samples_to_do) {
    int i, sample_count = 0, num_frame;
    int32_t hist1 = stream->adpcm_history1_32;
    int step_size = stream->adpcm_step_index;

    /* external interleave */
    int block_samples = (0x40 - 0x4) * 2;
    num_frame = first_sample / block_samples;
    first_sample = first_sample % block_samples;

    /* header (hist+step) */
    if (first_sample == 0) {
        off_t header_offset = stream->offset + 0x40*num_frame;

        hist1     = read_16bitLE(header_offset+0x00,stream->streamfile);
        step_size = read_16bitLE(header_offset+0x02,stream->streamfile);
        if (step_size < 0x7f) step_size = 0x7f;
        else if (step_size > 0x6000) step_size = 0x6000;
    }

    /* decode nibbles (layout: all nibbles from one channel) */
    for (i = first_sample; i < first_sample + samples_to_do; i++) {
        int code, delta, sample;
        off_t byte_offset = (stream->offset + 0x40*num_frame + 0x04) + i/2;
        int nibble_shift = (i&1?4:0); /* low nibble first? */

        code = (read_8bit(byte_offset,stream->streamfile) >> nibble_shift)&0xf;
        delta = (step_size * scale_delta[code]) / 8; //todo wrong
        sample = hist1 + delta;

        outbuf[sample_count] = clamp16(sample);
        hist1 = outbuf[sample_count];

        step_size = (step_size * scale_step_aica[code]) / 260.0; //todo wrong
        if (step_size < 0x7f) step_size = 0x7f;
        else if (step_size > 0x6000) step_size = 0x6000;

        sample_count += channelspacing;
    }

    stream->adpcm_history1_32 = hist1;
    stream->adpcm_step_index = step_size;
}

size_t yamaha_bytes_to_samples(size_t bytes, int channels) {
    if (channels <= 0) return 0;
    /* 2 samples per byte (2 nibbles) in stereo or mono config */
    return bytes * 2 / channels;
}

size_t aska_bytes_to_samples(size_t bytes, int channels) {
    int block_align = 0x40;
    if (channels <= 0) return 0;
    return (bytes / block_align) * (block_align - 0x04*channels) * 2 / channels
            + ((bytes % block_align) ? ((bytes % block_align) - 0x04*channels) * 2 / channels : 0);
}
