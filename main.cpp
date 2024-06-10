#include <iostream>
#include <format>
#include <ilias_qt.hpp>
#include "src/session.hpp"
#include <QApplication>
#include "ui_main.h"

#pragma comment(linker, "/SUBSYSTEM:console")

class App final : public QMainWindow {
public:
    App() {
        ui.setupUi(this);

        connect(ui.startButton, &QPushButton::clicked, this, [this]() {
            ui.bindEdit->setDisabled(true);
            ui.nodeIdEdit->setDisabled(true);
            ui.startButton->setDisabled(true);
            ui.randButton->setDisabled(true);

            start();
        });
        connect(ui.randButton, &QPushButton::clicked, this, [this]() {
            auto text = NodeId::rand().toHex();
            ui.nodeIdEdit->setText(QString::fromUtf8(text));
        });
    }
    auto start() -> void {
        mSession.start();   
    }
private:
    QIoContext mIo;
    DhtSession mSession {mIo};
    Ui::MainWindow ui;
};

int main(int argc, char **argv) {
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");

    QApplication a(argc, argv);
    App w;
    w.show();
    return a.exec();
}
