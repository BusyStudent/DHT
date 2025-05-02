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

class InfoHashListWidget : public QListWidget {
Q_OBJECT // Essential for signals and slots

    public : explicit InfoHashListWidget(QWidget *parent = nullptr);
    ~InfoHashListWidget();

    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    // Slot to handle the context menu request
    void showCustomContextMenu(const QPoint &pos);

    // Slots for handling the custom copy actions
    void onCopyAsText(QString text);
    void onCopyAsFile(QString path);
};
