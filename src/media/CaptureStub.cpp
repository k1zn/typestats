// A system without a capture backend yet: no devices, starting fails.

#include "Capture.h"

#include <QCoreApplication>
#include <QTimer>

struct Camera::Impl
{
};

Camera::Camera(QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {}
Camera::~Camera() = default;

QList<CaptureDevice> Camera::devices()
{
    return {};
}

void Camera::start(const QString &, QSize, int)
{
    QTimer::singleShot(0, this, [this] {
        emit failed(QCoreApplication::translate("Capture", "Запись камеры на этой системе пока не поддерживается"));
    });
}

void Camera::stop() {}

bool Camera::isActive() const
{
    return false;
}

struct Microphone::Impl
{
};

Microphone::Microphone(QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {}
Microphone::~Microphone() = default;

QList<CaptureDevice> Microphone::devices()
{
    return {};
}

void Microphone::start(const QString &)
{
    QTimer::singleShot(0, this, [this] {
        emit failed(QCoreApplication::translate("Capture", "Запись звука на этой системе пока не поддерживается"));
    });
}

void Microphone::stop() {}

bool Microphone::isActive() const
{
    return false;
}
