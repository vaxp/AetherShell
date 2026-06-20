#include "osd_sound.h"
#include "osd_logic_audio.h"
#include <glib.h>
#include <stdio.h>
#include <limits.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <pulse/pulseaudio.h>

#ifndef PREFIX
#define PREFIX "/usr/local"
#endif

typedef struct {
    int16_t *data;
    size_t size;
} SoundSample;

typedef struct {
    int16_t *data;
    size_t size;
    size_t offset;
} PlayInfo;

static SoundSample loaded_sounds[OSD_SOUND_EVENT_COUNT] = {0};
static volatile int active_streams_count = 0;
static gint64 last_play_time[OSD_SOUND_EVENT_COUNT] = {0};
static gint64 last_stop_time = 0;

static const char* get_sound_file_path(OsdSoundEvent event) {
    static char path[PATH_MAX];
    const char *filename = NULL;

    switch (event) {
        case OSD_SOUND_NOTIFICATION:
            filename = "notification.mp3";
            break;
        case OSD_SOUND_CHARGER_CONNECT:
            filename = "soundshelfstudio-ui-notification-for-pc-526570.mp3";
            break;
        case OSD_SOUND_CHARGER_DISCONNECT:
            filename = "soundshelfstudio-ui-notification-pop-minimal-523149.mp3";
            break;
        case OSD_SOUND_USB_CONNECT:
            filename = "universfield-new-notification-014-363678.mp3";
            break;
        case OSD_SOUND_USB_DISCONNECT:
            filename = "quiandrea96-notification-457196.mp3";
            break;
        case OSD_SOUND_LIMIT_HIGH:
            filename = "47313572-notification-alert-269289.mp3";
            break;
        case OSD_SOUND_LIMIT_LOW:
            filename = "u_4quckyrjhw-notification-sound-349341.mp3";
            break;
        case OSD_SOUND_ERROR:
            filename = "47313572-notification-alert-269289.mp3";
            break;
        default:
            return NULL;
    }

    // Try multiple paths:
    // 1. Primary sound path
    g_snprintf(path, sizeof(path), "/etc/vaxp/Sound/%s", filename);
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        return path;
    }

    // 2. Compile-time PREFIX /share/osd-notify/Sound/
    g_snprintf(path, sizeof(path), PREFIX "/share/osd-notify/Sound/%s", filename);
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        return path;
    }

    // 3. Local Sound/ directory (for development)
    g_snprintf(path, sizeof(path), "./Sound/%s", filename);
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        return path;
    }

    // 4. Flat Sound/ path
    g_snprintf(path, sizeof(path), "Sound/%s", filename);
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        return path;
    }

    // 5. Default /usr/share/osd-notify/Sound/
    g_snprintf(path, sizeof(path), "/usr/share/osd-notify/Sound/%s", filename);
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        return path;
    }

    // Fallback path
    g_snprintf(path, sizeof(path), "/etc/vaxp/Sound/%s", filename);
    return path;
}

static void pre_decode_sound(OsdSoundEvent event) {
    const char *path = get_sound_file_path(event);
    if (!path || !g_file_test(path, G_FILE_TEST_EXISTS)) {
        return;
    }

    AVFormatContext *format_ctx = NULL;
    if (avformat_open_input(&format_ctx, path, NULL, NULL) < 0) return;
    if (avformat_find_stream_info(format_ctx, NULL) < 0) {
        avformat_close_input(&format_ctx);
        return;
    }

    int audio_stream_idx = -1;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_idx = i;
            break;
        }
    }
    if (audio_stream_idx == -1) {
        avformat_close_input(&format_ctx);
        return;
    }

    AVCodecParameters *codecpar = format_ctx->streams[audio_stream_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&format_ctx);
        return;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        avformat_close_input(&format_ctx);
        return;
    }
    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0 || avcodec_open2(codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return;
    }

    SwrContext *swr_ctx = NULL;
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, 2);

    int ret = swr_alloc_set_opts2(&swr_ctx,
                                  &out_ch_layout,
                                  AV_SAMPLE_FMT_S16,
                                  44100,
                                  &codec_ctx->ch_layout,
                                  codec_ctx->sample_fmt,
                                  codec_ctx->sample_rate,
                                  0, NULL);
    if (ret < 0 || !swr_ctx || swr_init(swr_ctx) < 0) {
        if (swr_ctx) swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return;
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    GByteArray *pcm_buf = g_byte_array_new();

    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == audio_stream_idx) {
            if (avcodec_send_packet(codec_ctx, packet) >= 0) {
                while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                    int out_samples = av_rescale_rnd(swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples, 44100, codec_ctx->sample_rate, AV_ROUND_UP);
                    uint8_t *out_data[2] = {NULL};
                    int out_linesize = 0;
                    if (av_samples_alloc(out_data, &out_linesize, 2, out_samples, AV_SAMPLE_FMT_S16, 0) >= 0) {
                        int converted = swr_convert(swr_ctx, out_data, out_samples, (const uint8_t **)frame->data, frame->nb_samples);
                        if (converted > 0) {
                            g_byte_array_append(pcm_buf, out_data[0], converted * 2 * sizeof(int16_t));
                        }
                        if (out_data[0]) av_freep(&out_data[0]);
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    int out_samples = av_rescale_rnd(swr_get_delay(swr_ctx, codec_ctx->sample_rate), 44100, codec_ctx->sample_rate, AV_ROUND_UP);
    if (out_samples > 0) {
        uint8_t *out_data[2] = {NULL};
        int out_linesize = 0;
        if (av_samples_alloc(out_data, &out_linesize, 2, out_samples, AV_SAMPLE_FMT_S16, 0) >= 0) {
            int converted = swr_convert(swr_ctx, out_data, out_samples, NULL, 0);
            if (converted > 0) {
                g_byte_array_append(pcm_buf, out_data[0], converted * 2 * sizeof(int16_t));
            }
            if (out_data[0]) av_freep(&out_data[0]);
        }
    }

    loaded_sounds[event].size = pcm_buf->len;
    loaded_sounds[event].data = (int16_t *)g_byte_array_free(pcm_buf, FALSE);

    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);
}

void osd_sound_init(void) {
    for (int i = 0; i < OSD_SOUND_EVENT_COUNT; i++) {
        pre_decode_sound(i);
        if (loaded_sounds[i].data) {
            printf("[Sound] Pre-decoded sound event %d (size: %zu bytes)\n", i, loaded_sounds[i].size);
        } else {
            g_warning("[Sound] Failed to pre-decode sound event %d", i);
        }
    }
}

static void stream_write_cb(pa_stream *s, size_t length, void *userdata) {
    PlayInfo *pi = (PlayInfo *)userdata;
    if (!pi) return;

    size_t remaining = pi->size - pi->offset;
    if (remaining == 0) {
        pa_stream_disconnect(s);
        return;
    }

    size_t to_write = (remaining < length) ? remaining : length;
    if (to_write > 0) {
        if (pa_stream_write(s, (const uint8_t *)pi->data + pi->offset, to_write, NULL, 0, PA_SEEK_RELATIVE) < 0) {
            pa_stream_disconnect(s);
            return;
        }
        pi->offset += to_write;
    }
}

static void stream_state_cb(pa_stream *s, void *userdata) {
    PlayInfo *pi = (PlayInfo *)userdata;
    if (!pi) return;

    switch (pa_stream_get_state(s)) {
        case PA_STREAM_READY: {
            size_t writable = pa_stream_writable_size(s);
            stream_write_cb(s, writable, pi);
            break;
        }
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED: {
            pa_stream_set_write_callback(s, NULL, NULL);
            pa_stream_set_state_callback(s, NULL, NULL);
            pa_stream_unref(s);
            g_free(pi);
            g_atomic_int_add(&active_streams_count, -1);
            last_stop_time = g_get_monotonic_time();
            break;
        }
        default:
            break;
    }
}

static gboolean play_sound_deferred(gpointer data) {
    OsdSoundEvent event = GPOINTER_TO_INT(data);

    SoundSample *sample = &loaded_sounds[event];
    if (!sample->data || sample->size == 0) {
        return FALSE;
    }

    pa_context *ctx = osd_logic_audio_get_context();
    if (!ctx || pa_context_get_state(ctx) != PA_CONTEXT_READY) {
        return FALSE;
    }

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = 44100;
    ss.channels = 2;

    pa_stream *stream = pa_stream_new(ctx, "OSD Sound", &ss, NULL);
    if (!stream) {
        return FALSE;
    }

    PlayInfo *pi = g_new0(PlayInfo, 1);
    pi->data = sample->data;
    pi->size = sample->size;
    pi->offset = 0;

    pa_stream_set_state_callback(stream, stream_state_cb, pi);
    pa_stream_set_write_callback(stream, stream_write_cb, pi);

    g_atomic_int_add(&active_streams_count, 1);
    pa_stream_connect_playback(stream, NULL, NULL, PA_STREAM_NOFLAGS, NULL, NULL);

    return FALSE;
}

void osd_sound_play(OsdSoundEvent event) {
    if (event < 0 || event >= OSD_SOUND_EVENT_COUNT) return;

    // Rate limit the limit high/low alerts to once every 800ms
    gint64 now = g_get_monotonic_time();
    if (event == OSD_SOUND_LIMIT_HIGH || event == OSD_SOUND_LIMIT_LOW) {
        if (now - last_play_time[event] < 800000) {
            return;
        }
    }
    last_play_time[event] = now;

    // Defer to next main loop iteration to prevent PulseAudio lock deadlocks
    g_idle_add(play_sound_deferred, GINT_TO_POINTER(event));
}

gboolean osd_sound_should_ignore_events(void) {
    if (g_atomic_int_get(&active_streams_count) > 0) {
        return TRUE;
    }
    gint64 now = g_get_monotonic_time();
    if (now - last_stop_time < 1000000) { // 1.0s
        return TRUE;
    }
    return FALSE;
}
