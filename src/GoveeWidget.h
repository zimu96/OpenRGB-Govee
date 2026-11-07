#pragma once
#include <QWidget>
#include <QString>
class QLineEdit; class QLabel; class QListWidget; class QCheckBox; class QComboBox; class QSpinBox;
class GoveeBle;

class GoveeWidget : public QWidget
{
    Q_OBJECT
public:
    GoveeWidget(GoveeBle* ble, QWidget* parent = nullptr);
    void setStatus(const QString& s);
signals:
    void settingsChanged(QString addr, bool keepConnected, int idleSeconds, QString order, bool reconnect);
private:
    void emitSettings(bool reconnect);
    GoveeBle* ble;
    QLineEdit* addr;
    QLabel* status;
    QListWidget* found;
    QCheckBox* keep;
    QComboBox* order;
    QSpinBox* idle;
};
