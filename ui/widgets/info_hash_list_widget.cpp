#include "info_hash_list_widget.hpp"

#include <QMimeData>
#include <QUrl>
#include <QVBoxLayout> // For layout
#include <QMouseEvent>
#include <QFile>

#include "common.hpp"
#include "../torrent_card.hpp"

InfoHashListWidget::InfoHashListWidget(QWidget *parent) : QListWidget(parent) {
    // 3. Enable custom context menu policy
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &InfoHashListWidget::customContextMenuRequested, this, &InfoHashListWidget::showCustomContextMenu);

#if 0
  auto item = new QListWidgetItem("a test item, no any data");
  addItem(item);

  auto item1 = new QListWidgetItem("a test item with hash");
  item1->setData((int)CopyableDataFlag::Hash,
                 "39d037b465901cbaa743e4045b5b75cddb38d6eb");
  addItem(item1);

  auto item2 = new QListWidgetItem("a test item with torrent file");
  item2->setData((int)CopyableDataFlag::TorrentFile,
                 "./torrents/39d037b465901cbaa743e4045b5b75cddb38d6eb.torrent");
  addItem(item2);
#endif
    //   item->setData(, const QVariant &value)

    //   // 4. Connect the signal to our slot
    //   connect(this, &QListWidget::customContextMenuRequested, this,
    //           &InfoHashListWidget::showContextMenu);
}

InfoHashListWidget::~InfoHashListWidget() {
    // No need to delete listWidget explicitly if it has 'this' as parent 
}

void InfoHashListWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QListWidgetItem *item = itemAt(event->pos());
        if (item) {
            QString torrentFile = item->data((int)CopyableDataFlag::TorrentFile).value<QString>();
            if (!torrentFile.isEmpty()) {
                QFile   file(torrentFile);
                if (!file.open(QFile::ReadOnly)) {
                    return;
                }
                auto    data = file.readAll();
                Torrent torrent =
                    Torrent::parse({reinterpret_cast<const std::byte *>(data.constData()), (uint64_t)data.size()});
                auto card = new TorrentCard;
                card->setTorrent(torrent);
                card->setAttribute(Qt::WA_DeleteOnClose);
                card->show();
            }
        }
    }
    QListWidget::mouseDoubleClickEvent(event);
}

// Slot implementation: Show the context menu
void InfoHashListWidget::showCustomContextMenu(const QPoint &pos) {
    // Get the item at the requested position
    QListWidgetItem *item = itemAt(pos);

    // Only show the menu if an item was actually clicked
    if (item) {
        // Create the menu
        QMenu contextMenu(this); // Parent 'this' for proper memory management

        // --- Create Actions ---
        QAction *copyTextAction = contextMenu.addAction("Copy as Text");
        connect(copyTextAction, &QAction::triggered, this, [this, item]() { this->onCopyAsText(item->text()); });

        QString hash = item->data((int)CopyableDataFlag::Hash).value<QString>();
        if (!hash.isEmpty()) {
            QAction *copyHashAction = contextMenu.addAction("Copy as Hash (SHA1)");
            connect(copyHashAction, &QAction::triggered, this, [this, hash]() { this->onCopyAsText(hash); });
            QAction *copytMagnetAction = contextMenu.addAction("Copy as Magnet link");
            connect(copytMagnetAction, &QAction::triggered, this,
                    [this, hash]() { this->onCopyAsText(QString("magnet:?xt=urn:btih:" + hash)); });
        }

        QString path = item->data((int)CopyableDataFlag::TorrentFile).value<QString>();
        if (!path.isEmpty()) {
            QAction *copyTorrentAction = contextMenu.addAction("Copy as Torrent file");
            connect(copyTorrentAction, &QAction::triggered, this, [this, path]() { this->onCopyAsFile(path); });
        }

        contextMenu.addSeparator();
        // --- Connect Actions ---
        QAction *deleteAction = contextMenu.addAction("Delete");
        QAction *clearAction  = contextMenu.addAction("Clear");

        connect(deleteAction, &QAction::triggered, this, [this, item]() {
            auto itemp = takeItem(row(item));
            delete itemp;
        });

        connect(clearAction, &QAction::triggered, this, [this]() { clear(); });

        // --- Show the menu ---
        // mapToGlobal converts widget coordinates to screen coordinates
        contextMenu.exec(mapToGlobal(pos));
    }
    else {
        // add clear menu
        QMenu contextMenu(this); // Parent 'this' for proper memory management

        QAction *clearAction = contextMenu.addAction("Clear");
        connect(clearAction, &QAction::triggered, this, [this]() { clear(); });
    }
}

// --- Action Handlers ---

void InfoHashListWidget::onCopyAsText(QString text) {
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(text);
    qDebug() << "Copied as Text:" << text;
}

void InfoHashListWidget::onCopyAsFile(QString path) {
    QClipboard *clipboard = QApplication::clipboard();

    // 2. Create QMimeData
    QMimeData *mimeData = new QMimeData(); // Clipboard will take ownership

    // 3. Prepare File URLs (using absolute paths)
    QList<QUrl> urls;

    urls.append(QUrl::fromLocalFile(path));

    if (urls.isEmpty()) {
        qWarning() << "No valid files provided to copy.";
    }

    // 4. Set URLs on QMimeData
    mimeData->setUrls(urls);

    // 5. Set QMimeData on Clipboard
    // The clipboard takes ownership of mimeData pointer - DO NOT delete it
    clipboard->setMimeData(mimeData);
}
