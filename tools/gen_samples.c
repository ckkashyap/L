/* tools/gen_samples.c -- Generate WAV audio samples for L demos.
 * No external dependencies -- just the C math library.
 *
 * Compile: gcc -O2 -o tools/gen_samples tools/gen_samples.c -lm
 *     or:  cl.exe /O2 /Fe:tools\gen_samples.exe tools\gen_samples.c
 * Run:     tools/gen_samples       (from project root)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/stat.h>

#define SR 44100
#define PI 3.14159265358979323846

/* ---- WAV writer -------------------------------------------------------- */

static void write_wav(const char *path, const float *samples, int n) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot write %s\n", path); return; }
    int dbytes = n * 2; /* 16-bit mono */
    /* RIFF header */
    fwrite("RIFF", 1, 4, f);
    uint32_t riff_size = 36 + dbytes;
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmt_size = 16;     fwrite(&fmt_size, 4, 1, f);
    uint16_t fmt_tag = 1;       fwrite(&fmt_tag, 2, 1, f);   /* PCM */
    uint16_t channels = 1;      fwrite(&channels, 2, 1, f);
    uint32_t rate = SR;         fwrite(&rate, 4, 1, f);
    uint32_t bps = SR * 2;     fwrite(&bps, 4, 1, f);
    uint16_t align = 2;        fwrite(&align, 2, 1, f);
    uint16_t bits = 16;        fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    uint32_t data_size = dbytes; fwrite(&data_size, 4, 1, f);
    for (int i = 0; i < n; i++) {
        float s = samples[i];
        if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
        int16_t v = (int16_t)(s * 32767.0f);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
}

/* ---- Synthesis --------------------------------------------------------- */

static float *sine_tone(float freq, int dur_ms, float decay, int *out_n) {
    int n = SR * dur_ms / 1000;
    float *buf = calloc(n, sizeof(float));
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        buf[i] = sinf(2.0f * PI * freq * t) * expf(-decay * t) * 0.8f;
    }
    *out_n = n;
    return buf;
}

static float *kick_808(int *out_n) {
    int n = SR / 2; /* 500ms */
    float *buf = calloc(n, sizeof(float));
    float phase = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float freq = 40 + 160 * expf(-t * 20);
        float amp = 0.95f * expf(-t * 4);
        float s = sinf(phase * 2 * PI) * amp * 1.5f;
        buf[i] = s > 0.95f ? 0.95f : (s < -0.95f ? -0.95f : s);
        phase += freq / SR;
    }
    *out_n = n;
    return buf;
}

static float *snare_808(int *out_n) {
    int n = SR * 3 / 10; /* 300ms */
    float *buf = calloc(n, sizeof(float));
    unsigned rng = 42;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float body = sinf(i * 2 * PI * 180.0f / SR) * 0.5f * expf(-t * 25);
        rng = rng * 1664525u + 1013904223u;
        float noise = ((float)(rng >> 1) / (float)0x7FFFFFFFu * 2 - 1) * 0.7f * expf(-t * 15);
        float v = body + noise;
        buf[i] = v > 0.95f ? 0.95f : (v < -0.95f ? -0.95f : v);
    }
    *out_n = n;
    return buf;
}

static float *hihat(int dur_ms, int *out_n) {
    int n = SR * dur_ms / 1000;
    float *buf = calloc(n, sizeof(float));
    unsigned rng = 67890;
    float decay = dur_ms < 100 ? 60.0f : 8.0f;
    float prev = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        rng = rng * 1664525u + 1013904223u;
        float raw = ((float)(rng >> 1) / (float)0x7FFFFFFFu * 2 - 1) * 0.4f * expf(-t * decay);
        prev = prev * 0.95f + raw * 0.05f;
        buf[i] = raw - prev; /* high-pass */
    }
    *out_n = n;
    return buf;
}

static float *clap_808(int *out_n) {
    int n = SR / 4; /* 250ms */
    float *buf = calloc(n, sizeof(float));
    unsigned rng = 77;
    for (int burst = 0; burst < 4; burst++) {
        int start = (int)(burst * SR * 0.012f);
        int blen = SR * 20 / 1000;
        for (int i = 0; i < blen && start + i < n; i++) {
            float t = (float)i / SR;
            rng = rng * 1664525u + 1013904223u;
            buf[start + i] += ((float)(rng >> 1) / (float)0x7FFFFFFFu * 2 - 1) * 0.4f * expf(-t * 50);
        }
    }
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        rng = rng * 1664525u + 1013904223u;
        float v = buf[i] + ((float)(rng >> 1) / (float)0x7FFFFFFFu * 2 - 1) * 0.3f * expf(-t * 12);
        buf[i] = v > 0.95f ? 0.95f : (v < -0.95f ? -0.95f : v);
    }
    *out_n = n;
    return buf;
}

static float *tom_808(float freq, int *out_n) {
    int n = SR * 4 / 10; /* 400ms */
    float *buf = calloc(n, sizeof(float));
    float phase = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float f = freq + 40 * expf(-t * 15);
        buf[i] = sinf(phase * 2 * PI) * 0.8f * expf(-t * 6);
        phase += f / SR;
    }
    *out_n = n;
    return buf;
}

/* ---- Directory creation ------------------------------------------------ */

static void mkdirs(const char *path) {
#ifdef _WIN32
    char cmd[512]; snprintf(cmd, sizeof(cmd), "mkdir \"%s\" 2>nul", path);
    system(cmd);
#else
    char cmd[512]; snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
    system(cmd);
#endif
}

/* ---- Main -------------------------------------------------------------- */

int main(void) {
    printf("=== Generating audio samples ===\n");

    /* ---- Basic synth piano (sine + decay) ---- */
    mkdirs("deps/samples/piano");
    struct { const char *name; float freq; } piano[] = {
        {"C3",130.81},{"Cs3",138.59},{"D3",146.83},{"Ds3",155.56},
        {"E3",164.81},{"F3",174.61},{"Fs3",185.00},{"G3",196.00},
        {"Gs3",207.65},{"A3",220.00},{"As3",233.08},{"B3",246.94},
        {"C4",261.63},{"Cs4",277.18},{"D4",293.66},{"Ds4",311.13},
        {"E4",329.63},{"F4",349.23},{"Fs4",369.99},{"G4",392.00},
        {"Gs4",415.30},{"A4",440.00},{"As4",466.16},{"B4",493.88},
        {"C5",523.25}
    };
    int npiano = sizeof(piano) / sizeof(piano[0]);
    for (int i = 0; i < npiano; i++) {
        char path[256];
        snprintf(path, sizeof(path), "deps/samples/piano/%s.wav", piano[i].name);
        int n; float *buf = sine_tone(piano[i].freq, 2500, 2.5f, &n);
        write_wav(path, buf, n); free(buf);
    }
    printf("  Piano: %d notes (sine + decay)\n", npiano);

    /* ---- Basic synth drums ---- */
    mkdirs("deps/samples/drums");
    { int n; float *b;
      b = kick_808(&n);   write_wav("deps/samples/drums/kick.wav", b, n);  free(b);
      b = snare_808(&n);  write_wav("deps/samples/drums/snare.wav", b, n); free(b);
      b = hihat(45, &n);  write_wav("deps/samples/drums/hihat.wav", b, n); free(b);
      b = hihat(280, &n); write_wav("deps/samples/drums/ohat.wav", b, n);  free(b);
    }
    printf("  Drums: kick, snare, hihat, ohat\n");

    /* ---- 808 drum kit ---- */
    mkdirs("deps/samples/drums-808");
    { int n; float *b;
      b = kick_808(&n);     write_wav("deps/samples/drums-808/kick.wav", b, n);   free(b);
      b = snare_808(&n);    write_wav("deps/samples/drums-808/snare.wav", b, n);  free(b);
      b = hihat(60, &n);    write_wav("deps/samples/drums-808/hihat.wav", b, n);  free(b);
      b = hihat(400, &n);   write_wav("deps/samples/drums-808/ohat.wav", b, n);   free(b);
      b = clap_808(&n);     write_wav("deps/samples/drums-808/clap.wav", b, n);   free(b);
      b = tom_808(70, &n);  write_wav("deps/samples/drums-808/tom-lo.wav", b, n); free(b);
      b = tom_808(120, &n); write_wav("deps/samples/drums-808/tom-hi.wav", b, n); free(b);
    }
    printf("  808 kit: kick, snare, hihat, ohat, clap, tom-lo, tom-hi\n");

    printf("Done.\n");
    return 0;
}
