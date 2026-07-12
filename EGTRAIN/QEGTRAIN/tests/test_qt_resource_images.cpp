#include <QGuiApplication>
#include <QPixmap>

#include <array>

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    const std::array<const char*, 3> paths{
        ":/icons/station.png",
        ":/icons/passenger.png",
        ":/app/window.jpg",
    };
    for (const char* path : paths) {
        if (QPixmap(path).isNull())
            return 1;
    }
    return 0;
}
