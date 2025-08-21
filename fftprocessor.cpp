#include "fftprocessor.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>

// Mathematical constants
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

FFTProcessor::FFTProcessor(int size)
    : fft_in(nullptr), fft_out(nullptr), plan(nullptr), fft_size(size), initialized(false)
{
    if (size <= 0 || (size & (size - 1)) != 0) {
        throw std::invalid_argument("А размерчик не степень двушки, лучше пива");
    }

    initialized = initializeFFTW();
    if (initialized) {
        setWindowFunction("HANN"); // По дэфолту ханн
    }
}

FFTProcessor::~FFTProcessor()
{
    if (plan) {
        fftw_destroy_plan(plan);
    }
    if (fft_in) {
        fftw_free(fft_in);
    }
    if (fft_out) {
        fftw_free(fft_out);
    }
}

bool FFTProcessor::initializeFFTW()
{
    try {
        // Allocate input and output buffers
        fft_in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_size);
        fft_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_size);

        if (!fft_in || !fft_out) {
            return false;
        }

        // Create FFTW plan
        plan = fftw_plan_dft_1d(fft_size, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);

        if (!plan) {
            return false;
        }

        return true;
    }
    catch (...) {
        return false;
    }
}

std::vector<double> FFTProcessor::processIQData(const std::vector<uint8_t>& iq_buffer)
{
    return processIQData(iq_buffer, true); // Ну окна и чо
}

std::vector<double> FFTProcessor::processIQData(const std::vector<uint8_t>& iq_buffer, bool apply_window)
{
    if (!initialized) {
        throw std::runtime_error("FFTProcessor not properly initialized");
    }

    if (iq_buffer.size() != static_cast<size_t>(fft_size * 2)) {
        throw std::invalid_argument("IQ buffer size must be exactly 2 * FFT size");
    }

    // Поток с ртлки в чоткие пацанские сэмплы
    std::vector<double> i_samples(fft_size);
    std::vector<double> q_samples(fft_size);
    convertRTLSDRSamples(iq_buffer, i_samples, q_samples);

    // Ну окно и что
    if (apply_window && !window_coefficients.empty()) {
        applyWindow(i_samples, q_samples);
    }

    // Fill FFT input buffer
    for (int i = 0; i < fft_size; i++) {
        fft_in[i][0] = i_samples[i]; // Real part
        fft_in[i][1] = q_samples[i]; // Imaginary part
    }

    // Execute FFT
    fftw_execute(plan);

    // Из абсолюта в жалкие децибелы
    std::vector<double> spectrum(fft_size);
    for (int i = 0; i < fft_size; i++) {
        double magnitude = calculateMagnitude(fft_out[i][0], fft_out[i][1]);
        spectrum[i] = linearToDb(magnitude);
    }

    return spectrum;
}

std::vector<double> FFTProcessor::processComplexData(const std::vector<double>& i_samples,
                                                   const std::vector<double>& q_samples)
{
    if (!initialized) {
        throw std::runtime_error("FFTProcessor not properly initialized");
    }

    if (i_samples.size() != static_cast<size_t>(fft_size) ||
        q_samples.size() != static_cast<size_t>(fft_size)) {
        throw std::invalid_argument("Sample vectors must be exactly FFT size");
    }

    // Копируем в местные переменки
    std::vector<double> i_windowed = i_samples;
    std::vector<double> q_windowed = q_samples;

    // Окно епт
    if (!window_coefficients.empty()) {
        applyWindow(i_windowed, q_windowed);
    }

    // Загоняем дату в буфер ффт
    for (int i = 0; i < fft_size; i++) {
        fft_in[i][0] = i_windowed[i];
        fft_in[i][1] = q_windowed[i];
    }

    // Execute FFT
    fftw_execute(plan);

    // Convert to magnitude spectrum in dB
    std::vector<double> spectrum(fft_size);
    for (int i = 0; i < fft_size; i++) {
        double magnitude = calculateMagnitude(fft_out[i][0], fft_out[i][1]);
        spectrum[i] = linearToDb(magnitude);
    }

    return spectrum;
}

std::vector<double> FFTProcessor::getPowerSpectrum(const std::vector<uint8_t>& iq_buffer)
{
    if (!initialized) {
        throw std::runtime_error("ФФтшка не запустилася");
    }

    if (iq_buffer.size() != static_cast<size_t>(fft_size * 2)) {
        throw std::invalid_argument("А у нас ошибка буфер IQ должен быть 2*fftsize");
    }

    // Convert RTL-SDR samples to normalized complex values
    std::vector<double> i_samples(fft_size);
    std::vector<double> q_samples(fft_size);
    convertRTLSDRSamples(iq_buffer, i_samples, q_samples);

    // Apply window function
    if (!window_coefficients.empty()) {
        applyWindow(i_samples, q_samples);
    }

    // Fill FFT input buffer
    for (int i = 0; i < fft_size; i++) {
        fft_in[i][0] = i_samples[i];
        fft_in[i][1] = q_samples[i];
    }

    // Execute FFT
    fftw_execute(plan);

    // Convert to power spectrum (linear scale)
    std::vector<double> power_spectrum(fft_size);
    for (int i = 0; i < fft_size; i++) {
        double re = fft_out[i][0];
        double im = fft_out[i][1];
        power_spectrum[i] = re * re + im * im; // Power = |H|^2
    }

    return power_spectrum;
}

void FFTProcessor::setWindowFunction(const std::string& window_type)
{
    generateWindowCoefficients(window_type);
}

void FFTProcessor::generateWindowCoefficients(const std::string& window_type)
{
    window_coefficients.resize(fft_size);

    if (window_type == "RECTANGULAR" || window_type == "RECT") {
        // Rectangular window (no windowing)
        std::fill(window_coefficients.begin(), window_coefficients.end(), 1.0);
    }
    else if (window_type == "HANN" || window_type == "HANNING") {
        // Hann window
        for (int i = 0; i < fft_size; i++) {
            window_coefficients[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (fft_size - 1)));
        }
    }
    else if (window_type == "HAMMING") {
        // Hamming window
        for (int i = 0; i < fft_size; i++) {
            window_coefficients[i] = 0.54 - 0.46 * cos(2.0 * M_PI * i / (fft_size - 1));
        }
    }
    else if (window_type == "BLACKMAN") {
        // Blackman window
        for (int i = 0; i < fft_size; i++) {
            double n = static_cast<double>(i);
            double N = static_cast<double>(fft_size - 1);
            window_coefficients[i] = 0.42 - 0.5 * cos(2.0 * M_PI * n / N) +
                                   0.08 * cos(4.0 * M_PI * n / N);
        }
    }
    else if (window_type == "KAISER") {
        // Kaiser window (beta = 8.6)
        double beta = 8.6;
        double alpha = (fft_size - 1) / 2.0;

        // Calculate I0(beta) for normalization
        auto i0 = [](double x) -> double {
            double sum = 1.0;
            double term = 1.0;
            for (int k = 1; k < 50; k++) {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < 1e-12) break;
            }
            return sum;
        };

        double i0_beta = i0(beta);

        for (int i = 0; i < fft_size; i++) {
            double n = static_cast<double>(i);
            double arg = beta * sqrt(1.0 - pow((n - alpha) / alpha, 2.0));
            window_coefficients[i] = i0(arg) / i0_beta;
        }
    }
    else {
        // Default to Hann window
        for (int i = 0; i < fft_size; i++) {
            window_coefficients[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (fft_size - 1)));
        }
    }
}

void FFTProcessor::applyWindow(std::vector<double>& i_samples, std::vector<double>& q_samples)
{
    if (window_coefficients.size() != static_cast<size_t>(fft_size)) {
        return; // No windowing if coefficients not properly set
    }

    for (int i = 0; i < fft_size; i++) {
        i_samples[i] *= window_coefficients[i];
        q_samples[i] *= window_coefficients[i];
    }
}

double FFTProcessor::binToFrequency(int bin_index, double sample_rate) const
{
    // Handle negative frequencies (FFT bin wrapping)
    if (bin_index > fft_size / 2) {
        bin_index -= fft_size;
    }

    return (static_cast<double>(bin_index) * sample_rate) / fft_size;
}

double FFTProcessor::getFrequencyResolution(double sample_rate) const
{
    return sample_rate / fft_size;
}

// Static utility functions

void FFTProcessor::convertRTLSDRSamples(const std::vector<uint8_t>& iq_buffer,
                                      std::vector<double>& i_out,
                                      std::vector<double>& q_out)
{
    size_t num_samples = iq_buffer.size() / 2;
    i_out.resize(num_samples);
    q_out.resize(num_samples);

    for (size_t i = 0; i < num_samples; i++) {
        // Convert from unsigned 8-bit to signed normalized float
        // RTL-SDR samples are 0-255, centered at 127.5
        i_out[i] = (static_cast<double>(iq_buffer[2*i]) - 127.5) / 127.5;
        q_out[i] = (static_cast<double>(iq_buffer[2*i + 1]) - 127.5) / 127.5;
    }
}

double FFTProcessor::calculateMagnitude(double real, double imag)
{
    return sqrt(real * real + imag * imag);
}

double FFTProcessor::linearToDb(double linear_power)
{
    // Add small epsilon to avoid log(0)
    const double epsilon = 1e-12;
    return 20.0 * log10(linear_power + epsilon);
}
