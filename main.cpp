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
            ui.groupBox->setDisabled(false);

            start();
        });
        connect(ui.randButton, &QPushButton::clicked, this, [this]() {
            auto text = NodeId::rand().toHex();
            ui.nodeIdEdit->setText(QString::fromUtf8(text));
        });
        connect(ui.pingButton, &QPushButton::clicked, this, [this]() {
            ilias_spawn [this]() -> Task<> {
                co_await mSession.ping(IPEndpoint(ui.pingEdit->text().toStdString().c_str()));
                co_return {};
            }; 
        });
        connect(ui.bootstrapButton, &QPushButton::clicked, this, [this]() {
            ilias_spawn [this]() -> Task<> {
                co_await mSession.bootstrap(IPEndpoint(ui.bootstrapEdit->text().toStdString().c_str()));
                co_return {};
            }; 
        });
    }
    auto start() -> void {
        auto idText = ui.nodeIdEdit->text();
        if (!idText.isEmpty()) {
            mSession.setNodeId(NodeId::fromHex(idText.toStdString()));
        }
        mSession.setBindEndpoint(IPEndpoint(ui.bindEdit->text().toStdString().c_str()));
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
