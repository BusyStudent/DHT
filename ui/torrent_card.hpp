#pragma once

#include <QWidget>
#include "../src/torrent.hpp"

class TorrentCard : public QWidget {
Q_OBJECT
public:
    TorrentCard(QWidget *parent = nullptr);
    ~TorrentCard();

    /**
     * @brief Set the torrent info we show
     * 
     * @param torrent 
     * @return auto 
     */
    auto setTorrent(const Torrent &torrent) -> void;
private:
    void *ui = nullptr;
};