#include "torrent_card.hpp"
#include "ui_torrent_card.h"

#define mUi static_cast<Ui::TorrentCard*>(ui)

TorrentCard::TorrentCard(QWidget* parent) : QWidget(parent) {
    ui = new Ui::TorrentCard;
    mUi->setupUi(this);
}

TorrentCard::~TorrentCard() {
    delete mUi;
}

auto TorrentCard::setTorrent(const Torrent &torrent) -> void {
    mUi->hashBox->setTitle("InfoHash: " + QString::fromUtf8(torrent.infoHash().toHex()));
    mUi->nameLabel->setText("Name: " + QString::fromUtf8(torrent.name()));
    mUi->lengthLabel->setText("Length: " + QString("%1").arg(torrent.length()));
    for (const auto &file : torrent.files()) {
        std::string path;
        for (auto &p : file.paths) {
            path += p;
            path += '/';
        }
        if (!path.empty()) {
            path.pop_back();
        }
        mUi->fileListWidget->addItem(QString::fromUtf8(path));
    }
}
