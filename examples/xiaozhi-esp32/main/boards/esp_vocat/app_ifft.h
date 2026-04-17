#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// 直接复用 CSI 示例中的 IQmathLib 与复数/FFT 定义
#include "IQmathLib.h"

typedef struct {
    float real;
    float imag;
} Complex;

typedef struct {
    _iq16 real;
    _iq16 imag;
} Complex_Iq;

void IRAM_ATTR fft_iq(Complex_Iq *X, int inverse);
void IRAM_ATTR fft(Complex *X, int N, int inverse);

float complex_magnitude_iq(Complex_Iq z);
float complex_phase_iq(Complex_Iq z);
float complex_magnitude(Complex z);
float complex_phase(Complex z);

#ifdef __cplusplus
}
#endif


