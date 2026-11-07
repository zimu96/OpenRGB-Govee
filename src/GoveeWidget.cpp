#include "GoveeWidget.h"
#include "GoveeBle.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>

GoveeWidget::GoveeWidget(GoveeBle* b, QWidget* parent) : QWidget(parent), ble(b)
{
    auto* l = new QVBoxLayout(this);
    l->addWidget(new QLabel("<b>Govee - strisce LED via Bluetooth</b><br>"
        "Modi, colore, luminosita e velocita si scelgono nella scheda del dispositivo \"Govee H613A\".<br>"
        "La striscia accetta un solo collegamento alla volta: con \"resta sempre collegato\" l'app del telefono non puo' collegarsi."));
    status = new QLabel("Stato: " + ble->status());
    l->addWidget(status);

    keep = new QCheckBox("Resta sempre collegato (cambi di colore piu' reattivi; se spento, si collega solo quando serve)");
    keep->setChecked(ble->keepConnected());
    l->addWidget(keep);
    auto* rowi = new QHBoxLayout;
    rowi->addWidget(new QLabel("Se non resta collegato, si scollega dopo (secondi senza cambi):"));
    idle = new QSpinBox; idle->setRange(2, 600); idle->setValue(ble->idleSeconds());
    rowi->addWidget(idle); rowi->addStretch();
    l->addLayout(rowi);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("Indirizzo Bluetooth (vuoto = automatico):"));
    addr = new QLineEdit(ble->address());
    addr->setPlaceholderText("AA:BB:CC:DD:EE:FF");
    row->addWidget(addr);
    l->addLayout(row);

    auto* row2 = new QHBoxLayout;
    row2->addWidget(new QLabel("Ordine canali colore (se i colori sono sballati):"));
    order = new QComboBox;
    order->addItems({"RGB","RBG","GRB","GBR","BRG","BGR"});
    order->setCurrentText(ble->order());
    row2->addWidget(order);
    row2->addStretch();
    l->addLayout(row2);

    auto* row3 = new QHBoxLayout;
    auto* apply = new QPushButton("Applica e ricollega");
    row3->addWidget(apply);
    row3->addWidget(new QLabel("  Prova colore:"));
    struct T { const char* n; int r, g, b; } tests[] = {{"Rosso",255,0,0},{"Verde",0,255,0},{"Blu",0,0,255},{"Bianco",255,255,255}};
    for(auto& t : tests) {
        auto* bt = new QPushButton(t.n);
        int r = t.r, g = t.g, bl = t.b;
        connect(bt, &QPushButton::clicked, this, [this, r, g, bl]{ ble->testColor(r, g, bl); });
        row3->addWidget(bt);
    }
    row3->addStretch();
    l->addLayout(row3);

    l->addWidget(new QLabel("Strisce Govee trovate (clicca per usarla):"));
    found = new QListWidget;
    found->setMaximumHeight(110);
    l->addWidget(found);
    connect(found, &QListWidget::itemClicked, this, [this](auto* it){ addr->setText(it->data(Qt::UserRole).toString()); });
    connect(ble, &GoveeBle::deviceFound, this, [this](QString a, QString n){
        for(int i = 0; i < found->count(); i++) if(found->item(i)->data(Qt::UserRole).toString() == a) return;
        auto* it = new QListWidgetItem(n + "  " + a);
        it->setData(Qt::UserRole, a);
        found->addItem(it);
    });
    connect(apply, &QPushButton::clicked, this, [this]{ emitSettings(true); });
    connect(keep, &QCheckBox::toggled, this, [this]{ emitSettings(false); });
    connect(order, &QComboBox::currentTextChanged, this, [this]{ emitSettings(false); });
    connect(idle, &QSpinBox::editingFinished, this, [this]{ emitSettings(false); });
    l->addStretch();
}

void GoveeWidget::emitSettings(bool reconnect)
{
    emit settingsChanged(addr->text().trimmed(), keep->isChecked(), idle->value(), order->currentText(), reconnect);
}

void GoveeWidget::setStatus(const QString& s) { status->setText("Stato: " + s); }
