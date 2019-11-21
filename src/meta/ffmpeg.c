#include "meta.h"
#include "../coding/coding.h"

#ifdef VGM_USE_FFMPEG

static int read_pos_file(uint8_t * buf, size_t bufsize, STREAMFILE *streamFile);

/**
 * Generic init FFmpeg and vgmstream for any file supported by FFmpeg.
 * Called by vgmstream when trying to identify the file type (if the player allows it).
 */
VGMSTREAM * init_vgmstream_ffmpeg(STREAMFILE *streamFile) {
	return init_vgmstream_ffmpeg_offset( streamFile, 0, streamFile->get_size(streamFile) );
}

VGMSTREAM * init_vgmstream_ffmpeg_offset(STREAMFILE *streamFile, uint64_t start, uint64_t size) {
    VGMSTREAM *vgmstream = NULL;
    int loop_flag = 0;
    int32_t loop_start = -1, loop_end = -1, num_samples = 0;
    int total_subsongs, target_subsong = streamFile->stream_index;

    /* no checks */
    //if (!check_extensions(streamFile, "..."))
    //    goto fail;


    /* init ffmpeg */
    ffmpeg_codec_data *data = init_ffmpeg_offset(streamFile, start, size);
    if (!data) return NULL;

    total_subsongs = data->streamCount;
    if (target_subsong == 0) target_subsong = 1;
    if (target_subsong < 0 || target_subsong > total_subsongs || total_subsongs < 1) goto fail;

    /* try to get .pos data */
    {
        uint8_t posbuf[4+4+4];

        if ( read_pos_file(posbuf, 4+4+4, streamFile) ) {
            loop_start = get_32bitLE(posbuf+0);
            loop_end = get_32bitLE(posbuf+4);
            loop_flag = 1; /* incorrect looping will be validated outside */
            /* FFmpeg can't always determine totalSamples correctly so optionally load it (can be 0/NULL)
             * won't crash and will output silence if no loop points and bigger than actual stream's samples */
            num_samples = get_32bitLE(posbuf+8);
        } else {
            AVDictionary *streamMetadata = data->formatCtx->streams[streamFile->stream_index]->metadata;

            // Try to detect the loop flags based on current file metadata
            AVDictionaryEntry *avLoopStart = av_dict_get(streamMetadata, "LoopStart", NULL, AV_DICT_IGNORE_SUFFIX);
            if (avLoopStart != NULL) {
                loop_start = atoi(avLoopStart->value);
                loop_flag = 1;
            }

            AVDictionaryEntry *avLoopEnd = av_dict_get(streamMetadata, "LoopEnd", NULL, AV_DICT_IGNORE_SUFFIX);
            if (avLoopEnd != NULL) {
                loop_end = atoi(avLoopEnd->value);
                loop_flag = 1;
            }

            if (loop_flag) {
                if (loop_end <= 0) {
                    // Detected a loop, but loop_end is still undefined or wrong. Try to calculate it.
                    AVDictionaryEntry *avLoopLength = av_dict_get(streamMetadata, "LoopLength", NULL, AV_DICT_IGNORE_SUFFIX);
                    if (avLoopLength != NULL) {
                        int loop_length = atoi(avLoopLength->value);

                        if (loop_start != -1) {
                            loop_end = loop_start + loop_length;
                            loop_flag = 1;
                        }
                    }
                }

                if (loop_end <= 0) {
                    // Looks a calculation was not possible, or tag value is wrongly set. Use the end of track as end value
                    loop_end = data->totalSamples;
                }
            } else {
                // Every other attempt to detect loop information failed, reset all the flags to sane values
                loop_flag = 0;
                loop_start = 0;
                loop_end = 0;
            }
        }
    }

    /* hack for AAC files (will return 0 samples if not an actual file) */
    if (!num_samples && check_extensions(streamFile, "aac,laac")) {
        num_samples = aac_get_samples(streamFile, 0x00, get_streamfile_size(streamFile));
    }

#ifdef VGM_USE_MPEG
    /* hack for MP3 files (will return 0 samples if not an actual file) */
    if (!num_samples && check_extensions(streamFile, "mp3,lmp3")) {
        num_samples = mpeg_get_samples(streamFile, 0x00, get_streamfile_size(streamFile));
    }
#endif

    /* default but often inaccurate when calculated using bitrate (wrong for VBR) */
    if (!num_samples) {
        num_samples = data->totalSamples;
    }


    /* build VGMSTREAM */
    vgmstream = allocate_vgmstream(data->channels, loop_flag);
    if (!vgmstream) goto fail;
    
    vgmstream->sample_rate = data->sampleRate;
    vgmstream->meta_type = meta_FFMPEG;
    vgmstream->coding_type = coding_FFmpeg;
    vgmstream->codec_data = data;
    vgmstream->layout_type = layout_none;

    vgmstream->num_samples = num_samples;
    if (loop_flag) {
        vgmstream->loop_start_sample = loop_start;
        vgmstream->loop_end_sample = loop_end;
    }

    /* this may happen for some streams if FFmpeg can't determine it (ex. AAC) */
    if (vgmstream->num_samples <= 0)
        goto fail;

    vgmstream->channel_layout = ffmpeg_get_channel_layout(vgmstream->codec_data);

    return vgmstream;
    
fail:
    free_ffmpeg(data);
    if (vgmstream) {
        vgmstream->codec_data = NULL;
        close_vgmstream(vgmstream);
    }
    
    return NULL;
}


/**
 * open file containing looping data and copy to buffer
 *
 * returns true if found and copied
 */
int read_pos_file(uint8_t * buf, size_t bufsize, STREAMFILE *streamFile) {
    char posname[PATH_LIMIT];
    char filename[PATH_LIMIT];
    /*size_t bytes_read;*/
    STREAMFILE * streamFilePos= NULL;

    streamFile->get_name(streamFile,filename,sizeof(filename));

    if (strlen(filename)+4 > sizeof(posname)) goto fail;

    /* try to open a posfile using variations: "(name.ext).pos" */
    {
        strcpy(posname, filename);
        strcat(posname, ".pos");
        streamFilePos = streamFile->open(streamFile,posname,STREAMFILE_DEFAULT_BUFFER_SIZE);
        if (streamFilePos) goto found;

        goto fail;
    }

found:
    //if (get_streamfile_size(streamFilePos) != bufsize) goto fail;

    /* allow pos files to be of different sizes in case of new features, just fill all we can */
    memset(buf, 0, bufsize);
    read_streamfile(buf, 0, bufsize, streamFilePos);

    close_streamfile(streamFilePos);

    return 1;

fail:
    if (streamFilePos) close_streamfile(streamFilePos);

    return 0;
}

#endif
