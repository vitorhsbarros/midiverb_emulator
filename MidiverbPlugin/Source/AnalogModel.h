#pragma once

// Models of the analog filters around the converters, derived from the
// MIDIVerb / MIDIFex schematic (github.com/emeb/MIDIVerb_RE, schematics/ and
// spice/). Each filter is built from its analog poles and mapped to the
// digital domain with the bilinear transform, prewarped at the pole frequency.

#include <array>
#include <cmath>

namespace analog
{
    constexpr double pi = 3.14159265358979323846;

    struct Section
    {
        enum Kind { LowPass1, LowPass2, HighPass1, LowPassNotch } kind;
        double f0;       // Hz
        double q = 0.5;  // LowPass2 / LowPassNotch
        double fz = 0.0; // LowPassNotch: frequency of the transmission zero
    };

    // Unity-gain Sallen-Key low-pass: R1, R2 in series, C1 from the middle
    // node to the output, C2 from the op-amp input to ground.
    inline Section sallenKey(double R1, double R2, double C1, double C2)
    {
        const double w0 = 1.0 / std::sqrt(R1 * R2 * C1 * C2);
        return { Section::LowPass2, w0 / (2.0 * pi), 1.0 / (w0 * C2 * (R1 + R2)) };
    }

    inline Section rcHighPass(double R, double C) { return { Section::HighPass1, 1.0 / (2.0 * pi * R * C) }; }

    struct DigitalSection
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        void reset() { z1 = z2 = 0; }

        double process(double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void design(const Section& s, double fs)
        {
            const double w0 = 2.0 * pi * s.f0;
            // Prewarp so the pole lands at the right frequency; poles above
            // ~0.45 fs (only the 120 kHz DAC pole) use the plain transform.
            const double K = (s.f0 < 0.45 * fs) ? w0 / std::tan(w0 / (2.0 * fs)) : 2.0 * fs;
            reset();
            switch (s.kind)
            {
                case Section::LowPass1:
                {
                    const double a0 = K + w0;
                    b0 = w0 / a0; b1 = w0 / a0; b2 = 0.0;
                    a1 = (w0 - K) / a0; a2 = 0.0;
                    break;
                }
                case Section::HighPass1:
                {
                    const double a0 = K + w0;
                    b0 = K / a0; b1 = -K / a0; b2 = 0.0;
                    a1 = (w0 - K) / a0; a2 = 0.0;
                    break;
                }
                case Section::LowPass2:
                {
                    const double w2 = w0 * w0, K2 = K * K, kq = K * w0 / s.q;
                    const double a0 = K2 + kq + w2;
                    b0 = w2 / a0; b1 = 2.0 * w2 / a0; b2 = w2 / a0;
                    a1 = (2.0 * w2 - 2.0 * K2) / a0;
                    a2 = (K2 - kq + w2) / a0;
                    break;
                }
                case Section::LowPassNotch:
                {
                    // (s^2 + wz^2) / wz^2 * w0^2 / (s^2 + s w0/Q + w0^2), unity gain at DC
                    const double wz = 2.0 * pi * s.fz, wz2 = wz * wz;
                    const double w2 = w0 * w0, K2 = K * K, kq = K * w0 / s.q;
                    const double a0 = K2 + kq + w2, g = w2 / wz2;
                    // Zeros prewarped at their own frequency (DC gain is unaffected)
                    const double Kz = (s.fz < 0.45 * fs) ? wz / std::tan(wz / (2.0 * fs)) : K;
                    const double Kz2 = Kz * Kz;
                    b0 = g * (Kz2 + wz2) / a0; b1 = g * 2.0 * (wz2 - Kz2) / a0; b2 = g * (Kz2 + wz2) / a0;
                    a1 = (2.0 * w2 - 2.0 * K2) / a0;
                    a2 = (K2 - kq + w2) / a0;
                    break;
                }
            }
        }
    };

    template <int N>
    struct Chain
    {
        std::array<DigitalSection, N> sections;

        void design(const std::array<Section, N>& analogSections, double fs)
        {
            for (int i = 0; i < N; ++i)
                sections[(size_t) i].design(analogSections[(size_t) i], fs);
        }

        void reset() { for (auto& s : sections) s.reset(); }

        double process(double x)
        {
            for (auto& s : sections) x = s.process(x);
            return x;
        }
    };

    // MIDIVerb / MIDIFex input (schematic page 6 + page 5):
    //   0.1 uF / 51k high-pass (31 Hz) on each input,
    //   L+R summed through two 10k resistors (a 5k source for the first stage),
    //   then three unity-gain Sallen-Key low-pass stages before the ADC.
    inline std::array<Section, 4> midiverbInput()
    {
        return {
            rcHighPass(51e3, 0.1e-6),
            sallenKey(5e3, 5.1e3, 10e-9, 3.3e-9),       // "6.5kHz, mostly flat"
            sallenKey(10e3, 10e3, 10e-9, 330e-12),      // "13kHz, 10dB peak @ 8.5k"
            sallenKey(4.7e3, 4.7e3, 47e-9, 220e-12),    // "18kHz, 18dB peak @ 10k"
        };
    }

    // MIDIVerb / MIDIFex output, per channel (schematic page 6):
    //   DAC hold cap -> buffer -> 1k / 1500 pF -> Sallen-Key 6.8k / 6.8k,
    //   3300 pF / 1500 pF. The RC loads the Sallen-Key, so the poles come from
    //   solving the whole 3rd-order ladder (matches spice/MV_DAC_Filter.cir).
    inline std::array<Section, 2> midiverbOutput()
    {
        return {
            Section { Section::LowPass2, 9786.897, 0.73578 },
            Section { Section::LowPass1, 122590.288 },
        };
    }

    // MIDIVERB II (Alesis schematic 9-50-1003, page 2). ESTIMATE, not a faithful model:
    // the drawing leaves one capacitor without a value and its first input stage
    // cannot be transcribed consistently. See the commit message that introduced it.

    // Input: the two readable unity-gain Sallen-Key stages (10k/10k, 4700 pF / 330 pF and
    // 1.5k/1.5k, 0.1 uF / 470 pF), preceded by a low-pass-notch section standing in for the
    // unreadable first stage. Its parameters were fitted so the whole filter is flat
    // (within ~1 dB) to 14 kHz with a stop-band notch just above the 15.6 kHz Nyquist,
    // as the stage's topology suggests.
    inline std::array<Section, 3> midiverb2Input()
    {
        return {
            Section { Section::LowPassNotch, 8307.6, 0.5814, 18193.2 },
            sallenKey(10e3, 10e3, 4700e-12, 330e-12),
            sallenKey(1.5e3, 1.5e3, 0.1e-6, 470e-12),
        };
    }

    // Output, per channel: DAC hold -> buffer -> 470R / 0.047 uF -> Sallen-Key 6.8k/6.8k,
    // 6800 pF / 330 pF (poles of the loaded ladder below) -> Sallen-Key 2.4k/3.3k, 6800 pF / Cx.
    // Cx has no value on the drawing; 1000 pF gives the flattest passband (estimate).
    inline std::array<Section, 3> midiverb2Output()
    {
        return {
            Section { Section::LowPass2, 15563.973, 2.12451 },
            Section { Section::LowPass1, 7260.777 },
            sallenKey(2.4e3, 3.3e3, 6800e-12, 1000e-12),
        };
    }
}

