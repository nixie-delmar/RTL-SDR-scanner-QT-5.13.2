#ifndef FFTPROCESSOR_H
#define FFTPROCESSOR_H

#include <vector>
#include <cmath>
#include <fftw3.h>
#include <string>
/**
 * FFTProcessor - A class for handling FFT operations on RTL-SDR IQ data
 *
 * This class encapsulates FFTW3 functionality for processing complex IQ samples
 * from RTL-SDR devices. It handles memory management and provides convenient
 * methods for converting raw IQ data to frequency domain magnitude spectra.
 */
class FFTProcessor {
private:
    fftw_complex *fft_in;    // Input buffer for FFTW
    fftw_complex *fft_out;   // Output buffer for FFTW
    fftw_plan plan;          // FFTW execution plan
    int fft_size;            // Size of FFT
    bool initialized;        // Initialization state

    // Window functions
    std::vector<double> window_coefficients;

public:
    /**
     * Constructor
     * @param size FFT size (must be power of 2 for optimal performance)
     */
    explicit FFTProcessor(int size);

    /**
     * Destructor - cleans up FFTW resources
     */
    ~FFTProcessor();

    /**
     * Process raw IQ data and return magnitude spectrum in dB
     * @param iq_buffer Raw IQ data (interleaved I/Q samples, 8-bit unsigned)
     * @return Vector containing magnitude spectrum in dB
     */
    std::vector<double> processIQData(const std::vector<uint8_t>& iq_buffer);

    /**
     * Process raw IQ data with custom window function
     * @param iq_buffer Raw IQ data
     * @param apply_window Whether to apply windowing function
     * @return Vector containing magnitude spectrum in dB
     */
    std::vector<double> processIQData(const std::vector<uint8_t>& iq_buffer, bool apply_window);

    /**
     * Process complex IQ data (floating point)
     * @param i_samples In-phase samples
     * @param q_samples Quadrature samples
     * @return Vector containing magnitude spectrum in dB
     */
    std::vector<double> processComplexData(const std::vector<double>& i_samples,
                                         const std::vector<double>& q_samples);

    /**
     * Get power spectrum (linear scale, not dB)
     * @param iq_buffer Raw IQ data
     * @return Vector containing power spectrum (linear scale)
     */
    std::vector<double> getPowerSpectrum(const std::vector<uint8_t>& iq_buffer);

    /**
     * Set window function type
     * @param window_type Type of window (HANN, HAMMING, BLACKMAN, RECTANGULAR)
     */
    void setWindowFunction(const std::string& window_type);

    /**
     * Get FFT size
     * @return Current FFT size
     */
    int getFFTSize() const { return fft_size; }

    /**
     * Check if processor is properly initialized
     * @return true if initialized successfully
     */
    bool isInitialized() const { return initialized; }

    /**
     * Calculate frequency bin for given index
     * @param bin_index FFT bin index
     * @param sample_rate Sample rate in Hz
     * @return Frequency offset from center frequency
     */
    double binToFrequency(int bin_index, double sample_rate) const;

    /**
     * Get frequency resolution
     * @param sample_rate Sample rate in Hz
     * @return Frequency resolution in Hz
     */
    double getFrequencyResolution(double sample_rate) const;

    // Static utility functions

    /**
     * Convert RTL-SDR samples to normalized complex values
     * @param iq_buffer Raw IQ buffer
     * @param i_out Output I samples
     * @param q_out Output Q samples
     */
    static void convertRTLSDRSamples(const std::vector<uint8_t>& iq_buffer,
                                   std::vector<double>& i_out,
                                   std::vector<double>& q_out);

    /**
     * Calculate magnitude from complex values
     * @param real Real part
     * @param imag Imaginary part
     * @return Magnitude
     */
    static double calculateMagnitude(double real, double imag);

    /**
     * Convert linear power to dB
     * @param linear_power Linear power value
     * @return Power in dB
     */
    static double linearToDb(double linear_power);

private:
    /**
     * Initialize FFTW plans and buffers
     * @return true if initialization successful
     */
    bool initializeFFTW();

    /**
     * Generate window coefficients
     * @param window_type Type of window function
     */
    void generateWindowCoefficients(const std::string& window_type);

    /**
     * Apply window function to input data
     * @param i_samples In-phase samples (modified in place)
     * @param q_samples Quadrature samples (modified in place)
     */
    void applyWindow(std::vector<double>& i_samples, std::vector<double>& q_samples);
};

#endif // FFTPROCESSOR_H
