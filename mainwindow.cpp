#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "QString"
#include "QDebug"
#include <complex>
#include <include/fftw3.h>


#include <QtConcurrent>
#include <QFile>
#include <QFuture>
#include <algorithm>  // for std::sort
#include <QThread>    // for QThread::msleep instead of std::this_thread
#include <cmath>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChart>


const double sampleRates[] = {
    250000,
    1024000,
    1536000,
    1792000,
    1920000,
    2048000,
    2160000,
    2400000,
    2560000,
    2880000,
    3200000
};

QVector<int> gainslist;


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->button_go->setEnabled(false);
    for (int var=128;var<=30000;var*=2)
    {
        ui->comboBox_windowSizes->addItem(QString::number(var));
    }
    ui->comboBox_windowSizes->setCurrentIndex(4);
    for (int i=1;i<128;i*=2)
    {
        ui->comboBox_meanWind->addItem(QString::number(i));
    }
    ui->comboBox_meanWind->setCurrentIndex(3);

    ui->comboBox_sampleRates->clear();
    for(int i=0;i<11;i++)  // Fix: change from 10 to 11
    {
        ui->comboBox_sampleRates->addItem(QString::number(sampleRates[i]));
    }

    chart = new QtCharts::QChart;
    series = new QtCharts::QLineSeries;
    chart->addSeries(series);
    chart->createDefaultAxes();
    chart->setTitle("Спектр");
    chartView = new QtCharts::QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    ui->verticalLayout->addWidget(chartView, 0, 0);
    chartView->setMinimumSize(1,600);
    ui->button_rescan->click();
}

void MainWindow::on_button_rescan_clicked()
{
    ui->comboBox_devices->clear();
    if (rtlsdr_get_device_count()==0)
    {
        ui->textEdit_log->append("Устройства не обнаружены");
        return;
    }
    for (uint32_t i=0;i<rtlsdr_get_device_count();i++)
    {
        QString str = QString(QString::number(i)+" "+rtlsdr_get_device_name(i)); // Fix: use (i) not (0)
        ui->comboBox_devices->addItem(str);

        // Fix USB strings - allocate proper buffer size
        char vendor[256], product[256], serial[256];
        if (rtlsdr_get_device_usb_strings(i, vendor, product, serial) == 0) {
            QString to_log="Обнаружено устройство "+QString(vendor)+" "+QString(product)+" "+QString(serial);
            ui->textEdit_log->append(QString(to_log));
        }
    }
}

// 3. Fix the loop range in constructor (you have 11 sample rates, not 10)


MainWindow::~MainWindow()
{
    if (dev) {
        rtlsdr_close(dev);  // Close device if still open
    }
    delete ui;
    delete series;
    delete chart;
    delete chartView;
    std::system("Taskkill /IM scanner.exe /F");

}

uint32_t MainWindow::sampletoMHz()
{
    switch (ui->comboBox_sampleRates->currentIndex()){
    case 0:
        return 250000;
    case 1:
        return 1024000;
    case 2:
        return 2048000;
    default:
        ui->textEdit_log->append("Ошибка в samplerate");
        return 0;
    }
}



void MainWindow::on_comboBox_devices_currentIndexChanged(int index)
{
    ui->comboBox_gains->clear();

    if (rtlsdr_open(&dev, index) < 0)
    {
        ui->textEdit_log->append("Ошибка открытия устройства " +
                                 QString(rtlsdr_get_device_name(index)) +
                                 " с индексом " + QString::number(index));
        return;
    }
    else
    {
        int count = rtlsdr_get_tuner_gains(dev, NULL); // получаем количество значений

        if (count <= 0) {
            ui->textEdit_log->append("Нет доступных значений усиления");
            rtlsdr_close(dev);
            return;
        }
        gainslist.clear();
        std::vector<int> gains(count); // динамический массив нужного размера

        count = rtlsdr_get_tuner_gains(dev, gains.data());

        ui->textEdit_log->append("Количество значений: " + QString::number(count));

        for (int i = 0; i < count; i++) {
            ui->comboBox_gains->addItem(QString::number(float(gains[i])/10));
            gainslist.append(gains[i]);
        }
        ui->button_go->setEnabled(true);
        rtlsdr_close(dev);
    }
}


void MainWindow::on_button_go_clicked()
{

    ui->button_go->setEnabled(false);
    //QFuture<void> future = QtConcurrent::run(this, &MainWindow::worker);
    if (ui->checkBox_looping->isChecked())
    {while (ui->checkBox_looping->isChecked()) {
            worker();
        }
    }
    else
    {
        worker();
    }
    ui->button_go->setEnabled(true);
}

void MainWindow::worker()
{
    // Получение параметров из UI
    uint32_t samplerate = sampleRates[ui->comboBox_sampleRates->currentIndex()];
    uint32_t freq_start = ui->lineEdit_from->text().toULong();
    freq_start *= 1000000;
    uint32_t freq_end = ui->lineEdit_to->text().toULong();
    freq_end *= 1000000;
    uint8_t deviceindex = ui->comboBox_devices->currentIndex();
    uint16_t gain = uint16_t(ui->comboBox_gains->itemText(ui->comboBox_gains->currentIndex()).toFloat() * 10);
    int fftsize = ui->comboBox_windowSizes->currentText().toInt();
    int num_windows = ui->comboBox_meanWind->currentText().toInt();
    bool graphics = ui->checkBox_chart->isChecked();
    bool workerEnbl = ui->checkBox_worker->isChecked();
    // Парамы
    double signal_threshold_db = 7.0;  // Порог
    double min_signal_bandwidth = samplerate * 0.005; // От 250 до 3200
    int consecutive_bins_required = 4; // Скольк бинов нужн
    chart->axisX()->setRange(ui->lineEdit_from->text().toInt(), ui->lineEdit_to->text().toInt());
    chart->axisY()->setRange(-50, 50);
    ui->textEdit_log->append(QString("Начало сканирования: %1 MHz - %2 MHz").arg(freq_start/1000000.0).arg(freq_end/1000000.0));

    // Open RTL-SDR device
    if (rtlsdr_open(&dev, deviceindex) < 0) {
        ui->textEdit_log->append("Ошибка открытия устройства");
        return;
    }

    // Настройка устройства
    rtlsdr_set_tuner_gain_mode(dev, 1);
    rtlsdr_set_tuner_gain(dev, gain);
    rtlsdr_set_sample_rate(dev, samplerate);
    rtlsdr_set_agc_mode(dev, 0);
    rtlsdr_reset_buffer(dev);

    ui->textEdit_log->append(QString("Устройство настроено: SR=%1 kHz, Gain=%2 dB").arg(samplerate/1000).arg(gain/10.0));

    // Центры с шагом в 0.6, Уже с шагом в 1
    uint32_t freq_step = uint32_t(samplerate * 1);
    std::vector<uint32_t> central_freqs;

    for (uint32_t freq = freq_start; freq <= freq_end; freq += freq_step) {
        central_freqs.push_back(freq);
    }

    ui->textEdit_log->append(QString("Количество центральных частот: %1").arg(central_freqs.size()));

    double freq_resolution = double(samplerate) / fftsize;

    // Clear the chart at the beginning
    series->clear();

    // Initialize FFT processor
    FFTProcessor fft_processor(fftsize);
    fft_processor.setWindowFunction("RECT"); // Ну или BLACKMAN, HAMMING, HANN в общем чт сам хочу

    if (!fft_processor.isInitialized()) {
        ui->textEdit_log->append("Ошибка инициализации FFT процессора");
        rtlsdr_close(dev);
        return;
    }

    std::vector<uint8_t> iq_buffer(fftsize * 2);

    ui->textEdit_log->append(QString("FFT процессор инициализирован: размер=%1, окно=Hann").arg(fftsize));

    // Обработка центров
    for (size_t freq_idx = 0; freq_idx < central_freqs.size(); freq_idx++) {
        uint32_t center_freq = central_freqs[freq_idx];

        rtlsdr_set_center_freq(dev, center_freq);

        // Ожидание в 50мс
        QThread::msleep(50);
        rtlsdr_reset_buffer(dev);

        ui->textEdit_log->append(QString("Обработка частоты %1 MHz (%2/%3)")
                               .arg(center_freq/1000000.0).arg(freq_idx+1).arg(central_freqs.size()));

        // Ну считаем несколько спектров
        std::vector<double> averaged_spectrum = processFrequencyWindow(
            center_freq, num_windows, fftsize, iq_buffer, fft_processor
        );

        if (averaged_spectrum.empty()) {
            ui->textEdit_log->append(QString("Ошибка обработки частоты %1 MHz").arg(center_freq/1000000.0));
            continue;
        }

        // Выгоняем на спектры
        std::vector<std::pair<double, double>> freq_power_pairs = extractSpectrumData(
            averaged_spectrum, center_freq, freq_start, freq_end, fftsize, freq_resolution
        );

        // Реалтайм
        if (graphics){
        for (const auto& point : freq_power_pairs) {
            series->append(point.first / 1000000.0, point.second);
        }}

        // Функция говорит сама за себя
        if(workerEnbl){detectSignals(averaged_spectrum, center_freq, freq_resolution, signal_threshold_db, consecutive_bins_required, min_signal_bandwidth);}

        // Выкуриваем график
        QApplication::processEvents();
    }

    // Финальный штрих
    chart->axisX()->setTitleText("Частота (MHz)");
    chart->axisY()->setTitleText("Мощность (dB)");

    // Ну ранжируем
    if (series->count() > 0) {
        auto points = series->points();
        if (!points.empty()) {
            double min_freq = points.front().x();
            double max_freq = points.back().x();
            chart->axisX()->setRange(min_freq, max_freq);

            // Find min/max power
            double min_power = points[0].y();
            double max_power = points[0].y();
            for (const auto& point : points) {
                if (point.y() < min_power) min_power = point.y();
                if (point.y() > max_power) max_power = point.y();
            }
            chart->axisY()->setRange(min_power - 5, max_power + 5);
        }
    }

    rtlsdr_close(dev);
    ui->textEdit_log->append("Сканирование завершено успешно");
}



std::vector<double> MainWindow::processFrequencyWindow(
    uint32_t center_freq, int num_windows, int fftsize,
    std::vector<uint8_t>& iq_buffer, FFTProcessor& fft_processor)
{
    std::vector<std::vector<double>> window_spectrums(num_windows);

    // Читаем
    for (int window = 0; window < num_windows; window++) {
        int bytes_read = 0;
        int result = rtlsdr_read_sync(dev, iq_buffer.data(), fftsize * 2, &bytes_read);

        if (result < 0 || bytes_read != fftsize * 2) {
            ui->textEdit_log->append(QString("Ошибка чтения данных на частоте %1 MHz, окно %2")
                                   .arg(center_freq/1000000.0).arg(window+1));
            return std::vector<double>(); // Вернуть шляпу если рил шляпа
        }

        try {
            // Обкашливаем FFTшку
            window_spectrums[window] = fft_processor.processIQData(iq_buffer, true); // Apply windowing
        }
        catch (const std::exception& e) {
            ui->textEdit_log->append(QString("Ошибка FFT обработки: %1").arg(e.what()));
            return std::vector<double>();
        }
    }

    // Усредняем спектры
    std::vector<double> mean_spectrum(fftsize, 0.0);
    for (int i = 0; i < fftsize; i++) {
        double sum = 0.0;
        for (int window = 0; window < num_windows; window++) {
            sum += window_spectrums[window][i];
        }
        mean_spectrum[i] = sum / num_windows;
    }

    return mean_spectrum;
}


std::vector<std::pair<double, double>> MainWindow::extractSpectrumData(
    const std::vector<double>& spectrum, uint32_t center_freq,
    uint32_t freq_start, uint32_t freq_end, int fftsize, double freq_resolution)
{
    std::vector<std::pair<double, double>> freq_power_pairs;

    //
    int spectrum_bins = fftsize;
    int start_idx = (fftsize - spectrum_bins) / 2;
    int end_idx = start_idx + spectrum_bins;

    for (int i = start_idx; i < end_idx; i++) {
        int bin = i;
        if (bin > fftsize/2) {
            bin = bin - fftsize;
        }

        double freq = center_freq + bin * freq_resolution;

        if (freq >= freq_start && freq <= freq_end) {
            freq_power_pairs.push_back(std::make_pair(freq, spectrum[i]));
        }
    }

    // Ну сортонуть
    std::sort(freq_power_pairs.begin(), freq_power_pairs.end());

    return freq_power_pairs;
}



void MainWindow::detectSignals(
    const std::vector<double>& spectrum, uint32_t center_freq, double freq_resolution,
    double threshold_db, int min_consecutive_bins, double min_bandwidth)
{
    if (spectrum.empty()) return;

    // Спасибо Атоян Галина Ашотовна за знания чт такое перцентиль
    std::vector<double> sorted_spectrum = spectrum;
    std::sort(sorted_spectrum.begin(), sorted_spectrum.end());
    double noise_floor = sorted_spectrum[sorted_spectrum.size() * 0.25]; // 25th percentile

    // Ну выше порога и выше, чт с этого
    std::vector<bool> above_threshold(spectrum.size(), false);
    for (size_t i = 0; i < spectrum.size(); i++) {
        double snr = spectrum[i] - noise_floor;
        above_threshold[i] = (snr > threshold_db);
    }

    //
    std::vector<std::pair<int, int>> signal_regions;
    int start_idx = -1;

    for (size_t i = 0; i < above_threshold.size(); i++) {
        if (above_threshold[i] && start_idx == -1) {
            start_idx = i; // Начало потенциального излучения
        } else if (!above_threshold[i] && start_idx != -1) {
            // Конец потенциального излучения
            int length = i - start_idx;
            if (length >= min_consecutive_bins) {
                signal_regions.push_back(std::make_pair(start_idx, i - 1));
            }
            start_idx = -1;
        }
    }

    // А чо если спектр выходит за границы приема
    if (start_idx != -1) {
        int length = above_threshold.size() - start_idx;
        if (length >= min_consecutive_bins) {
            signal_regions.push_back(std::make_pair(start_idx, above_threshold.size() - 1));
        }
    }

    // АналИз и ВыВод
    for (const auto& region : signal_regions) {
        int start_bin = region.first;
        int end_bin = region.second;

        // Convert to chastoti
        int center_bin = start_bin;
        if (center_bin > int(spectrum.size())/2) {
            center_bin = center_bin - spectrum.size();
        }
        double start_freq = center_freq + start_bin * freq_resolution;

        center_bin = end_bin;
        if (center_bin > int(spectrum.size())/2) {
            center_bin = center_bin - spectrum.size();
        }
        double end_freq = center_freq + end_bin * freq_resolution;

        double signal_bandwidth = abs(end_freq - start_freq);

        // Check minimum bandwidth requirement
        if (signal_bandwidth < min_bandwidth) continue;

        // Find peak within the signal region
        double max_power = spectrum[start_bin];
        int peak_bin = start_bin;
        for (int i = start_bin; i <= end_bin; i++) {
            if (spectrum[i] > max_power) {
                max_power = spectrum[i];
                peak_bin = i;
            }
        }

        // Ну пик нашли
        int freq_bin = peak_bin;
        if (freq_bin > int(spectrum.size())/2) {
            freq_bin = freq_bin - spectrum.size();
        }
        double peak_freq = center_freq + freq_bin * freq_resolution;

        double snr = max_power - noise_floor;
        QString zalipuha ="";
        if(peak_freq>400000000 && peak_freq <430000000){zalipuha = "Обнаружено излучение стандарта DMR";}
        if(peak_freq>440000000 && peak_freq <460000000){zalipuha = "Обнаружено излучение стандарта Tetra";}


        // ЛогиЛогиЛоги
        if(zalipuha !=""){ui->textEdit_log->append(zalipuha);}
        ui->textEdit_log->append(QString("Излучение ОБНАРУЖЕНО: Частота=%1 MHz, SNR=%2 dB, Полоса=%3 kHz")
                               .arg(peak_freq/1000000.0, 0, 'f', 6)
                               .arg(snr, 0, 'f', 1)
                               .arg(signal_bandwidth/1000.0, 0, 'f', 1));
    }
}

void MainWindow::on_lineEdit_from_editingFinished()
{
    ui->lineEdit_to->setText(ui->lineEdit_from->text());
}


