#include "coding.h"
#include "../util.h"

/* Various EA ADPCM codecs evolved from CDXA */

static const int32_t EA_XA_TABLE[28] = {
    0,0,240,0,
    460,-208,0x0188,-220,
    0x0000,0x0000,0x00F0,0x0000,
    0x01CC,0x0000,0x0188,0x0000,
    0x0000,0x0000,0x0000,0x0000,
    -208,-1,-220,-1,
    0x0000,0x0000,0x0000,0x3F70
};

static const int EA_TABLE[20] = {
    0,  240,  460,  392,
    0,    0, -208, -220,
    0,    1,    3,    4,
    7,    8,   10,   11,
    0,   -1,   -3,   -4
};

/* EA's main ADPCM, inconsistently called EAXA or EA-XA */
void decode_ea_xa(VGMSTREAMCHANNEL * stream, sample * outbuf, int channelspacing, int32_t first_sample, int32_t samples_to_do,int channel) {
    uint8_t frame_info;
    int32_t sample_count;
    int32_t coef1, coef2;
    int i, shift;
    off_t channel_offset = stream->channel_start_offset; /* suboffset within frame */

    first_sample = first_sample%28;

    /* header */
    frame_info = (uint8_t)read_8bit(stream->offset+channel_offset,stream->streamfile);
    channel_offset++;

    if (frame_info == 0xEE) { /* PCM frame (used in later revisions), always BE */
        stream->adpcm_history1_32 = read_16bitBE(stream->offset+channel_offset+0x00,stream->streamfile);
        stream->adpcm_history2_32 = read_16bitBE(stream->offset+channel_offset+0x02,stream->streamfile);
        channel_offset += 4;

        for (i=first_sample,sample_count=0; i<first_sample+samples_to_do; i++,sample_count+=channelspacing) {
            outbuf[sample_count] = read_16bitBE(stream->offset+channel_offset,stream->streamfile);
            channel_offset+=2;
        }

        /* Only increment offset on complete frame */
        if (channel_offset-stream->channel_start_offset == (2*28)+5)
            stream->channel_start_offset += (2*28)+5;

    } else { /* ADPCM frame */
        coef1 = EA_XA_TABLE[(((frame_info >> 4) & 0x0F) << 1)];
        coef2 = EA_XA_TABLE[(((frame_info >> 4) & 0x0F) << 1) + 1];
        shift = (frame_info & 0x0F) + 8;

        for (i=first_sample,sample_count=0; i<first_sample+samples_to_do; i++,sample_count+=channelspacing) {
            uint8_t sample_byte = (uint8_t)read_8bit(stream->offset+channel_offset+i/2,stream->streamfile);
            int32_t sample = ((((i&1?
                            sample_byte & 0x0F:
                            sample_byte >> 4
                          ) << 0x1C) >> shift) +
                          (coef1 * stream->adpcm_history1_32) + (coef2 * stream->adpcm_history2_32)) >> 8;

            outbuf[sample_count] = clamp16(sample);
            stream->adpcm_history2_32 = stream->adpcm_history1_32;
            stream->adpcm_history1_32 = sample;
        }
        channel_offset += i/2;

        /* Only increment offset on complete frame */
        if (channel_offset - stream->channel_start_offset == 0x0F)
            stream->channel_start_offset += 0x0F;
    }
}

/* EA MicroTalk 10:1 stereo (aka "EA ADPCM") */
void decode_ea_mt10(VGMSTREAMCHANNEL * stream, sample * outbuf, int channelspacing, int32_t first_sample, int32_t samples_to_do,int channel) {
    uint8_t frame_info;
    int32_t coef1, coef2;
    int i, sample_count, shift;
    off_t channel_offset = stream->channel_start_offset; /* suboffset within frame */
    int hn = (channel==0); /* high nibble marker for stereo subinterleave, ch0/L=high nibble, ch1/R=low nibble */

    first_sample = first_sample % 28;

    /* header (coefs ch0+ch1 + shift ch0+ch1) */
    frame_info = read_8bit(stream->offset+channel_offset,stream->streamfile);
    channel_offset++;
    coef1 = EA_TABLE[(hn ? frame_info >> 4 : frame_info & 0x0F) + 0];
    coef2 = EA_TABLE[(hn ? frame_info >> 4 : frame_info & 0x0F) + 4];

    frame_info = read_8bit(stream->offset+channel_offset,stream->streamfile);
    channel_offset++;
    shift = (hn ? frame_info >> 4 : frame_info & 0x0F) + 8;

    /* samples */
    for (i=first_sample,sample_count=0; i<first_sample+samples_to_do; i++,sample_count+=channelspacing) {
        uint8_t sample_byte, sample_nibble;
        int32_t sample;
        off_t byte_offset = (stream->offset + channel_offset + i);

        sample_byte = (uint8_t)read_8bit(byte_offset,stream->streamfile);
        sample_nibble = (hn   ? sample_byte >> 4 : sample_byte & 0x0F);

        sample = (sample_nibble << 28) >> shift; /* sign extend to 32b and shift */
        sample = (sample + coef1 * stream->adpcm_history1_32 + coef2 * stream->adpcm_history2_32 + 0x80) >> 8;

        outbuf[sample_count] = clamp16(sample);
        stream->adpcm_history2_32 = stream->adpcm_history1_32;
        stream->adpcm_history1_32 = sample;
    }
    channel_offset += i;

    /* Only increment offset on complete frame */
    if(channel_offset - stream->channel_start_offset == 0x1E)
        stream->channel_start_offset += 0x1E;
}

/* EA MicroTalk 10:1 mono/interleave */
void decode_ea_mt10_int(VGMSTREAMCHANNEL * stream, sample * outbuf, int channelspacing, int32_t first_sample, int32_t samples_to_do,int channel) {
    uint8_t frame_info;
    int32_t coef1, coef2;
    int i, sample_count, shift;
    off_t channel_offset = stream->channel_start_offset; /* suboffset within frame */

    first_sample = first_sample % 28;

    /* header (coefs+shift ch0) */
    frame_info = read_8bit(stream->offset+channel_offset,stream->streamfile);
    channel_offset++;
    coef1 = EA_TABLE[(frame_info >> 4) + 0];
    coef2 = EA_TABLE[(frame_info >> 4) + 4];
    shift = (frame_info & 0x0F) + 8;

    /* samples */
    for (i=first_sample,sample_count=0; i<first_sample+samples_to_do; i++,sample_count+=channelspacing) {
        uint8_t sample_byte, sample_nibble;
        int32_t sample;
        off_t byte_offset = (stream->offset + channel_offset + i/2);

        sample_byte = (uint8_t)read_8bit(byte_offset,stream->streamfile);
        sample_nibble = (!(i%2) ? sample_byte >> 4 : sample_byte & 0x0F);  /* i=even > high nibble */
        sample = (sample_nibble << 28) >> shift; /* sign extend to 32b and shift */
        sample = (sample + coef1 * stream->adpcm_history1_32 + coef2 * stream->adpcm_history2_32 + 0x80) >> 8;

        outbuf[sample_count] = clamp16(sample);
        stream->adpcm_history2_32 = stream->adpcm_history1_32;
        stream->adpcm_history1_32 = sample;
    }
    channel_offset += i/2;

    /* Only increment offset on complete frame */
    if(channel_offset - stream->channel_start_offset == 0x0F)
        stream->channel_start_offset += 0x0F;
}

/* EA MicroTalk 5:1, unknown variation with optional PCM blocks */
//void decode_ea_mt5(VGMSTREAM * vgmstream, sample * outbuf, int channelspacing, int32_t first_sample, int32_t samples_to_do,int channel)

/* Maxis MicroTalk 10:1 (mono+stereo), differing slightly in the header layout in stereo mode */
void decode_maxis_mt10(VGMSTREAMCHANNEL * stream, sample * outbuf, int channelspacing, int32_t first_sample, int32_t samples_to_do,int channel) {
    uint8_t frame_info;
    int32_t coef1, coef2;
    int i, sample_count, shift;
    off_t channel_offset = stream->channel_start_offset;
    int frameSize = channelspacing*15; /* mono samples have a frame of 15, stereo files have frames of 30 */

    first_sample = first_sample%28;

    /* header (coefs+shift ch0 + coefs+shift ch1) */
    frame_info = read_8bit(channel_offset,stream->streamfile);
    channel_offset += channelspacing;
    coef1 = EA_TABLE[frame_info >> 4];
    coef2 = EA_TABLE[(frame_info >> 4) + 4];
    shift = (frame_info & 0x0F)+8;

    /* samples */
    for (i=first_sample,sample_count=0; i<first_sample+samples_to_do; i++,sample_count+=channelspacing) {
        uint8_t sample_byte, sample_nibble;
        int32_t sample;
        off_t byte_offset = (stream->offset + channel_offset);

        sample_byte = (uint8_t)read_8bit(byte_offset,stream->streamfile);
        sample_nibble = (i&1) ? sample_byte & 0x0F : sample_byte >> 4;
        sample = (sample_nibble << 28) >> shift; /* sign extend to 32b and shift */
        sample = (sample + coef1 * stream->adpcm_history1_32 + coef2 * stream->adpcm_history2_32 + 0x80) >> 8;

        outbuf[sample_count] = clamp16(sample);
        stream->adpcm_history2_32 = stream->adpcm_history1_32;
        stream->adpcm_history1_32 = sample;

        if(i&1)
            stream->offset+=channelspacing;
    }
    channel_offset+=i;

    /* Only increment offset on complete frame */
    if (channel_offset - stream->channel_start_offset == frameSize) {
        stream->channel_start_offset += frameSize;
        stream->offset=0;
    }
}