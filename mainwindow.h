#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <rtl-sdr.h>
#include <QMainWindow>
#include <include/convenience.h>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChartView>
#include <QGraphicsView>
#include <fftprocessor.h>

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>


struct FrequencyData {
    uint32_t center_freq;
    std::vector<uint8_t> iq_data;
    int fftsize;
    int num_windows;
    size_t freq_idx;
    size_t total_freqs;
};


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    rtlsdr_dev_t *dev;
    uint32_t sampletoMHz();

private slots:
    void on_button_rescan_clicked();

    void on_comboBox_devices_currentIndexChanged(int index);

    void on_button_go_clicked();


    void on_lineEdit_from_editingFinished();
    void worker();
    std::vector<double> processFrequencyWindow(uint32_t center_freq, int num_windows,
                                             int fftsize, std::vector<uint8_t>& iq_buffer,
                                             FFTProcessor& fft_processor);

    std::vector<std::pair<double, double>> extractSpectrumData(const std::vector<double>& spectrum,
                                                             uint32_t center_freq, uint32_t freq_start,
                                                             uint32_t freq_end, int fftsize, double freq_resolution);

    void detectSignals(const std::vector<double>& spectrum, uint32_t center_freq,
                      double freq_resolution, double threshold_db, int min_consecutive_bins,
                      double min_bandwidth);
private:
    Ui::MainWindow *ui;
    QtCharts::QChart *chart;
    QtCharts::QLineSeries *series;
    QtCharts::QChartView *chartView;
    QVector<int> gainslist;

    std::queue<FrequencyData> data_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::atomic<bool> processing_finished{false};
    std::atomic<bool> stop_processing{false};

    // Results storage
    std::mutex results_mutex;
    std::map<uint32_t, std::vector<double>> processed_spectrums;

};
#endif // MAINWINDOW_H
