#include <filesystem>
#include "src/bt.hpp"
#include "src/fetchmanager.hpp"
#include "src/metafetcher.hpp"
#include "src/session.hpp"
#include "src/torrent.hpp"
#include "ui/torrent_card.hpp"
#include "ui_main.h"
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

#include "./ui/widgets/info_hash_list_widget.hpp"

#if __cpp_lib_stacktrace
#include <stacktrace>
#endif

#pragma comment(linker, "/SUBSYSTEM:console")

class App final : public QMainWindow {
public:
    App() {
        ui.setupUi(this);

        // Prepare fetcher
        mFetchManager.setOnFetched([this](InfoHash hash, std::vector<std::byte> data) {
            onMetadataFetched(hash, std::move(data));
        });
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

    connect(ui.pingButton, &QPushButton::clicked, this,
            &App::onPingButtonClicked);

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

    connect(ui.findNodeButton, &QPushButton::clicked, this,
            &App::onFindNodeButtonClicked);

    connect(ui.sampleButton, &QPushButton::clicked, this,
            &App::onSampleButtonClicked);

    // Bt Peer Test part
    connect(ui.btConnectButton, &QPushButton::clicked, this,
            &App::onBtConnectButtonClicked);

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
    auto idText = ui.nodeIdEdit->text();
    auto endpoint =
        IPEndpoint::fromString(ui.bindEdit->text().toStdString().c_str());
    auto nodeId = idText.isEmpty()
                      ? NodeId::rand()
                      : NodeId::fromHex(idText.toStdString().c_str());
    // Make session and start it
    mSession.emplace(mIo, nodeId, endpoint.value());
    mSession->setOnAnouncePeer(
        [this](const InfoHash &hash, const IPEndpoint &endpoint) {
          onHashFound(hash);
          mFetchManager.addHash(hash, endpoint);
        });
    mSession->routingTable().setOnNodeChanged([&, this]() {
      setWindowTitle(
          QString("DhtClient Node: %1").arg(mSession->routingTable().size()));
    });
    if (ui.saveSessionBox->isChecked()) {
      mSession->loadFile("session.cache");
    }
    if (ui.skipBootstrapBox->isChecked()) {
      mSession->setSkipBootstrap(true);
    }
    mHandle = spawn(mIo, &DhtSession::run, &*mSession);
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
    } else {
      auto message = std::format("Ping {} success, peer id {}", endpoint, *res);
      ui.statusbar->showMessage(QString::fromUtf8(message));
    }
  }

  auto onFindNodeButtonClicked() -> QAsyncSlot<void> {
    auto id = NodeId::fromHex(ui.findNodeEdit->text().toStdString().c_str());
    if (id == NodeId::zero()) {
      ui.statusbar->showMessage("Invalid node id");
      co_return;
    }
    auto res = co_await mSession->findNode(id);
    if (res) {
      for (auto &node : *res) {
        auto str = std::format("node {} at {}", node.id, node.ip);
        ui.logWidget->addItem(QString::fromUtf8(str));
      }
    }
  }

  auto onSampleButtonClicked() -> QAsyncSlot<void> {
    IPEndpoint endpoint(ui.sampleEdit->text().toStdString().c_str());
    if (!endpoint.isValid()) {
      ui.statusbar->showMessage("Invalid endpoint");
      co_return;
    }
    auto res = co_await mSession->sampleInfoHashes(endpoint);
    if (!res) {
      auto message =
          std::format("Sample {} failed by {}", endpoint, res.error());
      co_return;
    }
    if (res->empty()) {
      auto message = std::format("No Message sampled from {}", endpoint);
      ui.logWidget->addItem("");
    }
    for (auto &hash : *res) {
      auto message = std::format("Sample {} success, hash {}", endpoint, hash);
      ui.logWidget->addItem(QString::fromUtf8(message));
    }
  }

  auto onBtConnectButtonClicked() -> QAsyncSlot<void> {
    ui.statusbar->clearMessage();
    auto endpoint = IPEndpoint(ui.btIpEdit->text().toStdString().c_str());
    auto infoHash =
        InfoHash::fromHex(ui.btHashEdit->text().toStdString().c_str());
    if (!endpoint.isValid() || infoHash == InfoHash::zero()) {
      ui.statusbar->showMessage("Invalid endpoint or hash");
      co_return;
    }
#if 0
        BT_LOG("Start connect to {}", endpoint);
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
        BT_LOG("Got torrent {}", torrent);
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
    BT_LOG("Got torrent {}", hash);
    auto items = ui.infoHashWidget->findItems(
        QString::fromStdString(hash.toHex()), Qt::MatchFixedString);
    for (auto item : items) {
      item->setText(QString::fromUtf8(torrent.name()));
    }
    // To Show a popup?
    auto card = new TorrentCard;
    card->setTorrent(torrent);
    card->setAttribute(Qt::WA_DeleteOnClose);
    card->show();

    // Try save to file
    auto fileName = QString::fromStdString(hash.toHex()) + ".torrent";
    QFile file("./torrents/" + fileName);

    if (file.open(QIODevice::WriteOnly)) {
      auto encoded = torrent.encode();
      file.write(reinterpret_cast<const char *>(encoded.data()),
                 encoded.size());
      QFileInfo fileInfo(file);
      QString absolutePath = fileInfo.absoluteFilePath();
      file.close();
      for (auto item : items) {
        item->setData((int)CopyableDataFlag::TorrentFile, absolutePath);
      }
      ui.statusbar->showMessage(QString("Saved torrent to %1").arg(fileName),
                                5);
    } else {
      ui.statusbar->showMessage(
          QString("Failed to save torrent to %1").arg(fileName), 5);
    }
  }

  ~App() {
    if (mHandle) {
      mHandle.cancel();
      mHandle.wait();
    }
    if (mSession && ui.saveSessionBox->isEnabled()) {
      mSession->saveFile("session.cache");
    }
  }

  auto onHashFound(const InfoHash &hash) -> void {
    auto it = mHashs.find(hash);
    if (it == mHashs.end()) {
      mHashs.insert(hash);
      QListWidgetItem *item =
          new QListWidgetItem(QString::fromStdString(hash.toHex()));
      item->setData((int)CopyableDataFlag::Hash,
                    QString::fromStdString(hash.toHex()));
      ui.infoHashWidget->addItem(item);
    }
  }

private:
  QIoContext mIo;
  Ui::MainWindow ui;
  std::optional<DhtSession> mSession;
  WaitHandle<> mHandle;

  std::set<InfoHash> mHashs;
  FetchManager mFetchManager;
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
  App w;
  w.show();
  return a.exec();
}
