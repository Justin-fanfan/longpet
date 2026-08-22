#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

class SettingsService;

// 实时天气数据结构（使用 QString，与 Qt 风格统一）
struct RealTimeWeather {
    QString province;
    QString city;
    QString weather;
    double temperatureCelsius = 0.0;   // 高德返回的摄氏温度
    QString winddirection;
    QString windpower;
    QString humidity;
    QString reporttime;
};

class WeatherService : public QObject {
    Q_OBJECT

public:
    // 构造函数：传入 SettingsService 指针用于获取温度单位，parent 用于 Qt 父子关系
    explicit WeatherService(SettingsService* settingsService, QObject* parent = nullptr);

    // 请求实时天气（异步，结果通过信号返回）
    void fetchRealTime(const QString& cityAdcode, const QString& apiKey = QString());

signals:
    // 天气数据准备好时发射，包含完整的 RealTimeWeather 结构
    void weatherReady(const RealTimeWeather& weather);
    // 发生错误时发射（例如网络错误、API 返回错误等）
    void errorOccurred(const QString& message);

private slots:
    // 处理网络请求完成
    void onReplyFinished(QNetworkReply* reply);

private:
    // 从 JSON 解析天气数据，填充 RealTimeWeather，返回是否成功
    bool parseWeatherData(const QByteArray& jsonData, RealTimeWeather& weather, QString& errorMsg);

    QNetworkAccessManager networkManager_;
    SettingsService* settingsService_ = nullptr;
};

#endif // WEATHERSERVICE_H