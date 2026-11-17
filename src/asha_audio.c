#include <stdatomic.h>
#include <string.h>

#ifdef PICO_ASHA_ENC_STATS
#include <pico/time.h>
#endif

#include <g722/g722_enc_dec.h>

#include "asha_audio.h"
#include "asha_fir.h"

static int16_t p_state_l[ASHA_FIR_STATE_SAMPLES];
static int16_t p_state_r[ASHA_FIR_STATE_SAMPLES];

struct AshaAudioEncBuffer {
    uint8_t l[ASHA_SDU_SIZE_BYTES_ALIGNED];
    uint8_t r[ASHA_SDU_SIZE_BYTES_ALIGNED];
#ifdef PICO_ASHA_ENC_STATS
    int16_t encode_times[20];
#endif
};

static atomic_bool pcm_streaming;
static atomic_bool encode_audio;
static atomic_bool encode_mono;
static atomic_bool requested_encode_mono;
static atomic_bool stream_reset_pending;

static atomic_uint_fast32_t write_index;

static atomic_int_least16_t vol_m;
static atomic_int_least16_t vol_l;
static atomic_int_least16_t vol_r;
static atomic_uint_fast32_t gain_l = ASHA_DIGITAL_GAIN_UNITY;
static atomic_uint_fast32_t gain_r = ASHA_DIGITAL_GAIN_UNITY;

static inline int16_t apply_gain(int16_t s, uint32_t g)
{
    if (g >= ASHA_DIGITAL_GAIN_UNITY) return s;
    return (int16_t)(((int32_t)s * (int32_t)g) >> 15);
}

g722_encode_state_t enc_state_l;
g722_encode_state_t enc_state_r;

static struct AshaAudioEncBuffer enc_ring_buff[ASHA_G722_RING_SIZE];
static unsigned int g_offset;
static unsigned int enc_time_index;
static uint8_t seq_num;

static int16_t pcm_buff_l[ASHA_PCM_MAX_SAMPLES];
static int16_t pcm_buff_r[ASHA_PCM_MAX_SAMPLES];

static int16_t pcm_buff_16khz_l[ASHA_PCM_PACKET_SIZE];
static int16_t pcm_buff_16khz_r[ASHA_PCM_PACKET_SIZE];

static inline uint32_t ring_buff_index(const uint32_t index)
{
    return index & ASHA_G722_RING_SIZE_MASK;
}

static void reset_encoders()
{
    g722_encode_init(&enc_state_l, 64000, G722_PACKED);
    g722_encode_init(&enc_state_r, 64000, G722_PACKED);
}

static void reset_decimators()
{
    asha_fir_reset(p_state_l);
    asha_fir_reset(p_state_r);
}

static void reset_stream_encoder()
{
    reset_encoders();
    reset_decimators();
    g_offset = 1;
    seq_num = 0;
    enc_time_index = 0;
    encode_mono = requested_encode_mono;
}

void asha_audio_init()
{
    memset(enc_ring_buff, 0, sizeof(enc_ring_buff));
    pcm_streaming = false;
    encode_audio = false;
    encode_mono = false;
    requested_encode_mono = false;
    stream_reset_pending = false;
    write_index = 0u;
    vol_l = ASHA_USB_VOL_MIN;
    vol_r = ASHA_USB_VOL_MIN;
    reset_stream_encoder();

}

uint32_t asha_audio_get_write_index()
{
    uint32_t wi = write_index;
    return wi;
}

static void encode_pcm(struct PCMStereoSample *samples, uint16_t count, bool scale)
{
#ifdef PICO_ASHA_ENC_STATS
    absolute_time_t start_time = get_absolute_time();
#endif
    if (atomic_exchange(&stream_reset_pending, false)) {
        // The hearing aid resets its decoder after an ACP stop/start. Reset on
        // this producer core so no partial SDU or stale predictor state leaks
        // into the next stream.
        reset_stream_encoder();
    }

    bool enc_audio = encode_audio;
    uint32_t w_index = write_index;
    if (!enc_audio) return;
    int buff_index = 0;
    struct AshaAudioEncBuffer* buff = &enc_ring_buff[ring_buff_index(w_index)];

    // A mode change must not split an SDU. When enabling the independent right
    // channel, its G.722 state must start clean for the newly starting aid.
    if (g_offset == 1) {
        bool requested_mono = requested_encode_mono;
        if (encode_mono != requested_mono) {
            encode_mono = requested_mono;
            if (!requested_mono) {
                g722_encode_init(&enc_state_r, 64000, G722_PACKED);
            }
        }
    }

    uint32_t const g_l = scale ? gain_l : ASHA_DIGITAL_GAIN_UNITY;
    uint32_t const g_r = scale ? gain_r : ASHA_DIGITAL_GAIN_UNITY;

    // Separate interleaved stereo samples to separate channels
    bool mono = encode_mono;
    if (mono) {
        int16_t val;
        for (unsigned int i = 0; i < count; ++i) {
            val = (int16_t)(((int32_t)apply_gain(samples[i].left, g_l) + (int32_t)apply_gain(samples[i].right, g_r)) / 2);
            pcm_buff_l[buff_index] = val;
            pcm_buff_r[buff_index] = val;
            ++buff_index;
        }       
    } else {
        for (unsigned int i = 0; i < count; ++i) {
            pcm_buff_l[buff_index] = apply_gain(samples[i].left, g_l);
            pcm_buff_r[buff_index] = apply_gain(samples[i].right, g_r);
            ++buff_index;
        }
    }
    int16_t* pcm_l = NULL;
    int16_t* pcm_r = NULL;
    if (count == ASHA_PCM_MAX_SAMPLES) {
        asha_fir_decimate_48_to_16_q15(pcm_buff_l, pcm_buff_16khz_l, p_state_l);
        if (!mono) {
            asha_fir_decimate_48_to_16_q15(pcm_buff_r, pcm_buff_16khz_r, p_state_r);
        }
        pcm_l = pcm_buff_16khz_l;
        pcm_r = pcm_buff_16khz_r;
    } else {
        pcm_l = pcm_buff_l;
        pcm_r = pcm_buff_r;
    }

    g722_encode(&enc_state_l, buff->l + g_offset, pcm_l, ASHA_PCM_PACKET_SIZE);
    if (mono) {
        memcpy(buff->r + g_offset, buff->l + g_offset, ASHA_G722_1MS_SIZE_BYTES);
    } else {
        g722_encode(&enc_state_r, buff->r + g_offset, pcm_r, ASHA_PCM_PACKET_SIZE);
    }
    
    g_offset += ASHA_G722_1MS_SIZE_BYTES;
#ifdef PICO_ASHA_ENC_STATS
    int64_t time_diff = absolute_time_diff_us(start_time, get_absolute_time());
    buff->encode_times[enc_time_index] = (int16_t)time_diff;
    ++enc_time_index;
#endif
    if (g_offset >= ASHA_SDU_SIZE_BYTES) {
        buff->l[0] = seq_num;
        buff->r[0] = seq_num;
        ++seq_num;
        g_offset = 1;
        write_index += 1;
        enc_time_index = 0;
    }
}

uint8_t* asha_audio_get_encoded_at_index(enum AshaAudioSide side, uint32_t index)
{
    struct AshaAudioEncBuffer* buff = &enc_ring_buff[ring_buff_index(index)];
    return side == AudioLeft ? buff->l : buff->r;
}

#ifdef PICO_ASHA_ENC_STATS
void asha_audio_encode_1ms_pcm(struct PCMStereoSample *samples, uint16_t count)
{
    encode_pcm(samples, count, true);
}

#if ASHA_A2DP
void asha_audio_encode_1ms_pcm_scaled(struct PCMStereoSample *samples, uint16_t count)
{
    encode_pcm(samples, count, false);
}
#endif

int16_t* asha_audio_get_encoding_time_at_index(uint32_t index)
{
    return (&enc_ring_buff[ring_buff_index(index)])->encode_times;
}
#endif

void asha_audio_set_curr_usb_vol(int16_t main_vol, int16_t left_vol, int16_t right_vol)
{
    vol_m = main_vol;
    vol_l = left_vol;
    vol_r = right_vol;
}

void asha_audio_set_digital_gain(uint32_t left_q15, uint32_t right_q15)
{
    gain_l = left_q15;
    gain_r = right_q15;
}


#if ASHA_A2DP
// ---------------------------------------------------------------------------
// Smartphone audio (A2DP). Single producer (Bluetooth core), single consumer
// (USB core). The ring holds decoded stereo frames at the phone's sample rate;
// the consumer resamples to 48 kHz with linear interpolation.
// ---------------------------------------------------------------------------
#define PHONE_RING_FRAMES 4096u            // power of two
#define PHONE_RING_MASK (PHONE_RING_FRAMES - 1u)
#define PHONE_PRIME_FRAMES 1024u           // start playing after about 23 ms
#define PHONE_MAX_FRAMES 2400u             // above this, drop the oldest audio
#define PHONE_KEEP_FRAMES 1200u
#define PHONE_AUDIBLE_PEAK 64              // about -54 dBFS
#define PHONE_HOLD_MS 1500u                // silence tolerated before the phone is "not playing"
#define PHONE_FADE_UP_STEP 34              // about 20 ms
#define PHONE_FADE_DOWN_STEP 7             // about 100 ms

static int16_t phone_ring[PHONE_RING_FRAMES * 2];
static atomic_uint_fast32_t phone_w;
static atomic_uint_fast32_t phone_r;
static atomic_uint_fast32_t phone_rate = 44100u;
static atomic_bool phone_stream_on;
static atomic_bool phone_is_audible;
static atomic_bool phone_mix_on;
static atomic_uint_fast32_t pgain_l = ASHA_DIGITAL_GAIN_UNITY;
static atomic_uint_fast32_t pgain_r = ASHA_DIGITAL_GAIN_UNITY;

// consumer state (USB core only)
static bool rs_primed;
static int16_t rs_pl, rs_pr, rs_cl, rs_cr;
static uint32_t rs_frac;
static uint32_t quiet_ms;
static int32_t phone_fade;                 // 0 .. ASHA_DIGITAL_GAIN_UNITY

void asha_audio_phone_push(const int16_t* stereo, uint32_t frames, uint32_t sample_rate)
{
    phone_rate = sample_rate;
    uint32_t w = phone_w;
    uint32_t const r = phone_r;
    uint32_t space = PHONE_RING_FRAMES - (w - r);
    if (frames > space) {
        frames = space; // the consumer is late: drop the newest audio
    }
    for (uint32_t i = 0; i < frames; ++i) {
        uint32_t const idx = ((w + i) & PHONE_RING_MASK) * 2u;
        phone_ring[idx] = stereo[2u * i];
        phone_ring[idx + 1u] = stereo[2u * i + 1u];
    }
    phone_w = w + frames;
}

void asha_audio_phone_set_stream(bool started)
{
    phone_stream_on = started;
}

bool asha_audio_phone_audible(void)
{
    return phone_is_audible;
}

void asha_audio_set_phone_mix(bool on)
{
    phone_mix_on = on;
}

void asha_audio_set_phone_gain(uint32_t left_q15, uint32_t right_q15)
{
    pgain_l = left_q15;
    pgain_r = right_q15;
}

static inline int16_t saturate16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

// Read the next phone frame into rs_c*, returns false on underrun
static bool phone_next_frame(void)
{
    uint32_t const r = phone_r;
    if (phone_w == r) {
        return false;
    }
    uint32_t const idx = (r & PHONE_RING_MASK) * 2u;
    rs_pl = rs_cl;
    rs_pr = rs_cr;
    rs_cl = phone_ring[idx];
    rs_cr = phone_ring[idx + 1u];
    phone_r = r + 1u;
    return true;
}

static void phone_reset_consumer(void)
{
    rs_primed = false;
    rs_pl = rs_pr = rs_cl = rs_cr = 0;
    rs_frac = 0;
    quiet_ms = PHONE_HOLD_MS;
    phone_is_audible = false;
    phone_r = phone_w; // discard everything
}

bool asha_audio_mix_phone(struct PCMStereoSample* usb, uint16_t usb_count, struct PCMStereoSample* out)
{
    bool const stream_on = phone_stream_on;
    bool const mix_on = phone_mix_on;
    if (!stream_on) {
        if (rs_primed || phone_is_audible) {
            phone_reset_consumer();
        }
        phone_is_audible = false;
        if (phone_fade == 0) return false;
    }

    // Keep the phone buffer short: drop the oldest audio if it has grown
    uint32_t avail = phone_w - phone_r;
    if (stream_on && avail > PHONE_MAX_FRAMES) {
        phone_r = phone_w - PHONE_KEEP_FRAMES;
        avail = PHONE_KEEP_FRAMES;
    }
    if (stream_on && !rs_primed && avail >= PHONE_PRIME_FRAMES) {
        rs_primed = true;
        rs_frac = 1u << 16; // load the first frame on the first output sample
    }

    int32_t peak = 0;
    uint32_t const step = (uint32_t)(((uint64_t)phone_rate << 16) / 48000u);
    uint32_t const gl = pgain_l, gr = pgain_r;
    uint32_t const ug_l = gain_l, ug_r = gain_r;
    int32_t const target = mix_on ? (int32_t)ASHA_DIGITAL_GAIN_UNITY : 0;

    for (unsigned int i = 0; i < ASHA_PCM_MAX_SAMPLES; ++i) {
        // phone sample at 48 kHz
        int32_t pl = 0, pr = 0;
        if (rs_primed) {
            while (rs_frac >= (1u << 16)) {
                rs_frac -= (1u << 16);
                if (!phone_next_frame()) {
                    // underrun: hold the last frame and wait for more audio
                    rs_primed = false;
                    break;
                }
            }
            if (rs_primed) {
                int32_t const f = (int32_t)(rs_frac >> 1); // 0 .. 32767
                pl = rs_pl + (((int32_t)rs_cl - rs_pl) * f >> 15);
                pr = rs_pr + (((int32_t)rs_cr - rs_pr) * f >> 15);
                rs_frac += step;
            }
        }
        int32_t const a = pl < 0 ? -pl : pl;
        int32_t const b = pr < 0 ? -pr : pr;
        if (a > peak) peak = a;
        if (b > peak) peak = b;

        // fade
        if (phone_fade < target) {
            phone_fade += PHONE_FADE_UP_STEP;
            if (phone_fade > target) phone_fade = target;
        } else if (phone_fade > target) {
            phone_fade -= PHONE_FADE_DOWN_STEP;
            if (phone_fade < target) phone_fade = target;
        }

        // USB sample (already 48 kHz) with the USB gains
        int32_t ul = 0, ur = 0;
        if (usb && usb_count == ASHA_PCM_MAX_SAMPLES) {
            ul = apply_gain(usb[i].left, ug_l);
            ur = apply_gain(usb[i].right, ug_r);
        }
        int32_t const mpl = (((int32_t)apply_gain(saturate16(pl), gl)) * phone_fade) >> 15;
        int32_t const mpr = (((int32_t)apply_gain(saturate16(pr), gr)) * phone_fade) >> 15;
        out[i].left = saturate16(ul + mpl);
        out[i].right = saturate16(ur + mpr);
    }

    // activity detection, once per millisecond
    if (peak > PHONE_AUDIBLE_PEAK) {
        quiet_ms = 0;
    } else if (quiet_ms < PHONE_HOLD_MS) {
        ++quiet_ms;
    }
    phone_is_audible = stream_on && rs_primed && quiet_ms < PHONE_HOLD_MS;

    // Use the mix while the phone is (or was just) audible, and until the fade
    // out is complete. Otherwise the USB audio is encoded as before.
    return phone_is_audible || phone_fade > 0;
}
#endif // ASHA_A2DP

int16_t asha_audio_get_curr_usb_vol(enum AshaAudioSide side)
{
    int16_t vol;
    if (vol_l != vol_r) {
        // Volume levels are different for each side, user likely wants this
        vol = side == AudioLeft ? vol_l : vol_r;
    } else {
        // Choose the lowest volume. Some OS drivers seem to prefer setting the "main"
        // channel (observed on MacOS with UAC1). Some set all the same (Windows UAC1) 
        // and some set the left/right pair (Windows UAC2). Channels that are not changed 
        // are often set to the loudest level - we don't want that!
        vol = (vol_m < vol_l) ? vol_m : vol_l;
    }
    return vol;
}

void asha_audio_set_encoding_enabled(bool enabled)
{
    encode_audio = enabled;
}

void asha_audio_request_stream_reset()
{
    stream_reset_pending = true;
}

bool asha_audio_get_encoding_enabled()
{
    bool enabled = encode_audio;
    return enabled;
}

void asha_audio_set_encode_mono(bool mono)
{
    requested_encode_mono = mono;
}

bool asha_audio_get_encode_mono()
{
    bool mono = encode_mono;
    return mono;
}

void asha_audio_set_pcm_streaming_enabled(bool enabled)
{
    pcm_streaming = enabled;
}

bool asha_audio_get_pcm_streaming_enabled()
{
    bool pcm = pcm_streaming;
    return pcm;
}
