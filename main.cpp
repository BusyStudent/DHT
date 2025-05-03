#include <filesystem>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStatusBar>
#include <format>
#include <ilias/platform/qt.hpp>
#include <ilias/platform/qt_utils.hpp>
#include <iostream>
#include <optional>
#include <QTimer>

#include "src/bt.hpp"
#include "src/fetchmanager.hpp"
#include "src/metafetcher.hpp"
#include "src/session.hpp"
#include "src/torrent.hpp"
#include "src/utp.hpp"
#include "ui/torrent_card.hpp"
#include "ui_main.h"
#include "./ui/widgets/info_hash_list_widget.hpp"
#include "src/bt.hpp"
#include "src/fetchmanager.hpp"
#include "src/metafetcher.hpp"
#include "src/session.hpp"
#include "src/torrent.hpp"
#include "ui/torrent_card.hpp"
#include "ui_main.h"
#include "ui/widgets/common.hpp"

#if __cpp_lib_stacktrace
#include <stacktrace>
#endif

#pragma comment(linker, "/SUBSYSTEM:console")

class App final : public QMainWindow {
public:
    App() {
        ui.setupUi(this);

        connect(&mStatusBarClearTimer, &QTimer::timeout, this, [this]() { statusBar()->clearMessage(); });
        mStatusBarClearTimer.setSingleShot(true);

        ui.algoComboBox->addItems({"a star", "bfs-dfs"});
        ui.algoComboBox->setCurrentIndex(0);

        // Prepare fetcher
        mFetchManager.setOnFetched(
            [this](InfoHash hash, std::vector<std::byte> data) { onMetadataFetched(hash, std::move(data)); });
        // if (!QDir("./torrents").exists()) {
        //     QDir("./").mkdir("torrents");
        // }
        if (!std::filesystem::exists("./torrents")) {
            std::filesystem::create_directory("./torrents");
        }
        for (auto &entry : std::filesystem::directory_iterator("./torrents")) {
            if (auto path = entry.path(); path.extension() == ".torrent") {
                mFetchManager.markFetched(InfoHash::fromHex(path.stem().string()));
            }
        }

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

        connect(ui.pingButton, &QPushButton::clicked, this, &App::onPingButtonClicked);

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
            //         subItem->setText(1,
            //         QString::fromStdString(node->endpoint.toString()));
            //     }
            // }
        });

        connect(ui.findNodeButton, &QPushButton::clicked, this, &App::onFindNodeButtonClicked);
        connect(ui.randFindNodeButton, &QPushButton::clicked, this, &App::onRandFindNodeButtonClikced);

        connect(ui.sampleButton, &QPushButton::clicked, this, &App::onSampleButtonClicked);

        // Bt Peer Test part
        connect(ui.btConnectButton, &QPushButton::clicked, this, &App::onBtConnectButtonClicked);

        connect(ui.dumpRouteTableButton, &QPushButton::clicked, this, [this]() {
            if (!mSession) {
                return;
            }
            mSession->routingTable().dumpInfo();
        });

        connect(ui.dumpPeersButton, &QPushButton::clicked, this, [this]() {
            if (!mSession) {
                return;
            }
            for (const auto &[hash, endpoints] : mSession->peers()) {
                DHT_LOG("Hash {}", hash);
                for (const auto &endpoint : endpoints) {
                    DHT_LOG("  {}", endpoint);
                }
            }
        });

        // Try Load config
        QFile file("config.json");
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        auto json = QJsonDocument::fromJson(file.readAll()).object();
        ui.bindEdit->setText(json["ip"].toString());
        ui.nodeIdEdit->setText(json["id"].toString());
        ui.btIpEdit->setText(json["bt_ip"].toString());
        ui.btHashEdit->setText(json["bt_hash"].toString());
        ui.saveSessionBox->setChecked(json["save_session"].toBool());
    }

    auto start() -> void {
        auto idText   = ui.nodeIdEdit->text();
        auto endpoint = IPEndpoint::fromString(ui.bindEdit->text().toStdString().c_str());
        auto nodeId   = idText.isEmpty() ? NodeId::rand() : NodeId::fromHex(idText.toStdString().c_str());
        // Make session and start it
        mUdp = UdpClient(mIo, endpoint->family());
        mUdp.setOption(sockopt::ReuseAddress(true));
        mUdp.bind(*endpoint).value();
        mScope.spawn(&App::processUdp, this);

        mUtp.emplace(mUdp);
        mFetchManager.setUtpContext(*mUtp);
#if 1
        mSession.emplace(mIo, nodeId, mUdp);
        mSession->setOnAnouncePeer([this](const InfoHash &hash, const IPEndpoint &endpoint) {
            onHashFound(hash);
            mFetchManager.addHash(hash, endpoint);
        });
        mSession->routingTable().setOnNodeChanged(
            [&, this]() { setWindowTitle(QString("DhtClient Node: %1").arg(mSession->routingTable().size())); });
        if (ui.saveSessionBox->isChecked()) {
            mSession->loadFile("session.cache");
        }
        if (ui.skipBootstrapBox->isChecked()) {
            mSession->setSkipBootstrap(true);
        }
        mScope.spawn(&DhtSession::start, &*mSession);
#endif
    }

    auto processUdp() -> Task<void> {
        APP_LOG("App::processUdp start");
        std::byte  buffer[65535];
        IPEndpoint endpoint;
        while (true) {
            auto res = co_await mUdp.recvfrom(buffer, endpoint);
            if (!res) {
                if (res.error() != Error::Canceled) {
                    APP_LOG("App::processUdp recvfrom failed: {}", res.error());
                }
                break;
            }
            auto data = std::span(buffer, res.value());
            if (mUtp->processUdp(data, endpoint)) { // Valid UTP packet
                continue;
            }
            else if (mSession) {
                co_await mSession->processUdp(data, endpoint);
            }
        }
        APP_LOG("App::processUdp quit");
    }

    auto onPingButtonClicked() -> QAsyncSlot<void> {
        IPEndpoint endpoint(ui.pingEdit->text().toStdString().c_str());
        if (!endpoint.isValid()) {
            ui.statusbar->showMessage("Invalid endpoint");
            co_return;
        }
        auto res = co_await mSession->ping(endpoint);
        if (!res) {
            auto message = std::format("Ping {} failed by {}", endpoint, res.error());
            ui.statusbar->showMessage(QString::fromUtf8(message));
        }
        else {
            auto message = std::format("Ping {} success, peer id {}", endpoint, *res);
            ui.statusbar->showMessage(QString::fromUtf8(message));
        }
    }

    auto onRandFindNodeButtonClikced() -> QAsyncSlot<void> {
        auto text = NodeId::rand().toHex();
        ui.findNodeEdit->setText(QString::fromUtf8(text));
        co_await onFindNodeButtonClicked();
    }

    auto onFindNodeButtonClicked() -> QAsyncSlot<void> {
        auto id = NodeId::fromHex(ui.findNodeEdit->text().toStdString().c_str());
        if (id == NodeId::zero()) {
            ui.statusbar->showMessage("Invalid node id");
            co_return;
        }
        auto res = co_await mSession->findNode(id, (DhtSession::FindAlgo)ui.algoComboBox->currentIndex());
        if (res) {
            for (auto &node : *res) {
                auto             str  = std::format("node {} at {}", node.id, node.ip);
                QListWidgetItem *item = new QListWidgetItem(QString::fromUtf8(str));
                QVariantMap      map;
                map.insert("copy node id", QString::fromUtf8(node.id.toHex()));
                map.insert("copy node ip", QString::fromStdString(node.ip.toString()));
                item->setData((int)CopyableDataFlag::TextMap, map);
                ui.logWidget->addItem(item);
            }
        }
    }

    auto onSampleButtonClicked() -> QAsyncSlot<void> {
        IPEndpoint endpoint(ui.sampleEdit->text().toStdString().c_str());
        if (!endpoint.isValid()) {
            ui.statusbar->showMessage("Invalid endpoint");
            mStatusBarClearTimer.start(2000);
            co_return;
        }
        ui.statusbar->showMessage("Sample ...");
        auto res = co_await mSession->sampleInfoHashes(endpoint);
        if (!res) {
            ui.statusbar->showMessage("Sample failed");
            auto message = std::format("Sample {} failed by {}", endpoint, res.error());
            mStatusBarClearTimer.start(2000);
            co_return;
        }
        if (res->empty()) {
            ui.statusbar->showMessage("Sample failed");
            auto             message = std::format("No Message sampled from {}", endpoint);
            QListWidgetItem *item    = new QListWidgetItem(QString::fromUtf8(message));
            item->setBackground(Qt::red);
            QVariantMap map;
            map.insert("copy endpoint", QString::fromStdString(endpoint.toString()));
            item->setData((int)CopyableDataFlag::TextMap, map);
            ui.logWidget->addItem(item);
        }
        for (auto &hash : *res) {
            ui.statusbar->showMessage("Sample success");
            auto             message = std::format("Sample {} success, hash {}", endpoint, hash);
            QListWidgetItem *item    = new QListWidgetItem(QString::fromUtf8(message));
            QVariantMap      map;
            map.insert("copy endpoint", QString::fromStdString(endpoint.toString()));
            map.insert("copy hash", QString::fromStdString(hash.toHex()));
            item->setData((int)CopyableDataFlag::TextMap, map);
            ui.logWidget->addItem(item);
        }
        mStatusBarClearTimer.start(2000);
    }

    auto onBtConnectButtonClicked() -> QAsyncSlot<void> {
        ui.statusbar->clearMessage();
        auto endpoint = IPEndpoint(ui.btIpEdit->text().toStdString().c_str());
        auto infoHash = InfoHash::fromHex(ui.btHashEdit->text().toStdString().c_str());
        if (!endpoint.isValid() || infoHash == InfoHash::zero()) {
            ui.statusbar->showMessage("Invalid endpoint or hash");
            co_return;
        }
#if 0
        APP_LOG("Start connect to {}", endpoint);
        TcpClient client = (co_await TcpClient::make(endpoint.family())).value();
        if (!co_await client.connect(endpoint)) {
            ui.statusbar->showMessage("Failed to connect bt peer");
            co_return;
        }
        MetadataFetcher fetcher { std::move(client), infoHash };
        auto res = co_await fetcher.fetch();
        if (!res) {
            ui.statusbar->showMessage("Failed to fetch metadata");
            co_return;
        }
        auto torrent = Torrent::parse(*res);
        APP_LOG("Got torrent {}", torrent);
        // To Show a popup?
        auto card = new TorrentCard;
        card->setTorrent(torrent);
        card->setAttribute(Qt::WA_DeleteOnClose);
        card->show();
#else
        mFetchManager.addHash(infoHash, endpoint);
#endif
    }

    auto onMetadataFetched(InfoHash hash, std::vector<std::byte> data) -> void {
        auto torrent = Torrent::parse(data);
        APP_LOG("Got torrent {}", hash);
        auto items = ui.infoHashWidget->findItems(QString::fromStdString(hash.toHex()), Qt::MatchFixedString);
        for (auto item : items) {
            item->setText(QString::fromUtf8(torrent.name()));
        }
        // To Show a popup?
        auto card = new TorrentCard;
        card->setTorrent(torrent);
        card->setAttribute(Qt::WA_DeleteOnClose);
        card->show();

        // Try save to file
        auto  fileName = QString::fromStdString(hash.toHex()) + ".torrent";
        QFile file("./torrents/" + fileName);

        if (file.open(QIODevice::WriteOnly)) {
            auto encoded = torrent.encode();
            file.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
            QFileInfo fileInfo(file);
            QString   absolutePath = fileInfo.absoluteFilePath();
            file.close();
            for (auto item : items) {
                item->setData((int)CopyableDataFlag::TorrentFile, absolutePath);
            }
            ui.statusbar->showMessage(QString("Saved torrent to %1").arg(fileName), 5);
        }
        else {
            ui.statusbar->showMessage(QString("Failed to save torrent to %1").arg(fileName), 5);
        }
    }

    ~App() {
        mScope.cancel();
        mScope.wait();
        mStatusBarClearTimer.stop();
        if (mSession && ui.saveSessionBox->isEnabled()) {
            mSession->saveFile("session.cache");
        }
    }

    auto onHashFound(const InfoHash &hash) -> void {
        auto it = mHashs.find(hash);
        if (it == mHashs.end()) {
            mHashs.insert(hash);
            QListWidgetItem *item = new QListWidgetItem(QString::fromStdString(hash.toHex()));
            item->setData((int)CopyableDataFlag::Hash, QString::fromStdString(hash.toHex()));
            ui.infoHashWidget->addItem(item);
        }
    }

private:
    QIoContext                mIo;
    Ui::MainWindow            ui;
    UdpClient                 mUdp;
    std::optional<UtpContext> mUtp;
    std::optional<DhtSession> mSession;
    TaskScope                 mScope;
    QTimer                    mStatusBarClearTimer;

    std::set<InfoHash> mHashs;
    FetchManager       mFetchManager;
};

int main(int argc, char **argv) {

#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG);

    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif

#if defined(_WIN32) && defined(__cpp_lib_stacktrace)
    ::SetUnhandledExceptionFilter([](PEXCEPTION_POINTERS) -> LONG {
        std::cerr << "Unhandled exception\n" << std::endl;
        std::cerr << std::stacktrace::current() << std::endl;
        __debugbreak();
        return EXCEPTION_CONTINUE_SEARCH; // 让调试器处理异常
    });
#endif

    QApplication a(argc, argv);
    App          w;
    w.show();
    return a.exec();
}
