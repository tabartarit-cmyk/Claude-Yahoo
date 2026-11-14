#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASHA_G722_RING_SIZE 8u
#define ASHA_G722_RING_SIZE_MASK 7u

// Number of samples per ms
#define ASHA_PCM_PACKET_SIZE 16u

// Max number of samples per ms
#define ASHA_PCM_MAX_SAMPLES 48u

// Stereo packet is twice the size of mono
#define ASHA_PCM_STEREO_PACKET_SIZE 32u

// 16-bits per sample
#define ASHA_PCM_PACKET_SIZE_BYTES 32u

// Number of bytes of 1ms of G.722 encoded audio
#define ASHA_G722_1MS_SIZE_BYTES 8u

// Nunber of bytes of 20ms of G.722 encoded audio
#define ASHA_G722_20MS_SIZE_BYTES 160u

// Size of l2cap SDU. Includes 20ms G.722 encoded audio
// plus a sequence number byte
#define ASHA_SDU_SIZE_BYTES 161u

// Add padding to sdu for memory alignment purposes
#define ASHA_SDU_SIZE_BYTES_ALIGNED 164u

/* ASHA volume range is -48dB to 0dB, and is set as an int8_t
 * with a range of -128 to 0. This gives steps of 0.375 dB.

 * USB volume control is an int16_t, from -128dB to +128dB, in
 * steps of 1/256 dB. The ASHA resolution is 96/256dB per step 
 * which gives a range of -12192 to 0 and a resolution of 96.
 */

#define ASHA_USB_VOL_MAX       0 // 0dB
#define ASHA_USB_VOL_MIN  -12192 // -47.625dB
#define ASHA_USB_VOL_RES      96 // 96/256 is the step between ASHA volume levels
#define ASHA_USB_VOL_MUTE -32768

// Extended lower bound for the configurable minimum volume (-90dB). Levels
// below ASHA_USB_VOL_MIN cannot be sent to the hearing aids, so they are
// produced by digitally attenuating the PCM signal instead.
#define ASHA_USB_VOL_MIN_EXT -23040

// Digital gain is Q15: 32768 = unity
#define ASHA_DIGITAL_GAIN_UNITY 32768u

// Volume curve exponent, in hundredths (100 = 1.00 = no change)
#define ASHA_VOL_CURVE_DEFAULT 100u
#define ASHA_VOL_CURVE_MIN      20u // 0.20
#define ASHA_VOL_CURVE_MAX     500u // 5.00
// Flag (bit 15) in the curve setting: interpolate linearly in POWER between
// Min and Max volume (25% on the slider = 1/4 of the power at Max Volume)
// instead of in dB. The exponent is still applied to the slider position.
#define ASHA_VOL_CURVE_POWER_FLAG 0x8000u
#define ASHA_VOL_CURVE_VALUE_MASK 0x7FFFu

enum AshaAudioSide {
    AudioLeft,
    AudioRight,
};

struct PCMStereoSample {
    int16_t left;
    int16_t right;
};

void asha_audio_init();

/**
 * Get the current write index.
 * 
 * If index is greater than 0, then the latest encoded
 * audio packet is index - 1
 */
uint32_t asha_audio_get_write_index();

/**
 * Encode 1ms of 16-bit 16kHz PCM stereo audio to G.722
 */
void asha_audio_encode_1ms_pcm(struct PCMStereoSample* samples, uint16_t count);

/**
 * Get encoded audio for side at index
 */
uint8_t* asha_audio_get_encoded_at_index(enum AshaAudioSide side, uint32_t index);

#ifdef PICO_ASHA_ENC_STATS
int16_t* asha_audio_get_encoding_time_at_index(uint32_t index);
#endif

/**
 * Set the current volume, as provided by USB
 */
//void asha_audio_set_curr_usb_vol(enum AshaAudioSide side, int16_t usb_volume);

void asha_audio_set_curr_usb_vol(int16_t main_vol, int16_t left_vol, int16_t right_vol);

/**
 * Set the digital gain (Q15, 32768 = unity) applied to the left/right PCM
 * before encoding. Used for levels below the hearing aids' volume floor.
 */
void asha_audio_set_digital_gain(uint32_t left_q15, uint32_t right_q15);

/**
 * Get the current volume, as provided by USB
 */
int16_t asha_audio_get_curr_usb_vol(enum AshaAudioSide side);

/**
 * Set whether to enable encoding or not
 */
void asha_audio_set_encoding_enabled(bool enabled);

/**
 * Discard any partial SDU and reset the codec/filter state before the next
 * encoded frame. This request is consumed by the PCM producer on core 0.
 */
void asha_audio_request_stream_reset();

/**
 * Get current encoding status
 */
bool asha_audio_get_encoding_enabled();

/**
 * Set whether to encode audio as mono
 */
void asha_audio_set_encode_mono(bool mono);

/**
 * Gets if encoding audio as mono
 */
bool asha_audio_get_encode_mono();

/**
 * Sets if PCM audio is playing
 */
void asha_audio_set_pcm_streaming_enabled(bool enabled);

/**
 * Gets state of PCM audio
 */
bool asha_audio_get_pcm_streaming_enabled();

#ifdef __cplusplus
}
#endif
