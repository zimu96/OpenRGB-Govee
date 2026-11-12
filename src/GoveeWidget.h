#pragma once
#include <QWidget>
#include <QString>
class QLineEdit; class QLabel; class QListWidget; class QCheckBox; class QComboBox; class QSpinBox;
class GoveeBle;

class GoveeWidget : public QWidget
{
    Q_OBJECT
public:
    GoveeWidget(GoveeBle* ble, bool segEnabled, QWidget* parent = nullptr);
    void setStatus(const QString& s);
    void setNotice(const QString& s);       // avviso in alto (es. un'altra istanza di OpenRGB attiva)
signals:
    void settingsChanged(QString addr, bool keepConnected, int idleSeconds, QString order, bool reconnect);
    void segmentsEnabledChanged(bool on);
private:
    void emitSettings(bool reconnect);
    GoveeBle* ble;
    bool segOn = false;
    QLabel* notice = nullptr;
    QLineEdit* addr;
    QLabel* status;
    QListWidget* found;
    QCheckBox* keep;
    QComboBox* order;
    QSpinBox* idle;
};
