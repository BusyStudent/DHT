#pragma once

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCryptographicHash> // For hashing
#include <QDebug>             // For output
#include <QListWidget>        // Include QListWidget
#include <QListWidgetItem>    // Include QListWidgetItem
#include <QMenu>
#include <QPoint>
#include <QWidget>

// Forward declaration if needed, but usually included directly
// class QListWidget;
// class QListWidgetItem;
// class QMenu;

enum class CopyableDataFlag {
  Hash = Qt::ItemDataRole::UserRole + 1,
  TorrentFile,
};

class InfoHashListWidget : public QListWidget {
Q_OBJECT // Essential for signals and slots

    public : explicit InfoHashListWidget(QWidget *parent = nullptr);
  ~InfoHashListWidget();

private slots:
  // Slot to handle the context menu request
  void showCustomContextMenu(const QPoint &pos);

  // Slots for handling the custom copy actions
  void onCopyAsText(QString text);
  void onCopyAsHash(QString text);
  void onCopyAsMagnetLink(QString text);
  void onCopyAsTorrentFile(QString path);

private:
  // Helper function for demonstration purposes
//   QString generateMagnetLink(const QString &data);
};
