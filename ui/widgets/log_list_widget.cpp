#include "log_list_widget.hpp"

#include <QMimeData>
#include <QUrl>

#include "common.hpp"

LogListWidget::LogListWidget(QWidget *parent) : QListWidget(parent) {
    // Set up the context menu
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &LogListWidget::customContextMenuRequested, this, &LogListWidget::showCustomContextMenu);
}

LogListWidget::~LogListWidget() {
}

// Slot to handle the context menu request
void LogListWidget::showCustomContextMenu(const QPoint &pos) {
    QListWidgetItem *item = itemAt(pos);
    QMenu            contextMenu(this);
    if (item) {
        auto text = item->text();
        if (!text.isEmpty()) {
            QAction *copyTextAction = contextMenu.addAction("Copy Text");
            connect(copyTextAction, &QAction::triggered, this, [this, text]() { onCopyAsText(text); });
        }

        auto hash = item->data((int)CopyableDataFlag::Hash).toString();
        if (!hash.isEmpty()) {
            QAction *copyHashAction = contextMenu.addAction("Copy Hash");
            connect(copyHashAction, &QAction::triggered, this, [this, hash]() { onCopyAsText(hash); });
        }

        auto files = item->data((int)CopyableDataFlag::Files).toStringList();
        if (!files.isEmpty()) {
            QAction *copyFilesAction = contextMenu.addAction("Copy Files");
            connect(copyFilesAction, &QAction::triggered, this, [this, files]() { onCopyAsFile(files); });
        }

        auto textMap = item->data((int)CopyableDataFlag::TextMap).toMap();
        if (!textMap.isEmpty()) {
            for (auto it = textMap.begin(); it != textMap.end(); ++it) {
                QAction *copyTextMapAction = contextMenu.addAction(QString("Copy %1").arg(it.key()));
                connect(copyTextMapAction, &QAction::triggered, this,
                        [this, text = it.value().toString()]() { onCopyAsText(text); });
            }
        }

        QAction *clearAction = contextMenu.addAction("Clear");
        connect(clearAction, &QAction::triggered, this, [this]() { clear(); });
    }
    else {
        QAction *clearAction = contextMenu.addAction("Clear");
        connect(clearAction, &QAction::triggered, this, [this]() { clear(); });
    }

    contextMenu.exec(viewport()->mapToGlobal(pos));
}

// Slots for handling the custom copy actions
void LogListWidget::onCopyAsText(QString text) {
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(text);
}

void LogListWidget::onCopyAsFile(QStringList files) {
    QClipboard *clipboard = QApplication::clipboard();

    // 2. Create QMimeData
    QMimeData *mimeData = new QMimeData(); // Clipboard will take ownership

    // 3. Prepare File URLs (using absolute paths)
    QList<QUrl> urls;

    for (auto file : files) {
        urls.append(QUrl::fromLocalFile(file));
    }

    if (urls.isEmpty()) {
        qWarning() << "No valid files provided to copy.";
    }

    // 4. Set URLs on QMimeData
    mimeData->setUrls(urls);

    // 5. Set QMimeData on Clipboard
    // The clipboard takes ownership of mimeData pointer - DO NOT delete it
    clipboard->setMimeData(mimeData);
}