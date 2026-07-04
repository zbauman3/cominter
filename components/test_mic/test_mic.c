// =============================================================================
// test_mic.c — Standalone microphone capture → filter → speaker playback test
//
// This is the hardware bring-up test for the intercom's analog audio path.
// One press of the talk button runs a full record-then-play cycle:
//
//   1. Button (GPIO35) is polled until pressed.
//   2. The mic signal on GPIO37 (A4) is captured for 5 seconds at 8 kHz
//      using the ADC in DMA "continuous" mode, oversampled 4x for less noise.
//   3. The captured buffer is cleaned up offline with a DSP chain:
//      a zero-phase band-pass filter (300–3400 Hz) plus a noise gate.
//   4. The cleaned audio is played back out the DAC on GPIO26 (A1), which
//      drives an LM386 amplifier + speaker.
//   5. Loop back to step 1.
//
// Signal path summary:
//   mic → MCP6002 preamp (~200x) → GPIO37/ADC1 → [this code] → GPIO26/DAC
//       → LM386 amp → speaker
//
// Key hardware constraint (see Step 2): on the ESP32 the ADC-DMA and DAC-DMA
// engines both live on the I2S0 peripheral, so only ONE can be open at a time.
// Capture and playback are sequential, so we open/close each around its phase.
//
// This file has ZERO dependencies on the rest of the cominter codebase.
// It doesn't use FreeRTOS tasks, queues, WiFi, or any application code.
// It's meant to be called directly from app_main() for hardware testing.
// =============================================================================

#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "driver/dac_continuous.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#include "test_mic/test_mic.h"

static const char *TAG = "TEST_MIC";


// ---- Pin Definitions ----

// The talk button — same pin as the main app uses.
// We poll it here instead of using an ISR to keep things simple.
#define MIC_TEST_BTN_PIN GPIO_NUM_35

// The microphone analog input — A4 on the ItsyBitsy ESP32.
// GPIO37 is on ADC1 channel 1, which means it works even when WiFi is on.
#define MIC_TEST_ADC_PIN GPIO_NUM_37
#define MIC_TEST_ADC_UNIT ADC_UNIT_1
#define MIC_TEST_ADC_CHANNEL ADC_CHANNEL_1 // GPIO37 = ADC1_CH1 on ESP32

// The audio output DAC — GPIO26 (A1 on ItsyBitsy ESP32).
// The ESP32 has two true 8-bit analog DACs, each fixed to a specific pin:
//   DAC_CHANNEL_MASK_CH0 = GPIO25 (A0),  DAC_CHANNEL_MASK_CH1 = GPIO26 (A1).
// We use channel 1 (A1). The DAC output swings 0–3.3V in 256 steps (~13 mV).
#define MIC_TEST_DAC_MASK    DAC_CHANNEL_MASK_CH1 // GPIO26 = A1

// DMA parameters for continuous DAC playback. The driver streams our sample
// buffer out through a ring of DMA descriptors, each pointing at one buffer.
//   - DAC_DMA_BUF_SIZE : bytes in each DMA buffer (one descriptor's worth).
//   - DAC_DMA_DESC_NUM : how many descriptors/buffers are chained in the ring.
//     More descriptors = more slack before the DMA underruns if the CPU is
//     briefly busy. desc_num * buf_size is the total in-flight audio.
//   - DAC_WRITE_CHUNK  : how many samples we hand to dac_continuous_write()
//     per call. At 8 kHz, 256 samples ≈ 32 ms of audio per write.
#define DAC_DMA_BUF_SIZE     1024                 // bytes per DMA descriptor
#define DAC_DMA_DESC_NUM     8                    // number of DMA descriptors (~4096 samples total)
#define DAC_WRITE_CHUNK      256                  // 8-bit samples per write call (~32ms)

// ---- Capture Parameters ----

// 8000 samples per second — the standard telephony sample rate.
// Good enough for voice (covers frequencies up to 4 kHz per Nyquist).
#define SAMPLE_RATE_HZ 8000

// Oversampling factor. Instead of sampling once per output sample, we run the
// ADC ADC_OVERSAMPLE times faster (8 kHz * 4 = 32 kHz) and average each group
// of ADC_OVERSAMPLE raw readings into one 8 kHz output sample. Averaging N
// independent readings cuts random noise by √N (4x averaging = 6 dB quieter).
#define ADC_OVERSAMPLE     4

// ADC_FRAME_SAMPLES : how many raw ADC readings we pull per read() call.
// ADC_FRAME_BYTES   : the same in bytes. On the ESP32 each ADC-DMA reading is
//   packed into a 4-byte "conversion result" word (SOC_ADC_DIGI_RESULT_BYTES),
//   which carries the 12-bit value plus the channel/unit it came from.
#define ADC_FRAME_SAMPLES  256            // ADC results per read call
#define ADC_FRAME_BYTES    (ADC_FRAME_SAMPLES * 4)  // ESP32: 4 bytes per ADC result

// How long to record, in seconds.
#define CAPTURE_DURATION_SEC 5

// Total number of samples we'll capture.
// 8000 * 5 = 40,000 samples.
#define TOTAL_SAMPLES (SAMPLE_RATE_HZ * CAPTURE_DURATION_SEC)

// ---- Sample Buffer ----
// Each sample is 12-bit (0–4095), stored in a uint16_t.
// Total memory: 40,000 * 2 bytes = 80 KB.
// The ESP32 has ~320 KB of DRAM, so this fits comfortably.
static uint16_t sample_buffer[TOTAL_SAMPLES];

// Parsed ADC frame buffer — static to keep off the FreeRTOS task stack.
// Holds one read frame (ADC_FRAME_SAMPLES results) from adc_continuous_read_parse().
static adc_continuous_data_t adc_frame[ADC_FRAME_SAMPLES];

// ---- DSP: 2nd-order "biquad" filter ----
//
// A biquad is the standard building block for digital audio filters. It
// computes each output sample from the two most recent inputs and the two
// most recent outputs:
//
//   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]      (feed-forward, the "zeros")
//                  - a1*y[n-1] - a2*y[n-2]      (feedback, the "poles")
//
// The five coefficients (b0,b1,b2,a1,a2) define the filter's shape — whether
// it's a high-pass, low-pass, etc., and where its corner frequency sits. The
// x[n-1..2]/y[n-1..2] history is the filter's "state". We split coefficients
// (biquad_t) from state (biquad_state_t) so ONE set of coefficients can be run
// with a fresh state in each direction — that's the trick that makes the
// zero-phase forward+backward pass in Step 4b possible.
//
// The coefficient formulas below are the well-known "RBJ audio EQ cookbook"
// equations, specialized to a Butterworth response (Q = 1/√2), which is
// maximally flat in the passband. They're pre-divided by a0 so the runtime
// inner loop doesn't need a division.
typedef struct {
  float b0, b1, b2, a1, a2;  // filter coefficients (shape); a0 normalized to 1
} biquad_t;

typedef struct {
  float x1, x2, y1, y2;      // delay memory: last two inputs and outputs
} biquad_state_t;

// Compute coefficients for a 2nd-order Butterworth HIGH-pass at corner fc.
// Frequencies below fc are attenuated (12 dB/octave); above fc pass through.
static biquad_t biquad_hpf(float fc, float fs) {
  float w0 = 2.0f * 3.14159265358979f * fc / fs;  // corner freq in radians/sample
  float cw = cosf(w0), sw = sinf(w0);
  float alpha = sw / (2.0f * 0.70710678f); // sets bandwidth; Q = 1/√2 = Butterworth
  float a0 = 1.0f + alpha;                 // normalization divisor
  biquad_t c = {
      .b0 =  (1.0f + cw) * 0.5f / a0,
      .b1 = -(1.0f + cw) / a0,
      .b2 =  (1.0f + cw) * 0.5f / a0,
      .a1 = -2.0f * cw / a0,
      .a2 =  (1.0f - alpha) / a0,
  };
  return c;
}

// Compute coefficients for a 2nd-order Butterworth LOW-pass at corner fc.
// Frequencies above fc are attenuated; below fc pass through. Same math as the
// high-pass but with the feed-forward (b) terms swapped in sign pattern.
static biquad_t biquad_lpf(float fc, float fs) {
  float w0 = 2.0f * 3.14159265358979f * fc / fs;
  float cw = cosf(w0), sw = sinf(w0);
  float alpha = sw / (2.0f * 0.70710678f);
  float a0 = 1.0f + alpha;
  biquad_t c = {
      .b0 = (1.0f - cw) * 0.5f / a0,
      .b1 = (1.0f - cw) / a0,
      .b2 = (1.0f - cw) * 0.5f / a0,
      .a1 = -2.0f * cw / a0,
      .a2 = (1.0f - alpha) / a0,
  };
  return c;
}

// Run one sample through a biquad. Applies the difference equation above, then
// shifts the delay memory: today's input/output become tomorrow's x1/y1, and
// yesterday's slide into x2/y2. Call this in a loop over the whole buffer.
static inline float biquad_process(const biquad_t *c, biquad_state_t *s, float x) {
  float y = c->b0 * x + c->b1 * s->x1 + c->b2 * s->x2
                      - c->a1 * s->y1 - c->a2 * s->y2;
  s->x2 = s->x1; s->x1 = x;   // age the input history
  s->y2 = s->y1; s->y1 = y;   // age the output history
  return y;
}

// Clamp an int back into the ADC's valid 12-bit range (0–4095) before we
// store it into the uint16 sample buffer. Filtering can briefly push values
// past the edges; this keeps them legal and avoids wrap-around glitches.
static inline uint16_t clamp_adc(int v) {
  if (v < 0) return 0;
  if (v > 4095) return 4095;
  return (uint16_t)v;
}

// ---- Helper: wait for button press ----
// Polls the button pin until it reads LOW (pressed), then waits for
// it to go back HIGH (released) to debounce. This is intentionally
// simple — no ISRs, no FreeRTOS, just a blocking loop.
static void wait_for_button_press(void) {
  // Wait for the pin to go LOW (button is active-low with a pull-up)
  while (gpio_get_level(MIC_TEST_BTN_PIN) == 1) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // Simple debounce: wait 50ms, then wait for release
  vTaskDelay(pdMS_TO_TICKS(50));
  while (gpio_get_level(MIC_TEST_BTN_PIN) == 0) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  vTaskDelay(pdMS_TO_TICKS(50));
}

// ---- Helper: play samples through the DAC via DMA ----
// Streams the captured buffer out the DAC. Two things worth understanding:
//
//   1. Bit depth conversion: our samples are 12-bit (0–4095) but the DAC is
//      only 8-bit (0–255), so we shift right by 4 (>> 4) to drop the low bits.
//      This is a plain volume-preserving rescale; the audio content survives.
//
//   2. Flow control for free: dac_continuous_write() copies our chunk into the
//      DMA ring and BLOCKS if the ring is full, only returning as the hardware
//      drains it at the 8 kHz sample clock. So the while-loop is paced by the
//      DAC itself — no manual timing, no CPU spin-wait, no jitter.
static void play_samples(dac_continuous_handle_t dac, uint16_t *samples, int count) {
  ESP_LOGI(TAG, "Playing %d samples at %d Hz via DMA DAC (GPIO26/A1)...", count, SAMPLE_RATE_HZ);

  uint8_t chunk[DAC_WRITE_CHUNK];
  int i = 0;

  while (i < count) {
    // Figure out how many samples go in this chunk (last one may be short).
    int n = count - i;
    if (n > DAC_WRITE_CHUNK) n = DAC_WRITE_CHUNK;

    // Downconvert this chunk from 12-bit to 8-bit for the DAC.
    for (int j = 0; j < n; j++) {
      chunk[j] = (uint8_t)(samples[i + j] >> 4);
    }

    // Hand the chunk to the DMA engine. 'loaded' tells us how many bytes it
    // actually accepted; timeout -1 = block until it takes them all.
    size_t loaded = 0;
    esp_err_t ret = dac_continuous_write(dac, chunk, (size_t)n, &loaded, -1);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "DAC write error: %s", esp_err_to_name(ret));
      break;
    }
    i += (int)loaded;
  }

  // Leave the DAC parked at mid-scale (128 = ~1.65V) so the output rail sits at
  // "silence" instead of holding the last sample's voltage, which would be an
  // audible DC step / pop into the amplifier.
  memset(chunk, 128, DAC_WRITE_CHUNK);
  size_t dummy;
  dac_continuous_write(dac, chunk, DAC_WRITE_CHUNK, &dummy, -1);

  ESP_LOGI(TAG, "Playback complete.");
}

// ---- Main test function ----
esp_err_t test_mic_run(void) {
  ESP_LOGI(TAG, "=== Microphone ADC Capture Test ===");
  ESP_LOGI(TAG, "Pin: GPIO%d (A4), Sample rate: %d Hz, Duration: %d sec",
           MIC_TEST_ADC_PIN, SAMPLE_RATE_HZ, CAPTURE_DURATION_SEC);
  ESP_LOGI(TAG, "Total samples: %d (%d KB buffer)",
           TOTAL_SAMPLES, (int)(TOTAL_SAMPLES * sizeof(uint16_t) / 1024));

  // ---- Step 1: Configure the button pin ----
  // Set up GPIO35 as a simple input. The board already has an external
  // pull-up resistor, so we don't enable the internal one.
  gpio_config_t btn_conf = {
      .pin_bit_mask = (1ULL << MIC_TEST_BTN_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE, // no interrupts — we're polling
  };
  gpio_config(&btn_conf);

  // ---- Step 2: Pre-define ADC and DAC configs (handles created per-cycle) ----
  // adc_continuous and dac_continuous both use I2S0 as DMA on ESP32, so they
  // can't be open simultaneously. Since capture and playback are sequential,
  // we create and destroy each handle within the loop, handing off I2S0
  // between the two phases.

  // ADC config — reused each capture cycle.
  // 'pattern' describes ONE channel to sample: which pin, how much input
  // attenuation, and the bit width. 12 dB attenuation maps the 0–3.1V input
  // range onto the full 0–4095 scale, covering our ~1.58V bias plus swing.
  adc_digi_pattern_config_t adc_pattern = {
      .atten     = ADC_ATTEN_DB_12,
      .channel   = MIC_TEST_ADC_CHANNEL,
      .unit      = MIC_TEST_ADC_UNIT,
      .bit_width = ADC_BITWIDTH_12,
  };
  // Handle config = the driver's internal buffering. max_store_buf_size is the
  // ring pool the driver fills from DMA; conv_frame_size is how much one read()
  // returns. We give it 4 frames of headroom so it won't overflow while the
  // CPU is busy averaging the previous frame.
  adc_continuous_handle_cfg_t adc_hdl_cfg = {
      .max_store_buf_size = ADC_FRAME_BYTES * 4,
      .conv_frame_size    = ADC_FRAME_BYTES,
  };
  // Run config = how to sample. We sample one channel (pattern_num = 1) at 4x
  // our audio rate (32 kHz). ADC_CONV_SINGLE_UNIT_1 uses only ADC1 — critical,
  // because ADC2 is shared with the WiFi radio and can't be read while WiFi is
  // on. TYPE1 is the ESP32's DMA result word format.
  adc_continuous_config_t adc_cont_cfg = {
      .pattern_num    = 1,
      .adc_pattern    = &adc_pattern,
      .sample_freq_hz = SAMPLE_RATE_HZ * ADC_OVERSAMPLE,
      .conv_mode      = ADC_CONV_SINGLE_UNIT_1,
      .format         = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
  };

  // DAC config — reused each playback cycle.
  // freq_hz is the output sample rate (8 kHz). clk_src MUST be APLL here: the
  // default PLL_D2 clock can't divide below ~19.6 kHz on the ESP32, which is
  // far above our 8 kHz; APLL reaches down to 648 Hz. chan_mode is irrelevant
  // with a single channel. offset shifts the DC level (0 = leave data as-is).
  dac_continuous_config_t dac_cfg = {
      .chan_mask  = MIC_TEST_DAC_MASK,
      .desc_num   = DAC_DMA_DESC_NUM,
      .buf_size   = DAC_DMA_BUF_SIZE,
      .freq_hz    = SAMPLE_RATE_HZ,
      .offset     = 0,
      .clk_src    = DAC_DIGI_CLK_SRC_APLL,
      .chan_mode  = DAC_CHANNEL_MODE_SIMUL,
  };

  // ---- Step 3: DC bias ----
  // The mic preamp rests at a DC voltage (not 0V) so the audio can swing both
  // up and down around it. In ADC counts that resting level is the "bias", and
  // the DSP stage subtracts it to get a signal centered on zero. We measured
  // the circuit's rest point at ~1.58V and hardcode the matching ADC count
  // (1.58 / 3.1 * 4095 ≈ 2087) instead of measuring it live — the reading is
  // stable and this avoids WiFi noise skewing a startup measurement.
  #define MEASURED_BIAS 2087
  ESP_LOGI(TAG, "DC bias: %d (~1.58V, hardcoded)", MEASURED_BIAS);

  esp_err_t ret;

  // ---- Step 4: Main loop — wait for button, capture, play, repeat ----
  while (true) {
    ESP_LOGI(TAG, "Press the button to start a %d-second capture...", CAPTURE_DURATION_SEC);
    wait_for_button_press();
    ESP_LOGI(TAG, "Capturing %d samples at %d Hz...", TOTAL_SAMPLES, SAMPLE_RATE_HZ);

    // ---- Step 4a: Capture phase — ADC holds I2S0 DMA ----
    adc_continuous_handle_t adc_handle;
    ret = adc_continuous_new_handle(&adc_hdl_cfg, &adc_handle);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "ADC init failed: %s", esp_err_to_name(ret));
      continue;
    }
    ret = adc_continuous_config(adc_handle, &adc_cont_cfg);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "ADC config failed: %s", esp_err_to_name(ret));
      adc_continuous_deinit(adc_handle);
      continue;
    }

    // samples_captured : how many finished 8 kHz output samples we have.
    // oversample_acc/count : running sum + counter for the 4-reading average
    // that produces each output sample.
    int samples_captured = 0;
    int32_t oversample_acc = 0;
    int oversample_count = 0;

    adc_continuous_start(adc_handle);  // hardware begins filling the DMA ring

    // Pull frames until we've assembled a full 5 seconds of averaged samples.
    while (samples_captured < TOTAL_SAMPLES) {
      // read_parse() blocks until a frame is ready, then decodes the raw DMA
      // words into adc_frame[] (value + channel + valid flag per reading).
      uint32_t num_parsed = 0;
      ret = adc_continuous_read_parse(adc_handle, adc_frame,
                                      ADC_FRAME_SAMPLES, &num_parsed,
                                      ADC_MAX_DELAY);
      if (ret != ESP_OK) continue;  // timeout/transient — just try again

      for (uint32_t j = 0; j < num_parsed && samples_captured < TOTAL_SAMPLES; j++) {
        // Skip malformed readings or any stray reading from another channel.
        if (!adc_frame[j].valid || adc_frame[j].channel != MIC_TEST_ADC_CHANNEL) continue;

        // Accumulate; every ADC_OVERSAMPLE readings, emit one averaged sample.
        oversample_acc += (int32_t)adc_frame[j].raw_data;
        if (++oversample_count == ADC_OVERSAMPLE) {
          sample_buffer[samples_captured++] = (uint16_t)(oversample_acc / ADC_OVERSAMPLE);
          oversample_acc = 0;
          oversample_count = 0;
        }
      }
    }

    // Stop the hardware and free the driver — this releases I2S0 so the DAC
    // can claim it for playback later in this same loop iteration.
    adc_continuous_stop(adc_handle);
    adc_continuous_deinit(adc_handle);  // releases I2S0

    ESP_LOGI(TAG, "Capture complete. %d/%d samples.", samples_captured, TOTAL_SAMPLES);

    // ---- Step 4b: DSP chain (zero-phase band-pass + noise gate) ----
    // Because we process the whole captured buffer offline, we can do things
    // a real-time filter can't:
    //   1. Zero-phase band-pass ("filtfilt"): each 2nd-order Butterworth
    //      section is run forward AND backward. This cancels phase distortion
    //      (voice stays natural) and doubles the effective rolloff to 4th
    //      order / 24 dB per octave.
    //   2. High-pass @ 300 Hz kills the low-frequency WiFi baseline drift.
    //   3. Low-pass @ 3400 Hz trims hiss above the voice band
    //      (classic 300–3400 Hz telephony passband).
    //   4. Noise gate ducks the residual WiFi hum between words.
    //
    // Everything is re-centered around MEASURED_BIAS on the way out.
    #define DSP_ENABLED     1
    #define HPF_CUTOFF_HZ   300.0f
    #define LPF_CUTOFF_HZ   3400.0f
    // Noise gate — set THRESHOLD just above the noise floor (in post-filter
    // ADC counts). FLOOR is the residual gain while gated (0.0 = full mute,
    // ~0.1 = -20 dB duck). Attack fast so speech onsets pass; release slow.
    #define GATE_ENABLED    1
    #define GATE_THRESHOLD  45.0f
    #define GATE_FLOOR      0.08f
    #define GATE_KNEE       35.0f    // soft-knee width below threshold (counts)
    #define GATE_ATTACK     0.35f    // envelope rise (~1 ms)
    #define GATE_RELEASE    0.0020f  // envelope fall (~60 ms)
    #define GATE_SMOOTH     0.030f   // gain slew (~4 ms, click-free)
    #if DSP_ENABLED
    {
      // Build the two filters once; we reuse the same coefficients for both
      // the forward and backward passes (with fresh state each time).
      biquad_t hpf = biquad_hpf(HPF_CUTOFF_HZ, (float)SAMPLE_RATE_HZ);
      biquad_t lpf = biquad_lpf(LPF_CUTOFF_HZ, (float)SAMPLE_RATE_HZ);

      // --- Forward pass: walk the buffer front-to-back through HPF then LPF ---
      // Each sample is centered (subtract bias), filtered, then re-centered
      // (add bias back) and clamped before being written in place.
      biquad_state_t hs = {0}, ls = {0};  // {0} = clear the filter's history
      for (int i = 0; i < TOTAL_SAMPLES; i++) {
        float x = (float)sample_buffer[i] - MEASURED_BIAS;
        x = biquad_process(&hpf, &hs, x);
        x = biquad_process(&lpf, &ls, x);
        sample_buffer[i] = clamp_adc((int)(x + 0.5f) + MEASURED_BIAS);
      }
      // --- Backward pass: walk the SAME data back-to-front through the SAME ---
      // filters (fresh state). A single IIR pass delays different frequencies
      // by different amounts (phase distortion, which smears transients).
      // Running it again in reverse applies an equal and opposite delay, so the
      // net phase shift is exactly zero — this is the "filtfilt" technique, and
      // it's only possible because we have the entire recording in memory. As a
      // bonus the response is applied twice, doubling the steepness to ~24
      // dB/octave (effectively a 4th-order band-pass).
      biquad_state_t hs2 = {0}, ls2 = {0};
      for (int i = TOTAL_SAMPLES - 1; i >= 0; i--) {
        float x = (float)sample_buffer[i] - MEASURED_BIAS;
        x = biquad_process(&hpf, &hs2, x);
        x = biquad_process(&lpf, &ls2, x);
        sample_buffer[i] = clamp_adc((int)(x + 0.5f) + MEASURED_BIAS);
      }
      ESP_LOGI(TAG, "Band-pass applied: %.0f-%.0f Hz (zero-phase, 4th order)",
               (double)HPF_CUTOFF_HZ, (double)LPF_CUTOFF_HZ);

#if GATE_ENABLED
      // --- Noise gate (soft-knee downward expander) ---
      // Idea: when the signal is loud (someone's talking) pass it at full gain;
      // when it's quiet (just WiFi hum between words) duck the gain down so the
      // hum is inaudible. Two smoothed state variables make this click-free:
      //
      //   env  — a running estimate of the signal's loudness (its envelope).
      //          It rises FAST (GATE_ATTACK) when a sample is louder than the
      //          current estimate, and falls SLOW (GATE_RELEASE) otherwise.
      //          Fast attack means a word's first syllable isn't chopped off;
      //          slow release means brief pauses mid-word don't slam the gate.
      //   gain — the multiplier we actually apply. It eases toward its target
      //          (GATE_SMOOTH) rather than jumping, so there's no click when
      //          the gate opens or closes.
      //
      // The "soft knee" is what keeps voice natural instead of robotic. Rather
      // than a hard on/off at GATE_THRESHOLD (which pumps and chops quiet
      // consonants like s/f/th), the target gain fades LINEARLY across a knee
      // window [threshold-knee .. threshold]:
      //   - env at/above threshold      → full gain (speech is untouched)
      //   - env at/below threshold-knee → GATE_FLOOR (true silence is ducked)
      //   - in between                  → a smooth blend
      // So real speech still plays at unity (preserving the sound you like) and
      // only the transition region is softened.
      float env = 0.0f, gain = GATE_FLOOR;
      for (int i = 0; i < TOTAL_SAMPLES; i++) {
        float x = (float)sample_buffer[i] - MEASURED_BIAS;
        float a = fabsf(x);                                    // instantaneous level
        env += (a - env) * (a > env ? GATE_ATTACK : GATE_RELEASE);

        // Soft-knee target: 1.0 above threshold, GATE_FLOOR below the knee,
        // linearly interpolated in between.
        float target;
        if (env >= GATE_THRESHOLD) {
          target = 1.0f;
        } else if (env <= GATE_THRESHOLD - GATE_KNEE) {
          target = GATE_FLOOR;
        } else {
          float t = (GATE_THRESHOLD - env) / GATE_KNEE;        // 0 at top of knee → 1 at bottom
          target = 1.0f - (1.0f - GATE_FLOOR) * t;
        }

        gain += (target - gain) * GATE_SMOOTH;                 // slew, don't jump
        sample_buffer[i] = clamp_adc((int)(x * gain + 0.5f) + MEASURED_BIAS);
      }
      ESP_LOGI(TAG, "Noise gate applied: threshold=%.0f, knee=%.0f, floor=%.2f",
               (double)GATE_THRESHOLD, (double)GATE_KNEE, (double)GATE_FLOOR);
#endif
    }
    #endif // DSP_ENABLED

    // ---- Step 5: Playback phase — DAC takes I2S0 DMA ----
    dac_continuous_handle_t dac_handle;
    ret = dac_continuous_new_channels(&dac_cfg, &dac_handle);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "DAC init failed: %s", esp_err_to_name(ret));
      continue;
    }
    ret = dac_continuous_enable(dac_handle);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "DAC enable failed: %s", esp_err_to_name(ret));
      dac_continuous_del_channels(dac_handle);
      continue;
    }

    play_samples(dac_handle, sample_buffer, TOTAL_SAMPLES);

    dac_continuous_disable(dac_handle);
    dac_continuous_del_channels(dac_handle);  // releases I2S0

    ESP_LOGI(TAG, "Done! Press the button again for another capture.");
  }

  return ESP_OK;
}
