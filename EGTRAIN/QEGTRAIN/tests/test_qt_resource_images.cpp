#include <QGuiApplication>
#include <QImage>
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
    const std::array<const char*, 2> legacy_paths{
        ":/icons/station.png",
        ":/icons/passenger.png",
    };
    for (const char* path : legacy_paths) {
        if (QImage(path).isNull())
            return 1;
    }
    return 0;
}
