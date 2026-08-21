#include "WeatherService.h"
#include "SettingsService.h"   // 用于获取温度单位
#include "CityService.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>

// 高德 API 配置（可直接使用宏，但为统一风格，改为静态常量）
static const QString WEATHER_API_BASE = "http://restapi.amap.com/v3/weather/weatherInfo";
static const QString WEATHER_EXTENSIONS = "base";

// 默认 API Key（建议通过环境变量或配置文件覆盖）
#ifndef WEATHER_API_KEY
#define WEATHER_API_KEY "c627e5fd4c5677128f6a6e4bc95f2612"
#endif

WeatherService::WeatherService(SettingsService* settingsService, QObject* parent)
    : QObject(parent), settingsService_(settingsService)
{
    // 连接网络管理器的 finished 信号到我们的槽
    connect(&networkManager_, &QNetworkAccessManager::finished,
            this, &WeatherService::onReplyFinished);
}

void WeatherService::fetchRealTime(const QString& cityAdcode, const QString& apiKey)
{
    // 确定使用的 API Key
    QString key = apiKey;
    if (key.isEmpty()) {
        // 尝试从环境变量读取
        QByteArray envKey = qgetenv("WEATHER_API_KEY");
        if (!envKey.isEmpty()) {
            key = QString::fromUtf8(envKey);
        } else {
            key = QStringLiteral(WEATHER_API_KEY);  // 回退到默认 Key
        }
    }

    if (key.isEmpty()) {
        emit errorOccurred("API Key is empty");
        return;
    }

    // 构造请求 URL
    QUrl url(WEATHER_API_BASE);
    QUrlQuery query;
    query.addQueryItem("city", cityAdcode);
    query.addQueryItem("key", key);
    query.addQueryItem("extensions", WEATHER_EXTENSIONS);
    url.setQuery(query);

    // 发送 GET 请求
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "LongPet/1.0");
    networkManager_.get(request);
}

void WeatherService::onReplyFinished(QNetworkReply* reply)
{
    if (!reply)
        return;

    // 检查网络错误
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred("Network error: " + reply->errorString());
        reply->deleteLater();
        return;
    }

    // 读取响应数据
    QByteArray data = reply->readAll();
    reply->deleteLater();

    // 解析 JSON
    RealTimeWeather weather;
    QString errorMsg;
    if (parseWeatherData(data, weather, errorMsg)) {
        emit weatherReady(weather);
    } else {
        emit errorOccurred(errorMsg);
    }
}

bool WeatherService::parseWeatherData(const QByteArray& jsonData, RealTimeWeather& weather, QString& errorMsg)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        errorMsg = "JSON parse error: " + parseError.errorString();
        return false;
    }

    QJsonObject root = doc.object();

    // 检查状态码
    QString status = root.value("status").toString();
    if (status != "1") {
        QString info = root.value("info").toString("Unknown error");
        errorMsg = "API returned error: " + info;
        return false;
    }

    // 获取 lives 数组（实时天气数据）
    QJsonArray lives = root.value("lives").toArray();
    if (lives.isEmpty()) {
        errorMsg = "No weather data found";
        return false;
    }

    // 取第一个（通常只有一个）
    QJsonObject live = lives[0].toObject();

    // 填充结构体
    weather.province = live.value("province").toString();
    weather.city = live.value("city").toString();
    weather.weather = live.value("weather").toString();
    weather.temperatureCelsius = live.value("temperature").toString().toDouble();
    weather.winddirection = live.value("winddirection").toString();
    weather.windpower = live.value("windpower").toString();
    weather.humidity = live.value("humidity").toString();
    weather.reporttime = live.value("reporttime").toString();

    return true;
}