#include <iostream>
#include <format>
#include <ilias/platform/qt.hpp>
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
            // ui.nodeIdEdit->setDisabled(true);
            ui.startButton->setDisabled(true);
            // ui.randButton->setDisabled(true);
            ui.groupBox->setDisabled(false);

            start();
        });
        connect(ui.randButton, &QPushButton::clicked, this, [this]() {
            auto text = NodeId::rand().toHex();
            ui.nodeIdEdit->setText(QString::fromUtf8(text));
        });
        connect(ui.pingButton, &QPushButton::clicked, this, [this]() {
            // ilias_spawn [this]() -> Task<> {
            //     co_await mSession.ping(IPEndpoint(ui.pingEdit->text().toStdString().c_str()));
            //     co_return {};
            // }; 
        });
        connect(ui.bootstrapButton, &QPushButton::clicked, this, [this]() {
            // ilias_spawn [this]() -> Task<> {
            //     co_await mSession.bootstrap(IPEndpoint(ui.bootstrapEdit->text().toStdString().c_str()));
            //     co_return {};
            // }; 
        });
        connect(ui.showBucketsButton, &QPushButton::clicked, this, [this]() {
            // auto tree = ui.treeWidget;
            // tree->clear();
            // auto &table = mSession.routingTable();
            // for (size_t i = 0; i < table.buckets.size(); i++) {
            //     auto &bucket = table.buckets[i];
            //     auto item = new QTreeWidgetItem(tree);
            //     item->setText(0, QString::number(i));
            //     for (auto &node : bucket.nodes) {
            //         auto subItem = new QTreeWidgetItem(item);
            //         subItem->setText(0, QString::fromStdString(node->id.toHex()));
            //         subItem->setText(1, QString::fromStdString(node->endpoint.toString()));
            //     }
            // }
        });
        connect(ui.findNodeButton, &QPushButton::clicked, this, [this]() {
            // ilias_spawn [this, id = NodeId::fromHex(ui.findNodeEdit->text().toStdString().c_str()) ]() -> Task<> {
            //     co_await mSession.findNode(id);
            //     co_return {};
            // }; 
        });
    }
    auto start() -> void {
        // auto idText = ui.nodeIdEdit->text();
        // if (!idText.isEmpty()) {
        //     mSession.setNodeId(NodeId::fromHex(idText.toStdString()));
        // }
        // mSession.setBindEndpoint(IPEndpoint(ui.bindEdit->text().toStdString().c_str()));
        // mSession.start();   
    }
private:
    QIoContext mIo;
    // DhtSession mSession {mIo};
    Ui::MainWindow ui;
};

int main(int argc, char **argv) {
#ifdef _WIN32
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif

    QApplication a(argc, argv);
    App w;
    w.show();
    return a.exec();
}
