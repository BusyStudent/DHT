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
#include <optional>
#include <QTimer>

#include "src/bt.hpp"
#include "src/fetchmanager.hpp"
#include "src/samplemanager.hpp"
#include "src/metafetcher.hpp"
#include "src/session.hpp"
#include "src/torrent.hpp"
#include "src/utp.hpp"
#include "ui/widgets/info_hash_list_widget.hpp"
#include "ui/torrent_card.hpp"
#include "ui_main.h"
#include "ui/widgets/common.hpp"

#if __cpp_lib_stacktrace
#include <stacktrace>
#include <iostream>
#endif

#pragma comment(linker, "/SUBSYSTEM:console")

template <typename... Args>
auto qFormat(std::format_string<Args...> fmt, Args &&...args) -> QString {
    return QString::fromUtf8(std::format(fmt, std::forward<Args>(args)...));
}

class App final : public QMainWindow {
public:
    App() {
        ui.setupUi(this);

        ui.algoComboBox->addItems({"a star", "bfs-dfs"});
        ui.algoComboBox->setCurrentIndex(0);

        ui.autoSampleBox->setDisabled(true);
        ui.randomDiffusionBox->setDisabled(true);

        // Prepare fetcher
        mFetchManager.setOnFetched(
            [this](InfoHash hash, std::vector<std::byte> data) { onMetadataFetched(hash, std::move(data)); });
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
            ui.autoSampleBox->setDisabled(false);

            start();
        });

        connect(ui.randButton, &QPushButton::clicked, this, [this]() {
            auto text = NodeId::rand().toHex();
            ui.nodeIdEdit->setText(QString::fromUtf8(text));
        });

        connect(ui.pingButton, &QPushButton::clicked, this, &App::onPingButtonClicked);

        connect(ui.refreshBucketsButton, &QPushButton::clicked, this, &App::refleshKBucketWidget);
        connect(ui.findClosetNodesButton, &QPushButton::clicked, this, [this]() {
            ui.closetNodesListWidget->clear();
            if (!mSession) {
                APP_LOG("Session not started");
                return;
            }
            auto nodeIdStr = ui.closetNodeIdLineEdit->text().toUtf8();
            if (nodeIdStr.isEmpty()) {
                APP_LOG("Please input node id");
                return;
            }
            auto nodeId = NodeId::fromHex(nodeIdStr.toStdString());
            if (nodeId == NodeId::zero()) {
                APP_LOG("Invalid node id");
                return;
            }
            auto nodes = mSession->routingTable().findClosestNodes(nodeId);
            for (const auto &node : nodes) {
                ui.closetNodesListWidget->addItem(QString::fromUtf8(node.ip.toString()) + " - " +
                                                  QString::fromUtf8(node.id.toHex()));
            }
        });

        connect(ui.findNodeButton, &QPushButton::clicked, this, &App::onFindNodeButtonClicked);
        connect(ui.randFindNodeButton, &QPushButton::clicked, this, &App::onRandFindNodeButtonClikced);

        connect(ui.sampleButton, &QPushButton::clicked, this, &App::onSampleButtonClicked);
        connect(ui.getPeersButton, &QPushButton::clicked, this, &App::onGetPeersButtonClicked);

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

        connect(ui.dumpSampleTableButton, &QPushButton::clicked, this, [this]() {
            if (!mSampleManager) {
                return;
            }
            mSampleManager->dump();
        });

        connect(ui.autoSampleBox, &QCheckBox::clicked, this, [this](bool checked) -> QAsyncSlot<> {
            if (mSampleManager != nullptr) {
                if (checked) {
                    ui.randomDiffusionBox->setDisabled(false);
                    co_await mSampleManager->start();
                }
                else {
                    ui.randomDiffusionBox->setDisabled(true);
                    co_await mSampleManager->stop();
                }
            }
        });

        connect(ui.randomDiffusionBox, &QCheckBox::clicked, this, [this](bool checked) -> void {
            if (mSampleManager != nullptr) {
                mSampleManager->setRandomDiffusion(checked);
            }
        });

        connect(ui.sampleRefreshButton, &QPushButton::clicked, this, [this]() {
            ui.sampleRefreshButton->setDisabled(true);
            QMetaObject::invokeMethod(this, &App::refleshSampleTableWidget, Qt::ConnectionType::QueuedConnection);
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
        mSampleManager = std::make_unique<SampleManager>(mSession.value());
        mSampleManager->setOnInfoHashs([this](const std::vector<InfoHash> &infohashs) {
            int count = 0;
            for (const auto &hash : infohashs) {
                count += onHashFound(hash);
            }
            return count;
        });
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
        ui.statusbar->showMessage("Ping ...");
        auto res = co_await mSession->ping(endpoint);
        if (!res) {
            auto message = qFormat("Ping {} failed by {}", endpoint, res.error());
            ui.statusbar->showMessage(message, 5000);
        }
        else {
            auto message = qFormat("Ping {} success, peer id {}", endpoint, *res);
            ui.statusbar->showMessage(message, 5000);
        }
    }

    auto refleshKBucketWidget() -> void {
        if (mSession) {
            auto  treeWidget = ui.kBucketTableWidget;
            auto &kbucket    = mSession->routingTable();

            QStringList headers = {"IpEndpoint", "NodeId", "Distance", "LastSeen"};
            treeWidget->clearContents(); // Clear existing data but not headers
            treeWidget->setColumnCount(headers.size());
            treeWidget->setRowCount(0);
            treeWidget->setHorizontalHeaderLabels(headers);
            ui.kBucketTableWidget->horizontalHeader()->setStretchLastSection(true);
            ui.kBucketTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

            int  row = 0;
            auto now = std::chrono::steady_clock::now();
            for (auto &node : kbucket.rawNodes()) {
                treeWidget->insertRow(row);

                QTableWidgetItem *ipItem = new QTableWidgetItem(QString::fromStdString(node.endpoint.ip.toString()));
                QTableWidgetItem *nodeIdItem = new QTableWidgetItem(QString::fromStdString(node.endpoint.id.toHex()));
                QTableWidgetItem *distanceItem =
                    new QTableWidgetItem(QString::number(kbucket.findBucketIndex(node.endpoint.id)));
                QTableWidgetItem *lastSeenItem = new QTableWidgetItem(
                    QString::number(std::chrono::duration_cast<std::chrono::seconds>(now - node.lastSeen).count()) +
                    "s ago");

                // no edit
                ipItem->setFlags(ipItem->flags() & ~Qt::ItemIsEditable);
                nodeIdItem->setFlags(nodeIdItem->flags() & ~Qt::ItemIsEditable);
                distanceItem->setFlags(distanceItem->flags() & ~Qt::ItemIsEditable);
                lastSeenItem->setFlags(lastSeenItem->flags() & ~Qt::ItemIsEditable);

                // Optional: Align numbers to the center
                ipItem->setTextAlignment(Qt::AlignCenter);
                nodeIdItem->setTextAlignment(Qt::AlignCenter);
                distanceItem->setTextAlignment(Qt::AlignCenter);
                lastSeenItem->setTextAlignment(Qt::AlignCenter);

                treeWidget->setItem(row, 0, ipItem);
                treeWidget->setItem(row, 1, nodeIdItem);
                treeWidget->setItem(row, 2, distanceItem);
                treeWidget->setItem(row, 3, lastSeenItem);
                row++;
            }

            auto next = kbucket.nextRefresh();
            if (next) {
                ui.nextRefreshLabel->setText(QString::fromStdString(next->ip.toString()));
            }
            else {
                ui.kBucketTableWidget->setToolTip("N/A");
            }
        }
    }

    auto refleshSampleTableWidget() -> void {
        if (mSampleManager != nullptr) {
            auto sampleNodes = mSampleManager->getSampleNodes();
            ui.sampleNodeTableWidget->clearContents(); // Clear existing data but not headers
            ui.sampleNodeTableWidget->setRowCount(0);  // Reset row count
            auto now =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();

            QStringList headers = {"IpEndpoint", "Status", "Timeout", "Hashs", "Success", "Failure"};
            ui.sampleNodeTableWidget->setColumnCount(headers.size());
            ui.sampleNodeTableWidget->setHorizontalHeaderLabels(headers);

            int row = 0;
            for (auto &node : sampleNodes) {
                ui.sampleNodeTableWidget->insertRow(row);

                QTableWidgetItem *ipItem     = new QTableWidgetItem(QString::fromStdString(node.endpoint.toString()));
                QTableWidgetItem *statusItem = new QTableWidgetItem(qFormat("{}", node.status));
                QTableWidgetItem *timeoutItem =
                    new QTableWidgetItem(QString::number(std::max(0, (int)(node.timeout - now))) + "s");
                QTableWidgetItem *hashsItem   = new QTableWidgetItem(QString::number(node.hashsCount));
                QTableWidgetItem *successItem = new QTableWidgetItem(QString::number(node.successCount));
                QTableWidgetItem *failureItem = new QTableWidgetItem(QString::number(node.failureCount));

                // no edit
                ipItem->setFlags(ipItem->flags() & ~Qt::ItemIsEditable);
                statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
                timeoutItem->setFlags(timeoutItem->flags() & ~Qt::ItemIsEditable);
                hashsItem->setFlags(hashsItem->flags() & ~Qt::ItemIsEditable);

                // Optional: Align numbers to the right
                timeoutItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                hashsItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                successItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                failureItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

                ui.sampleNodeTableWidget->setItem(row, 0, ipItem);
                ui.sampleNodeTableWidget->setItem(row, 1, statusItem);
                ui.sampleNodeTableWidget->setItem(row, 2, timeoutItem);
                ui.sampleNodeTableWidget->setItem(row, 3, hashsItem);
                ui.sampleNodeTableWidget->setItem(row, 4, successItem);
                ui.sampleNodeTableWidget->setItem(row, 5, failureItem);
                row++;
            }
            // Optional: Stretch the first column (IpEndpoint) if space allows
            if (ui.sampleNodeTableWidget->columnCount() > 0) {
                ui.sampleNodeTableWidget->horizontalHeader()->setMinimumSectionSize(60);
                ui.sampleNodeTableWidget->horizontalHeader()->setStretchLastSection(true);
                ui.sampleNodeTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            }

            auto excludeIps = mSampleManager->excludeIpEndpoints();
            ui.excludeIpListWidget->clear();
            for (auto &ip : excludeIps) {
                ui.excludeIpListWidget->addItem(QString::fromStdString(ip.toString()));
            }
        }
        ui.sampleRefreshButton->setEnabled(true);
    }

    auto onRandFindNodeButtonClikced() -> QAsyncSlot<void> {
        auto text = NodeId::rand().toHex();
        ui.findNodeEdit->setText(QString::fromUtf8(text));
        co_await onFindNodeButtonClicked();
    }

    auto onFindNodeButtonClicked() -> QAsyncSlot<void> {
        auto id = NodeId::fromHex(ui.findNodeEdit->text().toStdString().c_str());
        if (id == NodeId::zero()) {
            ui.statusbar->showMessage("Invalid node id", 5000);
            co_return;
        }
        auto res = co_await mSession->findNode(id, (DhtSession::FindAlgo)ui.algoComboBox->currentIndex());
        if (res) {
            for (auto &node : *res) {
                auto             str  = qFormat("node {} at {}", node.id, node.ip);
                QListWidgetItem *item = new QListWidgetItem(str);
                QVariantMap      map;
                map.insert("node id", QString::fromUtf8(node.id.toHex()));
                map.insert("node ip", QString::fromStdString(node.ip.toString()));
                item->setData((int)CopyableDataFlag::TextMap, map);
                ui.logWidget->addItem(item);
            }
        }
    }

    auto onSampleButtonClicked() -> QAsyncSlot<void> {
        IPEndpoint endpoint(ui.sampleEdit->text().toStdString().c_str());
        if (!endpoint.isValid()) {
            ui.statusbar->showMessage("Invalid endpoint", 5000);
            co_return;
        }
        ui.statusbar->showMessage("Sample ...");
        auto res = co_await mSession->sampleInfoHashes(endpoint);
        if (!res) {
            auto message = qFormat("Sample {} failed by {}", endpoint, res.error());
            ui.statusbar->showMessage(message, 5000);
            co_return;
        }
        if (res->samples.empty()) {
            ui.statusbar->showMessage("Sample failed", 5000);
            auto             message = qFormat("No Message sampled from {}", endpoint);
            QListWidgetItem *item    = new QListWidgetItem(message);
            item->setBackground(Qt::red);
            QVariantMap map;
            map.insert("endpoint", QString::fromUtf8(endpoint.toString()));
            item->setData((int)CopyableDataFlag::TextMap, map);
            ui.logWidget->addItem(item);
        }
        for (auto &hash : res->samples) {
            ui.statusbar->showMessage("Sample success", 5000);
            auto             message = qFormat("Sample {} success, hash {}", endpoint, hash);
            QListWidgetItem *item    = new QListWidgetItem(message);
            QVariantMap      map;
            map.insert("endpoint", QString::fromUtf8(endpoint.toString()));
            map.insert("hash", QString::fromUtf8(hash.toHex()));
            item->setData((int)CopyableDataFlag::TextMap, map);
            ui.logWidget->addItem(item);
        }
    }

    auto onGetPeersButtonClicked() -> QAsyncSlot<void> {
        auto endpoint = IPEndpoint(ui.getPeersIpEdit->text().toStdString().c_str());
        auto infoHash = InfoHash::fromHex(ui.getPeersHashEdit->text().toStdString().c_str());
        if (!endpoint.isValid() || infoHash == InfoHash::zero()) {
            ui.statusbar->showMessage("Invalid endpoint or hash", 5000);
            co_return;
        }
        ui.statusbar->showMessage("GetPeers ...");
        auto res = co_await mSession->getPeers(endpoint, infoHash);
        if (!res) {
            auto message = qFormat("GetPeers {} by hash {} failed by {}", endpoint, infoHash, res.error());
            ui.statusbar->showMessage(message, 5000);
            co_return;
        }
        ui.statusbar->showMessage("GetPeers success", 5000);
        if (res->values.empty()) {
            auto message = qFormat("GetPeers {} success, hash {}, but no peers found", endpoint, infoHash);
            ui.logWidget->addItem(message);
            for (auto &node : res->nodes) {
                QListWidgetItem *item = new QListWidgetItem(qFormat("node {} endpoint {}", node.id, node.ip));
                QVariantMap      map;
                map.insert("copy endpoint", QString::fromUtf8(node.ip.toString()));
                map.insert("copy id", QString::fromUtf8(node.id.toHex()));
                item->setData((int)CopyableDataFlag::TextMap, map);
                ui.logWidget->addItem(item);
            }
            co_return;
        }
        else {
            auto message =
                qFormat("GetPeers {} success, hash {}, got {} peers", endpoint, infoHash, res->values.size());
            ui.logWidget->addItem(message);
            for (auto &peer : res->values) {
                QListWidgetItem *item = new QListWidgetItem(qFormat("peer {}", peer));
                QVariantMap      map;
                map.insert("copy endpoint", QString::fromUtf8(peer.toString()));
                item->setData((int)CopyableDataFlag::TextMap, map);
                ui.logWidget->addItem(item);
            }
        }
    }

    auto onBtConnectButtonClicked() -> QAsyncSlot<void> {
        ui.statusbar->clearMessage();
        auto endpoint = IPEndpoint(ui.btIpEdit->text().toStdString().c_str());
        auto infoHash = InfoHash::fromHex(ui.btHashEdit->text().toStdString().c_str());
        if (!endpoint.isValid() || infoHash == InfoHash::zero()) {
            ui.statusbar->showMessage("Invalid endpoint or hash", 5000);
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
        auto items = ui.infoHashWidget->findItems(QString::fromUtf8(hash.toHex()), Qt::MatchFixedString);
        for (auto item : items) {
            item->setText(QString::fromUtf8(torrent.name()));
        }
        // To Show a popup?
        auto card = new TorrentCard;
        card->setTorrent(torrent);
        card->setAttribute(Qt::WA_DeleteOnClose);
        card->show();

        // Try save to file
        auto  fileName = QString::fromUtf8(hash.toHex()) + ".torrent";
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
            ui.statusbar->showMessage(QString("Saved torrent to %1").arg(fileName), 5000);
        }
        else {
            ui.statusbar->showMessage(QString("Failed to save torrent to %1").arg(fileName), 5000);
        }
    }

    ~App() {
        mScope.cancel();
        mScope.wait();
        if (mSampleManager) {
            mSampleManager->stop().wait();
        }
        mSampleManager.reset();
        if (mSession && ui.saveSessionBox->isEnabled()) {
            mSession->saveFile("session.cache");
        }
    }

    auto onHashFound(const InfoHash &hash) -> int {
        auto it = mHashs.find(hash);
        if (it == mHashs.end()) {
            mHashs.insert(hash);
            QListWidgetItem *item = new QListWidgetItem(QString::fromStdString(hash.toHex()));
            item->setData((int)CopyableDataFlag::Hash, QString::fromStdString(hash.toHex()));
            ui.infoHashWidget->addItem(item);
            return 1;
        }
        return 0;
    }

private:
    QIoContext                     mIo;
    Ui::MainWindow                 ui;
    UdpClient                      mUdp;
    std::optional<UtpContext>      mUtp;
    std::unique_ptr<SampleManager> mSampleManager;
    TaskScope                      mScope;
    std::optional<DhtSession>      mSession;

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
