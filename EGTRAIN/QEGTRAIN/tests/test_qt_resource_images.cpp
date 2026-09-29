#include <QGuiApplication>
#include <QImage>
#include <QIcon>
#include <QSize>

#include <array>
#include <utility>

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    const std::array<std::pair<const char*, int>, 8> icons{{
        {":/app/egtrain-16.png", 16},
        {":/app/egtrain-32.png", 32},
        {":/app/egtrain-48.png", 48},
        {":/app/egtrain-64.png", 64},
        {":/app/egtrain-128.png", 128},
        {":/app/egtrain-256.png", 256},
        {":/app/egtrain-512.png", 512},
        {":/app/egtrain-1024.png", 1024},
    }};
    for (const auto& [path, size] : icons) {
        const QImage image(path);
        if (image.isNull() || image.size() != QSize(size, size) || !image.hasAlphaChannel())
            return 1;
    }
    const std::array<const char*, 12> entity_icons{
        ":/icons/station.svg",
        ":/icons/station-dark.svg",
        ":/icons/passenger.svg",
        ":/icons/train-passenger.svg",
        ":/icons/train-sprinter.svg",
        ":/icons/train-intercity.svg",
        ":/icons/train-high-speed.svg",
        ":/icons/train-freight.svg",
        ":/icons/signal-neutral.svg",
        ":/icons/signal-stop.svg",
        ":/icons/signal-caution.svg",
        ":/icons/signal-proceed.svg",
    };
    for (const char* path : entity_icons) {
        const QImage image(path);
        if (image.isNull() || image.size() != QSize(24, 24) || !image.hasAlphaChannel())
            return 1;
    }
    for (const char* path : {":/icons/station.svg", ":/icons/station-dark.svg"}) {
        for (const int size : {16, 24, 32, 48, 300, 600}) {
            const QImage image = QIcon(path).pixmap(size, size).toImage();
            if (image.size() != QSize(size, size) || image.pixelColor(0, 0).alpha() != 0
                || image.pixelColor(size * 5 / 24, size * 16 / 24).alpha() == 0)
                return 1;
        }
    }
    if (QImage(":/icons/station.svg").pixelColor(5, 16).lightness()
        <= QImage(":/icons/station-dark.svg").pixelColor(5, 16).lightness())
        return 1;
    const QImage historicalPassenger(":/icons/pax_icon.png");
    if (historicalPassenger.isNull() || historicalPassenger.size() != QSize(1200, 1200)
        || !historicalPassenger.hasAlphaChannel())
        return 1;
    const std::array<const char*, 5> command_icons{
        ":/icons/run.svg",
        ":/icons/pause.svg",
        ":/icons/stop.svg",
        ":/icons/zoom-in.svg",
        ":/icons/zoom-out.svg",
    };
    for (const char* path : command_icons) {
        const QImage image(path);
        if (image.isNull() || image.size() != QSize(16, 16) || !image.hasAlphaChannel())
            return 1;
    }
    return 0;
}
